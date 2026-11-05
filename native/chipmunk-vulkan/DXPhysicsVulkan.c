// Chipmunk2DVulkan: the game's physics DLL (DXPhysicsNative.dll, same exports as the original) on upstream Chipmunk2D
// (fetched by scripts\build.ps1 from codeberg.org/slembcke/Chipmunk2D) with the step on the GPU (Vulkan compute,
// cpvk.comp / cpvk_gpu.c).
//
// Bodies and shapes are upstream cpBody / cpCircleShape / cpPolyShape structs, embedded in slot records (VBody,
// VShape) so the GPU can address them by slot. In GPU mode they are not in a cpSpace: each step uploads the bodies,
// runs Chipmunk's integration / collision / solver order as kernels (cpvk.comp), calls the game's begin and separate
// callbacks between the two halves (as cpSpaceStep does between collision and solving), and writes the results back.
// Queries use the grid the GPU built (dynamic shapes), a grid of static shapes built here, and a list of shapes made
// since. Without a usable Vulkan device (or with DX_VK_GPU=0, or after a GPU failure) everything lives in a cpSpace
// and upstream cpSpaceStep runs instead (CPU mode).
#define CP_USE_DOUBLES 0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "chipmunk/chipmunk_private.h"
#include "chipmunk/chipmunk_unsafe.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "dxpool.h"
#include "cpvk_gpu.h"

// The game writes into the physics DLL at fixed offsets of the original DLL's layout: 5 bytes at base + 0x13B3 (inside
// dx_text_pad, dxdiag.c) and a value at base + 0x3AE30 (the end of the original's .data). In the smaller Chipmunk and
// Rapier DLLs that second offset is read-only data; in this larger DLL it would be code (the write crashed the game in
// the C runtime). dx_text_pad2 (dxpad.asm: nops in .text right after dx_text_pad) keeps every real function past it;
// scripts\build.ps1 checks in the link map that it covers the offset. It is never executed.
#pragma comment(linker, "/INCLUDE:dx_text_pad2")   // dxpad.asm

void dx_diag_init(void);
void dx_diag_thread(void);
void dx_diag_call(unsigned id, void *p, float a, float b, float c, float d);
void dx_check_patches(const char *where);
#define DXD(id, p, a, b, c, d) dx_diag_call((id), (void *)(p), (float)(a), (float)(b), (float)(c), (float)(d))
#define DX_EXPORT __declspec(dllexport)

typedef struct { float x, y; } DXPointF;
typedef int(__cdecl *DXCollisionCallback)(void *userA, void *userB);
typedef void(__cdecl *DXSeparationCallback)(void *userA, void *userB);
typedef void(__cdecl *DXEntityCallback)(void *userData);
static DXPointF dxp(float x, float y) { DXPointF p; p.x = x; p.y = y; return p; }

double now_us(void) { static double inv; LARGE_INTEGER c, f; if (inv == 0) { QueryPerformanceFrequency(&f); inv = 1e6 / (double)f.QuadPart; } QueryPerformanceCounter(&c); return (double)c.QuadPart * inv; }
int env_int(const char *name, int def)
{
	char t[32]; DWORD n = GetEnvironmentVariableA(name, t, sizeof t);
	if (n == 0 || n >= sizeof t) return def;
	char *end; long v = strtol(t, &end, 10);
	return (end != t && *end == 0) ? (int)v : def;
}

// ---------- records ----------
typedef struct World World;

typedef struct VBody {
	cpBody cp;                  // first: the game's body handle is &cp
	World *world;
	int slot, alive, fresh;
	float mass, moment;         // dynamic mass properties, kept across type changes
	int uploadStamp, patchStamp, posStamp;
	int grp, gslot;             // body group (DX_GroupNew slot + 1, 0 none) and index in it; under g_grplk
} VBody;

typedef struct VShape {
	union { cpShape s; cpCircleShape c; cpPolyShape p; } u;   // first: the game's shape handle
	World *world; VBody *body;
	int slot, alive, dirty, kind;    // kind: CVK_SK_CIRCLE / CVK_SK_BOX
	uint32_t gen;
	float r, w, h;                   // circle radius; box size and corner radius r
	float bb[4];                     // box at creation (Chipmunk caches it when the shape is added)
} VShape;

#define CHUNK_BITS 12
#define CHUNK (1 << CHUNK_BITS)
#define MAX_CHUNKS 2048              // 8M slots

typedef struct { int *v; int n, cap; } IntVec;
static int iv_push(IntVec *a, int x)
{
	if (a->n == a->cap) { int c = a->cap ? a->cap * 2 : 256; int *g = (int *)realloc(a->v, (size_t)c * sizeof(int)); if (!g) return 0; a->v = g; a->cap = c; }
	a->v[a->n++] = x; return 1;
}

typedef struct {
	char *chunk[MAX_CHUNKS]; size_t elem;
	int hi;                          // slots below hi have been used
	IntVec freeList, limbo, cut;     // free: reusable; limbo: freed since the last upload; cut: freed before it
} Slots;

static void *slot_ptr(Slots *s, int i) { char *c = s->chunk[i >> CHUNK_BITS]; return c ? c + (size_t)(i & (CHUNK - 1)) * s->elem : NULL; }

// Static grid (shapes of static bodies, each in every cell its box covers): built here, used by the GPU and queries.
typedef struct {
	uint32_t hs, nItems, nRecs, capItems, capRecs, capStart;
	uint32_t *start, *items; CvkStaticRec *recs;
	float cell;
} SGrid;

// Pending shapes (made since the upload of the step whose grid queries use), in fixed chunks so readers never see a move.
#define PEND_CHUNKS 4096
typedef struct { int *chunk[PEND_CHUNKS]; volatile LONG count; int cut; } Pending;

// Callback pairs (collision types 1 and 0) that touch: begin was called, ignored = begin returned false.
typedef struct { uint64_t key; uint32_t ga, gb; int ignored, used; } PairEnt;
typedef struct { PairEnt *e; uint32_t cap, n; } PairMap;

struct World {
	int gpu;                         // 1 = GPU mode, 0 = CPU mode (cpu != NULL)
	cpSpace *cpu;
	int iterations; float gx, gy, damping, slop, collisionBias;
	Slots bodies, shapes;
	CRITICAL_SECTION cs;             // slots, dirty lists, pending appends, deferred disposals, patches
	IntVec dirtyShapes;
	volatile LONG staticDirty;
	VBody **dead; int ndead, capdead;
	int stamp; float currDt;
	volatile LONG stepping;
	// query index
	SRWLOCK idx;
	SGrid sg[2]; int sgCur;
	SGrid dg[2]; int dgCur;          // query grid of the dynamic shapes (built each step from the GPU's boxes)
	Pending pend;
	PairMap pairs[2]; int pairCur;
	IntVec patches;
	IntVec disable;
	float cell0;
};

static World *g_world;
// Body groups (TABPerf: the swarm minimap indicator, as in the Rapier wrapper): ids are slot | generation << 12.
typedef struct { VBody **b; int n, cap; int gen; int used; } BodyGroup;
#define GRP_SLOTS 4096
static BodyGroup *g_grps; static int g_ngrps;
static SRWLOCK g_grplk = SRWLOCK_INIT;   // taken after W->cs, never before it
static void group_release(BodyGroup *g) { for (int i = 0; i < g->n; i++) g->b[i]->grp = 0; g->n = 0; g->used = 0; }
static void group_remove(VBody *b)   // caller holds g_grplk exclusively
{
	if (!b->grp) return;
	BodyGroup *g = &g_grps[b->grp - 1];
	int i = b->gslot; VBody *last = g->b[--g->n];
	g->b[i] = last; last->gslot = i;
	b->grp = 0;
}
static DXCollisionCallback g_collision;
static DXSeparationCallback g_separation;
static volatile LONG g_cbwindow;    // the step is inside the game's callbacks: body changes are sent to the second half
static int g_threads_explicit;
static char g_status[512] = "not started";
static int g_gpuWanted = -1;        // -1 unknown, 0 = CPU mode asked (DX_VK_GPU=0), 1 = GPU
static int g_iterOverride;           // DX_VK_ITERATIONS / DX_SetOption(210): solver passes (0 = the game's value)
static int g_jacobiMax = -1;         // DX_VK_JACOBI_MAX / DX_SetOption(211): colours of at most this many contacts go to the Jacobi pass
static int g_steps_seen;

// step section times (µs) and counts since the last reset (DX_SetOption 300-309, 399)
static volatile double g_st[10]; static volatile LONG g_stSteps;

// ---------- helpers ----------
static int body_type(const cpBody *b) { return b->sleeping.idleTime == INFINITY ? CVK_BT_STATIC : b->m == INFINITY ? CVK_BT_KINEMATIC : CVK_BT_DYNAMIC; }
static int is_static(const VBody *b) { return b->cp.sleeping.idleTime == INFINITY; }
static VShape *vshape(cpShape *s) { return (VShape *)s; }
static VBody *vbody(cpBody *b) { return (VBody *)b; }

static void shape_bb(const VShape *s, float out[4])
{
	const cpBody *b = &s->body->cp;
	float px = b->p.x, py = b->p.y;
	if (s->kind == CVK_SK_CIRCLE) { out[0] = px - s->r; out[1] = py - s->r; out[2] = px + s->r; out[3] = py + s->r; return; }
	float c = cosf(b->a), sn = sinf(b->a), hx = s->w * 0.5f, hy = s->h * 0.5f;
	float ex = fabsf(c) * hx + fabsf(sn) * hy + s->r, ey = fabsf(sn) * hx + fabsf(c) * hy + s->r;
	out[0] = px - ex; out[1] = py - ey; out[2] = px + ex; out[3] = py + ey;
}
static int bb_sane(const float b[4]) { return fabsf(b[0]) < 1e6f && fabsf(b[1]) < 1e6f && fabsf(b[2]) < 1e6f && fabsf(b[3]) < 1e6f && b[0] <= b[2] && b[1] <= b[3]; }
static int bb_overlap(const float a[4], const float b[4]) { return !(a[2] < b[0] || b[2] < a[0] || a[3] < b[1] || b[3] < a[1]); }
// the game's filter against a query mask (the original DLL's cpShapeFilterReject with categories 0)
static int query_pass(const cpShape *s, cpBitmask mask) { return (s->filter.categories & mask) != 0; }

// ---------- slots ----------
static int slots_alloc(World *W, Slots *s, int stepping)
{
	int i;
	if (!stepping && s->freeList.n) i = s->freeList.v[--s->freeList.n];
	else
	{
		i = s->hi;
		if ((i >> CHUNK_BITS) >= MAX_CHUNKS) return -1;
		if (!s->chunk[i >> CHUNK_BITS]) { s->chunk[i >> CHUNK_BITS] = (char *)calloc(CHUNK, s->elem); if (!s->chunk[i >> CHUNK_BITS]) return -1; }
		s->hi++;
	}
	(void)W;
	return i;
}

