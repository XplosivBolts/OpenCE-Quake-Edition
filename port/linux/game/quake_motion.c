/* QuakeWorld's slide/step/ground movement rules, independently implemented
over Halo collision queries. Halo's capsule and airborne crouch displacement
are deliberate compatibility choices; combat stun also remains Halo's. */
#include "cseries.h"
#include "quake_movement.h"
long game_time_get(void);
#include "physics/collisions.h"
#include "physics/collision_usage.h"

static void quake_contact(struct quake_motion *motion, struct collision_plane const *collision)
{
	/* Halo treats 16 contacts as a splatter; repeated substep support
	probes must not look like a crushing collision. */
	if (motion->collision_count < 15)
		motion->collisions[motion->collision_count++] = *collision;
}

boolean quake_motion_climbable(struct quake_motion const *motion)
{
	short contact;

	/* Halo collision surface flag bit 2 marks climbable faces. */
	for (contact = 0; contact < motion->collision_count; ++contact)
		if (TEST_FLAG(motion->collisions[contact].flags, 2) && motion->collisions[contact].plane.n.k < 0.7f)
			return TRUE;
	return FALSE;
}

static void quake_clip(real_vector3d const *velocity, real_vector3d const *normal, real_vector3d *out, short style)
{
	real into = dot_product3d(velocity, normal);
	real epsilon = 0.1f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;
	short axis;

	if (style == _quake_physics_quake3) into *= into < 0.f ? 1.001f : (1.f / 1.001f);
	else if (style == _quake_physics_quake2) into *= 1.01f;
	for (axis = 0; axis < 3; ++axis)
	{
		out->n[axis] = velocity->n[axis] - normal->n[axis] * into;
		if (style != _quake_physics_quake3 && fabs(out->n[axis]) < epsilon)
			out->n[axis] = 0.f;
	}
}

static void quake_slide(struct quake_motion *motion, real fraction,
	quake_trace_function trace, void *context)
{
	real_vector3d initial = motion->velocity, original = initial;
	real_vector3d planes[5];
	short count = 0, bump;
	real remaining = fraction;

	motion->wall_hit = FALSE;
	for (bump = 0; bump < 4; ++bump)
	{
		struct quake_trace hit;
		real_vector3d delta, candidate;
		short plane, other;

		if (magnitude_squared3d(&motion->velocity) == 0.f)
			break;
		scale_vector3d(&motion->velocity, remaining, &delta);
		trace(context, &motion->position, &delta, &hit);
		if (hit.allsolid)
		{
			if (motion->style == _quake_physics_quake2) motion->velocity.k = 0.f;
			else motion->velocity = *global_zero_vector3d;
			return;
		}
		if (hit.fraction > 0.f)
		{
			motion->position = hit.end;
			original = motion->velocity;
			count = 0;
		}
		else
			motion->position = hit.end; /* Halo capsule overlap repair can move a zero-time contact */
		if (hit.fraction == 1.f)
			break;
		if (hit.collision.plane.n.k == 0.f)
		{ motion->wall_hit = TRUE; motion->wall_normal = hit.collision.plane.n; }
		quake_contact(motion, &hit.collision);
		if (hit.collision.plane.n.k >= 0.7f && motion->velocity.k < 0.f)
			motion->landing_velocity = MAX(motion->landing_velocity,
				-motion->velocity.k);
		remaining *= 1.f - hit.fraction;
		if (count == 5)
		{
			motion->velocity = motion->style == _quake_physics_quake2 && motion->q2_land_time ? initial : *global_zero_vector3d;
			return;
		}
		planes[count++] = hit.collision.plane.n;
		for (plane = 0; plane < count; ++plane)
		{
			quake_clip(motion->style == _quake_physics_quake2 ? &motion->velocity : &original, &planes[plane], &candidate, motion->style);
			if (motion->style == _quake_physics_quake2) motion->velocity = candidate;
			for (other = 0; other < count; ++other)
				if (other != plane && dot_product3d(&candidate, &planes[other]) < 0.f)
					break;
			if (other == count)
				break;
		}
		if (plane < count)
			motion->velocity = candidate;
		else if (count == 2)
		{
			real_vector3d crease;
			real along;

			cross_product3d(&planes[0], &planes[1], &crease);
			along = dot_product3d(&crease, &motion->velocity);
			scale_vector3d(&crease, along, &motion->velocity);
		}
		else
			motion->velocity = *global_zero_vector3d;
		if (dot_product3d(&motion->velocity, &initial) <= 0.f)
		{
			motion->velocity = motion->style == _quake_physics_quake2 && motion->q2_land_time ? initial : *global_zero_vector3d;
			return;
		}
	}
	if (motion->style == _quake_physics_quake2 && motion->q2_land_time) motion->velocity = initial;
}

