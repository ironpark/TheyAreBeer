#define CP_USE_DOUBLES 0
#include "chipmunk/chipmunk_private.h"
#include <math.h>
#include <stdlib.h>
#include "dxpool.h"

extern int dx_skip_threading, dx_par_independent, dx_par_solver, dx_last_colors, dx_par_collide, dx_par_record, dx_block_solver, dx_par_filter, dx_block_alternate;

int dx_defer_dispose = 1;
static cpBody **g_dead; static int g_ndead, g_capdead;
static void dx_compact(cpArray *arr) { int k = 0; for (int i = 0; i < arr->num; i++) { cpBody *b = (cpBody *)arr->arr[i]; if (!b->dx_dead) arr->arr[k++] = b; } for (int i = k; i < arr->num; i++) arr->arr[i] = NULL; arr->num = k; }
static void dx_dead_begin(cpSpace *space)
{
	if (!g_ndead) return;
	for (int i = 0; i < g_ndead; i++)
	{
		cpBody *body = g_dead[i]; if (body->space != space) continue;
		cpBool isStatic = (cpBodyGetType(body) == CP_BODY_TYPE_STATIC);
		CP_BODY_FOREACH_SHAPE(body, shape) { cpSpatialIndexRemove(isStatic ? space->staticShapes : space->dynamicShapes, shape, shape->hashid); shape->space = NULL; }
	}
	dx_compact(space->dynamicBodies); dx_compact(space->staticBodies);
	for (int i = 0; i < g_ndead; i++) if (g_dead[i]->space == space) g_dead[i]->space = NULL;
}
static void dx_dead_end(void)
{
	int k = 0;
	for (int i = 0; i < g_ndead; i++)
	{
		cpBody *body = g_dead[i];
		if (body->space != NULL) { g_dead[k++] = body; continue; }
		cpShape *shape = body->shapeList;
		while (shape) { cpShape *next = shape->next; shape->body = NULL; cpShapeFree(shape); shape = next; }
		body->shapeList = NULL;
		cpBodyFree(body);
	}
	g_ndead = k;
}

#define DX_EXPORT __declspec(dllexport)

typedef struct { float x, y; } DXPointF;

typedef int(__cdecl *DXCollisionCallback)(void *userA, void *userB);
typedef void(__cdecl *DXSeparationCallback)(void *userA, void *userB);
typedef void(__cdecl *DXEntityCallback)(void *userData);

static DXCollisionCallback g_collision;
static DXSeparationCallback g_separation;
static cpSpace *g_space;

static DXPointF dxp(float x, float y) { DXPointF p; p.x = x; p.y = y; return p; }

void dx_diag_call(unsigned id, void *p, float a, float b, float c, float d);
#define DXD(id, p, a, b, c, d) dx_diag_call((id), (void *)(p), (float)(a), (float)(b), (float)(c), (float)(d))
void dx_check_patches(const char *where);
static int g_steps_seen;

#ifdef DX_PROFILE
#include <windows.h>
extern double dx_prof[32];
static double dx_t(void) { static double inv = 0; LARGE_INTEGER c; if (inv == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); inv = 1.0 / (double)f.QuadPart; } QueryPerformanceCounter(&c); return (double)c.QuadPart * inv; }
#define DXT0() double _t0 = dx_t()
#define DXT1(i) do { dx_prof[i] += dx_t() - _t0; dx_prof[(i) + 1] += 1; } while (0)
#else
#define DXT0() do {} while (0)
#define DXT1(i) do {} while (0)
#endif

static cpBool DX_Begin(cpArbiter *arb, cpSpace *space, void *data)
{
	(void)space; (void)data;
	if (!arb->body_a->userData || !arb->body_b->userData) return cpFalse;
	return (cpBool)g_collision(arb->body_a->userData, arb->body_b->userData);
}
static void DX_Separate(cpArbiter *arb, cpSpace *space, void *data)
{
	(void)space; (void)data;
	if (!arb->body_a->userData || !arb->body_b->userData) return;
	g_separation(arb->body_a->userData, arb->body_b->userData);
}