static void mark_shape_dirty(World *W, VShape *s)
{
	if (!W->gpu || s->dirty) return;
	s->dirty = 1;
	iv_push(&W->dirtyShapes, s->slot);
}

static void pending_add(World *W, int slot)
{
	Pending *p = &W->pend;
	LONG n = p->count;
	int ci = (int)(n >> CHUNK_BITS);
	if (ci >= PEND_CHUNKS) return;
	if (!p->chunk[ci]) { p->chunk[ci] = (int *)calloc(CHUNK, sizeof(int)); if (!p->chunk[ci]) return; }
	p->chunk[ci][n & (CHUNK - 1)] = slot;
	MemoryBarrier();
	p->count = n + 1;
}

static VBody *new_body(World *W, float mass, float moment, float x, float y)
{
	EnterCriticalSection(&W->cs);
	int slot = slots_alloc(W, &W->bodies, W->stepping);
	LeaveCriticalSection(&W->cs);
	if (slot < 0) return NULL;
	VBody *b = (VBody *)slot_ptr(&W->bodies, slot);
	memset(b, 0, sizeof *b);
	cpBodyInit(&b->cp, mass, moment);
	b->world = W; b->slot = slot; b->alive = 1; b->fresh = 1;
	b->mass = mass; b->moment = moment;
	cpBodySetPosition(&b->cp, cpv(x, y));
	if (!W->gpu) cpSpaceAddBody(W->cpu, &b->cp);
	return b;
}

// a new shape of body b: kind circle (r) or box (w, h, corner radius r)
static VShape *new_shape(World *W, VBody *b, int kind, float r, float w, float h)
{
	EnterCriticalSection(&W->cs);
	int slot = slots_alloc(W, &W->shapes, W->stepping);
	LeaveCriticalSection(&W->cs);
	if (slot < 0) return NULL;
	VShape *s = (VShape *)slot_ptr(&W->shapes, slot);
	uint32_t gen = s->gen + 1;
	memset(s, 0, sizeof *s);
	if (kind == CVK_SK_CIRCLE) cpCircleShapeInit(&s->u.c, &b->cp, r, cpvzero);
	else cpBoxShapeInit(&s->u.p, &b->cp, w, h, r);
	s->world = W; s->body = b; s->slot = slot; s->alive = 1; s->kind = kind; s->gen = gen;
	s->r = r; s->w = w; s->h = h;
	if (W->gpu)
	{
		cpBodyAddShape(&b->cp, &s->u.s);
		cpShapeUpdate(&s->u.s, b->cp.transform);
		shape_bb(s, s->bb);
		EnterCriticalSection(&W->cs);
		mark_shape_dirty(W, s);
		pending_add(W, slot);
		if (is_static(b)) W->staticDirty = 1;
		LeaveCriticalSection(&W->cs);
	}
	else
	{
		cpSpaceAddShape(W->cpu, &s->u.s);
		shape_bb(s, s->bb);
	}
	return s;
}

// ---------- body changes made inside the game's callbacks go to the second half of the step ----------
static void note_change(VBody *b, int pos)
{
	if (!g_cbwindow) return;
	World *W = b->world;
	if (W != g_world || !W->gpu || b->uploadStamp != W->stamp) return;
	EnterCriticalSection(&W->cs);
	if (b->patchStamp != W->stamp) { b->patchStamp = W->stamp; iv_push(&W->patches, b->slot); }
	if (pos) b->posStamp = W->stamp;
	LeaveCriticalSection(&W->cs);
}

// ---------- body types (as cpBodySetType, but the dynamic mass is kept) ----------
static void set_body_type(VBody *b, int t)
{
	cpBody *c = &b->cp;
	if (body_type(c) == t) return;
	World *W = b->world;
	if (!W->gpu && c->space)
	{
		cpBodySetType(c, t == CVK_BT_STATIC ? CP_BODY_TYPE_STATIC : t == CVK_BT_KINEMATIC ? CP_BODY_TYPE_KINEMATIC : CP_BODY_TYPE_DYNAMIC);
		if (t == CVK_BT_DYNAMIC) { c->m = b->mass; c->m_inv = 1.0f / b->mass; c->i = b->moment; c->i_inv = 1.0f / b->moment; }
		return;
	}
	int wasStatic = is_static(b);
	if (t == CVK_BT_DYNAMIC)
	{
		c->sleeping.idleTime = 0.0f;
		c->m = b->mass; c->m_inv = 1.0f / b->mass; c->i = b->moment; c->i_inv = 1.0f / b->moment;
	}
	else
	{
		c->sleeping.idleTime = t == CVK_BT_STATIC ? INFINITY : 0.0f;
		c->m = c->i = INFINITY; c->m_inv = c->i_inv = 0.0f;
		c->v = cpvzero; c->w = 0.0f;
	}
	if (wasStatic || t == CVK_BT_STATIC) InterlockedExchange(&W->staticDirty, 1);
}

// ---------- disposal ----------
static void free_body_now(World *W, VBody *b)
{
	cpBody *c = &b->cp;
	if (!W->gpu)
	{
		cpShape *s;
		// CPU mode has no GPU grid that could still name the slots: they are free right away
		while ((s = c->shapeList) != NULL) { cpSpaceRemoveShape(W->cpu, s); vshape(s)->alive = 0; EnterCriticalSection(&W->cs); iv_push(&W->shapes.freeList, vshape(s)->slot); LeaveCriticalSection(&W->cs); }
		cpSpaceRemoveBody(W->cpu, c);
		EnterCriticalSection(&W->cs);
		AcquireSRWLockExclusive(&g_grplk); group_remove(b); b->alive = 0; ReleaseSRWLockExclusive(&g_grplk);
		iv_push(&W->bodies.freeList, b->slot);
		LeaveCriticalSection(&W->cs);
		return;
	}
	else
	{
		EnterCriticalSection(&W->cs);
		int st = is_static(b);
		for (cpShape *s = c->shapeList; s; s = s->next)
		{
			VShape *v = vshape(s);
			v->alive = 0;
			mark_shape_dirty(W, v);
			iv_push(&W->shapes.limbo, v->slot);
		}
		if (st && c->shapeList) W->staticDirty = 1;
		LeaveCriticalSection(&W->cs);
	}
	EnterCriticalSection(&W->cs);
	// with alive = 0 under the group lock: DX_GroupAdd either sees it dead or is undone here
	AcquireSRWLockExclusive(&g_grplk); group_remove(b); b->alive = 0; ReleaseSRWLockExclusive(&g_grplk);
	iv_push(&W->bodies.limbo, b->slot);
	LeaveCriticalSection(&W->cs);
}

static void cpu_post_remove(cpSpace *space, void *key, void *data) { (void)space; (void)data; VBody *b = (VBody *)key; free_body_now(b->world, b); }

// ---------- CPU mode: everything into a cpSpace, upstream cpSpaceStep ----------
static cpBool cpu_begin(cpArbiter *arb, cpSpace *space, void *data)
{
	(void)space; (void)data;
	if (!arb->body_a->userData || !arb->body_b->userData || !g_collision) return cpFalse;
	return (cpBool)g_collision(arb->body_a->userData, arb->body_b->userData);
}
static void cpu_separate(cpArbiter *arb, cpSpace *space, void *data)
{
	(void)space; (void)data;
	if (!arb->body_a->userData || !arb->body_b->userData || !g_separation) return;
	g_separation(arb->body_a->userData, arb->body_b->userData);
}

static cpSpace *new_cpu_space(World *W)
{
	cpSpace *space = cpSpaceNew();
	space->gravity = cpv(W->gx, W->gy);
	cpSpaceSetIterations(space, W->iterations);
	cpCollisionHandler *h = cpSpaceAddCollisionHandler(space, 1, 0);
	h->beginFunc = cpu_begin; h->separateFunc = cpu_separate;
	space->damping = W->damping;
	space->collisionSlop = W->slop;
	return space;
}

// GPU mode to CPU mode (no Vulkan device, or the GPU failed): the live bodies and shapes go into a new cpSpace.
static void switch_to_cpu(World *W, const char *why)
{
	if (!W->gpu) return;
	_snprintf(g_status, sizeof g_status, "CPU (upstream cpSpaceStep): %s", why); g_status[sizeof g_status - 1] = 0;
	W->cpu = new_cpu_space(W);
	W->gpu = 0;
	for (int i = 0; i < W->bodies.hi; i++)
	{
		VBody *b = (VBody *)slot_ptr(&W->bodies, i);
		if (!b || !b->alive) continue;
		cpShape *list[64]; int n = 0;
		while (b->cp.shapeList && n < 64) { cpShape *s = b->cp.shapeList; cpBodyRemoveShape(&b->cp, s); list[n++] = s; }
		cpSpaceAddBody(W->cpu, &b->cp);
		for (int k = n - 1; k >= 0; k--) cpSpaceAddShape(W->cpu, list[k]);   // keeps the list order (first shape first)
	}
}

// ---------- static grid ----------
static void sg_reserve(SGrid *g, uint32_t hs, uint32_t items, uint32_t recs)
{
	if (g->capStart < hs + 1) { free(g->start); g->start = (uint32_t *)malloc((size_t)(hs + 1) * 4); g->capStart = g->start ? hs + 1 : 0; }
	if (g->capItems < items) { free(g->items); g->capItems = items + items / 2 + 64; g->items = (uint32_t *)malloc((size_t)g->capItems * 4); if (!g->items) g->capItems = 0; }
	if (g->capRecs < recs) { free(g->recs); g->capRecs = recs + recs / 2 + 64; g->recs = (CvkStaticRec *)malloc((size_t)g->capRecs * sizeof(CvkStaticRec)); if (!g->recs) g->capRecs = 0; }
}