/* NetQuake's blocked walking step reduces tangential speed according to
   the view's approach to the wall. Airborne slides do not use this friction. */
static void quake_wall_friction(struct quake_motion *motion, real_vector3d const *normal)
{
	real approach = dot_product3d(normal, &motion->view) + 0.5f;
	real into;
	if (approach >= 0.f) return;
	into = dot_product3d(normal, &motion->velocity);
	motion->velocity.i = (motion->velocity.i - normal->i * into) * (1.f + approach);
	motion->velocity.j = (motion->velocity.j - normal->j * into) * (1.f + approach);
}

/* Q3 command scaling includes a held jump while airborne. A held jump on
   support is cleared by the jump check, even if landing occurs mid-tick. */
static real quake_command_scale(struct quake_motion const *motion)
{
	real f = motion->command.i, side = motion->command.j;
	real up = !motion->airborne && motion->jump_held ? 0.f : motion->command.k;
	real length = square_root(f*f + side*side + up*up);
	real maximum = MAX(fabs(f), MAX(fabs(side), fabs(up)));
	return length > 0.f ? maximum / length : 0.f;
}

static void quake_ground(struct quake_motion *motion, real fraction,
	quake_trace_function trace, void *context)
{
	struct quake_motion down, up;
	struct quake_trace hit;
	real_vector3d delta;
	real dx, dy, down_distance, up_distance;
	real_point3d start = motion->position;
	real step_down = motion->style == _quake_physics_netquake ? motion->velocity.k * fraction : 0.f;

	/* Flat motion first; on obstruction choose the farther of a slide
	and an 18-unit stair attempt, retaining the slide's vertical velocity. */
	motion->velocity.k = 0.f;
	scale_vector3d(&motion->velocity, fraction, &delta);
	trace(context, &start, &delta, &hit);
	if (hit.fraction == 1.f && !hit.allsolid)
	{
		motion->position = hit.end;
		return;
	}
	down = *motion;
	quake_slide(&down, fraction, trace, context);
	up = *motion;
	delta.i = delta.j = 0.f;
	delta.k = 18.f * QUAKE_WORLD_SCALE;
	trace(context, &start, &delta, &hit);
	if (hit.allsolid || hit.fraction == 0.f)
	{
		*motion = down;
		return;
	}
	up.position = hit.end;
	quake_slide(&up, fraction, trace, context);
	if (motion->style == _quake_physics_netquake && up.wall_hit)
		quake_wall_friction(&up, &up.wall_normal);
	delta.k = -18.f * QUAKE_WORLD_SCALE + step_down;
	trace(context, &up.position, &delta, &hit);
	if (hit.allsolid || hit.fraction == 1.f || hit.collision.plane.n.k < 0.7f)
	{
		*motion = down;
		return;
	}
	up.position = hit.end;
	quake_contact(&up, &hit.collision);
	dx = down.position.x - start.x; dy = down.position.y - start.y;
	down_distance = dx * dx + dy * dy;
	dx = up.position.x - start.x; dy = up.position.y - start.y;
	up_distance = dx * dx + dy * dy;
	/* NetQuake keeps a valid walking-step result, including wall friction. */
	if (down_distance > up_distance && motion->style != _quake_physics_netquake)
		*motion = down;
	else
	{
		up.velocity.k = down.velocity.k;
		*motion = up;
	}
}



