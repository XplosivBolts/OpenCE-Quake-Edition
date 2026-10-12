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

struct quake_motion
{
	long biped_index;
	unsigned long collision_flags;
	real_point3d position;
	real_vector3d velocity;
	real_vector3d wish;
	real height, radius, speed_limit;
	real crouch_velocity;
	real ground_stun_acceleration, air_stun_acceleration;
	real jump_velocity;
	boolean airborne, stunned, jump, jumped;
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
boolean quake_movement_preference(short local_player_index);
char const *quake_weapons_setting(short local_player_index);
boolean quake_weapons_preference(short local_player_index);
void quake_player_weapons_request(long player_index, long tick, boolean enabled);
boolean quake_player_weapons_known(long biped_index);
boolean quake_player_weapons(long unit_index);
void quake_player_set_weapons(long biped_index, boolean enabled);
void quake_preferences_reset(void);
void quake_player_request(long player_index, long tick, boolean enabled);
boolean quake_player_style(long biped_index);
boolean quake_player_style_known(long biped_index);
void quake_player_set_style(long biped_index, boolean enabled);
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

#endif