// Bucket table of a grid whose records (g->recs, g->nRecs) are set: each record in every cell its box covers, once per
// bucket. hs = 0 when there are none.
static int grid_fill(SGrid *g, float cell, int salt)
{
	uint32_t k = g->nRecs;
	g->cell = cell;
	if (k == 0) { g->hs = 0; g->nItems = 0; return 1; }
	uint64_t total = 0;
	for (uint32_t r = 0; r < k; r++)
	{
		const float *bb = g->recs[r].bb;
		int x0 = (int)floorf(bb[0] / cell), x1 = (int)floorf(bb[2] / cell), y0 = (int)floorf(bb[1] / cell), y1 = (int)floorf(bb[3] / cell);
		total += (uint64_t)(x1 - x0 + 1) * (uint64_t)(y1 - y0 + 1);
	}
	if (total > (1u << 26)) return 0;
	uint32_t hs = 64; while (hs < total * 2 && hs < (1u << 24)) hs <<= 1;
	sg_reserve(g, hs, (uint32_t)total, k);
	uint32_t *last = (uint32_t *)malloc((size_t)hs * 4);
	if (!g->start || !g->items || !last) { free(last); return 0; }
	memset(g->start, 0, (size_t)(hs + 1) * 4);
	memset(last, 0xFF, (size_t)hs * 4);
	for (int pass = 0; pass < 2; pass++)
	{
		if (pass == 1)
		{
			uint32_t sum = 0;
			for (uint32_t b = 0; b < hs; b++) { uint32_t c = g->start[b]; g->start[b] = sum; sum += c; }
			g->start[hs] = sum; g->nItems = sum;
			memset(last, 0xFF, (size_t)hs * 4);
		}
		for (uint32_t r = 0; r < k; r++)
		{
			const float *bb = g->recs[r].bb;
			int x0 = (int)floorf(bb[0] / cell), x1 = (int)floorf(bb[2] / cell), y0 = (int)floorf(bb[1] / cell), y1 = (int)floorf(bb[3] / cell);
			for (int y = y0; y <= y1; y++)
				for (int x = x0; x <= x1; x++)
				{
					uint32_t b = cvk_hash3(x, y, salt) & (hs - 1);
					if (last[b] == r) continue;
					last[b] = r;
					if (pass == 0) g->start[b]++;
					else g->items[g->start[b]++] = r;
				}
		}
	}
	// the fill pass moved every start to the next bucket's: shift back
	for (uint32_t b = hs; b > 0; b--) g->start[b] = g->start[b - 1];
	g->start[0] = 0;
	g->hs = hs;
	free(last);
	return 1;
}

// Builds the static grid from the live shapes of static bodies. Cell 2 (larger when the shapes would cover too many).
static int sg_build(World *W, SGrid *g)
{
	uint32_t nrec = 0;
	for (int i = 0; i < W->shapes.hi; i++)
	{
		VShape *s = (VShape *)slot_ptr(&W->shapes, i);
		if (s && s->alive && is_static(s->body)) nrec++;
	}
	sg_reserve(g, 0, 0, nrec ? nrec : 1);
	if (!g->recs) return 0;
	uint32_t k = 0; double area = 0;
	for (int i = 0; i < W->shapes.hi && k < nrec; i++)
	{
		VShape *s = (VShape *)slot_ptr(&W->shapes, i);
		if (!s || !s->alive || !is_static(s->body)) continue;
		float bb[4]; shape_bb(s, bb);
		if (!bb_sane(bb)) continue;
		memcpy(g->recs[k].bb, bb, sizeof bb); g->recs[k].shape = (uint32_t)i; g->recs[k].pad[0] = g->recs[k].pad[1] = g->recs[k].pad[2] = 0;
		area += (double)(bb[2] - bb[0] + 2.0f) * (double)(bb[3] - bb[1] + 2.0f);
		s->bb[0] = bb[0]; s->bb[1] = bb[1]; s->bb[2] = bb[2]; s->bb[3] = bb[3];
		k++;
	}
	g->nRecs = k;
	float cell = 2.0f;
	while (area / ((double)cell * cell) > 8.0e6) cell *= 2.0f;
	return grid_fill(g, cell, CVK_STATIC_SALT);
}

// Query grid of the dynamic (and kinematic) shapes, from the boxes the GPU computed this step: every shape in each
// 2-unit cell its box covers. Queries walk only the cells under them (rays: along the ray, nearest cells first).
#define DYN_SALT 86
static int dg_build(World *W, SGrid *g, const CvkFrame *F)
{
	uint32_t n = F->nEntries;
	sg_reserve(g, 0, 0, n ? n : 1);
	if (!g->recs) return 0;
	uint32_t k = 0;
	for (uint32_t e = 0; e < n; e++)
	{
		uint32_t sl = F->eshape[e];
		if (sl >= F->nShapes) continue;
		const float *bb = F->sbb + (size_t)sl * 4;
		if (!bb_sane(bb)) continue;
		memcpy(g->recs[k].bb, bb, 4 * sizeof(float)); g->recs[k].shape = sl; g->recs[k].pad[0] = g->recs[k].pad[1] = g->recs[k].pad[2] = 0;
		k++;
	}
	g->nRecs = k;
	(void)W;
	return grid_fill(g, 2.0f, DYN_SALT);
}

// The dynamic grid is built on its own thread while the GPU runs the second half of the step (its inputs come back
// with the first half). One job at a time: dg_start, then dg_join before the step ends.
static struct { HANDLE thread, go, done; World *W; SGrid *g; const CvkFrame *F; int ok, running; } g_dg;
static DWORD WINAPI dg_thread(void *p)
{
	(void)p;
	dx_diag_thread();
	for (;;)
	{
		WaitForSingleObject(g_dg.go, INFINITE);
		g_dg.ok = dg_build(g_dg.W, g_dg.g, g_dg.F);
		SetEvent(g_dg.done);
	}
}
static void dg_start(World *W, SGrid *g, const CvkFrame *F)
{
	g_dg.W = W; g_dg.g = g; g_dg.F = F;
	if (!g_dg.thread)
	{
		g_dg.go = CreateEventA(NULL, FALSE, FALSE, NULL); g_dg.done = CreateEventA(NULL, FALSE, FALSE, NULL);
		if (g_dg.go && g_dg.done) g_dg.thread = CreateThread(NULL, 0, dg_thread, NULL, 0, NULL);
	}
	if (g_dg.thread) { g_dg.running = 1; SetEvent(g_dg.go); }
	else g_dg.ok = dg_build(W, g, F);   // no thread: here
}
static int dg_join(void)
{
	if (g_dg.running) { WaitForSingleObject(g_dg.done, INFINITE); g_dg.running = 0; }
	return g_dg.ok;
}

// ---------- queries (GPU mode) ----------
typedef void (*CandFn)(VShape *s, const float *bb, void *ctx);

static VShape *live_shape(World *W, uint32_t slot)
{
	if ((int)slot >= W->shapes.hi) return NULL;
	VShape *s = (VShape *)slot_ptr(&W->shapes, (int)slot);
	return s && s->alive ? s : NULL;
}

static VShape *grid_rec_shape(World *W, const CvkStaticRec *r) { return live_shape(W, r->shape); }

// records of a grid whose box overlaps q, each once: a record is taken in the cell holding the low corner of the overlap
static void grid_each_bb(World *W, SGrid *g, int salt, const float q[4], CandFn fn, void *ctx)
{
	if (!g->hs) return;
	float c = g->cell;
	int x0 = (int)floorf(q[0] / c), x1 = (int)floorf(q[2] / c), y0 = (int)floorf(q[1] / c), y1 = (int)floorf(q[3] / c);
	double cells = (double)(x1 - x0 + 1) * (double)(y1 - y0 + 1);
	if (cells > (double)g->nRecs)
	{
		for (uint32_t r = 0; r < g->nRecs; r++)
			if (bb_overlap(g->recs[r].bb, q)) { VShape *s = grid_rec_shape(W, &g->recs[r]); if (s) fn(s, g->recs[r].bb, ctx); }
		return;
	}
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			uint32_t b = cvk_hash3(x, y, salt) & (g->hs - 1);
			for (uint32_t e = g->start[b]; e < g->start[b + 1]; e++)
			{
				const CvkStaticRec *r = &g->recs[g->items[e]];
				if (!bb_overlap(r->bb, q)) continue;
				float cx = r->bb[0] > q[0] ? r->bb[0] : q[0], cy = r->bb[1] > q[1] ? r->bb[1] : q[1];
				if ((int)floorf(cx / c) != x || (int)floorf(cy / c) != y) continue;
				VShape *s = grid_rec_shape(W, r); if (s) fn(s, r->bb, ctx);
			}
		}
}

// every shape whose box overlaps q: static grid first, then the dynamic shapes, then the shapes made since
static void gpu_each_bb(World *W, const float q[4], CandFn fn, void *ctx)
{
	if (!bb_sane(q)) return;
	grid_each_bb(W, &W->sg[W->sgCur], CVK_STATIC_SALT, q, fn, ctx);
	grid_each_bb(W, &W->dg[W->dgCur], DYN_SALT, q, fn, ctx);
	LONG n = W->pend.count; MemoryBarrier();
	for (LONG i = 0; i < n; i++)
	{
		int sl = W->pend.chunk[i >> CHUNK_BITS][i & (CHUNK - 1)];
		VShape *s = live_shape(W, (uint32_t)sl);
		if (s && bb_overlap(s->bb, q)) fn(s, s->bb, ctx);
	}
}

// CPU mode: the cpSpace's indexes, as the original DLL
struct CpuQ { float q[4]; CandFn fn; void *ctx; };
static cpCollisionID cpu_bb_cb(struct CpuQ *c, cpShape *shape, cpCollisionID id, void *data)
{
	(void)data;
	float bb[4] = { shape->bb.l, shape->bb.b, shape->bb.r, shape->bb.t };
	if (bb_overlap(bb, c->q)) c->fn(vshape(shape), bb, c->ctx);
	return id;
}
static void each_bb(World *W, const float q[4], CandFn fn, void *ctx)
{
	if (W->gpu) { AcquireSRWLockShared(&W->idx); gpu_each_bb(W, q, fn, ctx); ReleaseSRWLockShared(&W->idx); return; }
	struct CpuQ c; memcpy(c.q, q, sizeof c.q); c.fn = fn; c.ctx = ctx;
	cpBB b = cpBBNew(q[0], q[1], q[2], q[3]);
	cpSpatialIndexQuery(W->cpu->dynamicShapes, &c, b, (cpSpatialIndexQueryFunc)cpu_bb_cb, NULL);
	cpSpatialIndexQuery(W->cpu->staticShapes, &c, b, (cpSpatialIndexQueryFunc)cpu_bb_cb, NULL);
}

// ---------- shape segment queries (cpShapeSegmentQuery for circles and rounded boxes) ----------
static float circle_seg(float cx, float cy, float r1, float ax, float ay, float bx, float by, float r2)
{
	float dax = ax - cx, day = ay - cy, dbx = bx - cx, dby = by - cy, rsum = r1 + r2;
	float dd = dax * dax + day * day, de = dax * dbx + day * dby, ee = dbx * dbx + dby * dby;
	float qa = dd - 2.0f * de + ee, qb = de - dd;
	float det = qb * qb - qa * (dd - rsum * rsum);
	if (det >= 0.0f) { float t = (-qb - sqrtf(det)) / qa; if (0.0f <= t && t <= 1.0f) return t; }
	return 2.0f;
}