DX_EXPORT DXPointF Sumar(int a, int b) { return dxp((float)a, (float)b); }
DX_EXPORT void Algo(void) {}

DX_EXPORT float Space_IsLocked(cpSpace *space) { return space->locked > 0 ? 1.0f : 0.0f; }

DX_EXPORT void Space_Step(cpSpace *space, float timeStep)
{
	DXD(1, space, timeStep, g_ndead, 0, 0);
	if (g_steps_seen < 200 && (++g_steps_seen % 20) == 1) dx_check_patches("Space_Step");
	dx_dead_begin(space); cpSpaceStep(space, timeStep); dx_dead_end();
}

DX_EXPORT cpSpace *Space_Create(float gravityX, float gravityY, int iterations)
{
	DXD(2, 0, gravityX, gravityY, iterations, 0);
	dx_check_patches("Space_Create");
	cpSpace *space = cpSpaceNew();
	space->gravity = cpv(gravityX, gravityY);
	cpSpaceSetIterations(space, iterations);
	cpCollisionHandler *h = cpSpaceAddCollisionHandler(space, 1, 0);
	h->beginFunc = DX_Begin;
	h->separateFunc = DX_Separate;
	space->damping = 0.001f;
	space->collisionSlop = 0.1f;
	g_space = space;

	{ const char *t = getenv("DX_PHYS_THREADS"); dx_pool_set_threads(t ? atoi(t) : dx_pool_auto_threads()); }
	return space;
}

DX_EXPORT void Space_SetCollisionCallback(cpSpace *space, DXCollisionCallback func) { (void)space; g_collision = func; }
DX_EXPORT void Space_SetSeparationCallback(cpSpace *space, DXSeparationCallback func) { (void)space; g_separation = func; }

struct DXBBContext { cpBB bb; cpShapeFilter filter; void (*func)(cpShape *shape, void *data); };
static cpCollisionID DX_BBQuery(struct DXBBContext *context, cpShape *shape, cpCollisionID id, void *data)
{
	if (!cpShapeFilterReject(shape->filter, context->filter) && cpBBIntersects(context->bb, shape->bb)) context->func(shape, data);
	return id;
}
static void DX_Query(cpSpace *space, cpBB bb, cpBitmask mask, void (*func)(cpShape *, void *), void *data)
{
	struct DXBBContext context;
	context.bb = bb;
	context.filter.group = 0; context.filter.categories = 0; context.filter.mask = mask;
	context.func = func;
	cpSpatialIndexQuery(space->dynamicShapes, &context, bb, (cpSpatialIndexQueryFunc)DX_BBQuery, data);
	cpSpatialIndexQuery(space->staticShapes, &context, bb, (cpSpatialIndexQueryFunc)DX_BBQuery, data);
}

static void DX_AreaCb(cpShape *shape, void *data) { ((DXEntityCallback)data)(shape->body->userData); }
DX_EXPORT void Space_GetBodiesInArea(cpSpace *space, float x1, float y1, float x2, float y2, cpBitmask mask, DXEntityCallback func)
{
	DX_Query(space, cpBBNew(x1, y1, x2, y2), mask, DX_AreaCb, (void *)func);
}

struct DXRadiusData { float x, y, r2; DXEntityCallback func; };
static void DX_RadiusCb(cpShape *shape, void *data)
{
	struct DXRadiusData *d = (struct DXRadiusData *)data;
	cpBody *b = shape->body;
	float dy = d->y - b->p.y, dx = d->x - b->p.x;
	if (dy * dy + dx * dx > d->r2) return;
	d->func(b->userData);
}
DX_EXPORT void Space_GetBodiesInRadius(cpSpace *space, float x, float y, float radius, cpBitmask mask, DXEntityCallback func)
{
	struct DXRadiusData d; d.x = x; d.y = y; d.r2 = radius * radius; d.func = func;
	DX_Query(space, cpBBNew(x - radius, y - radius, radius + x, radius + y), mask, DX_RadiusCb, &d);
}