/* Q3 resolves the support plane, original travel direction, and new impact
   planes together; end-of-step gravity velocity is clipped by the same planes. */
static boolean quake3_slide(struct quake_motion *motion, real fraction, boolean gravity,
	quake_trace_function trace, void *context)
{
	real_vector3d planes[5], velocity, primal, end_velocity;
	real remaining = fraction / TICKS_PER_SECOND;
	real epsilon = 0.1f;
	short count = 0, bump;
	scale_vector3d(&motion->velocity, TICKS_PER_SECOND / QUAKE_WORLD_SCALE, &velocity);
	primal = end_velocity = velocity;
	if (gravity)
	{
		end_velocity.k -= 800.f * fraction / TICKS_PER_SECOND;
		velocity.k = (velocity.k + end_velocity.k) * 0.5f;
		primal.k = end_velocity.k;
		if (motion->ground_plane) quake_clip(&velocity, &motion->ground.plane.n, &velocity, _quake_physics_quake3);
	}
	if (motion->ground_plane) planes[count++] = motion->ground.plane.n;
	planes[count] = velocity; normalize3d(&planes[count]); ++count;
	for (bump = 0; bump < 4; ++bump)
	{
		struct quake_trace hit;
		real_vector3d delta;
		short plane, other, third;
		scale_vector3d(&velocity, remaining * QUAKE_WORLD_SCALE, &delta);
		trace(context, &motion->position, &delta, &hit);
		if (hit.allsolid) { velocity.k = 0.f; scale_vector3d(&velocity, QUAKE_WORLD_SCALE / TICKS_PER_SECOND, &motion->velocity); return TRUE; }
		if (hit.fraction > 0.f) motion->position = hit.end;
		if (hit.fraction == 1.f) break;
		quake_contact(motion, &hit.collision);
		if (hit.collision.plane.n.k >= 0.7f && velocity.k < 0.f)
			motion->landing_velocity = MAX(motion->landing_velocity, -velocity.k * QUAKE_WORLD_SCALE / TICKS_PER_SECOND);
		remaining *= 1.f - hit.fraction;
		if (count == 5) { motion->velocity = *global_zero_vector3d; return TRUE; }
		for (plane = 0; plane < count; ++plane)
			if (dot_product3d(&hit.collision.plane.n, &planes[plane]) > 0.99f)
			{
				/* One source unit/s nudges repeated contact off its plane. */
				velocity.i += hit.collision.plane.n.i;
				velocity.j += hit.collision.plane.n.j;
				velocity.k += hit.collision.plane.n.k;
				break;
			}
		if (plane < count) continue;
		planes[count++] = hit.collision.plane.n;
		for (plane = 0; plane < count; ++plane)
		{
			real_vector3d clipped, clipped_end;
			if (dot_product3d(&velocity, &planes[plane]) >= epsilon) continue;
			quake_clip(&velocity, &planes[plane], &clipped, _quake_physics_quake3);
			quake_clip(&end_velocity, &planes[plane], &clipped_end, _quake_physics_quake3);
			for (other = 0; other < count; ++other)
			{
				real_vector3d crease;
				real along;
				if (other == plane || dot_product3d(&clipped, &planes[other]) >= epsilon) continue;
				quake_clip(&clipped, &planes[other], &clipped, _quake_physics_quake3);
				quake_clip(&clipped_end, &planes[other], &clipped_end, _quake_physics_quake3);
				if (dot_product3d(&clipped, &planes[plane]) >= 0.f) continue;
				cross_product3d(&planes[plane], &planes[other], &crease); normalize3d(&crease);
				along = dot_product3d(&crease, &velocity); scale_vector3d(&crease, along, &clipped);
				along = dot_product3d(&crease, &end_velocity); scale_vector3d(&crease, along, &clipped_end);
				for (third = 0; third < count; ++third)
					if (third != plane && third != other && dot_product3d(&clipped, &planes[third]) < epsilon)
					{ motion->velocity = *global_zero_vector3d; return TRUE; }
			}
			velocity = clipped; end_velocity = clipped_end;
			break;
		}
	}
	if (gravity) velocity = end_velocity;
	if (motion->knockback || motion->q3_land_time) velocity = primal;
	scale_vector3d(&velocity, QUAKE_WORLD_SCALE / TICKS_PER_SECOND, &motion->velocity);
	return bump != 0;
}

