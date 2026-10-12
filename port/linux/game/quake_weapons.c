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

static void quake1_grenade_bounce(real_vector3d *velocity, real_vector3d const *normal);

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
	SET_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_Q2_BIT, quake_weapons_tag_kind(projectile->definition_index) && quake_player_weapons(projectile->object.owner_object_index) == _quake_weapons_quake2);
	if (TEST_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_Q2_BIT))
		projectile->projectile.maximum_damage_distance = quake_weapons_tag_kind(projectile->definition_index) == 1 ? (real)(long)real_random_range(100.f, 120.f) : 120.f;
	SET_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_Q3_BIT, quake_player_weapons(projectile->object.owner_object_index) == _quake_weapons_quake3);
	SET_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_BIT,
		quake_weapons_tag_kind(projectile->definition_index) && quake_player_weapons(projectile->object.owner_object_index));
}
short quake_weapons_projectile_kind(long projectile_index)
{
	struct projectile_datum *projectile = projectile_get(projectile_index);
	return TEST_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_BIT) ?
		quake_weapons_tag_kind(projectile->definition_index) : 0;
}
short quake_weapons_projectile_profile(long projectile_index)
{
	if (TEST_FLAG(projectile_get(projectile_index)->projectile.flags, QUAKE_PROJECTILE_Q2_BIT)) return _quake_weapons_quake2;
	return TEST_FLAG(projectile_get(projectile_index)->projectile.flags, QUAKE_PROJECTILE_Q3_BIT) ? _quake_weapons_quake3 : _quake_weapons_quake1;
}
void quake_weapons_launch(long projectile_index)
{
	struct projectile_datum *projectile = projectile_get(projectile_index);
	struct unit_datum *owner = unit_try_and_get(projectile->object.owner_object_index);
	short kind = quake_weapons_projectile_kind(projectile_index);
	boolean q3 = quake_weapons_projectile_profile(projectile_index) == _quake_weapons_quake3;
	boolean q2 = quake_weapons_projectile_profile(projectile_index) == _quake_weapons_quake2;
	short axis;
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
			position.z += q2 ? MAX(0.f, (height + 2.f * radius) * (6.f / 7.f) - 10.f * QUAKE_WORLD_SCALE) :
				(height + 2.f * radius) * ((q3 ? 50.f : kind == 1 ? 40.f : 24.f) / 56.f);
		}
		else position.z += (kind == 1 ? 40.f : 24.f) * QUAKE_WORLD_SCALE;
	}
	if (q3)
	{
		point_from_line3d(&position, &aim, 14.f * QUAKE_WORLD_SCALE, &position);
		if (kind == 2) { aim.k += 0.2f; normalize3d(&aim); }
		scale_vector3d(&aim, kind == 1 ? 900.f : 700.f, &velocity);
		for (axis = 0; axis < 3; ++axis)
			velocity.n[axis] = (long)velocity.n[axis] * scale;
		position.x = (long)(position.x / QUAKE_WORLD_SCALE) * QUAKE_WORLD_SCALE;
		position.y = (long)(position.y / QUAKE_WORLD_SCALE) * QUAKE_WORLD_SCALE;
		position.z = (long)(position.z / QUAKE_WORLD_SCALE) * QUAKE_WORLD_SCALE;
	}
	else if (q2)
	{
		point_from_line3d(&position, &aim, 8.f * QUAKE_WORLD_SCALE, &position);
		point_from_line3d(&position, &right, 8.f * QUAKE_WORLD_SCALE, &position);
		scale_vector3d(&aim, (kind == 1 ? 650.f : 600.f) * scale, &velocity);
		if (kind == 2)
		{
			real vertical = real_random_range(-10.f, 10.f), sideways = real_random_range(-10.f, 10.f);
			velocity.i += (up.i * (200.f + vertical) + right.i * sideways) * scale;
			velocity.j += (up.j * (200.f + vertical) + right.j * sideways) * scale;
			velocity.k += (up.k * (200.f + vertical) + right.k * sideways) * scale;
		}
	}
	else if (kind == 1)
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
static long quake_direct_target = NONE;
long quake_weapons_direct_target(void) { return quake_direct_target; }
void quake_weapons_explode(long projectile_index)
{
	struct projectile_datum *projectile = projectile_get(projectile_index);
	short kind = quake_weapons_projectile_kind(projectile_index);
	long definition = tag_loaded('jpt!', kind == 1 ? "weapons\\rocket launcher\\explosion" : "weapons\\frag grenade\\explosion");
	struct damage_data damage;
	long saved_target = quake_direct_target;
	boolean q3 = quake_weapons_projectile_profile(projectile_index) == _quake_weapons_quake3;
	boolean q2 = quake_weapons_projectile_profile(projectile_index) == _quake_weapons_quake2;
	if (definition == NONE) return;
	damage_data_new(&damage, definition);
	SET_FLAG(damage.flags, _damage_quake_blast_bit, TRUE);
	SET_FLAG(damage.flags, _damage_quake3_blast_bit, q3);
	SET_FLAG(damage.flags, _damage_quake2_blast_bit, q2);
	SET_FLAG(damage.flags, _damage_quake2_grenade_bit, q2 && kind == 2);
	SET_FLAG(damage.flags, _damage_quake3_grenade_bit, q3 && kind == 2);
	SET_FLAG(damage.flags, _damage_from_weapon_bit, TRUE);
	damage.origin = damage.epicenter = projectile->object.position;
	damage.location = projectile->object.location;
	damage.direction = projectile->object.forward;
	damage.owner_object_index = projectile->object.owner_object_index;
	damage.owner_player_index = projectile->object.owner_player_index;
	damage.owner_team_index = projectile->object.owner_team_index;
	quake_direct_target = (q3 || q2) && TEST_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_DIRECT_BIT) ? projectile->projectile.target_object_index : NONE;
	area_of_effect_cause_damage(&damage, projectile_index);
	quake_direct_target = saved_target;
}