struct DXFastData { float x, y, r2, maxElements; void **elements; int count; };
static void DX_FastCb(cpShape *shape, void *data)
{
	struct DXFastData *d = (struct DXFastData *)data;
	if ((float)d->count >= d->maxElements) return;
	cpBody *b = shape->body;
	float dy = d->y - b->p.y, dx = d->x - b->p.x;
	if (dy * dy + dx * dx > d->r2) return;
	d->elements[d->count++] = b->userData;
}
DX_EXPORT int Space_GetBodiesInRadiusFast(cpSpace *space, float x, float y, float radius, cpBitmask mask, int maxElements, void **elements)
{
	struct DXFastData d; d.x = x; d.y = y; d.r2 = radius * radius; d.maxElements = (float)maxElements; d.elements = elements; d.count = 0;
	DX_Query(space, cpBBNew(x - radius, y - radius, radius + x, radius + y), mask, DX_FastCb, &d);
	return d.count;
}

struct DXNearestData { float best; cpShape *shape; float x, y; };
static void DX_NearestCb(cpShape *shape, void *data)
{
	struct DXNearestData *d = (struct DXNearestData *)data;
	cpBody *b = shape->body;
	float dy = b->p.y - d->y, dx = b->p.x - d->x;
	float d2 = dy * dy + dx * dx;
	if (d2 < d->best) { d->best = d2; d->shape = shape; }
}
DX_EXPORT void *Space_GetNearestBody(cpSpace *space, float x, float y, float radius, cpBitmask mask)
{
	struct DXNearestData d; d.best = 9999999.0f; d.shape = NULL; d.x = x; d.y = y;
	DX_Query(space, cpBBNew(x - radius, y - radius, (x + radius) + radius, (y + radius) + radius), mask, DX_NearestCb, &d);
	cpShape *s = d.shape; if (!s) return NULL;
	float dx = s->body->p.x - x, dy = s->body->p.y - y;
	float d2 = dy * dy + dx * dx;
	if (s->klass->type == CP_CIRCLE_SHAPE)
	{
		float rr = radius + ((cpCircleShape *)s)->r; rr = rr * rr;
		return d2 < rr ? s->userData : NULL;
	}
	return d2 < radius * radius ? s->userData : NULL;
}

DX_EXPORT void *Space_RayCast(cpSpace *space, float x1, float y1, float x2, float y2, float radius, cpBitmask mask)
{
	cpShapeFilter filter; filter.group = 0; filter.categories = 0; filter.mask = mask;
	cpShape *s = cpSpaceSegmentQueryFirst(space, cpv(x1, y1), cpv(x2, y2), radius, filter, NULL);
	return s ? s->body->userData : NULL;
}

static void DX_SpaceCb(cpShape *shape, cpVect point, cpVect normal, cpFloat alpha, void *data)
{
	(void)point; (void)normal; (void)alpha;
	float *acc = (float *)data;
	*acc = (shape->bb.r + *acc) - shape->bb.l;
}
DX_EXPORT float Space_GetAvailableSpace(cpSpace *space, float x1, float y1, float x2, float y2, float radius, cpBitmask mask)
{
	cpShapeFilter filter; filter.group = 0; filter.categories = 0; filter.mask = mask;
	float acc = 0.0f;
	cpSpaceSegmentQuery(space, cpv(x1, y1), cpv(x2, y2), radius, filter, DX_SpaceCb, &acc);
	float dy = y1 - y2, dx = x1 - x2;
	return sqrtf(dy * dy + dx * dx) - acc;
}