static void quake3_step(struct quake_motion *motion, real fraction, boolean gravity,
	quake_trace_function trace, void *context)
{
	struct quake_motion start = *motion;
	struct quake_trace hit;
	real_vector3d delta = { 0.f, 0.f, -18.f * QUAKE_WORLD_SCALE };
	real height;
	if (!quake3_slide(motion, fraction, gravity, trace, context)) return;
	trace(context, &start.position, &delta, &hit);
	if (motion->velocity.k > 0.f && (hit.fraction == 1.f || hit.collision.plane.n.k < 0.7f)) return;
	delta.k = -delta.k;
	trace(context, &start.position, &delta, &hit);
	if (hit.allsolid) return;
	height = hit.end.z - start.position.z;
	motion->position = hit.end; motion->velocity = start.velocity;
	quake3_slide(motion, fraction, gravity, trace, context);
	delta.k = -height;
	trace(context, &motion->position, &delta, &hit);
	if (!hit.allsolid) motion->position = hit.end;
	if (hit.fraction < 1.f) quake_clip(&motion->velocity, &hit.collision.plane.n, &motion->velocity, _quake_physics_quake3);
}

/* Q2 tries the same 18-unit stair path while airborne as while walking. */
static void quake2_step(struct quake_motion *motion, real fraction, quake_trace_function trace, void *context)
{
	struct quake_motion flat = *motion, raised = *motion;
	struct quake_trace hit;
	real_vector3d lift = {0.f, 0.f, 18.f * QUAKE_WORLD_SCALE};
	real dx, dy, flat_distance, raised_distance;
	quake_slide(&flat, fraction, trace, context);
	trace(context, &motion->position, &lift, &hit);
	if (hit.allsolid) { *motion = flat; return; }
	raised.position.z += lift.k;
	quake_slide(&raised, fraction, trace, context);
	lift.k = -lift.k;
	trace(context, &raised.position, &lift, &hit);
	if (!hit.allsolid) raised.position = hit.end;
	dx = flat.position.x - motion->position.x; dy = flat.position.y - motion->position.y;
	flat_distance = dx*dx + dy*dy;
	dx = raised.position.x - motion->position.x; dy = raised.position.y - motion->position.y;
	raised_distance = dx*dx + dy*dy;
	if (flat_distance > raised_distance || hit.collision.plane.n.k < 0.7f) *motion = flat;
	else { raised.velocity.k = flat.velocity.k; *motion = raised; }
}

/* The protocol's one-eighth-unit truncation is part of Q2 movement, not
   merely rendering. Try its outward jitter choices before reverting. */
static void quake2_snap(struct quake_motion *motion, real_point3d const *previous, quake_trace_function trace, void *context)
{
	static short const choices[8] = {0, 4, 1, 2, 3, 5, 6, 7};
	real unit = QUAKE_WORLD_SCALE * 0.125f;
	real base[3] = {motion->position.x, motion->position.y, motion->position.z};
	short sign[3], axis, choice;
	for (axis = 0; axis < 3; ++axis)
	{
		real value = base[axis];
		base[axis] = (long)(value / unit) * unit;
		sign[axis] = value == base[axis] ? 0 : value < 0.f ? -1 : 1;
		motion->velocity.n[axis] = (long)(motion->velocity.n[axis] / (unit / TICKS_PER_SECOND)) * (unit / TICKS_PER_SECOND);
	}
	for (choice = 0; choice < 8; ++choice)
	{
		struct quake_trace hit;
		real_point3d candidate;
		real coordinates[3] = {base[0], base[1], base[2]};
		for (axis = 0; axis < 3; ++axis)
			if (choices[choice] & (1 << axis)) coordinates[axis] += sign[axis] * unit;
		candidate.x = coordinates[0]; candidate.y = coordinates[1]; candidate.z = coordinates[2];
		trace(context, &candidate, global_zero_vector3d, &hit);
		if (!hit.allsolid) { motion->position = candidate; return; }
	}
	motion->position = *previous;
}