static void box_verts(const VShape *s, float vx[4], float vy[4])
{
	const cpBody *b = &s->body->cp;
	float c = cosf(b->a), sn = sinf(b->a), hw = s->w * 0.5f, hh = s->h * 0.5f;
	static const float lx[4] = { 1, 1, -1, -1 }, ly[4] = { -1, 1, 1, -1 };   // cpBoxShapeInit2: (r,b) (r,t) (l,t) (l,b)
	for (int i = 0; i < 4; i++) { float x = lx[i] * hw, y = ly[i] * hh; vx[i] = b->p.x + x * c - y * sn; vy[i] = b->p.y + x * sn + y * c; }
}

// signed distance from a point to the shape (cpShapePointQuery distance)
static float shape_dist(const VShape *s, float px, float py)
{
	const cpBody *b = &s->body->cp;
	if (s->kind == CVK_SK_CIRCLE) { float dx = px - b->p.x, dy = py - b->p.y; return sqrtf(dx * dx + dy * dy) - s->r; }
	float c = cosf(b->a), sn = sinf(b->a), hw = s->w * 0.5f, hh = s->h * 0.5f;
	float dx = px - b->p.x, dy = py - b->p.y, qx = dx * c + dy * sn, qy = -dx * sn + dy * c;
	float ox = fabsf(qx) - hw, oy = fabsf(qy) - hh;
	float d;
	if (ox > 0.0f || oy > 0.0f) { float mx = ox > 0.0f ? ox : 0.0f, my = oy > 0.0f ? oy : 0.0f; d = sqrtf(mx * mx + my * my); }
	else d = ox > oy ? ox : oy;
	return d - s->r;
}

// alpha of the first contact of a segment of radius r2 with the shape, or > 1 for none
static float shape_seg(const VShape *s, float ax, float ay, float bx, float by, float r2)
{
	if (shape_dist(s, ax, ay) <= r2) return 0.0f;
	const cpBody *b = &s->body->cp;
	if (s->kind == CVK_SK_CIRCLE) return circle_seg(b->p.x, b->p.y, s->r, ax, ay, bx, by, r2);
	float vx[4], vy[4]; box_verts(s, vx, vy);
	float rsum = s->r + r2, best = 2.0f;
	for (int i = 0; i < 4; i++)
	{
		int p = (i + 3) & 3;
		float ex = vx[i] - vx[p], ey = vy[i] - vy[p], l = sqrtf(ex * ex + ey * ey);
		float nx = l > 0 ? ey / l : 0, ny = l > 0 ? -ex / l : 0;   // cpvrperp
		float an = ax * nx + ay * ny, d = an - (vx[i] * nx + vy[i] * ny) - rsum;
		if (d < 0.0f) continue;
		float bn = bx * nx + by * ny, den = an - bn; if (den < 1.17549435e-38f) den = 1.17549435e-38f;
		float t = d / den;
		if (t < 0.0f || 1.0f < t) continue;
		float px = ax + (bx - ax) * t, py = ay + (by - ay) * t;
		float dt = nx * py - ny * px, dmin = nx * vy[p] - ny * vx[p], dmax = nx * vy[i] - ny * vx[i];
		if (dmin <= dt && dt <= dmax) best = t;
	}
	if (rsum > 0.0f)
		for (int i = 0; i < 4; i++) { float t = circle_seg(vx[i], vy[i], s->r, ax, ay, bx, by, r2); if (t < best) best = t; }
	return best;
}

// ---------- exports: space ----------
DX_EXPORT DXPointF Sumar(int a, int b) { return dxp((float)a, (float)b); }
DX_EXPORT void Algo(void) {}

static int gpu_wanted(void)
{
	if (g_gpuWanted < 0) g_gpuWanted = env_int("DX_VK_GPU", 1) != 0;
	return g_gpuWanted;
}

DX_EXPORT cpSpace *Space_Create(float gravityX, float gravityY, int iterations)
{
	DXD(2, 0, gravityX, gravityY, iterations, 0);
	dx_check_patches("Space_Create");
	dx_diag_init();
	World *W = (World *)calloc(1, sizeof(World));
	if (!W) return NULL;
	InitializeCriticalSection(&W->cs);
	InitializeSRWLock(&W->idx);
	W->bodies.elem = sizeof(VBody); W->shapes.elem = sizeof(VShape);
	W->iterations = iterations; W->gx = gravityX; W->gy = gravityY;
	W->damping = 0.001f; W->slop = 0.1f; W->collisionBias = cpfpow(1.0f - 0.1f, 60.0f);
	W->cell0 = 0.5f;
	if (g_iterOverride == 0) g_iterOverride = env_int("DX_VK_ITERATIONS", 0);
	if (g_jacobiMax < 0) { int v = env_int("DX_VK_JACOBI_MAX", 256); g_jacobiMax = v < 0 ? 0 : v; }
	char err[256] = "";
	if (gpu_wanted() && cvk_init(err, sizeof err))
	{
		W->gpu = 1;
		cvk_forget_history();
		_snprintf(g_status, sizeof g_status, "GPU: %s", cvk_device_name()); g_status[sizeof g_status - 1] = 0;
	}
	else
	{
		W->cpu = new_cpu_space(W);
		if (!gpu_wanted()) _snprintf(g_status, sizeof g_status, "CPU (upstream cpSpaceStep): DX_VK_GPU=0");
		else _snprintf(g_status, sizeof g_status, "CPU (upstream cpSpaceStep): %s", err);
		g_status[sizeof g_status - 1] = 0;
	}
	// The previous world is never stepped again; its records stay (the game may still dispose its bodies). Groups empty.
	AcquireSRWLockExclusive(&g_grplk); for (int k = 0; k < g_ngrps; k++) group_release(&g_grps[k]); ReleaseSRWLockExclusive(&g_grplk);
	g_world = W;
	if (!g_threads_explicit)
	{
		int n = env_int("DX_PHYS_THREADS", -1);
		dx_pool_set_threads(n >= 0 ? n : dx_pool_auto_threads());
	}
	return (cpSpace *)W;
}

DX_EXPORT float Space_IsLocked(cpSpace *space)
{
	World *W = (World *)space;
	if (!W) return 0.0f;
	if (!W->gpu && W->cpu) return W->cpu->locked > 0 ? 1.0f : 0.0f;
	return W->stepping ? 1.0f : 0.0f;
}

DX_EXPORT void Space_SetCollisionCallback(cpSpace *space, DXCollisionCallback func) { (void)space; g_collision = func; }
DX_EXPORT void Space_SetSeparationCallback(cpSpace *space, DXSeparationCallback func) { (void)space; g_separation = func; }

// ---------- the GPU step ----------
static uint64_t pair_key(uint32_t a, uint32_t b) { return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a; }
static uint32_t pm_hash(uint64_t k) { k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33; return (uint32_t)k; }
static void pm_clear(PairMap *m, uint32_t want)
{
	uint32_t cap = 64; while (cap < want * 2) cap <<= 1;
	if (m->cap < cap) { free(m->e); m->e = (PairEnt *)calloc(cap, sizeof(PairEnt)); m->cap = m->e ? cap : 0; }
	else memset(m->e, 0, (size_t)m->cap * sizeof(PairEnt));
	m->n = 0;
}
static PairEnt *pm_find(PairMap *m, uint64_t key)
{
	if (!m->cap) return NULL;
	for (uint32_t i = pm_hash(key) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) { if (!m->e[i].used) return NULL; if (m->e[i].key == key) return &m->e[i]; }
}
static PairEnt *pm_insert(PairMap *m, uint64_t key)
{
	if (!m->cap) return NULL;
	for (uint32_t i = pm_hash(key) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) { if (!m->e[i].used || m->e[i].key == key) { if (!m->e[i].used) { m->e[i].used = 1; m->e[i].key = key; m->n++; } return &m->e[i]; } }
}
static int cb_cmp(const void *x, const void *y)
{
	const CvkCbPair *a = (const CvkCbPair *)x, *b = (const CvkCbPair *)y;
	uint64_t ka = pair_key(a->a, a->b), kb = pair_key(b->a, b->b);
	return ka < kb ? -1 : ka > kb ? 1 : 0;
}

// begin / separate callbacks for the touching pairs of collision types 1 and 0 (cpSpaceCollideShapes, cpSpaceArbiterSetFilter)
static void run_callbacks(World *W, const CvkFrame *F)
{
	W->disable.n = 0;
	CvkCbPair *cb = NULL;
	if (F->nCb) { cb = (CvkCbPair *)malloc((size_t)F->nCb * sizeof *cb); if (cb) { memcpy(cb, F->cb, (size_t)F->nCb * sizeof *cb); qsort(cb, F->nCb, sizeof *cb, cb_cmp); } }
	PairMap *old = &W->pairs[W->pairCur], *nw = &W->pairs[W->pairCur ^ 1];
	pm_clear(nw, F->nCb);
	InterlockedExchange(&g_cbwindow, 1);
	for (uint32_t i = 0; cb && i < F->nCb; i++)
	{
		VShape *a = live_shape(W, cb[i].a), *b = live_shape(W, cb[i].b);
		if (!a || !b || a->gen != cb[i].ga || b->gen != cb[i].gb) continue;
		uint64_t key = pair_key(cb[i].a, cb[i].b);
		uint32_t gLo = cb[i].a < cb[i].b ? cb[i].ga : cb[i].gb, gHi = cb[i].a < cb[i].b ? cb[i].gb : cb[i].ga;
		PairEnt *o = pm_find(old, key);
		int ignored;
		if (o && o->ga == gLo && o->gb == gHi) ignored = o->ignored;
		else
		{
			void *ua = a->body->cp.userData, *ub = b->body->cp.userData;
			ignored = (!ua || !ub || !g_collision) ? 1 : !g_collision(ua, ub);
		}
		PairEnt *e = pm_insert(nw, key);
		if (e) { e->ga = gLo; e->gb = gHi; e->ignored = ignored; }
		if (ignored && cb[i].contact != CVK_NO_CONTACT) iv_push(&W->disable, (int)cb[i].contact);
	}
	// pairs that touched before but not now: separate (also for ignored pairs, as Chipmunk); a removed shape: nothing
	for (uint32_t i = 0; i < old->cap; i++)
	{
		PairEnt *o = &old->e[i];
		if (!o->used) continue;
		PairEnt *n = pm_find(nw, o->key);
		if (n && n->ga == o->ga && n->gb == o->gb) continue;
		VShape *a = live_shape(W, (uint32_t)(o->key >> 32)), *b = live_shape(W, (uint32_t)o->key);
		if (!a || !b || a->gen != o->ga || b->gen != o->gb) continue;
		void *ua = a->body->cp.userData, *ub = b->body->cp.userData;
		if (ua && ub && g_separation) g_separation(ua, ub);
	}
	InterlockedExchange(&g_cbwindow, 0);
	W->pairCur ^= 1;
	free(cb);
}