DX_EXPORT cpBody *Body_Create(cpSpace *space, float mass, float x, float y, float radius)
{
	DXD(3, space, mass, x, y, radius);
	cpBody *body = cpBodyNew(mass, cpMomentForCircle(mass, 0.0f, radius, cpvzero));
	cpSpaceAddBody(space, body);
	cpBodySetPosition(body, cpv(x, y));
	return body;
}

DX_EXPORT cpBody *Body_CreateCircle(cpSpace *space, float mass, float x, float y, float radius)
{
	DXD(4, space, mass, x, y, radius);
	DXT0();
	cpBody *body = cpBodyNew(mass, cpMomentForCircle(mass, 0.0f, radius, cpvzero));
	cpSpaceAddBody(space, body);
	cpBodySetPosition(body, cpv(x, y));
	cpShape *shape = cpSpaceAddShape(space, cpCircleShapeNew(body, radius, cpvzero));
	cpBodyActivate(shape->body);
	shape->u = 0.0f;
	shape->type = 0;
	DXT1(12);
	return body;
}

DX_EXPORT cpBody *Body_CreateRectangle(cpSpace *space, float mass, float x, float y, float width, float height)
{
	DXD(5, space, mass, x, width, height);
	cpBody *body = cpBodyNew(mass, cpMomentForBox(mass, width, height));
	cpSpaceAddBody(space, body);
	cpBodySetPosition(body, cpv(x, y));
	cpShape *shape = cpSpaceAddShape(space, cpBoxShapeNew(body, width, height, 0.05f));
	cpBodyActivate(shape->body);
	shape->u = 0.0f;
	shape->type = 0;
	return body;
}

DX_EXPORT cpShape *Body_AddSensor(cpBody *body, int shapeType, float width, float height, cpBitmask categoryMask, cpBitmask collisionMask)
{
	DXD(6, body, shapeType, width, height, categoryMask);
	cpShape *shape;
	if (shapeType == 1) shape = cpCircleShapeNew(body, width, cpvzero);
	else if (shapeType == 2) shape = cpBoxShapeNew(body, width, height, 0.05f);
	else return NULL;
	cpSpaceAddShape(body->space, shape);
	cpBodyActivate(shape->body);
	shape->u = 0.0f;
	cpBodyActivate(shape->body);
	shape->filter.categories = categoryMask;
	shape->filter.mask = collisionMask;
	shape->sensor = cpTrue;
	return shape;
}

DX_EXPORT void Body_Dispose(cpBody *body)
{
	DXD(7, body, body->dx_dead, g_ndead, 0, 0);
	if (body->dx_dead) return;
	cpSpace *space;
	cpShape *shape;
	DXT0();

	if (dx_defer_dispose && body->space)
	{
		CP_BODY_FOREACH_SHAPE(body, s) { s->filter.categories = 0; s->filter.mask = 0; }
		body->dx_dead = 1;
		if (g_ndead == g_capdead) { g_capdead = g_capdead ? g_capdead * 2 : 1024; g_dead = (cpBody **)realloc(g_dead, g_capdead * sizeof(cpBody *)); }
		g_dead[g_ndead++] = body;
		DXT1(10);
		return;
	}
	while ((shape = body->shapeList) != NULL)
	{
		space = body->space;
		cpSpaceRemoveShape(space, shape);
		cpShapeFree(shape);
	}
	cpSpaceRemoveBody(body->space, body);
	cpBodyFree(body);
	DXT1(10);
}

DX_EXPORT cpShape *Body_GetShape(cpBody *body) { return body->shapeList; }
DX_EXPORT void *Shape_GetUserData(cpShape *shape) { return shape->userData; }

