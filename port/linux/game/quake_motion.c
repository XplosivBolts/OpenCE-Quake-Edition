/* QuakeWorld's slide/step/ground movement rules, independently implemented
over Halo collision queries. Halo's capsule and airborne crouch displacement
are deliberate compatibility choices; combat stun also remains Halo's. */
#include "cseries.h"
#include "quake_movement.h"
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

static void quake_clip(real_vector3d const *velocity, real_vector3d const *normal, real_vector3d *out)
{
	real into = dot_product3d(velocity, normal);
	real epsilon = 0.1f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;
	short axis;

	for (axis = 0; axis < 3; ++axis)
	{
		out->n[axis] = velocity->n[axis] - normal->n[axis] * into;
		if (fabs(out->n[axis]) < epsilon)
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
			motion->velocity = *global_zero_vector3d;
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
		quake_contact(motion, &hit.collision);
		if (hit.collision.plane.n.k >= 0.7f && motion->velocity.k < 0.f)
			motion->landing_velocity = MAX(motion->landing_velocity,
				-motion->velocity.k);
		remaining *= 1.f - hit.fraction;
		if (count == 5)
		{
			motion->velocity = *global_zero_vector3d;
			return;
		}
		planes[count++] = hit.collision.plane.n;
		for (plane = 0; plane < count; ++plane)
		{
			quake_clip(&original, &planes[plane], &candidate);
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
			motion->velocity = *global_zero_vector3d;
			return;
		}
	}
}

static void quake_ground(struct quake_motion *motion, real fraction,
	quake_trace_function trace, void *context)
{
	struct quake_motion down, up;
	struct quake_trace hit;
	real_vector3d delta;
	real dx, dy, down_distance, up_distance;
	real_point3d start = motion->position;

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
	delta.k = -18.f * QUAKE_WORLD_SCALE;
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
	if (down_distance > up_distance)
		*motion = down;
	else
	{
		up.velocity.k = down.velocity.k;
		*motion = up;
	}
}

static void quake_categorize(struct quake_motion *motion, quake_trace_function trace, void *context)
{
	struct quake_trace hit;
	real_vector3d down = { 0.f, 0.f, -QUAKE_WORLD_SCALE };
	boolean was_airborne = motion->airborne;

	motion->airborne = TRUE;
	/* Halo injury can reduce a jump below Quake's usual 180-unit upward
	ground-probe cutoff. Do not snap an ascending, weakened jump back down. */
	if (motion->velocity.k > 180.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND ||
		(was_airborne && (motion->stunned || motion->crouch_velocity != 0.f) &&
			motion->velocity.k + motion->crouch_velocity > 0.f))
		return;
	trace(context, &motion->position, &down, &hit);
	if (!hit.allsolid && hit.fraction < 1.f && hit.collision.plane.n.k >= 0.7f)
	{
		motion->airborne = FALSE;
		motion->position = hit.end;
		motion->ground = hit.collision;
		/* The Halo adapter snaps the feet onto support. Resolve the incoming
		normal velocity too, so a pending jump does not inherit a fall. */
		if (dot_product3d(&motion->velocity, &hit.collision.plane.n) < 0.f)
		{
			real_vector3d supported;

			if (was_airborne)
				motion->landing_velocity = MAX(motion->landing_velocity,
					-motion->velocity.k);
			quake_clip(&motion->velocity, &hit.collision.plane.n, &supported);
			motion->velocity = supported;
		}
		quake_contact(motion, &hit.collision);
	}
}

void quake_motion_integrate(struct quake_motion *motion, quake_trace_function trace,
	quake_trace_function edge_trace, void *context)
{
	short step;
	real fraction = 1.f / QUAKE_MOVEMENT_SUBSTEPS;
	real dt = fraction / TICKS_PER_SECOND;
	real maximum = 2000.f * QUAKE_WORLD_SCALE / TICKS_PER_SECOND;

	motion->collision_count = 0;
	motion->landing_velocity = 0.f;
	motion->jumped = FALSE;
	quake_categorize(motion, trace, context);
	for (step = 0; step < QUAKE_MOVEMENT_SUBSTEPS; ++step)
	{
		boolean edge = FALSE;
		short axis;

		if (!motion->airborne && motion->jump)
		{
			motion->velocity.k += motion->jump_velocity;
			motion->airborne = TRUE;
			motion->jumped = TRUE;
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
		quake_movement_step_time(&motion->velocity, &motion->wish, motion->speed_limit,
			quake_player_speed(), motion->airborne, edge, quake_player_gravity(), dt);
		if (motion->stunned)
			quake_movement_halo_stun(&motion->velocity, &motion->wish,
				(motion->airborne ? motion->air_stun_acceleration : motion->ground_stun_acceleration) * fraction);
		for (axis = 0; axis < 3; ++axis)
			motion->velocity.n[axis] = PIN(motion->velocity.n[axis], -maximum, maximum);
		/* Preserve Halo's vertical displacement as its crouching capsule
		changes height in the air, with collision determining the result. */
		motion->velocity.k += motion->crouch_velocity;
		if (motion->airborne || motion->crouch_velocity != 0.f)
			quake_slide(motion, fraction, trace, context);
		else
			quake_ground(motion, fraction, trace, context);
		motion->velocity.k -= motion->crouch_velocity;
		quake_categorize(motion, trace, context);
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
	quake_motion_integrate(motion, quake_halo_trace, quake_halo_edge_trace, &context);
	--global_current_collision_user_depth;
}
