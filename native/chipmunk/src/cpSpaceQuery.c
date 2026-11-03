/* Copyright (c) 2013 Scott Lembcke and Howling Moon Software
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "chipmunk/chipmunk_private.h"

struct PointQueryContext {
	cpVect point;
	cpFloat maxDistance;
	cpShapeFilter filter;
	cpSpacePointQueryFunc func;
};

static cpCollisionID
NearestPointQuery(struct PointQueryContext *context, cpShape *shape, cpCollisionID id, void *data)
{
	if(
		!cpShapeFilterReject(shape->filter, context->filter)
	){
		cpPointQueryInfo info;
		cpShapePointQuery(shape, context->point, &info);

		if(info.shape && info.distance < context->maxDistance) context->func(shape, info.point, info.distance, info.gradient, data);
	}

	return id;
}

void
cpSpacePointQuery(cpSpace *space, cpVect point, cpFloat maxDistance, cpShapeFilter filter, cpSpacePointQueryFunc func, void *data)
{
	struct PointQueryContext context = {point, maxDistance, filter, func};
	cpBB bb = cpBBNewForCircle(point, cpfmax(maxDistance, 0.0f));

	cpSpaceLock(space); {
		cpSpatialIndexQuery(space->dynamicShapes, &context, bb, (cpSpatialIndexQueryFunc)NearestPointQuery, data);
		cpSpatialIndexQuery(space->staticShapes, &context, bb, (cpSpatialIndexQueryFunc)NearestPointQuery, data);
	} cpSpaceUnlock(space, cpTrue);
}

static cpCollisionID
NearestPointQueryNearest(struct PointQueryContext *context, cpShape *shape, cpCollisionID id, cpPointQueryInfo *out)
{
	if(
		!cpShapeFilterReject(shape->filter, context->filter) && !shape->sensor
	){
		cpPointQueryInfo info;
		cpShapePointQuery(shape, context->point, &info);

		if(info.distance < out->distance) (*out) = info;
	}

	return id;
}

cpShape *
cpSpacePointQueryNearest(cpSpace *space, cpVect point, cpFloat maxDistance, cpShapeFilter filter, cpPointQueryInfo *out)
{
	cpPointQueryInfo info = {NULL, cpvzero, maxDistance, cpvzero};
	if(out){
		(*out) = info;
  } else {
		out = &info;
	}

	struct PointQueryContext context = {
		point, maxDistance,
		filter,
		NULL
	};

	cpBB bb = cpBBNewForCircle(point, cpfmax(maxDistance, 0.0f));
	cpSpatialIndexQuery(space->dynamicShapes, &context, bb, (cpSpatialIndexQueryFunc)NearestPointQueryNearest, out);
	cpSpatialIndexQuery(space->staticShapes, &context, bb, (cpSpatialIndexQueryFunc)NearestPointQueryNearest, out);

	return (cpShape *)out->shape;
}

struct SegmentQueryContext {
	cpVect start, end;
	cpFloat radius;
	cpShapeFilter filter;
	cpSpaceSegmentQueryFunc func;
};

static cpFloat
SegmentQuery(struct SegmentQueryContext *context, cpShape *shape, void *data)
{
	cpSegmentQueryInfo info;

	if(
		!cpShapeFilterReject(shape->filter, context->filter) &&
		cpShapeSegmentQuery(shape, context->start, context->end, context->radius, &info)
	){
		context->func(shape, info.point, info.normal, info.alpha, data);
	}

	return 1.0f;
}

void
cpSpaceSegmentQuery(cpSpace *space, cpVect start, cpVect end, cpFloat radius, cpShapeFilter filter, cpSpaceSegmentQueryFunc func, void *data)
{
	struct SegmentQueryContext context = {
		start, end,
		radius,
		filter,
		func,
	};

	cpSpaceLock(space); {
    cpSpatialIndexSegmentQuery(space->staticShapes, &context, start, end, 1.0f, (cpSpatialIndexSegmentQueryFunc)SegmentQuery, data);
    cpSpatialIndexSegmentQuery(space->dynamicShapes, &context, start, end, 1.0f, (cpSpatialIndexSegmentQueryFunc)SegmentQuery, data);
	} cpSpaceUnlock(space, cpTrue);
}

static cpFloat
SegmentQueryFirst(struct SegmentQueryContext *context, cpShape *shape, cpSegmentQueryInfo *out)
{
	cpSegmentQueryInfo info;

	if(
		!cpShapeFilterReject(shape->filter, context->filter) && !shape->sensor &&
		cpShapeSegmentQuery(shape, context->start, context->end, context->radius, &info) &&
		info.alpha < out->alpha
	){
		(*out) = info;
	}

	return out->alpha;
}

#ifdef DX_PROFILE

#include <intrin.h>
double dx_now(void);
volatile long long dx_ray_stat[6];
static __declspec(thread) long long dx_ray_leaves;
static cpFloat SegmentQueryFirst(struct SegmentQueryContext *context, cpShape *shape, cpSegmentQueryInfo *out);
static cpFloat dx_SegmentQueryFirstCount(struct SegmentQueryContext *context, cpShape *shape, cpSegmentQueryInfo *out){ dx_ray_leaves++; return SegmentQueryFirst(context, shape, out); }
#endif

cpShape *
cpSpaceSegmentQueryFirst(cpSpace *space, cpVect start, cpVect end, cpFloat radius, cpShapeFilter filter, cpSegmentQueryInfo *out)
{
	cpSegmentQueryInfo info = {NULL, end, cpvzero, 1.0f};
	if(out){
		(*out) = info;
  } else {
		out = &info;
	}

	struct SegmentQueryContext context = {
		start, end,
		radius,
		filter,
		NULL
	};

#ifdef DX_PROFILE
	double _r0 = dx_now(); dx_ray_leaves = 0;
	cpSpatialIndexSegmentQuery(space->staticShapes, &context, start, end, 1.0f, (cpSpatialIndexSegmentQueryFunc)dx_SegmentQueryFirstCount, out);
	long long _ls = dx_ray_leaves; double _r1 = dx_now();
	cpSpatialIndexSegmentQuery(space->dynamicShapes, &context, start, end, out->alpha, (cpSpatialIndexSegmentQueryFunc)dx_SegmentQueryFirstCount, out);
	double _r2 = dx_now();
	_InterlockedExchangeAdd64(&dx_ray_stat[0], (long long)((_r1 - _r0) * 1e9)); _InterlockedExchangeAdd64(&dx_ray_stat[1], (long long)((_r2 - _r1) * 1e9));
	_InterlockedExchangeAdd64(&dx_ray_stat[2], 1); _InterlockedExchangeAdd64(&dx_ray_stat[3], _ls); _InterlockedExchangeAdd64(&dx_ray_stat[4], dx_ray_leaves - _ls);
	if(out->shape) _InterlockedExchangeAdd64(&dx_ray_stat[5], 1);
#else
	cpSpatialIndexSegmentQuery(space->staticShapes, &context, start, end, 1.0f, (cpSpatialIndexSegmentQueryFunc)SegmentQueryFirst, out);
	cpSpatialIndexSegmentQuery(space->dynamicShapes, &context, start, end, out->alpha, (cpSpatialIndexSegmentQueryFunc)SegmentQueryFirst, out);
#endif

	return (cpShape *)out->shape;
}

struct BBQueryContext {
	cpBB bb;
	cpShapeFilter filter;
	cpSpaceBBQueryFunc func;
};

static cpCollisionID
BBQuery(struct BBQueryContext *context, cpShape *shape, cpCollisionID id, void *data)
{
	if(
		!cpShapeFilterReject(shape->filter, context->filter) &&
		cpBBIntersects(context->bb, shape->bb)
	){
		context->func(shape, data);
	}

	return id;
}

void
cpSpaceBBQuery(cpSpace *space, cpBB bb, cpShapeFilter filter, cpSpaceBBQueryFunc func, void *data)
{
	struct BBQueryContext context = {bb, filter, func};

	cpSpaceLock(space); {
    cpSpatialIndexQuery(space->dynamicShapes, &context, bb, (cpSpatialIndexQueryFunc)BBQuery, data);
    cpSpatialIndexQuery(space->staticShapes, &context, bb, (cpSpatialIndexQueryFunc)BBQuery, data);
	} cpSpaceUnlock(space, cpTrue);
}

struct ShapeQueryContext {
	cpSpaceShapeQueryFunc func;
	void *data;
	cpBool anyCollision;
};

static cpCollisionID
ShapeQuery(cpShape *a, cpShape *b, cpCollisionID id, struct ShapeQueryContext *context)
{
	if(cpShapeFilterReject(a->filter, b->filter) || a == b) return id;

	cpContactPointSet set = cpShapesCollide(a, b);
	if(set.count){
		if(context->func) context->func(b, &set, context->data);
		context->anyCollision = !(a->sensor || b->sensor);
	}

	return id;
}

cpBool
cpSpaceShapeQuery(cpSpace *space, cpShape *shape, cpSpaceShapeQueryFunc func, void *data)
{
	cpBody *body = shape->body;
	cpBB bb = (body ? cpShapeUpdate(shape, body->transform) : shape->bb);
	struct ShapeQueryContext context = {func, data, cpFalse};

	cpSpaceLock(space); {
    cpSpatialIndexQuery(space->dynamicShapes, shape, bb, (cpSpatialIndexQueryFunc)ShapeQuery, &context);
    cpSpatialIndexQuery(space->staticShapes, shape, bb, (cpSpatialIndexQueryFunc)ShapeQuery, &context);
	} cpSpaceUnlock(space, cpTrue);

	return context.anyCollision;
}