static void pack_body(const VBody *b, CvkBody *o)
{
	const cpBody *c = &b->cp;
	o->px = c->p.x; o->py = c->p.y; o->vx = c->v.x; o->vy = c->v.y; o->fx = c->f.x; o->fy = c->f.y;
	o->a = c->a; o->w = c->w; o->t = c->t; o->m_inv = c->m_inv; o->i_inv = c->i_inv;
	o->flags = (uint32_t)body_type(c) | (b->fresh ? CVK_BF_FRESH : 0);
}

static void pack_shape(const VShape *s, CvkShape *o)
{
	memset(o, 0, sizeof *o);
	if (!s || !s->alive) return;
	const cpShape *c = &s->u.s;
	uint32_t t = c->type == 0 ? 0u : c->type == 1 ? 1u : 2u;
	o->body = (uint32_t)s->body->slot;
	o->flags = (uint32_t)s->kind | (c->sensor ? CVK_SF_SENSOR : 0u) | (t << CVK_SF_CB_SHIFT);
	o->r = s->r; o->hx = s->kind == CVK_SK_BOX ? s->w * 0.5f : 0.0f; o->hy = s->kind == CVK_SK_BOX ? s->h * 0.5f : 0.0f;
	o->cat = (uint32_t)c->filter.categories; o->mask = (uint32_t)c->filter.mask; o->gen = s->gen;
}

struct PackCtx { World *W; CvkFrame *F; int stamp; };
static void pack_bodies_range(void *ctx, int begin, int end, int worker)
{
	(void)worker;
	struct PackCtx *p = (struct PackCtx *)ctx;
	for (int i = begin; i < end; i++)
	{
		VBody *b = (VBody *)slot_ptr(&p->W->bodies, i);
		if (b && b->alive) { pack_body(b, &p->F->bodies[i]); b->uploadStamp = p->stamp; b->fresh = 0; }
		else memset(&p->F->bodies[i], 0, sizeof(CvkBody));
	}
}
static void writeback_range(void *ctx, int begin, int end, int worker)
{
	(void)worker;
	struct PackCtx *p = (struct PackCtx *)ctx;
	for (int i = begin; i < end; i++)
	{
		VBody *b = (VBody *)slot_ptr(&p->W->bodies, i);
		if (!b || !b->alive || b->uploadStamp != p->stamp) continue;
		const float *o = p->F->bodyOut + (size_t)i * 8;
		cpBody *c = &b->cp;
		int t = body_type(c);
		if (t == CVK_BT_STATIC) continue;
		c->p.x = o[0]; c->p.y = o[1]; c->v.x = o[2]; c->v.y = o[3]; c->a = o[4]; c->w = o[5];
		float cs = cosf(c->a), sn = sinf(c->a);
		c->transform = cpTransformNewTranspose(cs, -sn, c->p.x - (c->cog.x * cs - c->cog.y * sn), sn, cs, c->p.y - (c->cog.x * sn + c->cog.y * cs));
		c->v_bias = cpvzero; c->w_bias = 0.0f;
		if (t == CVK_BT_DYNAMIC) { c->f = cpvzero; c->t = 0.0f; }
	}
}

static int gpu_step(World *W, float dt)
{
	CvkFrame F; memset(&F, 0, sizeof F);
	double t0 = now_us();
	EnterCriticalSection(&W->cs);
	W->stamp++;
	int stamp = W->stamp;
	uint32_t nb = (uint32_t)W->bodies.hi, ns = (uint32_t)W->shapes.hi;
	// slots freed before this upload can be reused once the grid of this step replaces the one queries use now
	for (int i = 0; i < W->bodies.limbo.n; i++) iv_push(&W->bodies.cut, W->bodies.limbo.v[i]);
	for (int i = 0; i < W->shapes.limbo.n; i++) iv_push(&W->shapes.cut, W->shapes.limbo.v[i]);
	W->bodies.limbo.n = W->shapes.limbo.n = 0;
	W->pend.cut = (int)W->pend.count;
	int fresh = cvk_prepare_shapes(ns);
	if (fresh < 0) { LeaveCriticalSection(&W->cs); return 0; }
	IntVec patchSlots = { 0 };
	if (fresh)
	{
		for (uint32_t i = 0; i < ns; i++) { VShape *s = (VShape *)slot_ptr(&W->shapes, (int)i); if (s) s->dirty = 0; iv_push(&patchSlots, (int)i); }
		W->dirtyShapes.n = 0;
	}
	else
	{
		for (int i = 0; i < W->dirtyShapes.n; i++) { VShape *s = (VShape *)slot_ptr(&W->shapes, W->dirtyShapes.v[i]); if (s) s->dirty = 0; }
		patchSlots = W->dirtyShapes; memset(&W->dirtyShapes, 0, sizeof W->dirtyShapes);
	}
	int rebuilt = 0;
	if (InterlockedExchange(&W->staticDirty, 0)) { if (!sg_build(W, &W->sg[W->sgCur ^ 1])) { LeaveCriticalSection(&W->cs); free(patchSlots.v); return 0; } rebuilt = 1; }
	SGrid *sg = &W->sg[rebuilt ? W->sgCur ^ 1 : W->sgCur];
	InterlockedExchange(&W->stepping, 1);
	LeaveCriticalSection(&W->cs);

	float prev = W->currDt; W->currDt = dt;
	F.nBodies = nb; F.nShapes = ns; F.nShapePatches = (uint32_t)patchSlots.n;
	F.hs = sg->hs; F.nStaticItems = sg->nItems; F.nStaticRecs = sg->nRecs; F.scell = sg->cell;
	// The coloured passes are Gauss-Seidel, but a push travels only a few contacts per pass in colour order (Chipmunk's
	// sequential order carries it further): 2.5 passes per game iteration (10 -> 25) gave the harness crowd of 60,000 at
	// most the overlap of upstream Chipmunk with 10 (touching pairs per body 3.00 against 3.30, depth 95th percentile 0.29
	// against 0.32, 99th 0.41 against 0.43; 20 passes left the 99th at 0.47; the same with DX_VK_JACOBI_MAX 256, which
	// moves only that scene's few small colours). DX_VK_ITERATIONS / DX_SetOption(210)
	// set the count directly.
	F.iterations = (uint32_t)(g_iterOverride > 0 ? g_iterOverride : (W->iterations * 5 + 1) / 2);
	F.jacobiMax = (uint32_t)g_jacobiMax;
	F.dt = dt; F.damping = cpfpow(W->damping, dt); F.biasCoef = 1.0f - cpfpow(W->collisionBias, dt); F.slop = W->slop;
	F.dtCoef = prev == 0.0f ? 0.0f : dt / prev; F.gx = W->gx; F.gy = W->gy; F.cell0 = W->cell0;
	int ok = cvk_reserve1(&F);
	if (ok)
	{
		struct PackCtx pc = { W, &F, stamp };
		dx_parallel_for((int)nb, 4096, 16384, pack_bodies_range, &pc);
		for (int i = 0; i < patchSlots.n; i++)
		{
			F.shapePatches[i].slot = (uint32_t)patchSlots.v[i]; F.shapePatches[i].pad[0] = F.shapePatches[i].pad[1] = F.shapePatches[i].pad[2] = 0;
			pack_shape((VShape *)slot_ptr(&W->shapes, patchSlots.v[i]), &F.shapePatches[i].s);
		}
		if (sg->hs)
		{
			memcpy(F.staticStart, sg->start, (size_t)(sg->hs + 1) * 4);
			memcpy(F.staticItems, sg->items, (size_t)sg->nItems * 4);
			memcpy(F.staticRecs, sg->recs, (size_t)sg->nRecs * sizeof(CvkStaticRec));
		}
	}
	free(patchSlots.v);
	double t1 = now_us();
	if (!ok || !cvk_phase1(&F)) return 0;
	double t2 = now_us();
	// the query grid of this step from the boxes phase 1 computed, while the callbacks and phase 2 run
	dg_start(W, &W->dg[W->dgCur ^ 1], &F);

	W->patches.n = 0;
	run_callbacks(W, &F);
	double t3 = now_us();

	EnterCriticalSection(&W->cs);
	F.nDisable = (uint32_t)W->disable.n; F.nBodyPatches = (uint32_t)W->patches.n;
	ok = cvk_reserve2(&F);
	if (ok)
	{
		memcpy(F.disable, W->disable.v, (size_t)F.nDisable * 4);
		for (uint32_t i = 0; i < F.nBodyPatches; i++)
		{
			VBody *b = (VBody *)slot_ptr(&W->bodies, W->patches.v[i]);
			CvkBodyPatch *p = &F.bodyPatches[i];
			p->slot = (uint32_t)b->slot; p->keepPos = b->posStamp != stamp; p->pad[0] = p->pad[1] = 0;
			pack_body(b, &p->b);
			p->b.flags &= ~(uint32_t)CVK_BF_FRESH;
			if (!b->alive) p->b.flags = CVK_BT_NONE;
		}
	}
	LeaveCriticalSection(&W->cs);
	if (!ok || !cvk_phase2(&F)) { dg_join(); return 0; }
	double t4 = now_us();

	struct PackCtx wc = { W, &F, stamp };
	dx_parallel_for((int)nb, 4096, 16384, writeback_range, &wc);
	int dgOk = dg_join();

	// the new grid for queries; slots freed before this step's upload are free now
	AcquireSRWLockExclusive(&W->idx);
	if (rebuilt) W->sgCur ^= 1;
	if (dgOk) W->dgCur ^= 1; else W->dg[W->dgCur].hs = 0;
	EnterCriticalSection(&W->cs);
	{
		Pending *p = &W->pend;
		int n = (int)p->count, cut = p->cut;
		for (int i = cut; i < n; i++) p->chunk[(i - cut) >> CHUNK_BITS][(i - cut) & (CHUNK - 1)] = p->chunk[i >> CHUNK_BITS][i & (CHUNK - 1)];
		p->count = n - cut; p->cut = 0;
	}
	for (int i = 0; i < W->bodies.cut.n; i++) iv_push(&W->bodies.freeList, W->bodies.cut.v[i]);
	for (int i = 0; i < W->shapes.cut.n; i++) iv_push(&W->shapes.freeList, W->shapes.cut.v[i]);
	W->bodies.cut.n = W->shapes.cut.n = 0;
	LeaveCriticalSection(&W->cs);
	ReleaseSRWLockExclusive(&W->idx);
	double t5 = now_us();

	g_st[0] += t1 - t0; g_st[1] += t2 - t1; g_st[2] += t3 - t2; g_st[3] += t4 - t3; g_st[4] += t5 - t4;
	g_st[5] += F.nContacts; g_st[6] += F.nCb; g_st[7] += F.usGpu1 + F.usGpu2; g_st[8] += nb; g_st[9] += F.nBodyPatches;
	InterlockedIncrement(&g_stSteps);
	// DX_VK_STATS=<file>: one CSV row per step (ms; the two halves as CPU recording / fence wait / GPU time)
	static FILE *statsFile; static int statsTried;
	if (!statsTried)
	{
		statsTried = 1;
		char path[MAX_PATH]; DWORD n = GetEnvironmentVariableA("DX_VK_STATS", path, sizeof path);
		if (n > 0 && n < sizeof path && (statsFile = fopen(path, "w")) != NULL)
			fprintf(statsFile, "step,t_s,bodies,shapes,shape_patches,static_rebuilt,contacts,colors,uncolored,max_degree,jacobi,cb_pairs,body_patches,prep_ms,p1_rec_ms,p1_wait_ms,p1_gpu_ms,callbacks_ms,p2_rec_ms,p2_wait_ms,p2_gpu_ms,writeback_ms,total_ms\n");
	}
	if (statsFile)
	{
		static double tStart; if (tStart == 0) tStart = t0;
		fprintf(statsFile, "%d,%.3f,%u,%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", stamp, (t0 - tStart) / 1e6, nb, ns, F.nShapePatches, rebuilt,
			F.nContacts, F.nColors, F.nUncolored, F.maxDegree, F.nJacobi, F.nCb, F.nBodyPatches, (t1 - t0) / 1e3, F.usRecord1 / 1e3, F.usWait1 / 1e3, F.usGpu1 / 1e3, (t3 - t2) / 1e3, F.usRecord2 / 1e3, F.usWait2 / 1e3, F.usGpu2 / 1e3, (t5 - t4) / 1e3, (t5 - t0) / 1e3);
		if ((stamp & 15) == 0) fflush(statsFile);
	}
	return 1;
}

