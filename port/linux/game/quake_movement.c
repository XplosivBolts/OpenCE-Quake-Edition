/*
Independent implementation of Quake 1's directional acceleration and
friction equations, with Quake baselines at a shared proportional world scale.
Jump-before-friction follows QuakeWorld's player movement ordering. Halo's
pill collision queries and support bookkeeping remain in bipeds.c.
*/
#include "cseries.h"
#include "quake_movement.h"
#include "cutscene/cinematics.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "physics/physics.h"
#include "networking/network_game_globals.h"
#include "game/players.h"

int config_boolean(char const *name);

boolean quake_movement_local_enabled(void)
{
	return config_boolean("game.quake_physics") != 0;
}

char const *quake_movement_setting(short local_player_index)
{
	static char const *const settings[MAXIMUM_LOCAL_PLAYERS] = {
		"game.quake_physics", "game.quake_physics_player2",
		"game.quake_physics_player3", "game.quake_physics_player4"
	};

	return settings[PIN(local_player_index, 0, MAXIMUM_LOCAL_PLAYERS - 1)];
}

boolean quake_movement_preference(short local_player_index)
{
	return config_boolean(quake_movement_setting(local_player_index)) != 0;
}

struct quake_player_preference
{
	boolean valid;
	boolean known;
	boolean requested;
	boolean weapons_known, weapons_requested;
	long weapons_tick;
	long player_index;
	long tick;
};
static struct quake_player_preference quake_preferences[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];

void quake_preferences_reset(void)
{
	csmemset(quake_preferences, 0, sizeof(quake_preferences));
}

static struct quake_player_preference *quake_preference(long player_index)
{
	long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	struct quake_player_preference *preference;

