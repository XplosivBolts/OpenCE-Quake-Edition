/* Quake-style movement in Halo world units per 30 Hz tick. */
#ifndef QUAKE_MOVEMENT_H
#define QUAKE_MOVEMENT_H

#include "math/real_math.h"
#include "physics/collision_features.h"

/* 80 Quake units per world unit maps the 32 x 56 Quake standing hull
   to Halo's 0.4 x 0.7 standing collider. Use this scale for every length,
   velocity and acceleration; time and dimensionless coefficients stay fixed. */
#define QUAKE_WORLD_SCALE (1.f / 80.f)
#define QUAKE_MOVEMENT_SUBSTEPS 3

enum quake_physics_profile { _quake_physics_halo, _quake_physics_quakeworld, _quake_physics_netquake, _quake_physics_quake3, _quake_physics_quake2 };
enum quake_weapon_profile { _quake_weapons_halo, _quake_weapons_quake1, _quake_weapons_quake3, _quake_weapons_quake2 };

struct quake_motion
{
	long biped_index;
	short style;
	short q2_land_time;
	unsigned long collision_flags;
	real_point3d position;
	real_vector3d velocity;
	real_vector3d wish, previous_wish;
	real_vector3d view, command; /* command: forward, side, jump-button strength */
	boolean command_known, jump_held;
	real_vector3d wall_normal;
	boolean wall_hit;
	boolean previous_wish_known;
	real height, radius, speed_limit;
	real crouch_velocity;
	real ground_stun_acceleration, air_stun_acceleration;
	real jump_velocity;
	boolean airborne, stunned, jump, jumped, knockback;
	struct collision_plane ground;
	real landing_velocity;
	short collision_count;
	struct collision_plane collisions[16];
};

struct quake_trace
{
	real fraction;
	real_point3d end;
	boolean allsolid;
	struct collision_plane collision;
};
typedef void (*quake_trace_function)(void *context, real_point3d const *start,
	real_vector3d const *delta, struct quake_trace *trace);

void quake_motion_integrate(struct quake_motion *motion, quake_trace_function trace,
	quake_trace_function edge_trace, void *context);
void quake_motion_halo(struct quake_motion *motion);
boolean quake_motion_climbable(struct quake_motion const *motion);

boolean quake_player_active(long biped_index);
boolean quake_movement_local_enabled(void);
char const *quake_movement_setting(short local_player_index);
short quake_movement_preference(short local_player_index);
char const *quake_weapons_setting(short local_player_index);
short quake_weapons_preference(short local_player_index);
void quake_player_weapons_request(long player_index, long tick, short enabled);
boolean quake_player_weapons_known(long biped_index);
short quake_player_weapons(long unit_index);
void quake_player_set_weapons(long biped_index, short enabled);
void quake_preferences_reset(void);
void quake_player_request(long player_index, long tick, short enabled);
short quake_player_style(long biped_index);
boolean quake_player_style_known(long biped_index);
void quake_player_set_style(long biped_index, short enabled);
real quake_player_gravity(void);
real quake_player_speed(void);
real quake_player_jump_velocity(void);
real quake_fall_points(real downward_velocity, boolean water);
real quake_movement_self_damage(real distance, real world_scale, real maximum_health);
void quake_movement_step(real_vector3d *velocity, real_vector3d const *wish,
	real speed_limit, real reference_speed, boolean airborne, boolean edge, real gravity);
void quake_movement_step_time(real_vector3d *velocity, real_vector3d const *wish,
	real speed_limit, real reference_speed, boolean airborne, boolean edge, real gravity, real dt);
void quake_movement_halo_stun(real_vector3d *velocity, real_vector3d const *wish, real acceleration);
real quake_movement_crouch_speed(real native_run, real native_sneak, real crouch);
real quake_movement_splash_points(real distance, real world_scale, boolean self_damage);
real quake_movement_blast_velocity(real distance, boolean self_damage);
void quake_player_blast(long biped_index, real_vector3d const *acceleration);

short quake_profile_parse(char const *text, boolean weapons);
char const *quake_profile_name(short profile, boolean weapons);
void quake_movement_step_profile(real_vector3d *velocity, real_vector3d const *wish, real speed_limit, real reference_speed, boolean airborne, boolean edge, real gravity, real dt, short style);
real quake3_fall_points(real downward_velocity, real crouch, boolean water);
boolean quake_player_knockback(long biped_index);
void quake_player_motion_heading(long biped_index, real_vector3d const *wish, real_vector3d *previous, boolean *known);
real quake_player_profile_speed(short style);
short quake2_player_land_time(long biped_index, short time, boolean write);
real quake2_fall_points(real downward_velocity, boolean water);
#endif