static void flush_dead(World *W)
{
	for (;;)
	{
		EnterCriticalSection(&W->cs);
		if (!W->ndead) { LeaveCriticalSection(&W->cs); return; }
		VBody *b = W->dead[--W->ndead];
		LeaveCriticalSection(&W->cs);
		if (b->alive) free_body_now(W, b);
	}
}

DX_EXPORT void Space_Step(cpSpace *space, float timeStep)
{
	World *W = (World *)space;
	DXD(1, space, timeStep, W ? W->ndead : 0, 0, 0);
	if (g_steps_seen < 200 && (++g_steps_seen % 20) == 1) dx_check_patches("Space_Step");
	if (!W || timeStep == 0.0f || W != g_world) return;
	dx_diag_thread();
	if (W->gpu)
	{
		int ok = gpu_step(W, timeStep);
		InterlockedExchange(&W->stepping, 0);
		flush_dead(W);
		if (ok) return;
		char why[300]; _snprintf(why, sizeof why, "GPU step failed: %s", cvk_last_error()); why[sizeof why - 1] = 0;
		switch_to_cpu(W, why);
	}
	double t0 = now_us();
	cpSpaceStep(W->cpu, timeStep);
	g_st[1] += now_us() - t0; InterlockedIncrement(&g_stSteps);
}

// ---------- queries ----------
struct AreaCtx { DXEntityCallback fn; cpBitmask mask; };
static void area_cb(VShape *s, const float *bb, void *c) { (void)bb; struct AreaCtx *a = (struct AreaCtx *)c; if (query_pass(&s->u.s, a->mask)) a->fn(s->body->cp.userData); }
DX_EXPORT void Space_GetBodiesInArea(cpSpace *space, float x1, float y1, float x2, float y2, cpBitmask mask, DXEntityCallback func)
{
	World *W = (World *)space; if (!W || !func) return;
	float q[4] = { x1, y1, x2, y2 }; struct AreaCtx a = { func, mask };
	each_bb(W, q, area_cb, &a);
}

struct RadiusCtx { float x, y, r2; DXEntityCallback fn; cpBitmask mask; };
static void radius_cb(VShape *s, const float *bb, void *c)
{
	(void)bb; struct RadiusCtx *d = (struct RadiusCtx *)c;
	if (!query_pass(&s->u.s, d->mask)) return;
	cpBody *b = &s->body->cp; float dy = d->y - b->p.y, dx = d->x - b->p.x;
	if (dy * dy + dx * dx > d->r2) return;
	d->fn(b->userData);
}
DX_EXPORT void Space_GetBodiesInRadius(cpSpace *space, float x, float y, float radius, cpBitmask mask, DXEntityCallback func)
{
	World *W = (World *)space; if (!W || !func) return;
	struct RadiusCtx d = { x, y, radius * radius, func, mask };
	float q[4] = { x - radius, y - radius, radius + x, radius + y };
	each_bb(W, q, radius_cb, &d);
}

struct FastCtx { float x, y, r2, maxElements; void **elements; int count; cpBitmask mask; };
static void fast_cb(VShape *s, const float *bb, void *c)
{
	(void)bb; struct FastCtx *d = (struct FastCtx *)c;
	if (!query_pass(&s->u.s, d->mask) || (float)d->count >= d->maxElements) return;
	cpBody *b = &s->body->cp; float dy = d->y - b->p.y, dx = d->x - b->p.x;
	if (dy * dy + dx * dx > d->r2) return;
	d->elements[d->count++] = b->userData;
}
DX_EXPORT int Space_GetBodiesInRadiusFast(cpSpace *space, float x, float y, float radius, cpBitmask mask, int maxElements, void **elements)
{
	World *W = (World *)space; if (!W || !elements) return 0;
	struct FastCtx d = { x, y, radius * radius, (float)maxElements, elements, 0, mask };
	float q[4] = { x - radius, y - radius, radius + x, radius + y };
	each_bb(W, q, fast_cb, &d);
	return d.count;
}

struct NearestCtx { float best; VShape *shape; float x, y; cpBitmask mask; };
static void nearest_cb(VShape *s, const float *bb, void *c)
{
	(void)bb; struct NearestCtx *d = (struct NearestCtx *)c;
	if (!query_pass(&s->u.s, d->mask)) return;
	cpBody *b = &s->body->cp; float dy = b->p.y - d->y, dx = b->p.x - d->x, d2 = dy * dy + dx * dx;
	if (d2 < d->best) { d->best = d2; d->shape = s; }
}
DX_EXPORT void *Space_GetNearestBody(cpSpace *space, float x, float y, float radius, cpBitmask mask)
{
	World *W = (World *)space; if (!W) return NULL;
	struct NearestCtx d = { 9999999.0f, NULL, x, y, mask };
	float q[4] = { x - radius, y - radius, (x + radius) + radius, (y + radius) + radius };
	each_bb(W, q, nearest_cb, &d);
	VShape *s = d.shape; if (!s) return NULL;
	float dx = s->body->cp.p.x - x, dy = s->body->cp.p.y - y, d2 = dy * dy + dx * dx;
	if (s->kind == CVK_SK_CIRCLE) { float rr = radius + s->u.c.r; rr = rr * rr; return d2 < rr ? s->u.s.userData : NULL; }
	return d2 < radius * radius ? s->u.s.userData : NULL;
}

// segment queries: candidates whose box (grown by the radius) the segment crosses, then the exact shape test
struct SegCtx { float ax, ay, bx, by, r; cpBitmask mask; int first; float best; VShape *hit; float acc; };
static int seg_bb(const float *bb, const struct SegCtx *c)
{
	float l = bb[0] - c->r, b = bb[1] - c->r, r = bb[2] + c->r, t = bb[3] + c->r;
	float dx = c->bx - c->ax, dy = c->by - c->ay, t0 = 0.0f, t1 = 1.0f;
	if (fabsf(dx) < 1e-12f) { if (c->ax < l || c->ax > r) return 0; }
	else { float u0 = (l - c->ax) / dx, u1 = (r - c->ax) / dx; if (u0 > u1) { float s = u0; u0 = u1; u1 = s; } if (u0 > t0) t0 = u0; if (u1 < t1) t1 = u1; }
	if (fabsf(dy) < 1e-12f) { if (c->ay < b || c->ay > t) return 0; }
	else { float u0 = (b - c->ay) / dy, u1 = (t - c->ay) / dy; if (u0 > u1) { float s = u0; u0 = u1; u1 = s; } if (u0 > t0) t0 = u0; if (u1 < t1) t1 = u1; }
	return t0 <= t1;
}
static void seg_cb(VShape *s, const float *bb, void *ctx)
{
	struct SegCtx *c = (struct SegCtx *)ctx;
	if (!query_pass(&s->u.s, c->mask)) return;
	if (c->first && s->u.s.sensor) return;                  // cpSpaceSegmentQueryFirst skips sensors
	if (!seg_bb(bb, c)) return;
	float t = shape_seg(s, c->ax, c->ay, c->bx, c->by, c->r);
	if (t > 1.0f) return;
	if (c->first) { if (t < c->best) { c->best = t; c->hit = s; } }
	else c->acc = (bb[2] + c->acc) - bb[0];
}
// a small set of record numbers (a record covers several cells): on the stack, on the heap past that
typedef struct { uint32_t *v; uint32_t cap, n; uint32_t local[1024]; } RecSet;
static void rs_init(RecSet *s) { s->v = s->local; s->cap = 1024; s->n = 0; memset(s->local, 0xFF, sizeof s->local); }
static void rs_free(RecSet *s) { if (s->v != s->local) free(s->v); }
static int rs_add(RecSet *s, uint32_t x)   // 1 = new
{
	if (s->n * 2 >= s->cap)
	{
		uint32_t cap = s->cap * 4, *v = (uint32_t *)malloc((size_t)cap * 4);
		if (!v) return 1;
		memset(v, 0xFF, (size_t)cap * 4);
		for (uint32_t i = 0; i < s->cap; i++) if (s->v[i] != 0xFFFFFFFFu) { uint32_t h = (s->v[i] * 2654435761u) & (cap - 1); while (v[h] != 0xFFFFFFFFu) h = (h + 1) & (cap - 1); v[h] = s->v[i]; }
		if (s->v != s->local) free(s->v);
		s->v = v; s->cap = cap;
	}
	uint32_t h = (x * 2654435761u) & (s->cap - 1);
	while (s->v[h] != 0xFFFFFFFFu) { if (s->v[h] == x) return 0; h = (h + 1) & (s->cap - 1); }
	s->v[h] = x; s->n++;
	return 1;
}

