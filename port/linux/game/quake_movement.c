/*
Independent implementation of Quake 1's directional acceleration and
friction equations, using Halo's tagged movement speeds and gravity.
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
char const *config_string(char const *name);
long game_time_get(void);

short quake_profile_parse(char const *text, boolean weapons)
{
	if (!text || !strcmp(text, "halo") || !strcmp(text, "false") || !strcmp(text, "0")) return 0;
	if (!strcmp(text, "quake2")) return weapons ? _quake_weapons_quake2 : _quake_physics_quake2;
	if (!strcmp(text, "quake3")) return weapons ? _quake_weapons_quake3 : _quake_physics_quake3;
	if (!weapons && !strcmp(text, "netquake")) return _quake_physics_netquake;
	if (!strcmp(text, "true") || !strcmp(text, "1") || !strcmp(text, "quake1") || !strcmp(text, "quakeworld")) return 1;
	return 0;
}
char const *quake_profile_name(short profile, boolean weapons)
{
	static char const *const physics[] = { "halo", "quakeworld", "netquake", "quake3", "quake2" };
	static char const *const weapon[] = { "halo", "quake1", "quake3", "quake2" };
	return weapons ? weapon[PIN(profile, 0, 3)] : physics[PIN(profile, 0, 4)];
}

boolean quake_movement_local_enabled(void)
{
	return quake_profile_parse(config_string("game.quake_physics"), FALSE) != 0;
}

char const *quake_movement_setting(short local_player_index)
{
	static char const *const settings[MAXIMUM_LOCAL_PLAYERS] = {
		"game.quake_physics", "game.quake_physics_player2",
		"game.quake_physics_player3", "game.quake_physics_player4"
	};

	return settings[PIN(local_player_index, 0, MAXIMUM_LOCAL_PLAYERS - 1)];
}

short quake_movement_preference(short local_player_index)
{
	return quake_profile_parse(config_string(quake_movement_setting(local_player_index)), FALSE);
}

struct quake_player_preference
{
	boolean valid;
	boolean known;
	short requested;
	boolean weapons_known;
	short weapons_requested;
	long weapons_tick;
	long knockback_unit, knockback_until;
	long motion_unit, motion_tick;
	long q2_land_unit;
	short q2_land_time;
	long q3_land_unit;
	short q3_land_time;
	real_vector3d motion_wish;
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

void quake_player_request(long player_index, long tick, short enabled)
{
	struct quake_player_preference *preference = quake_preference(player_index);

	if (!preference || tick < 0 || tick <= preference->tick)
		return;
	preference->known = TRUE;
	preference->requested = PIN(enabled, 0, 4);
	preference->tick = tick;
}

boolean quake_player_style_known(long biped_index)
{
	return TEST_FLAG(biped_get(biped_index)->biped.flags, _biped_quake_style_set_bit);
}

void quake_player_set_style(long biped_index, short enabled)
{
	struct biped_datum *biped = biped_get(biped_index);
	enabled = PIN(enabled, 0, 4);

	if (!quake_player_style_known(biped_index) ||
		(TEST_FLAG(biped->biped.flags, _biped_quake_style_bit) | (TEST_FLAG(biped->biped.flags, _biped_quake_style_high_bit) << 1) | (TEST_FLAG(biped->biped.flags, _biped_quake_style_extra_bit) << 2)) != enabled)
	{
		SET_FLAG(biped->biped.flags, _biped_quake_jump_held_bit, FALSE);
		quake2_player_land_time(biped_index, 0, TRUE);
		quake3_player_land_time(biped_index, 0, TRUE);
	}
	SET_FLAG(biped->biped.flags, _biped_quake_style_bit, enabled & 1);
	SET_FLAG(biped->biped.flags, _biped_quake_style_high_bit, enabled & 2);
	SET_FLAG(biped->biped.flags, _biped_quake_style_extra_bit, enabled & 4);
	SET_FLAG(biped->biped.flags, _biped_quake_style_set_bit, TRUE);
}

short quake_player_style(long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct player_datum *player = biped->unit.player_index != NONE ?
		player_try_and_get(biped->unit.player_index) : NULL;
	struct quake_player_preference *preference;

	if (!player)
		return FALSE;
	if (!network_game_get_game())
	{
		short profile = quake_movement_preference(player->local_player_index);
		quake_player_set_style(biped_index, profile);
		return profile;
	}
	if (quake_player_style_known(biped_index))
		return TEST_FLAG(biped->biped.flags, _biped_quake_style_bit) | (TEST_FLAG(biped->biped.flags, _biped_quake_style_high_bit) << 1) | (TEST_FLAG(biped->biped.flags, _biped_quake_style_extra_bit) << 2);
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
	return TEST_FLAG(biped->biped.flags, _biped_quake_style_bit) | (TEST_FLAG(biped->biped.flags, _biped_quake_style_high_bit) << 1) | (TEST_FLAG(biped->biped.flags, _biped_quake_style_extra_bit) << 2);
}

real quake_player_gravity(void)
{
	return 800.f * QUAKE_WORLD_SCALE / (TICKS_PER_SECOND * TICKS_PER_SECOND);
}

real quake_player_speed(void)
{
	return 320.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;
}

real quake_player_profile_speed(short style)
{
	return (style == _quake_physics_quake2 ? 300.f : 320.f) * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;
}

short quake2_player_land_time(long biped_index, short time, boolean write)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct quake_player_preference *preference = quake_preference(biped->unit.player_index);
	if (!preference) return 0;
	if (preference->q2_land_unit != biped_index)
	{
		preference->q2_land_unit = biped_index;
		preference->q2_land_time = 0;
	}
	if (write) preference->q2_land_time = PIN(time, 0, 25);
	return preference->q2_land_time;
}

short quake3_player_land_time(long biped_index, short time, boolean write)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct quake_player_preference *preference = quake_preference(biped->unit.player_index);
	if (!preference) return 0;
	if (preference->q3_land_unit != biped_index)
	{
		preference->q3_land_unit = biped_index;
		preference->q3_land_time = 0;
	}
	if (write) preference->q3_land_time = PIN(time, 0, 250);
	return preference->q3_land_time;
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

void quake_movement_step_profile(real_vector3d *velocity, real_vector3d const *wish,
	real speed_limit, real reference_speed, boolean airborne, boolean edge, real gravity, real dt, short style)
{
	boolean uncapped = style == _quake_physics_quake3 || style == _quake_physics_quake2;
	real_vector3d direction = *wish;
	real wish_speed;
	real target_speed;
	real current_speed;
	real addition;
	real acceleration;

	if (style != _quake_physics_quake3 || airborne) direction.k = 0.f;
	wish_speed = normalize3d(&direction);
	wish_speed = MIN(wish_speed, MAX(speed_limit, 0.f));
	if (uncapped && (airborne ? magnitude3d(velocity) : square_root(velocity->i*velocity->i + velocity->j*velocity->j)) < QUAKE_WORLD_SCALE / TICKS_PER_SECOND)
		velocity->i = velocity->j = 0.f;
	if (!airborne)
	{
		real speed = style == _quake_physics_quake3 ? square_root(velocity->i * velocity->i + velocity->j * velocity->j) : magnitude3d(velocity);
		real stop_speed = reference_speed * (100.f / (style == _quake_physics_quake2 ? 300.f : 320.f));

		if (speed > 0.f)
		{
			real remaining = MAX(0.f, speed - MAX(speed, stop_speed) *
				((uncapped ? 6.f : 4.f) * dt) * (edge && !uncapped ? 2.f : 1.f));
			real fraction = remaining / speed;

			velocity->i *= fraction;
			velocity->j *= fraction;
			velocity->k *= fraction;
		}
	}
	/* The air target uses the capped speed; acceleration uses the full
	input speed. Preserving that difference permits strafe speed gains. */
	target_speed = airborne && !uncapped ? MIN(wish_speed, reference_speed * (30.f / 320.f)) : wish_speed;
	current_speed = velocity->i * direction.i + velocity->j * direction.j + (style == _quake_physics_quake3 ? velocity->k * direction.k : 0.f);
	addition = target_speed - current_speed;
	if (wish_speed > 0.f && addition > 0.f)
	{
		acceleration = MIN(addition, (airborne && uncapped ? 1.f : 10.f) * dt * wish_speed);
		velocity->i += direction.i * acceleration;
		velocity->j += direction.j * acceleration;
		if (style == _quake_physics_quake3) velocity->k += direction.k * acceleration;
	}
	/* Halo stores velocity and gravity as displacement per tick and
	change in that displacement per tick, respectively. */
	velocity->k -= gravity * dt * TICKS_PER_SECOND;
}