boolean quake_weapons_blast_tag(long definition_index)
{
	char const *name;
	if (definition_index == NONE) return FALSE;
	name = tag_get_name(definition_index);
	return !strcmp(name, "weapons\\rocket launcher\\explosion") ||
		!strcmp(name, "weapons\\frag grenade\\explosion");
}

void quake_weapons_grenade_bounce(real_vector3d *velocity, real_vector3d const *normal, short profile)
{
	short axis;
	real backoff;
	if (profile != _quake_weapons_quake3) { quake1_grenade_bounce(velocity, normal); return; }
	backoff = dot_product3d(velocity, normal) * 2.f;
	for (axis = 0; axis < 3; ++axis) velocity->n[axis] = (velocity->n[axis] - normal->n[axis] * backoff) * 0.65f;
	if (normal->k > 0.2f && magnitude3d(velocity) < 40.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND) *velocity = *global_zero_vector3d;
}
real quake3_blast_points(real distance, boolean grenade)
{
	real radius = (grenade ? 150.f : 120.f) * QUAKE_WORLD_SCALE;
	return distance >= radius ? 0.f : (real)(long)MAX(0.f, 100.f * (1.f - distance / radius));
}
real quake3_damage_distance(real_point3d const *origin, long object_index)
{
	struct object_datum *object = object_get(object_index);
	real_point3d base = object->object.position;
	real height = 56.f * QUAKE_WORLD_SCALE, radius = 15.f * QUAKE_WORLD_SCALE;
	real dx, dy, dz;
	if (biped_try_and_get(object_index))
	{
		real_point3d center;
		biped_get_physics_pill(object_index, &center, &height, &radius);
		height += 2.f * radius;
	}
	dx = MAX(0.f, fabs(origin->x - base.x) - radius);
	dy = MAX(0.f, fabs(origin->y - base.y) - radius);
	dz = MAX(0.f, MAX(base.z - origin->z, origin->z - base.z - height));
	return square_root(dx*dx + dy*dy + dz*dz);
}
real quake2_blast_points(real distance, boolean self_damage, boolean grenade)
{
	if (distance < 0.f || distance > (grenade ? 160.f : 120.f) * QUAKE_WORLD_SCALE) return 0.f;
	return (real)(long)(MAX(0.f, 120.f - 0.5f * distance / QUAKE_WORLD_SCALE) * (self_damage ? 0.5f : 1.f));
}
real quake2_direct_points(long projectile_index)
{
	struct projectile_datum *projectile = projectile_get(projectile_index);
	real points = projectile->projectile.maximum_damage_distance;
	if (quake_weapons_projectile_kind(projectile_index) == 2)
	{
		struct object_datum *target = object_get(projectile->projectile.target_object_index);
		real_point3d center = target->object.position;
		center.z += 28.f * QUAKE_WORLD_SCALE;
		points -= 0.5f * distance3d(&projectile->object.position, &center) / QUAKE_WORLD_SCALE;
	}
	return (real)(long)MAX(0.f, points);
}
void quake_weapons_impact(long projectile_index)
{
	struct projectile_datum *projectile = projectile_get(projectile_index);
	struct damage_data damage;
	long definition;
	boolean q2 = quake_weapons_projectile_profile(projectile_index) == _quake_weapons_quake2;
	if ((!q2 && quake_weapons_projectile_profile(projectile_index) != _quake_weapons_quake3) ||
		!TEST_FLAG(projectile->projectile.flags, QUAKE_PROJECTILE_DIRECT_BIT)) return;
	definition = tag_loaded('jpt!', quake_weapons_projectile_kind(projectile_index) == 1 ? "weapons\\rocket launcher\\explosion" : "weapons\\frag grenade\\explosion");
	if (definition == NONE) return;
	damage_data_new(&damage, definition);
	SET_FLAG(damage.flags, _damage_quake3_direct_bit, !q2);
	SET_FLAG(damage.flags, _damage_quake2_direct_bit, q2);
	SET_FLAG(damage.flags, _damage_from_weapon_bit, TRUE);
	damage.scale = q2 ? quake2_direct_points(projectile_index) / 120.f : 1.f;
	damage.origin = damage.epicenter = projectile->object.position;
	damage.location = projectile->object.location;
	damage.direction = projectile->object.translational_velocity;
	if (q2 && quake_weapons_projectile_kind(projectile_index) == 2)
	{
		real_point3d target = object_get(projectile->projectile.target_object_index)->object.position;
		target.z += 24.f * QUAKE_WORLD_SCALE;
		vector_from_points3d(&damage.origin, &target, &damage.direction);
	}
	normalize3d(&damage.direction);
	damage.owner_object_index = projectile->object.owner_object_index;
	damage.owner_player_index = projectile->object.owner_player_index;
	damage.owner_team_index = projectile->object.owner_team_index;
	object_cause_damage(&damage, projectile->projectile.target_object_index, NONE, NONE, NONE, NULL);
}

boolean quake3_blast_visible(real_point3d const *origin, long unit_index)
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
		{
			real_point3d base;
			real height, radius;
			biped_get_physics_pill(unit_index, &base, &height, &radius);
			target.z += (height + 2.f * radius) * 0.5f;
		}
		vector_from_points3d(origin, &target, &delta);
		if (!collision_test_vector(flags, origin, &delta, unit_index, &collision)) return TRUE;
	}
	return FALSE;
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

static void quake1_grenade_bounce(real_vector3d *velocity, real_vector3d const *normal)
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