// The records of a grid near the segment, cell by cell from its start (a 2D DDA over the cells, widened by the radius).
// For the first hit, the walk stops once no record in later cells can be nearer than the best so far.
static void grid_walk_seg(World *W, SGrid *g, int salt, struct SegCtx *c)
{
	if (!g->hs) return;
	float cs = g->cell, dx = c->bx - c->ax, dy = c->by - c->ay, len = sqrtf(dx * dx + dy * dy);
	int e = (int)ceilf(c->r / cs);
	int x = (int)floorf(c->ax / cs), y = (int)floorf(c->ay / cs), xe = (int)floorf(c->bx / cs), ye = (int)floorf(c->by / cs);
	int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
	float tdx = dx != 0 ? cs / fabsf(dx) : INFINITY, tdy = dy != 0 ? cs / fabsf(dy) : INFINITY;
	float tmx = dx != 0 ? (((float)x + (dx > 0)) * cs - c->ax) / dx : INFINITY, tmy = dy != 0 ? (((float)y + (dy > 0)) * cs - c->ay) / dy : INFINITY;
	float slack = len > 0 ? ((float)e + 1.5f) * cs * 1.5f / len : INFINITY, tEnter = 0.0f;
	RecSet seen; rs_init(&seen);
	for (int steps = 0; steps < 1 << 20; steps++)
	{
		if (c->first && c->hit && tEnter - slack > c->best) break;
		for (int yy = y - e; yy <= y + e; yy++)
			for (int xx = x - e; xx <= x + e; xx++)
			{
				uint32_t b = cvk_hash3(xx, yy, salt) & (g->hs - 1);
				for (uint32_t k = g->start[b]; k < g->start[b + 1]; k++)
				{
					uint32_t ri = g->items[k];
					if (!rs_add(&seen, ri)) continue;
					VShape *s = grid_rec_shape(W, &g->recs[ri]);
					if (s) seg_cb(s, g->recs[ri].bb, c);
				}
			}
		if (x == xe && y == ye) break;
		if (tmx < tmy) { tEnter = tmx; if (tEnter > 1.0f) break; x += sx; tmx += tdx; }
		else { tEnter = tmy; if (tEnter > 1.0f) break; y += sy; tmy += tdy; }
	}
	rs_free(&seen);
}

static void each_seg(World *W, struct SegCtx *c)
{
	float q[4] = { fminf(c->ax, c->bx) - c->r, fminf(c->ay, c->by) - c->r, fmaxf(c->ax, c->bx) + c->r, fmaxf(c->ay, c->by) + c->r };
	if (!W->gpu) { each_bb(W, q, seg_cb, c); return; }
	if (!bb_sane(q)) return;
	AcquireSRWLockShared(&W->idx);
	// static first, then dynamic, as cpSpaceSegmentQueryFirst (a tie keeps the first)
	grid_walk_seg(W, &W->sg[W->sgCur], CVK_STATIC_SALT, c);
	grid_walk_seg(W, &W->dg[W->dgCur], DYN_SALT, c);
	LONG n = W->pend.count; MemoryBarrier();
	for (LONG i = 0; i < n; i++)
	{
		int sl = W->pend.chunk[i >> CHUNK_BITS][i & (CHUNK - 1)];
		VShape *s = live_shape(W, (uint32_t)sl);
		if (s && bb_overlap(s->bb, q)) seg_cb(s, s->bb, c);
	}
	ReleaseSRWLockShared(&W->idx);
}

DX_EXPORT void *Space_RayCast(cpSpace *space, float x1, float y1, float x2, float y2, float radius, cpBitmask mask)
{
	World *W = (World *)space; if (!W) return NULL;
	struct SegCtx c; memset(&c, 0, sizeof c);
	c.ax = x1; c.ay = y1; c.bx = x2; c.by = y2; c.r = radius; c.mask = mask; c.first = 1; c.best = 1.0f;
	each_seg(W, &c);
	return c.hit ? c.hit->body->cp.userData : NULL;
}

DX_EXPORT float Space_GetAvailableSpace(cpSpace *space, float x1, float y1, float x2, float y2, float radius, cpBitmask mask)
{
	World *W = (World *)space; if (!W) return 0.0f;
	struct SegCtx c; memset(&c, 0, sizeof c);
	c.ax = x1; c.ay = y1; c.bx = x2; c.by = y2; c.r = radius; c.mask = mask;
	each_seg(W, &c);
	float dy = y1 - y2, dx = x1 - x2;
	return sqrtf(dy * dy + dx * dx) - c.acc;
}

// ---------- exports: bodies ----------
DX_EXPORT cpBody *Body_Create(cpSpace *space, float mass, float x, float y, float radius)
{
	DXD(3, space, mass, x, y, radius);
	World *W = (World *)space; if (!W) return NULL;
	VBody *b = new_body(W, mass, cpMomentForCircle(mass, 0.0f, radius, cpvzero), x, y);
	return b ? &b->cp : NULL;
}

DX_EXPORT cpBody *Body_CreateCircle(cpSpace *space, float mass, float x, float y, float radius)
{
	DXD(4, space, mass, x, y, radius);
	World *W = (World *)space; if (!W) return NULL;
	VBody *b = new_body(W, mass, cpMomentForCircle(mass, 0.0f, radius, cpvzero), x, y);
	if (!b) return NULL;
	VShape *s = new_shape(W, b, CVK_SK_CIRCLE, radius, 0, 0);
	if (s) { s->u.s.u = 0.0f; s->u.s.type = 0; }
	cpBodyActivate(&b->cp);
	return &b->cp;
}

DX_EXPORT cpBody *Body_CreateRectangle(cpSpace *space, float mass, float x, float y, float width, float height)
{
	DXD(5, space, mass, x, width, height);
	World *W = (World *)space; if (!W) return NULL;
	VBody *b = new_body(W, mass, cpMomentForBox(mass, width, height), x, y);
	if (!b) return NULL;
	VShape *s = new_shape(W, b, CVK_SK_BOX, 0.05f, width, height);
	if (s) { s->u.s.u = 0.0f; s->u.s.type = 0; }
	cpBodyActivate(&b->cp);
	return &b->cp;
}

DX_EXPORT cpShape *Body_AddSensor(cpBody *body, int shapeType, float width, float height, cpBitmask categoryMask, cpBitmask collisionMask)
{
	DXD(6, body, shapeType, width, height, categoryMask);
	if (!body) return NULL;
	VBody *b = vbody(body); World *W = b->world;
	if (shapeType != 1 && shapeType != 2) return NULL;
	VShape *s = new_shape(W, b, shapeType == 1 ? CVK_SK_CIRCLE : CVK_SK_BOX, shapeType == 1 ? width : 0.05f, width, height);
	if (!s) return NULL;
	s->u.s.u = 0.0f;
	s->u.s.filter.categories = categoryMask; s->u.s.filter.mask = collisionMask;
	s->u.s.sensor = cpTrue;
	cpBodyActivate(body);
	return &s->u.s;
}

DX_EXPORT void Body_Dispose(cpBody *body)
{
	DXD(7, body, 0, 0, 0, 0);
	if (!body) return;
	VBody *b = vbody(body); World *W = b->world;
	if (!b->alive) return;
	if (W->gpu && W->stepping)
	{
		// during a step: after it (the step's records of this body stay valid until then)
		EnterCriticalSection(&W->cs);
		if (W->ndead == W->capdead) { int c = W->capdead ? W->capdead * 2 : 1024; VBody **g = (VBody **)realloc(W->dead, (size_t)c * sizeof *g); if (g) { W->dead = g; W->capdead = c; } }
		int queued = W->ndead < W->capdead;
		if (queued) W->dead[W->ndead++] = b;
		LeaveCriticalSection(&W->cs);
		if (queued) return;
	}
	if (!W->gpu && W->cpu && W->cpu->locked) { cpSpaceAddPostStepCallback(W->cpu, cpu_post_remove, b, NULL); return; }
	free_body_now(W, b);
}

DX_EXPORT cpShape *Body_GetShape(cpBody *body) { return body ? body->shapeList : NULL; }
DX_EXPORT void *Shape_GetUserData(cpShape *shape) { return shape ? shape->userData : NULL; }

DX_EXPORT void Body_SetSize(cpBody *body, float width, float height)
{
	DXD(8, body, width, height, 0, 0);
	if (!body || !body->shapeList) return;
	VShape *s = vshape(body->shapeList); World *W = s->world;
	if (s->kind == CVK_SK_CIRCLE) { s->r = width * 0.5f; s->u.c.r = s->r; }
	else
	{
		s->w = width; s->h = height;
		float hw = width * 0.5f, hh = height * 0.5f;
		cpVect v[4] = { cpv(hw, -hh), cpv(hw, hh), cpv(-hw, hh), cpv(-hw, -hh) };
		cpPolyShapeSetVertsRaw(&s->u.s, 4, v);
	}
	if (W->gpu) { EnterCriticalSection(&W->cs); mark_shape_dirty(W, s); if (is_static(s->body)) W->staticDirty = 1; LeaveCriticalSection(&W->cs); }
	else if (W->cpu && body->space) cpSpaceReindexShapesForBody(W->cpu, body);
}

DX_EXPORT DXPointF Body_GetSize(cpBody *body)
{
	if (!body || !body->shapeList) return dxp(0.0f, 0.0f);
	VShape *s = vshape(body->shapeList);
	if (s->kind == CVK_SK_CIRCLE) { float d = s->r + s->r; return dxp(d, d); }
	return dxp(s->w, s->h);
}

DX_EXPORT void Body_SetAngle(cpBody *body, float angle)
{
	DXD(9, body, angle, 0, 0, 0);
	if (!body) return;
	VBody *b = vbody(body); World *W = b->world;
	cpBodySetAngle(body, angle);
	note_change(b, 1);
	if (W->gpu) { if (is_static(b)) InterlockedExchange(&W->staticDirty, 1); }
	else if (W->cpu && body->space) cpSpaceReindexShapesForBody(W->cpu, body);
}
DX_EXPORT float Body_GetAngle(cpBody *body) { return body ? body->a : 0.0f; }
DX_EXPORT void Body_SetPosition(cpBody *body, float x, float y)
{
	DXD(10, body, x, y, 0, 0);
	if (!body) return;
	VBody *b = vbody(body);
	cpBodySetPosition(body, cpv(x, y));
	note_change(b, 1);
	if (b->world->gpu && is_static(b)) InterlockedExchange(&b->world->staticDirty, 1);
}
DX_EXPORT DXPointF Body_GetPosition(cpBody *body) { return body ? dxp(body->p.x, body->p.y) : dxp(0, 0); }
DX_EXPORT void Body_SetVelocity(cpBody *body, float x, float y) { if (!body) return; cpBodyActivate(body); body->v = cpv(x, y); note_change(vbody(body), 0); }
DX_EXPORT DXPointF Body_GetVelocity(cpBody *body) { return body ? dxp(body->v.x, body->v.y) : dxp(0, 0); }
DX_EXPORT void Body_SetCollisionCallbackType(cpBody *body, unsigned int type)
{
	if (!body || !body->shapeList) return;
	VShape *s = vshape(body->shapeList);
	s->u.s.type = (cpCollisionType)type;
	if (s->world->gpu) { EnterCriticalSection(&s->world->cs); mark_shape_dirty(s->world, s); LeaveCriticalSection(&s->world->cs); }
}
DX_EXPORT void Body_SetMass(cpBody *body, float mass)
{
	DXD(12, body, mass, 0, 0, 0);
	if (!body || !(mass > 0.0f) || !(mass < INFINITY)) return;
	VBody *b = vbody(body);
	b->mass = mass;
	if (body_type(body) == CVK_BT_DYNAMIC) { cpBodyActivate(body); body->m = mass; body->m_inv = 1.0f / mass; }
	note_change(b, 0);
}
DX_EXPORT float Body_GetMass(cpBody *body) { return body ? body->m : 0.0f; }
DX_EXPORT void Body_SetForce(cpBody *body, float x, float y) { if (!body) return; cpBodyActivate(body); body->f = cpv(x, y); note_change(vbody(body), 0); }
DX_EXPORT DXPointF Body_GetForce(cpBody *body) { return body ? dxp(body->f.x, body->f.y) : dxp(0, 0); }
DX_EXPORT void Body_ApplyImpulseAtWorldPoint(cpBody *body, float ix, float iy, float wx, float wy) { if (!body) return; cpBodyApplyImpulseAtWorldPoint(body, cpv(ix, iy), cpv(wx, wy)); note_change(vbody(body), 0); }
DX_EXPORT void Body_ApplyForceAtWorldPoint(cpBody *body, float fx, float fy, float wx, float wy) { if (!body) return; cpBodyApplyForceAtWorldPoint(body, cpv(fx, fy), cpv(wx, wy)); note_change(vbody(body), 0); }