void quake_movement_step_time(real_vector3d *velocity, real_vector3d const *wish,
	real speed_limit, real reference_speed, boolean airborne, boolean edge, real gravity, real dt)
{
	quake_movement_step_profile(velocity, wish, speed_limit, reference_speed, airborne, edge, gravity, dt, _quake_physics_quakeworld);
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

boolean quake_player_knockback(long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct quake_player_preference *preference = quake_preference(biped->unit.player_index);
	return preference && preference->knockback_unit == biped_index && game_time_get() < preference->knockback_until;
}
void quake_player_blast(long biped_index, real_vector3d const *acceleration)
{
	struct biped_datum *biped = biped_get(biped_index);
	if (magnitude3d(acceleration) == 0.f) return;

	if (quake_player_style(biped_index) == _quake_physics_quake3 && !quake_player_knockback(biped_index) && !quake3_player_land_time(biped_index, 0, FALSE))
	{
		struct quake_player_preference *preference = quake_preference(biped->unit.player_index);
		real points = magnitude3d(acceleration) / QUAKE_WORLD_SCALE * TICKS_PER_SECOND / 5.f;
		if (preference)
		{
			preference->knockback_unit = biped_index;
			preference->knockback_until = game_time_get() + (long)ceil(PIN(points * 2.f, 50.f, 200.f) * TICKS_PER_SECOND / 1000.f);
		}
	}
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
short quake_weapons_preference(short local_player_index)
{
	return quake_profile_parse(config_string(quake_weapons_setting(local_player_index)), TRUE);
}
void quake_player_weapons_request(long player_index, long tick, short enabled)
{
	struct quake_player_preference *preference = quake_preference(player_index);
	if (!preference || tick < 0 || tick <= preference->weapons_tick) return;
	preference->weapons_known = TRUE;
	preference->weapons_requested = PIN(enabled, 0, 3);
	preference->weapons_tick = tick;
}
boolean quake_player_weapons_known(long biped_index)
{
	return TEST_FLAG(biped_get(biped_index)->biped.flags, _biped_quake_weapons_set_bit);
}
void quake_player_set_weapons(long biped_index, short enabled)
{
	struct biped_datum *biped = biped_get(biped_index);
	enabled = PIN(enabled, 0, 3);
	SET_FLAG(biped->biped.flags, _biped_quake_weapons_bit, enabled & 1);
	SET_FLAG(biped->biped.flags, _biped_quake_weapons_high_bit, enabled & 2);
	SET_FLAG(biped->biped.flags, _biped_quake_weapons_set_bit, TRUE);
}
short quake_player_weapons(long unit_index)
{
	struct biped_datum *biped = biped_try_and_get(unit_index);
	struct player_datum *player;
	struct quake_player_preference *preference;
	if (!biped || biped->unit.player_index == NONE) return FALSE;
	player = player_try_and_get(biped->unit.player_index);
	if (!player) return FALSE;
	if (!network_game_get_game())
	{
		short profile = quake_weapons_preference(player->local_player_index);
		quake_player_set_weapons(unit_index, profile);
		return profile;
	}
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
	return TEST_FLAG(biped->biped.flags, _biped_quake_weapons_bit) | (TEST_FLAG(biped->biped.flags, _biped_quake_weapons_high_bit) << 1);
}

/* Fixed Quake landing damage; Halo health/shields use the common damage path. */
real quake_fall_points(real downward_velocity, boolean water)
{
	return !water && downward_velocity > 650.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND ? 5.f : 0.f;
}


real quake3_fall_points(real downward_velocity, real crouch, boolean water)
{
	real speed = downward_velocity / QUAKE_WORLD_SCALE * TICKS_PER_SECOND;
	real severity = speed * speed * 0.0001f * (crouch > 0.5f ? 2.f : 1.f);
	return water ? 0.f : severity > 60.f ? 10.f : severity > 40.f ? 5.f : 0.f;
}

void quake_player_motion_heading(long biped_index, real_vector3d const *wish, real_vector3d *previous, boolean *known)
{
	struct quake_player_preference *preference = quake_preference(biped_get(biped_index)->unit.player_index);
	*known = preference && preference->motion_unit == biped_index && preference->motion_tick == game_time_get() - 1 &&
		magnitude3d(&preference->motion_wish) > 0.f && magnitude3d(wish) > 0.f;
	if (*known) *previous = preference->motion_wish;
	if (preference) { preference->motion_wish = *wish; preference->motion_unit = biped_index; preference->motion_tick = game_time_get(); }
}

real quake2_fall_points(real downward_velocity, boolean water)
{
	real speed = downward_velocity / QUAKE_WORLD_SCALE * TICKS_PER_SECOND;
	real severity = speed * speed * 0.0001f;
	/* Halo supplies an underwater boolean, not Q2 water-depth levels. */
	return water || severity <= 30.f ? 0.f : MAX(1.f, (real)(long)((severity - 30.f) * 0.5f));
}