static void quake_categorize(struct quake_motion *motion, quake_trace_function trace, void *context)
{
	struct quake_trace hit;
	real_vector3d down = { 0.f, 0.f, -QUAKE_WORLD_SCALE };
	boolean was_airborne = motion->airborne;

	if (motion->style == _quake_physics_quake3 || motion->style == _quake_physics_quake2) down.k *= 0.25f;
	motion->airborne = TRUE;
	motion->ground_plane = FALSE;
	/* Halo injury can reduce a jump below Quake's usual 180-unit upward
	ground-probe cutoff. Do not snap an ascending, weakened jump back down. */
	if ((motion->style != _quake_physics_quake3 && motion->velocity.k > 180.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND) ||
		(was_airborne && (motion->stunned || motion->crouch_velocity != 0.f) &&
			motion->velocity.k + motion->crouch_velocity > 0.f))
		return;
	trace(context, &motion->position, &down, &hit);
	if (motion->style == _quake_physics_quake3 && !hit.allsolid && hit.fraction < 1.f)
	{
		if (motion->velocity.k > 0.f && dot_product3d(&motion->velocity, &hit.collision.plane.n) > 10.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND) return;
		motion->ground_plane = TRUE; motion->ground = hit.collision;
	}
	if (!hit.allsolid && hit.fraction < 1.f && hit.collision.plane.n.k >= 0.7f)
	{
		motion->airborne = FALSE;
		if (motion->style == _quake_physics_quake3 && was_airborne && motion->previous_vertical_velocity < -200.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND) motion->q3_land_time = 250;
		if (motion->style == _quake_physics_quake2 && was_airborne && motion->velocity.k < -200.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND)
			motion->q2_land_time = motion->velocity.k < -400.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND ? 25 : 18;
		if (motion->style != _quake_physics_quake3 && motion->style != _quake_physics_quake2) motion->position = hit.end;
		motion->ground = hit.collision;
		/* The Halo adapter snaps the feet onto support. Resolve the incoming
		normal velocity too, so a pending jump does not inherit a fall. */
		if (dot_product3d(&motion->velocity, &hit.collision.plane.n) < 0.f)
		{
			real_vector3d supported;

			if (was_airborne)
				motion->landing_velocity = MAX(motion->landing_velocity,
					-motion->velocity.k);
			if (motion->style != _quake_physics_quake2 && motion->style != _quake_physics_quake3)
			{
				quake_clip(&motion->velocity, &hit.collision.plane.n, &supported, motion->style);
				motion->velocity = supported;
			}
		}
		quake_contact(motion, &hit.collision);
	}
}