DX_EXPORT void Body_SetSize(cpBody *body, float width, float height)
{
	DXD(8, body, width, height, 0, 0);
	cpShape *shape = body->shapeList; if (!shape) return;
	float hw = width * 0.5f;
	if (shape->klass->type == CP_CIRCLE_SHAPE) { ((cpCircleShape *)shape)->r = hw; return; }
	struct cpSplittingPlane *pl = ((cpPolyShape *)shape)->planes;
	float hh = height * 0.5f, nh = height * -0.5f, nw = width * -0.5f;
	pl[0].v0.x = hw; pl[0].v0.y = hh;
	pl[1].v0.x = hw; pl[1].v0.y = nh;
	pl[2].v0.x = nw; pl[2].v0.y = nh;
	pl[3].v0.x = nw; pl[3].v0.y = hh;
}

DX_EXPORT DXPointF Body_GetSize(cpBody *body)
{
	cpShape *shape = body->shapeList; if (!shape) return dxp(0.0f, 0.0f);
	if (shape->klass->type == CP_CIRCLE_SHAPE) { float d = ((cpCircleShape *)shape)->r + ((cpCircleShape *)shape)->r; return dxp(d, d); }
	struct cpSplittingPlane *pl = ((cpPolyShape *)shape)->planes;
	return dxp(pl[0].v0.x - pl[3].v0.x, pl[0].v0.y - pl[1].v0.y);
}

DX_EXPORT void Body_SetAngle(cpBody *body, float angle)
{
	DXD(9, body, angle, 0, 0, 0);
	cpBodySetAngle(body, angle);
	cpSpaceReindexShapesForBody(g_space, body);
}
DX_EXPORT float Body_GetAngle(cpBody *body) { return body->a; }
DX_EXPORT void Body_SetPosition(cpBody *body, float x, float y) { DXD(10, body, x, y, 0, 0); cpBodySetPosition(body, cpv(x, y)); }
DX_EXPORT DXPointF Body_GetPosition(cpBody *body) { return dxp(body->p.x, body->p.y); }
DX_EXPORT void Body_SetVelocity(cpBody *body, float x, float y) { cpBodyActivate(body); body->v = cpv(x, y); }
DX_EXPORT DXPointF Body_GetVelocity(cpBody *body) { return dxp(body->v.x, body->v.y); }
DX_EXPORT void Body_SetCollisionCallbackType(cpBody *body, unsigned int type) { if (body->shapeList) body->shapeList->type = (cpCollisionType)type; }
DX_EXPORT void Body_SetMass(cpBody *body, float mass) { DXD(12, body, mass, 0, 0, 0); cpBodySetMass(body, mass); }
DX_EXPORT float Body_GetMass(cpBody *body) { return body->m; }
DX_EXPORT void Body_SetForce(cpBody *body, float x, float y) { cpBodyActivate(body); body->f = cpv(x, y); }
DX_EXPORT DXPointF Body_GetForce(cpBody *body) { return dxp(body->f.x, body->f.y); }
DX_EXPORT void Body_ApplyImpulseAtWorldPoint(cpBody *body, float ix, float iy, float wx, float wy) { cpBodyApplyImpulseAtWorldPoint(body, cpv(ix, iy), cpv(wx, wy)); }
DX_EXPORT void Body_ApplyForceAtWorldPoint(cpBody *body, float fx, float fy, float wx, float wy) { cpBodyApplyForceAtWorldPoint(body, cpv(fx, fy), cpv(wx, wy)); }

DX_EXPORT void Body_SetType(cpBody *body, unsigned char type)
{
	DXD(14, body, type, 0, 0, 0);
	cpShape *shape = body->shapeList;
	if (shape) shape->sensor = cpFalse;
	if (type == 1) cpBodySetType(body, CP_BODY_TYPE_STATIC);
	else if (type == 2) cpBodySetType(body, CP_BODY_TYPE_DYNAMIC);
	else if (type == 3) cpBodySetType(body, CP_BODY_TYPE_KINEMATIC);
	else if (type == 4) { cpBodySetType(body, CP_BODY_TYPE_DYNAMIC); shape = body->shapeList; if (shape) shape->sensor = cpTrue; }
}

