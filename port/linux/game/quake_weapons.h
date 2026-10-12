/* Independent projectile behavior; the owning player's movement is separate. */
#ifndef QUAKE_WEAPONS_H
#define QUAKE_WEAPONS_H
#include "cseries.h"
#include "math/real_math.h"
enum { QUAKE_PROJECTILE_BIT = 8, QUAKE_PROJECTILE_STARTED_BIT = 9, QUAKE_PROJECTILE_Q3_BIT = 10, QUAKE_PROJECTILE_DIRECT_BIT = 11, QUAKE_PROJECTILE_Q2_BIT = 12 };
short quake_weapons_tag_kind(long definition_index);
short quake_weapons_projectile_kind(long projectile_index);
boolean quake_weapons_blast_tag(long definition_index);
void quake_weapons_stamp(long projectile_index);
void quake_weapons_launch(long projectile_index);
void quake_weapons_explode(long projectile_index);
short quake_weapons_projectile_profile(long projectile_index);
void quake_weapons_grenade_bounce(real_vector3d *velocity, real_vector3d const *normal, short profile);
real quake3_blast_points(real distance, boolean grenade);
real quake3_damage_distance(real_point3d const *origin, long object_index);
long quake_weapons_direct_target(void);
void quake_weapons_impact(long projectile_index);
boolean quake3_blast_visible(real_point3d const *origin, long unit_index);
real quake2_blast_points(real distance, boolean self_damage, boolean grenade);
real quake2_direct_points(long projectile_index);
boolean quake_weapons_blast_visible(real_point3d const *origin, long unit_index);
#endif