void quake_motion_integrate(struct quake_motion *motion, quake_trace_function trace,
	quake_trace_function edge_trace, void *context)
{
	short step;
	long tick = game_time_get();
	short count = QUAKE_MOVEMENT_SUBSTEPS;
	real fraction = 1.f / QUAKE_MOVEMENT_SUBSTEPS;
	real dt;
	real_vector3d input_wish = motion->wish;
	real angle = 0.f, change = 0.f, wish_speed = magnitude3d(&input_wish);
	real maximum = 2000.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;

	if (motion->style == _quake_physics_netquake || motion->style == _quake_physics_quakeworld)
	{
		count = (short)(((tick % 5 + 1) * 12) / 5 - ((tick % 5) * 12) / 5);
		fraction = TICKS_PER_SECOND / 72.f;
	}
	else if (motion->style == _quake_physics_quake3)
	{
		count = tick % 6 == 5 ? 5 : 4;
		fraction = TICKS_PER_SECOND * 0.008f;
	}
	if (motion->style == _quake_physics_quake2) count = 3;
	dt = fraction / TICKS_PER_SECOND;
	if (motion->previous_wish_known)
	{
		angle = atan2(motion->previous_wish.j, motion->previous_wish.i);
		change = atan2(input_wish.j, input_wish.i) - angle;
		if (change > _pi) change -= 2.f * _pi;
		if (change < -_pi) change += 2.f * _pi;
	}
	motion->previous_vertical_velocity = motion->velocity.k;
	motion->collision_count = 0;
	motion->landing_velocity = 0.f;
	motion->jumped = FALSE;
	quake_categorize(motion, trace, context);
	for (step = 0; step < count; ++step)
	{
		boolean edge = FALSE;
		short axis;
		real_point3d previous = motion->position;
		real_vector3d step_wish;
		motion->previous_vertical_velocity = motion->velocity.k;
		if (motion->style == _quake_physics_quake3) motion->q3_land_time = MAX(0, motion->q3_land_time - 8);
		motion->wish = input_wish;
		if (motion->style == _quake_physics_quake2)
		{
			/* 90 Hz reference commands retain Q2 integer milliseconds. */
			dt = (tick * 3 + step) % 9 == 8 ? 0.012f : 0.011f;
			fraction = dt * TICKS_PER_SECOND;
			if (motion->q2_land_time) --motion->q2_land_time;
		}

		if (motion->previous_wish_known)
		{
			real heading = angle + change * (step + 1.f) / count;
			motion->wish.i = cosine(heading) * wish_speed;
			motion->wish.j = sine(heading) * wish_speed;
		}
		if (!motion->airborne && motion->jump && motion->style != _quake_physics_netquake &&
			(motion->style != _quake_physics_quake2 || !motion->q2_land_time))
		{
			if (motion->style == _quake_physics_quake3) motion->velocity.k = motion->jump_velocity;
			else if (motion->style == _quake_physics_quake2) motion->velocity.k = MAX(motion->velocity.k + motion->jump_velocity, motion->jump_velocity);
			else motion->velocity.k += motion->jump_velocity;
			motion->airborne = TRUE;
			motion->jumped = TRUE;
			if (motion->style == _quake_physics_quake3) { motion->jump_held = TRUE; motion->ground_plane = FALSE; }
			motion->jump = FALSE;
		}
		if (!motion->airborne)
		{
			real speed = square_root(motion->velocity.i * motion->velocity.i + motion->velocity.j * motion->velocity.j);

			if (speed > 0.f)
			{
				struct quake_trace hit;
				real_point3d point = motion->position;
				real_vector3d down = { 0.f, 0.f, -34.f * QUAKE_WORLD_SCALE };

				point.x += motion->velocity.i / speed * 16.f * QUAKE_WORLD_SCALE;
				point.y += motion->velocity.j / speed * 16.f * QUAKE_WORLD_SCALE;
				point.z -= motion->radius;
				edge_trace(context, &point, &down, &hit);
				edge = hit.fraction == 1.f;
			}
		}
		step_wish = motion->wish;
		/* Q3 commands encode movement headings in 16 bits. Keep Halo's
		   view/aim unchanged while reproducing that movement precision. */
		if (motion->style == _quake_physics_quake3 && motion->command_known && magnitude_squared3d(&step_wish) > 0.f)
		{
			real heading = atan2(step_wish.j, step_wish.i), speed = magnitude3d(&step_wish);
			real degrees;
			unsigned short word;
			if (heading < 0.f) heading += 2.f * _pi;
			degrees = heading * (180.f / _pi);
			word = (unsigned short)(long)(degrees * (65536.f / 360.f));
			heading = ((short)word) * (360.f / 65536.f) * (_pi / 180.f);
			step_wish.i = cosine(heading) * speed; step_wish.j = sine(heading) * speed;
		}

		if (motion->style == _quake_physics_quake3 && motion->command_known)
			scale_vector3d(&step_wish, quake_command_scale(motion), &step_wish);
		if (motion->style == _quake_physics_quake3 && !motion->airborne)
		{
			real speed = magnitude3d(&step_wish);
			real into = dot_product3d(&step_wish, &motion->ground.plane.n);
			step_wish.i -= motion->ground.plane.n.i * into;
			step_wish.j -= motion->ground.plane.n.j * into;
			step_wish.k -= motion->ground.plane.n.k * into;
			if (normalize3d(&step_wish) > 0.f) scale_vector3d(&step_wish, speed, &step_wish);
		}
		quake_movement_step_profile(&motion->velocity, &step_wish, motion->speed_limit,
			quake_player_profile_speed(motion->style), motion->airborne || motion->knockback, edge, quake_player_gravity() * (motion->style == _quake_physics_quake3 ? 0.f : 1.f), dt, motion->style);
		if (!motion->airborne && motion->jump && motion->style == _quake_physics_netquake)
		{
			if (motion->style == _quake_physics_quake3) motion->velocity.k = motion->jump_velocity;
			else motion->velocity.k += motion->jump_velocity;
			motion->airborne = TRUE;
			motion->jumped = TRUE;
			if (motion->style == _quake_physics_quake3) { motion->jump_held = TRUE; motion->ground_plane = FALSE; }
			motion->jump = FALSE;
		}
		if (motion->stunned)
			quake_movement_halo_stun(&motion->velocity, &step_wish,
				(motion->airborne ? motion->air_stun_acceleration : motion->ground_stun_acceleration) * fraction);
		for (axis = 0; axis < 3; ++axis)
			if (motion->style != _quake_physics_quake3 && motion->style != _quake_physics_quake2) motion->velocity.n[axis] = PIN(motion->velocity.n[axis], -maximum, maximum);
		/* Preserve Halo's vertical displacement as its crouching capsule
		changes height in the air, with collision determining the result. */
		motion->velocity.k += motion->crouch_velocity;
		if (motion->style == _quake_physics_quake3)
		{
			if (!motion->airborne)
			{
				real speed;
				if (motion->knockback) motion->velocity.k -= quake_player_gravity() * fraction;
				speed = magnitude3d(&motion->velocity);
				quake_clip(&motion->velocity, &motion->ground.plane.n, &motion->velocity, motion->style);
				if (normalize3d(&motion->velocity) > 0.f) scale_vector3d(&motion->velocity, speed, &motion->velocity);
				if (motion->velocity.i != 0.f || motion->velocity.j != 0.f) quake3_step(motion, fraction, FALSE, trace, context);
			}
			else
			{
				if (motion->ground_plane) quake_clip(&motion->velocity, &motion->ground.plane.n, &motion->velocity, motion->style);
				quake3_step(motion, fraction, TRUE, trace, context);
			}
		}
		else if (motion->style == _quake_physics_quake2)
		{
			if (!motion->airborne) motion->velocity.k = motion->crouch_velocity;
			quake2_step(motion, fraction, trace, context);
		}
		else if (motion->airborne || motion->crouch_velocity != 0.f)
			quake_slide(motion, fraction, trace, context);
		else
			quake_ground(motion, fraction, trace, context);
		motion->velocity.k -= motion->crouch_velocity;

		quake_categorize(motion, trace, context);
		if (motion->style == _quake_physics_quake3)
		{
			for (axis = 0; axis < 3; ++axis)
				{
					real value = motion->velocity.n[axis] / QUAKE_WORLD_SCALE * TICKS_PER_SECOND;
					real whole = floor(value), part = value - whole;
					if (part > 0.5f || (part == 0.5f && ((long)whole & 1))) whole += 1.f;
					motion->velocity.n[axis] = whole * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;
				}
		}
		if (motion->style == _quake_physics_quake2) quake2_snap(motion, &previous, trace, context);
	}
}