DX_EXPORT unsigned char Body_GetType(cpBody *body)
{
	if (body->shapeList && body->shapeList->sensor == 1) return 4;
	if (body->sleeping.idleTime == INFINITY) return 1;
	if (body->m == INFINITY) return 3;
	return 2;
}

DX_EXPORT void *Body_GetUserData(cpBody *body) { return body->userData; }

DX_EXPORT void Body_SetUserData(cpBody *body, void *data) { DXD(15, body, 0, 0, 0, 0); body->userData = data; if (body->shapeList) body->shapeList->userData = data; }

DX_EXPORT cpBitmask Body_GetCategoryMask(cpBody *body) { return body->shapeList ? body->shapeList->filter.categories : 0; }
DX_EXPORT cpBitmask Body_GetCollisionMask(cpBody *body) { return body->shapeList ? body->shapeList->filter.mask : 0; }
DX_EXPORT void Body_SetCollisionMask(cpBody *body, cpBitmask mask) { if (body->shapeList) body->shapeList->filter.mask = mask; }
DX_EXPORT void Body_SetCategoryMask(cpBody *body, cpBitmask mask) { if (body->shapeList) body->shapeList->filter.categories = mask; }

DX_EXPORT int Body_Collides(cpBody *body1, cpBody *body2)
{
	cpContactPointSet set = cpShapesCollide(body1->shapeList, body2->shapeList);
	return set.count == 0;
}

#ifdef DX_PROFILE
extern double dx_prof[32]; extern long long dx_prof_steps, dx_prof_arbiters, dx_prof_bodies;
DX_EXPORT int DX_ProfileGet(double *out, int n)
{
	extern volatile long long dx_ray_stat[6]; int k; for (k = 0; k < 6; k++) dx_prof[21 + k] = (double)dx_ray_stat[k];
	int i; dx_prof[29] = (double)dx_prof_steps; dx_prof[30] = (double)dx_prof_arbiters; dx_prof[31] = (double)dx_prof_bodies;
	for (i = 0; i < n && i < 32; i++) out[i] = dx_prof[i];
	return 32;
}
DX_EXPORT void DX_ProfileReset(void) { extern volatile long long dx_ray_stat[6]; int i; for (i = 0; i < 6; i++) dx_ray_stat[i] = 0; for (i = 0; i < 32; i++) dx_prof[i] = 0; dx_prof_steps = dx_prof_arbiters = dx_prof_bodies = 0; }
#endif

DX_EXPORT int DX_SetOption(int name, int value)
{
	if (name == 0) { dx_pool_set_threads(value); return dx_pool_threads(); }
	if (name == 1) { int o = dx_par_independent; dx_par_independent = value; return o; }
	if (name == 2) { int o = dx_skip_threading; dx_skip_threading = value; return o; }
	if (name == 3) { int o = dx_par_solver; dx_par_solver = value; return o; }
	if (name == 4) { int o = dx_defer_dispose; dx_defer_dispose = value; return o; }
	if (name == 5) { int o = dx_par_collide; dx_par_collide = value; return o; }
	if (name == 6) { int o = dx_par_record; dx_par_record = value; return o; }
	if (name == 7) { int o = dx_block_solver; dx_block_solver = value; return o; }
	if (name == 8) { int o = dx_par_filter; dx_par_filter = value; return o; }
	if (name == 10) { extern int dx_fat_enabled; int o = dx_fat_enabled; dx_fat_enabled = value; return o; }
	if (name == 11) { extern float dx_fat_vel; int o = (int)(dx_fat_vel * 100); dx_fat_vel = value / 100.0f; return o; }
	if (name == 12) { extern float dx_fat_min; int o = (int)(dx_fat_min * 100); dx_fat_min = value / 100.0f; return o; }
	if (name == 9) { int o = dx_block_alternate; dx_block_alternate = value; return o; }
	if (name == 100) return dx_last_colors;
	return -1;
}