	if (player_index == NONE || !VALID_INDEX(slot, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
		return NULL;
	preference = &quake_preferences[slot];
	if (!preference->valid || preference->player_index != player_index)
	{
		csmemset(preference, 0, sizeof(*preference));
		preference->valid = TRUE;
		preference->player_index = player_index;
		preference->tick = NONE;
		preference->weapons_tick = NONE;
	}
	return preference;
}

void quake_player_request(long player_index, long tick, boolean enabled)
{
	struct quake_player_preference *preference = quake_preference(player_index);

	if (!preference || tick < 0 || tick <= preference->tick)
		return;
	preference->known = TRUE;
	preference->requested = enabled != FALSE;
	preference->tick = tick;
}

boolean quake_player_style_known(long biped_index)
{
	return TEST_FLAG(biped_get(biped_index)->biped.flags, _biped_quake_style_set_bit);
}

void quake_player_set_style(long biped_index, boolean enabled)
{
	struct biped_datum *biped = biped_get(biped_index);

	if (!quake_player_style_known(biped_index) ||
		TEST_FLAG(biped->biped.flags, _biped_quake_style_bit) != enabled)
		SET_FLAG(biped->biped.flags, _biped_quake_jump_held_bit, FALSE);
	SET_FLAG(biped->biped.flags, _biped_quake_style_bit, enabled);
	SET_FLAG(biped->biped.flags, _biped_quake_style_set_bit, TRUE);
}

boolean quake_player_style(long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct player_datum *player = biped->unit.player_index != NONE ?
		player_try_and_get(biped->unit.player_index) : NULL;
	struct quake_player_preference *preference;

	if (!player)
		return FALSE;
	if (!network_game_get_game())
		return quake_movement_preference(player->local_player_index);
	if (quake_player_style_known(biped_index))
		return TEST_FLAG(biped->biped.flags, _biped_quake_style_bit);
	if (player->local_player_index != NONE)
		quake_player_set_style(biped_index, quake_movement_preference(player->local_player_index));
	else if (!network_game_distributed_client())
	{
		preference = quake_preference(biped->unit.player_index);
		/* The first authenticated input selects the first life. Until it
		arrives, do not announce an unconfirmed CE default to clients. */
		if (preference && preference->known)
			quake_player_set_style(biped_index, preference->requested);
	}
	return TEST_FLAG(biped->biped.flags, _biped_quake_style_bit);
}

real quake_player_gravity(void)
{
	return 800.f * QUAKE_WORLD_SCALE / (TICKS_PER_SECOND * TICKS_PER_SECOND);
}

real quake_player_speed(void)
{
	return 320.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;
}

real quake_player_jump_velocity(void)
{
	return 270.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;
}

real quake_movement_self_damage(real distance, real world_scale, real maximum_health)
{
	return quake_movement_splash_points(distance, world_scale, TRUE) * MAX(maximum_health, 0.f) / 100.f;
}

real quake_movement_splash_points(real distance, real world_scale, boolean self_damage)
{
	real quake_distance;

	if (world_scale <= 0.f || distance < 0.f)
		return 0.f;
	quake_distance = distance / world_scale;
	/* Quake rockets and grenades both use 120 splash points, enumerate
	within 160 units, subtract half the distance, then halve self damage.
	A full Halo body-health bar represents Quake's 100 starting health. */
	if (quake_distance > 160.f)
		return 0.f;
	return MAX(0.f, 120.f - 0.5f * quake_distance) * (self_damage ? 0.5f : 1.f);
}

boolean quake_player_active(long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	return quake_player_style(biped_index) && biped->unit.player_index != NONE && biped->object.parent_object_index == NONE &&
		!cinematic_in_progress() && !TEST_FLAG(biped->object.damage_flags, _object_dead_bit) &&
		!TEST_FLAG(biped->unit.flags, _unit_suspended_bit) &&
		biped->unit.animation.state != _unit_state_user_animation &&
		TEST_FLAG(definition->biped.flags, _biped_uses_player_physics_bit) &&
		!(definition->biped.flags & (FLAG(_biped_flying_bit) | FLAG(_biped_climbs_anything_bit))) &&
		!(biped->biped.flags & (FLAG(_biped_absolute_movement_bit) | FLAG(_biped_no_collision_bit)));
}

void quake_movement_step(real_vector3d *velocity, real_vector3d const *wish,
	real speed_limit, real reference_speed, boolean airborne, boolean edge, real gravity)
{
	quake_movement_step_time(velocity, wish, speed_limit, reference_speed, airborne, edge,
		gravity, 1.f / TICKS_PER_SECOND);
}

void quake_movement_step_time(real_vector3d *velocity, real_vector3d const *wish,
	real speed_limit, real reference_speed, boolean airborne, boolean edge, real gravity, real dt)
{
	real_vector3d direction = *wish;
	real wish_speed;
	real target_speed;
	real current_speed;
	real addition;
	real acceleration;

	direction.k = 0.f;
	wish_speed = normalize3d(&direction);
	wish_speed = MIN(wish_speed, MAX(speed_limit, 0.f));
	if (!airborne)
	{
		real speed = magnitude3d(velocity);
		real stop_speed = reference_speed * (100.f / 320.f);

		if (speed > 0.f)
		{
			real remaining = MAX(0.f, speed - MAX(speed, stop_speed) *
				(4.f * dt) * (edge ? 2.f : 1.f));
			real fraction = remaining / speed;

			velocity->i *= fraction;
			velocity->j *= fraction;
			velocity->k *= fraction;
		}
	}
	/* The air target uses the capped speed; acceleration uses the full
	input speed. Preserving that difference permits strafe speed gains. */
	target_speed = airborne ? MIN(wish_speed, reference_speed * (30.f / 320.f)) : wish_speed;
	current_speed = velocity->i * direction.i + velocity->j * direction.j;
	addition = target_speed - current_speed;
	if (wish_speed > 0.f && addition > 0.f)
	{
		acceleration = MIN(addition, 10.f * dt * wish_speed);
		velocity->i += direction.i * acceleration;
		velocity->j += direction.j * acceleration;
	}
	/* Halo stores velocity and gravity as displacement per tick and
	change in that displacement per tick, respectively. */
	velocity->k -= gravity * dt * TICKS_PER_SECOND;
}

void quake_movement_halo_stun(real_vector3d *velocity, real_vector3d const *wish, real acceleration)
{
	real_vector3d delta;
	real length;

	/* Halo's injury steering remains while stunned, including in the air.
	This prevents preserved Quake momentum from bypassing plasma-rifle stun. */
	delta.i = wish->i - velocity->i;
	delta.j = wish->j - velocity->j;
	delta.k = 0.f;
	length = normalize3d(&delta);
	length = MIN(length, MAX(acceleration, 0.f));
	velocity->i += delta.i * length;
	velocity->j += delta.j * length;
}

real quake_movement_crouch_speed(real native_run, real native_sneak, real crouch)
{
	real ratio = native_run > 0.f ? MAX(native_sneak, 0.f) / native_run : 0.f;

	crouch = PIN(crouch, 0.f, 1.f);
	return quake_player_speed() * (1.f - crouch + crouch * ratio);
}

real quake_movement_blast_velocity(real distance, boolean self_damage)
{
	/* Momentum uses raw splash points before armor/shields, not Halo's
	damage-effect acceleration tag or the eventual vitality lost. */
	return quake_movement_splash_points(distance, QUAKE_WORLD_SCALE, self_damage) *
		8.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;
}

void quake_player_blast(long biped_index, real_vector3d const *acceleration)
{
	struct biped_datum *biped = biped_get(biped_index);

	/* No living-biped halving, upward bias, or rotation of the player's
	facing. An explosion adds momentum to the existing movement. */
	add_vectors3d(&biped->object.translational_velocity, acceleration,
		&biped->object.translational_velocity);
	SET_FLAG(biped->object.flags, _object_at_rest_bit, FALSE);
	SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);
	SET_FLAG(biped->biped.flags, _biped_slipping_bit, TRUE);
}

char const *quake_weapons_setting(short local_player_index)
{
	static char const *const settings[MAXIMUM_LOCAL_PLAYERS] = {
		"game.quake_weapons", "game.quake_weapons_player2",
		"game.quake_weapons_player3", "game.quake_weapons_player4"
	};
	return settings[PIN(local_player_index, 0, MAXIMUM_LOCAL_PLAYERS - 1)];
}
boolean quake_weapons_preference(short local_player_index)
{
	return config_boolean(quake_weapons_setting(local_player_index)) != 0;
}
void quake_player_weapons_request(long player_index, long tick, boolean enabled)
{
	struct quake_player_preference *preference = quake_preference(player_index);
	if (!preference || tick < 0 || tick <= preference->weapons_tick) return;
	preference->weapons_known = TRUE;
	preference->weapons_requested = enabled != FALSE;
	preference->weapons_tick = tick;
}
boolean quake_player_weapons_known(long biped_index)
{
	return TEST_FLAG(biped_get(biped_index)->biped.flags, _biped_quake_weapons_set_bit);
}
void quake_player_set_weapons(long biped_index, boolean enabled)
{
	struct biped_datum *biped = biped_get(biped_index);
	SET_FLAG(biped->biped.flags, _biped_quake_weapons_bit, enabled);
	SET_FLAG(biped->biped.flags, _biped_quake_weapons_set_bit, TRUE);
}
boolean quake_player_weapons(long unit_index)
{
	struct biped_datum *biped = biped_try_and_get(unit_index);
	struct player_datum *player;
	struct quake_player_preference *preference;
	if (!biped || biped->unit.player_index == NONE) return FALSE;
	player = player_try_and_get(biped->unit.player_index);
	if (!player) return FALSE;
	if (!network_game_get_game()) return quake_weapons_preference(player->local_player_index);
	if (!quake_player_weapons_known(unit_index))
	{
		if (player->local_player_index != NONE)
			quake_player_set_weapons(unit_index, quake_weapons_preference(player->local_player_index));
		else if (!network_game_distributed_client())
		{
			preference = quake_preference(biped->unit.player_index);
			if (preference && preference->weapons_known)
    quake_player_set_weapons(unit_index, preference->weapons_requested);
		}
	}
	return TEST_FLAG(biped->biped.flags, _biped_quake_weapons_bit);
}

/* Fixed Quake landing damage; Halo health/shields use the common damage path. */
real quake_fall_points(real downward_velocity, boolean water)
{
	return !water && downward_velocity > 650.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND ? 5.f : 0.f;
}