struct quake_halo_trace_context
{
	struct collision_feature_list features;
	struct quake_motion *motion;
};

static void quake_repair_contact(struct quake_trace *trace)
{
	if (trace->fraction < 1.f)
	{
		real depth = trace->collision.plane.d - dot_product3d((real_vector3d const *)&trace->end,
			&trace->collision.plane.n);

		if (depth > 0.f)
			point_from_line3d(&trace->end, &trace->collision.plane.n,
				depth + 0.03125f * QUAKE_WORLD_SCALE, &trace->end);
	}
}

static void quake_halo_trace(void *opaque, real_point3d const *start,
	real_vector3d const *delta, struct quake_trace *trace)
{
	struct quake_halo_trace_context *context = opaque;
	short type, index;
	real epsilon = 0.03125f * QUAKE_WORLD_SCALE;

	csmemset(trace, 0, sizeof(*trace));
	trace->fraction = 1.f;
	point_from_line3d(start, delta, 1.f, &trace->end);
	for (type = 0; type < NUMBER_OF_COLLISION_FEATURE_TYPES; ++type)
	{
		for (index = 0; index < context->features.count[type]; ++index)
		{
			real t;
			real_plane3d plane;
			struct collision_feature const *feature;
			boolean hit;

			if (type == _collision_feature_sphere)
			{
				feature = (struct collision_feature const *)&context->features.spheres[index];
				hit = collision_sphere_test_vector(&context->features.spheres[index], start, delta, &t, &plane);
			}
			else if (type == _collision_feature_cylinder)
			{
				feature = (struct collision_feature const *)&context->features.cylinders[index];
				hit = collision_cylinder_test_vector(&context->features.cylinders[index], start, delta, &t, &plane);
			}
			else
			{
				feature = (struct collision_feature const *)&context->features.prisms[index];
				hit = collision_prism_test_vector(&context->features.prisms[index], start, delta, &t, &plane);
			}
			if (hit && t < trace->fraction && dot_product3d(delta, &plane.n) < -_real_epsilon)
			{
				real approach = -dot_product3d(delta, &plane.n);

				trace->fraction = MAX(0.f, t - epsilon / approach);
				trace->collision.plane = plane;
				trace->collision.object_index = feature->object_index;
				trace->collision.surface_index = feature->surface_index;
				trace->collision.flags = feature->flags;
				trace->collision.breakable_surface_index = feature->breakable_surface_index;
				trace->collision.material_index = feature->material_index;
			}
		}
	}
	point_from_line3d(start, delta, trace->fraction, &trace->end);
	/* Halo features describe expanded boundary surfaces, not Quake solid
	brush contents. An initial capsule overlap is repaired onto the contact
	plane, as Halo's original pill solver does, rather than called allsolid. */
	quake_repair_contact(trace);
	trace->collision.t = trace->fraction;
	trace->collision.point = trace->end;
}