DX_EXPORT void Body_SetType(cpBody *body, unsigned char type)
{
	DXD(14, body, type, 0, 0, 0);
	if (!body) return;
	VBody *b = vbody(body); World *W = b->world;
	cpShape *shape = body->shapeList;
	int sensorBefore = shape ? shape->sensor : 0;
	if (shape) shape->sensor = cpFalse;
	if (type == 1) set_body_type(b, CVK_BT_STATIC);
	else if (type == 2) set_body_type(b, CVK_BT_DYNAMIC);
	else if (type == 3) set_body_type(b, CVK_BT_KINEMATIC);
	else if (type == 4) { set_body_type(b, CVK_BT_DYNAMIC); shape = body->shapeList; if (shape) shape->sensor = cpTrue; }
	if (shape && W->gpu && shape->sensor != sensorBefore) { EnterCriticalSection(&W->cs); mark_shape_dirty(W, vshape(shape)); LeaveCriticalSection(&W->cs); }
	note_change(b, 0);
}

DX_EXPORT unsigned char Body_GetType(cpBody *body)
{
	if (!body) return 2;
	if (body->shapeList && body->shapeList->sensor == 1) return 4;
	if (body->sleeping.idleTime == INFINITY) return 1;
	if (body->m == INFINITY) return 3;
	return 2;
}

DX_EXPORT void *Body_GetUserData(cpBody *body) { return body ? body->userData : NULL; }
DX_EXPORT void Body_SetUserData(cpBody *body, void *data) { DXD(15, body, 0, 0, 0, 0); if (!body) return; body->userData = data; if (body->shapeList) body->shapeList->userData = data; }

DX_EXPORT cpBitmask Body_GetCategoryMask(cpBody *body) { return body && body->shapeList ? body->shapeList->filter.categories : 0; }
DX_EXPORT cpBitmask Body_GetCollisionMask(cpBody *body) { return body && body->shapeList ? body->shapeList->filter.mask : 0; }
static void set_filter(cpBody *body, int which, cpBitmask mask)
{
	if (!body || !body->shapeList) return;
	VShape *s = vshape(body->shapeList);
	cpBitmask *f = which ? &s->u.s.filter.mask : &s->u.s.filter.categories;
	if (*f == mask) return;
	*f = mask;
	if (s->world->gpu) { EnterCriticalSection(&s->world->cs); mark_shape_dirty(s->world, s); LeaveCriticalSection(&s->world->cs); }
}
DX_EXPORT void Body_SetCollisionMask(cpBody *body, cpBitmask mask) { set_filter(body, 1, mask); }
DX_EXPORT void Body_SetCategoryMask(cpBody *body, cpBitmask mask) { set_filter(body, 0, mask); }

// 1 when the two bodies' first shapes do NOT touch (the original DLL's inverted result)
DX_EXPORT int Body_Collides(cpBody *body1, cpBody *body2)
{
	if (!body1 || !body2 || !body1->shapeList || !body2->shapeList) return 1;
	cpShapeUpdate(body1->shapeList, body1->transform);
	cpShapeUpdate(body2->shapeList, body2->transform);
	cpContactPointSet set = cpShapesCollide(body1->shapeList, body2->shapeList);
	return set.count == 0;
}

// ---------- options and status ----------
// 1 = the Vulkan device is ready (msg: its name), 2 = CPU mode asked (DX_VK_GPU=0), 0 = no usable device (msg: why).
DX_EXPORT int DX_VulkanProbe(char *msg, int cap)
{
	char err[256] = "";
	int r;
	if (!gpu_wanted()) { r = 2; _snprintf(err, sizeof err, "DX_VK_GPU=0"); }
	else if (cvk_init(err, sizeof err)) { r = 1; _snprintf(err, sizeof err, "%s", cvk_device_name()); }
	else r = 0;
	if (msg && cap > 0) { _snprintf(msg, (size_t)cap, "%s", err); msg[cap - 1] = 0; }
	return r;
}
DX_EXPORT int DX_VulkanStatus(char *msg, int cap) { if (msg && cap > 0) { _snprintf(msg, (size_t)cap, "%s", g_status); msg[cap - 1] = 0; } return g_world && g_world->gpu ? 1 : 0; }

// ---------- body groups (TABPerf: the swarm minimap indicator; the same calls as the Rapier wrapper's) ----------
static BodyGroup *group_of(int id)   // caller holds g_grplk
{
	if (id < 0) return NULL;
	int slot = id & (GRP_SLOTS - 1), gen = id >> 12;
	if (slot >= g_ngrps || !g_grps[slot].used || g_grps[slot].gen != gen) return NULL;
	return &g_grps[slot];
}

// New empty group: its id (>= 0), or -1 when all GRP_SLOTS are in use.
DX_EXPORT int DX_GroupNew(void)
{
	AcquireSRWLockExclusive(&g_grplk);
	int slot = 0;
	while (slot < g_ngrps && g_grps[slot].used) slot++;
	if (slot == g_ngrps)
	{
		BodyGroup *g = g_ngrps < GRP_SLOTS ? (BodyGroup *)realloc(g_grps, (size_t)(g_ngrps + 1) * sizeof *g_grps) : NULL;
		if (!g) { ReleaseSRWLockExclusive(&g_grplk); return -1; }
		g_grps = g; memset(&g_grps[g_ngrps++], 0, sizeof *g_grps);
	}
	BodyGroup *g = &g_grps[slot];
	g->used = 1; g->n = 0; g->gen = (g->gen + 1) & 0x7FFFF;
	int id = slot | (g->gen << 12);
	ReleaseSRWLockExclusive(&g_grplk);
	return id;
}

DX_EXPORT void DX_GroupFree(int id)
{
	AcquireSRWLockExclusive(&g_grplk);
	BodyGroup *g = group_of(id);
	if (g) group_release(g);
	ReleaseSRWLockExclusive(&g_grplk);
}

// Adds bodies (a body in another group moves; null, disposed and member ones are skipped): the number added, -1 stale id.
DX_EXPORT int DX_GroupAdd(int id, cpBody **bodies, int n)
{
	int added = 0;
	AcquireSRWLockExclusive(&g_grplk);
	BodyGroup *g = group_of(id);
	if (!g) { ReleaseSRWLockExclusive(&g_grplk); return -1; }
	int gi = (int)(g - g_grps) + 1;
	for (int k = 0; k < n; k++)
	{
		VBody *b = bodies[k] ? vbody(bodies[k]) : NULL;
		if (!b || !b->alive || b->grp == gi) continue;
		if (g->n == g->cap)
		{
			int c = g->cap ? g->cap * 2 : 1024;
			VBody **m = (VBody **)realloc(g->b, (size_t)c * sizeof *m);
			if (!m) break;
			g->b = m; g->cap = c;
		}
		group_remove(b);
		b->grp = gi; b->gslot = g->n; g->b[g->n++] = b;
		added++;
	}
	ReleaseSRWLockExclusive(&g_grplk);
	return added;
}

// The members still alive - head shape category not 0 (the game clears it when a unit dies and keeps the body for the
// death animation) - and the sums of their cells (each position coordinate truncated to int, int sums wrapping like the
// game's Point.Add): out[0] alive, out[1] sum x, out[2] sum y. Returns the number of members, -1 for a stale id.
DX_EXPORT int DX_GroupCells(int id, int *out)
{
	AcquireSRWLockShared(&g_grplk);
	BodyGroup *g = group_of(id);
	if (!g) { ReleaseSRWLockShared(&g_grplk); out[0] = out[1] = out[2] = 0; return -1; }
	unsigned alive = 0, sx = 0, sy = 0;
	VBody **b = g->b; int n = g->n;
	for (int i = 0; i < n; i++)
	{
		if (i + 8 < n) _mm_prefetch((const char *)b[i + 8], _MM_HINT_T0);
		const VBody *r = b[i];
		const cpShape *s = r->cp.shapeList;
		if (!r->alive || (s && s->filter.categories == 0)) continue;
		alive++; sx += (unsigned)(int)r->cp.p.x; sy += (unsigned)(int)r->cp.p.y;
	}
	ReleaseSRWLockShared(&g_grplk);
	out[0] = (int)alive; out[1] = (int)sx; out[2] = (int)sy;
	return n;
}

DX_EXPORT int DX_SetOption(int name, int value)
{
	if (name == 0) { g_threads_explicit = 1; dx_pool_set_threads(value); return dx_pool_threads(); }
	if (name == 200) return 3;                                   // backend: Chipmunk2DVulkan
	if (name == 201) return g_world && g_world->gpu ? 1 : 0;      // 1 = steps on the GPU, 0 = CPU mode
	if (name == 210) { int o = g_iterOverride; g_iterOverride = value; return o; }
	if (name == 211) { int o = g_jacobiMax; g_jacobiMax = value < 0 ? 0 : value; return o; }
	if (name >= 300 && name <= 309)
	{
		LONG n = g_stSteps;
		if (name == 309) return (int)n;
		return (int)(g_st[name - 300] > 2.0e9 ? 2.0e9 : g_st[name - 300]);
	}
	if (name == 399) { for (int i = 0; i < 10; i++) g_st[i] = 0; g_stSteps = 0; return 0; }
	return -1;
}
