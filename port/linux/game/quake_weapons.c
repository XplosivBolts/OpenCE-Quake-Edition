/* Independently implemented Quake rocket and grenade launch parameters. */
#include "cseries.h"
#include "quake_weapons.h"
#include "quake_movement.h"
#include "items/projectiles.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "tag_files/tag_files.h"
#include "objects/damage.h"
#include "effects/effects.h"
#include "physics/collisions.h"
#include "physics/collision_usage.h"

short quake_weapons_tag_kind(long definition_index)
{
	char const *name;
	if (definition_index == NONE) return 0;
	name = tag_get_name(definition_index);
	if (!strcmp(name, "weapons\\rocket launcher\\rocket")) return 1;
	if (!strcmp(name, "weapons\\frag grenade\\frag grenade")) return 2;
	return 0;
}
void quake_weapons_stamp(long projectile_index)
{
	struct projectile_datum *projectile = projectile_get(projectile_index);
	SET_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_BIT,
		quake_weapons_tag_kind(projectile->definition_index) && quake_player_weapons(projectile->object.owner_object_index));
}
short quake_weapons_projectile_kind(long projectile_index)
{
	struct projectile_datum *projectile = projectile_get(projectile_index);
	return TEST_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_BIT) ?
		quake_weapons_tag_kind(projectile->definition_index) : 0;
}
void quake_weapons_launch(long projectile_index)
{
	struct projectile_datum *projectile = projectile_get(projectile_index);
	struct unit_datum *owner = unit_try_and_get(projectile->object.owner_object_index);
	short kind = quake_weapons_projectile_kind(projectile_index);
	real_vector3d aim = projectile->object.forward, right, up, velocity;
	real_point3d position, eye;
	struct collision_result clearance;
	real scale = QUAKE_WORLD_SCALE / TICKS_PER_SECOND;
	if (!kind || projectile->object.parent_object_index != NONE ||
		TEST_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_STARTED_BIT)) return;
	SET_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_STARTED_BIT, TRUE);
	projectile->projectile.detonation_timer = 0.f;
	if (kind == 2 && owner) aim = owner->unit.aiming_vector;
	normalize3d(&aim);
	cross_product3d(&aim, global_up3d, &right);
	/* At a vertical aim the cross product loses yaw and float roundoff can
	   reverse it. Use the owner's horizontal facing at this singularity. */
	if (normalize3d(&right) < 0.00001f)
	{
		if (owner) cross_product3d(&owner->object.forward, global_up3d, &right);
		if (normalize3d(&right) == 0.f) scale_vector3d(global_left3d, -1.f, &right);
	}
	cross_product3d(&right, &aim, &up); normalize3d(&up);
	position = owner ? owner->object.position : projectile->object.position;
	if (owner)
	{
		unit_get_camera_position(projectile->object.owner_object_index, &eye);
		if (biped_try_and_get(projectile->object.owner_object_index))
		{
			real_point3d base;
			real height, radius;
			/* Scale both original launch heights with the current Halo body,
			   including its crouch blend; Quake's standing hull is 56 high. */
			biped_get_physics_pill(projectile->object.owner_object_index, &base, &height, &radius);
			position.z += (height + 2.f * radius) * ((kind == 1 ? 40.f : 24.f) / 56.f);
		}
		else position.z += (kind == 1 ? 40.f : 24.f) * QUAKE_WORLD_SCALE;
	}
	if (kind == 1)
	{
		point_from_line3d(&position, &aim, 8.f * QUAKE_WORLD_SCALE, &position);
		scale_vector3d(&aim, 1000.f * scale, &velocity);
	}
	else
	{
		scale_vector3d(&aim, 600.f * scale, &velocity);
		if (fabs(aim.k) > 0.000001f)
		{
			real sideways = real_random_range(-10.f, 10.f), vertical = real_random_range(-10.f, 10.f);
			velocity.i += (up.i * (200.f + vertical) + right.i * sideways) * scale;
			velocity.j += (up.j * (200.f + vertical) + right.j * sideways) * scale;
			velocity.k += (up.k * (200.f + vertical) + right.k * sideways) * scale;
		}
		else velocity.k = 200.f * scale;
	}
	if (owner)
	{
		/* Keep the spawn on the eye's side of nearby walls/floors/ceilings. */
		global_current_collision_users[global_current_collision_user_depth++] = _collision_user_projectiles;
		if (collision_test_line(_collision_test_for_projectiles_flags, &eye, &position,
			projectile->projectile.ignore_object_index, &clearance))
			point_from_line3d(&clearance.point, &clearance.plane.n,
				0.03125f * QUAKE_WORLD_SCALE, &position);
		--global_current_collision_user_depth;
	}
	object_translate(projectile_index, &position, NULL);
	projectile->object.translational_velocity = velocity;
	SET_FLAG(projectile->object.flags, _object_at_rest_bit, FALSE);
}
void quake_weapons_explode(long projectile_index)
{
	struct projectile_datum *projectile = projectile_get(projectile_index);
	short kind = quake_weapons_projectile_kind(projectile_index);
	long definition = tag_loaded('jpt!', kind == 1 ? "weapons\\rocket launcher\\explosion" : "weapons\\frag grenade\\explosion");
	struct damage_data damage;
	if (definition == NONE) return;
	damage_data_new(&damage, definition);
	SET_FLAG(damage.flags, _damage_quake_blast_bit, TRUE);
	SET_FLAG(damage.flags, _damage_from_weapon_bit, TRUE);
	damage.origin = damage.epicenter = projectile->object.position;
	damage.location = projectile->object.location;
	damage.direction = projectile->object.forward;
	damage.owner_object_index = projectile->object.owner_object_index;
	damage.owner_player_index = projectile->object.owner_player_index;
	damage.owner_team_index = projectile->object.owner_team_index;
	area_of_effect_cause_damage(&damage, projectile_index);
}

boolean quake_weapons_blast_tag(long definition_index)
{
	char const *name;
	if (definition_index == NONE) return FALSE;
	name = tag_get_name(definition_index);
	return !strcmp(name, "weapons\\rocket launcher\\explosion") ||
		!strcmp(name, "weapons\\frag grenade\\explosion");
}

boolean quake_weapons_blast_visible(real_point3d const *origin, long unit_index)
{
	static short const offsets[5][2] = { {0,0}, {15,15}, {-15,-15}, {-15,15}, {15,-15} };
	struct unit_datum *unit = unit_get(unit_index);
	unsigned long flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_machines_bit);
	short ray;
	for (ray = 0; ray < 5; ++ray)
	{
		real_point3d target = unit->object.position;
		real_vector3d delta;
		struct collision_result collision;
		target.x += offsets[ray][0] * QUAKE_WORLD_SCALE;
		target.y += offsets[ray][1] * QUAKE_WORLD_SCALE;
		target.z += 24.f * QUAKE_WORLD_SCALE;
		vector_from_points3d(origin, &target, &delta);
		if (!collision_test_vector(flags, origin, &delta, unit_index, &collision)) return TRUE;
	}
	return FALSE;
}

void quake_weapons_grenade_bounce(real_vector3d *velocity, real_vector3d const *normal)
{
	real backoff = dot_product3d(velocity, normal) * 1.5f;
	real epsilon = 0.1f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;
	short axis;
	for (axis = 0; axis < 3; ++axis)
	{
		velocity->n[axis] -= backoff * normal->n[axis];
		if (fabs(velocity->n[axis]) < epsilon) velocity->n[axis] = 0.f;
	}
	if (normal->k > 0.7f && velocity->k < 60.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND)
		*velocity = *global_zero_vector3d;
}