static void quake_halo_edge_trace(void *opaque, real_point3d const *start,
	real_vector3d const *delta, struct quake_trace *trace)
{
	struct quake_halo_trace_context *context = opaque;
	struct collision_result hit;

	csmemset(trace, 0, sizeof(*trace));
	trace->fraction = collision_test_vector(context->motion->collision_flags, start, delta,
		context->motion->biped_index, &hit) ? hit.t : 1.f;
}

void quake_motion_halo(struct quake_motion *motion)
{
	struct quake_halo_trace_context context;
	real_point3d center = motion->position;
	real radius = magnitude3d(&motion->velocity) + quake_player_jump_velocity() +
		36.f * QUAKE_WORLD_SCALE + motion->height * 0.5f + motion->radius + fabs(motion->crouch_velocity);

	context.motion = motion;
	center.z += motion->height * 0.5f;
	match_assert(__FILE__, __LINE__, global_current_collision_user_depth < MAXIMUM_COLLISION_USER_STACK_DEPTH);
	global_current_collision_users[global_current_collision_user_depth++] = _collision_user_bipeds;
	collision_features_new(&context.features);
	collision_get_features_in_sphere(motion->collision_flags, &center, radius, motion->height,
		motion->radius, motion->biped_index, &context.features);
	quake_player_motion_heading(motion->biped_index, &motion->wish, &motion->previous_wish, &motion->previous_wish_known);
	quake_motion_integrate(motion, quake_halo_trace, quake_halo_edge_trace, &context);
	--global_current_collision_user_depth;
}
