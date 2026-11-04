/*
 * DXPhysicsNative.dll backed by Rapier 2D (official C bindings, rapier2d_ffi.dll / rapier2d_ffi_avx2.dll next to this DLL).
 *
 * Exposes the same exports as the Chipmunk wrapper in native/dxphysics/DXPhysicsNative.c.
 *
 * Threading model (the game calls getters/setters and queries from many threads at once):
 *  - Per-body state the game reads and writes every frame (position, velocity, force, angle) lives in this
 *    DLL's own records, as 64-bit/32-bit atomics. Writes set dirty bits and are pushed to Rapier at the start
 *    of the next step; the step pulls the results back. These calls never take a lock.
 *  - Everything that changes the Rapier world (create, dispose, sensors, type, size, mass, step) runs under
 *    g_world (exclusive).
 *  - Queries never touch Rapier: they use a grid of shape bounding boxes rebuilt after every step (same
 *    bounding-box rules as the Chipmunk wrapper), under g_index (shared). Rapier's scene queries are not used:
 *    any world access from another thread during a step fails with R2_WORLD_BUSY instead of waiting.
 *  - begin/separate callbacks into the game run after the step, with no lock held, so the game may call
 *    anything from them. A pair whose begin callback returns false is ignored (no impulses) from the next
 *    step on, until it separates — Chipmunk ignores it from the same step.
 *
 * Rapier features used instead of per-pair callbacks:
 *  - Category/mask filtering is Rapier's collision groups in OR test mode ((a.cat & b.mask) || (b.cat & a.mask)),
 *    evaluated inside the narrow phase. Masks the game changes are synced before the step.
 *  - Begin/separate come from Rapier collision events, enabled only on callback-type-1 colliders, so the cost
 *    follows the number of touch transitions, not the number of contacts.
 *  - The contact-filter hook is enabled only on colliders that have an ignored pair (begin returned false).
 *  - Bodies can sleep (DX_RAPIER_SLEEP=0 disables it). Values the game writes back unchanged are not pushed,
 *    so they do not wake sleeping bodies. Results are read back for the island manager's active bodies only.
 *  - Bodies Rapier quarantines (non-finite state) are re-enabled at their restored pose.
 */
#include <windows.h>
#include <intrin.h>
#include <delayimp.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "rapier.h"
#include "dxpool.h"

#define DX_EXPORT __declspec(dllexport)

void dx_diag_init(void);
void dx_diag_thread(void);
void dx_diag_call(unsigned id, void *p, float a, float b, float c, float d);
void dx_check_patches(const char *where);
#define DXD(id, p, a, b, c, d) dx_diag_call((id), (void *)(p), (float)(a), (float)(b), (float)(c), (float)(d))

typedef struct { float x, y; } DXPointF;
typedef int(__cdecl *DXCollisionCallback)(void *userA, void *userB);
typedef void(__cdecl *DXSeparationCallback)(void *userA, void *userB);
typedef void(__cdecl *DXEntityCallback)(void *userData);

static DXPointF dxp(float x, float y) { DXPointF p; p.x = x; p.y = y; return p; }

/* ---------- packed atomics ---------- */
typedef union { LONG64 v; struct { float x, y; } f; } Pk;
static LONG64 pk(float x, float y) { Pk p; p.f.x = x; p.f.y = y; return p.v; }
static Pk upk(LONG64 v) { Pk p; p.v = v; return p; }
static LONG fbits(float f) { LONG l; memcpy(&l, &f, 4); return l; }
static float bitsf(LONG l) { float f; memcpy(&f, &l, 4); return f; }

/* ---------- records ---------- */
enum { DX_CIRCLE = 0, DX_BOX = 1 };
enum { D_POS = 1, D_VEL = 2, D_ANGLE = 4, D_ANGVEL = 8, D_MASS = 16, D_FLAGS = 32 };
#define DX_BOX_RADIUS 0.05f
#define DX_WAKE_SPEED 0.01f       /* velocities pushed below this (units/s, rad/s) do not wake a sleeping body */

typedef struct DxBody DxBody;
typedef struct DxShape {
	struct R2ColliderHandle h;
	DxBody *body;
	int kind;
	float hw, hh;             /* circle: hw = radius */
	volatile LONG cat, mask, ctype, sensor, dead;
	void *volatile user;
	float bb[4];              /* l, b, r, t of the indexed / pending box */
	int pending;
	LONG appCat, appMask, appEvents;   /* collision groups / events last given to Rapier */
	int nignore;              /* ignored pairs this (type-1) shape is in; > 0 turns the contact-filter hook on */
	struct DxShape *next;     /* body shape list (head = most recently added, like Chipmunk) */
} DxShape;

struct DxBody {
	struct R2RigidBodyHandle h;
	volatile LONG64 pos, vel, force;
	volatile LONG angle, angvel, torque;
	volatile LONG dirty;
	volatile LONG type;       /* 1 static, 2 dynamic, 3 kinematic (sensor flag lives on the head shape) */
	volatile LONG dead;
	float mass, moment;
	void *volatile user;
	DxShape *shapes;
	int slot;                 /* index in g_bodies */
	LONG64 rpos, rvel; LONG rangle, rangvel;   /* what Rapier holds (pulled or pushed last) */
	unsigned pulled;          /* step stamp of the last pull */
};

typedef struct { unsigned long long a, b; int ignore; } DxPair;   /* a = type-1 shape, b = type-0 shape */

typedef struct {
	int n;
	DxShape **it;
	float *bb;                /* 4 per item */
	int *ix0, *iy0;
	float cell; int gx0, gy0, gw, gh;
	int *cs, *ci;             /* cell start (gw*gh+1), cell items */
	int *big; int nbig;       /* items spanning too many cells */
} DxIndex;

static struct R2World *g_rw;
static struct R2EventCollector *g_events;
static int g_sleep = 1;
static void *g_space_token = &g_space_token;
static SRWLOCK g_world = SRWLOCK_INIT, g_index = SRWLOCK_INIT;
static DXCollisionCallback g_collision;
static DXSeparationCallback g_separation;
static DxBody **g_bodies; static int g_nbodies, g_capbodies;
static DxShape **g_byIndex; static unsigned g_capIndex;       /* collider index -> shape */
static DxShape **g_allShapes; static int g_nshapes, g_capshapes;
static DxShape **g_pending; static int g_npending, g_cappending;
static DxIndex *g_idx;
static void **g_grave; static int g_ngrave, g_capgrave;         /* records freed after the next index swap */
static DxPair *g_pairs; static int g_npairs, g_cappairs;
static DxBody **g_awake; static int g_nawake;                  /* bodies pulled last step */
static float g_dt = 1.0f / 12.0f, g_damping = 0.0f;
static int g_threads = -1, g_threads_explicit;
static volatile LONG g_steps;
/* Step section timings (microseconds, accumulated): read with DX_SetOption(300 + k), reset with DX_SetOption(399, 0). */
static double g_prof[8]; static double g_qpc_us;
static double now_us(void) { LARGE_INTEGER c; if (!g_qpc_us) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpc_us = (double)f.QuadPart / 1e6; } QueryPerformanceCounter(&c); return (double)c.QuadPart / g_qpc_us; }

/* Rapier copy-out calls return the full count: grow the buffer and call again when it did not fit. */
#define FETCH(n, buf, cap, call) do { n = (call); if (n > (size_t)(cap)) { buf = grow(buf, &(cap), (int)n, sizeof *(buf)); n = (call); } if (n > (size_t)(cap)) n = (size_t)(cap); } while (0)
#define APPEND(arr, n, cap, val) do { arr = grow(arr, &(cap), (n) + 1, sizeof *(arr)); (arr)[(n)++] = (val); } while (0)
static void *grow(void *p, int *cap, int need, size_t elt) { if (need <= *cap) return p; int c = *cap ? *cap : 256; while (c < need) c *= 2; void *q = realloc(p, (size_t)c * elt); if (!q) abort(); *cap = c; return q; }

/* Integer environment switch in [lo, hi]; def when unset or malformed. */
static int env_int(const char *name, int lo, int hi, int def)
{
	char t[32]; DWORD len = GetEnvironmentVariableA(name, t, sizeof t);
	if (len == 0 || len >= sizeof t) return def;
	char *end; long v = strtol(t, &end, 10);
	return end != t && *end == 0 && v >= lo && v <= hi ? (int)v : def;
}

/* ---------- loading the Rapier library from this DLL's folder (it is delay-loaded as rapier2d_ffi.dll) ---------- */
/* rapier2d_ffi_avx2.dll is the same library built for AVX2 + FMA (8-lane SIMD solver); it is used only when the CPU
   and the OS support it (DX_RAPIER_AVX2=0 forces the baseline SSE2 build, rapier2d_ffi.dll). */
static int cpu_avx2(void)
{
	int r[4];
	__cpuid(r, 0); if (r[0] < 7) return 0;
	__cpuid(r, 1);
	if (!((r[2] >> 12) & 1) || !((r[2] >> 27) & 1) || !((r[2] >> 28) & 1)) return 0;   /* FMA, OSXSAVE, AVX */
	if ((_xgetbv(0) & 6) != 6) return 0;                                               /* OS saves XMM + YMM */
	__cpuidex(r, 7, 0);
	return (r[1] >> 5) & 1;                                                            /* AVX2 */
}
static HMODULE g_rapier_mod;
static int g_rapier_avx2, g_rapier_release;
/* The delay-load helper asks before loading rapier2d_ffi.dll: hand it the variant chosen above. */
static FARPROC WINAPI dl_hook(unsigned note, PDelayLoadInfo li)
{
	if (note == dliNotePreLoadLibrary && g_rapier_mod && _stricmp(li->szDll, "rapier2d_ffi.dll") == 0) return (FARPROC)g_rapier_mod;
	return NULL;
}
const PfnDliHook __pfnDliNotifyHook2 = dl_hook;

static int g_rapier_ok = -1;
static int load_rapier(void)
{
	if (g_rapier_ok >= 0) return g_rapier_ok;
	wchar_t path[MAX_PATH]; HMODULE self = NULL;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(void *)load_rapier, &self);
	DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
	wchar_t *slash = wcsrchr(path, L'\\');
	g_rapier_ok = 0;
	if (!n || !slash || (size_t)(slash - path) + 22 >= MAX_PATH) return 0;
	if (cpu_avx2() && env_int("DX_RAPIER_AVX2", 0, 1, 1))
	{
		wcscpy(slash + 1, L"rapier2d_ffi_avx2.dll");
		if ((g_rapier_mod = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH)) != NULL) g_rapier_avx2 = 1;
	}
	if (!g_rapier_mod) { wcscpy(slash + 1, L"rapier2d_ffi.dll"); g_rapier_mod = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH); }
	if (g_rapier_mod)
		g_rapier_ok = r2CheckAbi(R2_ABI_VERSION, R2_DIMENSION, sizeof(R2Real), sizeof(struct R2Vector), sizeof(struct R2Pose), R2_ABI_FEATURES) == R2_OK;
	/* An unoptimized (debug) Rapier is about 100x slower; it still runs, and the mod log warns (DX_SetOption(202)). */
	if (g_rapier_ok) { const char *prof = r2BuildProfile(); g_rapier_release = prof && strcmp(prof, "release") == 0; }
	return g_rapier_ok;
}

/* ---------- geometry ---------- */
static void shape_bb(const DxShape *s, float px, float py, float a, float *bb)
{
	if (s->kind == DX_CIRCLE) { float r = s->hw; bb[0] = px - r; bb[1] = py - r; bb[2] = px + r; bb[3] = py + r; return; }
	float c = fabsf(cosf(a)), sn = fabsf(sinf(a));
	float ex = c * s->hw + sn * s->hh + DX_BOX_RADIUS, ey = sn * s->hw + c * s->hh + DX_BOX_RADIUS;
	bb[0] = px - ex; bb[1] = py - ey; bb[2] = px + ex; bb[3] = py + ey;
}
static void body_pose(const DxBody *b, float *px, float *py, float *a) { Pk p = upk(b->pos); *px = p.f.x; *py = p.f.y; *a = bitsf(b->angle); }
static int bb_hit(const float *a, const float *b) { return a[0] <= b[2] && b[0] <= a[2] && a[1] <= b[3] && b[1] <= a[3]; }

/* Entry time of the segment a + t*(b-a), t in [0,1], into the shape grown by `grow`; the start must be outside. */
static int seg_hit(const DxShape *s, float ax, float ay, float bx, float by, float grow, float *tout)
{
	float px, py, ang; body_pose(s->body, &px, &py, &ang);
	float dx = bx - ax, dy = by - ay;
	if (s->kind == DX_CIRCLE)
	{
		float r = s->hw + grow, fx = ax - px, fy = ay - py;
		float qa = dx * dx + dy * dy, qb = 2.0f * (fx * dx + fy * dy), qc = fx * fx + fy * fy - r * r;
		if (qc < 0.0f || qa <= 0.0f) return 0;
		float det = qb * qb - 4.0f * qa * qc; if (det < 0.0f) return 0;
		float t = (-qb - sqrtf(det)) / (2.0f * qa);
		if (t < 0.0f || t > 1.0f) return 0;
		*tout = t; return 1;
	}
	float c = cosf(-ang), sn = sinf(-ang);
	float lax = c * (ax - px) - sn * (ay - py), lay = sn * (ax - px) + c * (ay - py);
	float ldx = c * dx - sn * dy, ldy = sn * dx + c * dy;
	float ex = s->hw + DX_BOX_RADIUS + grow, ey = s->hh + DX_BOX_RADIUS + grow;
	if (fabsf(lax) <= ex && fabsf(lay) <= ey) return 0;
	float t0 = 0.0f, t1 = 1.0f;
	float o[2] = { lax, lay }, d[2] = { ldx, ldy }, e[2] = { ex, ey };
	for (int k = 0; k < 2; k++)
	{
		if (fabsf(d[k]) < 1e-12f) { if (o[k] < -e[k] || o[k] > e[k]) return 0; continue; }
		float ta = (-e[k] - o[k]) / d[k], tb = (e[k] - o[k]) / d[k];
		if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
		if (ta > t0) t0 = ta; if (tb < t1) t1 = tb;
		if (t0 > t1) return 0;
	}
	*tout = t0; return 1;
}

/* Separating-axis overlap of the two head shapes (box corners are treated as square, grown by their border). */
static void box_axes(const DxShape *s, float *ux, float *uy, float *vx, float *vy, float *cx, float *cy)
{
	float a; body_pose(s->body, cx, cy, &a);
	*ux = cosf(a); *uy = sinf(a); *vx = -*uy; *vy = *ux;
}
static int shapes_overlap(const DxShape *s1, const DxShape *s2)
{
	if (s1->kind == DX_BOX && s2->kind == DX_CIRCLE) { const DxShape *t = s1; s1 = s2; s2 = t; }
	float x1, y1, a1, x2, y2, a2; body_pose(s1->body, &x1, &y1, &a1); body_pose(s2->body, &x2, &y2, &a2);
	if (s1->kind == DX_CIRCLE && s2->kind == DX_CIRCLE)
	{
		float dx = x2 - x1, dy = y2 - y1, r = s1->hw + s2->hw;
		return dx * dx + dy * dy < r * r;
	}
	if (s1->kind == DX_CIRCLE)
	{
		float c = cosf(-a2), sn = sinf(-a2), lx = c * (x1 - x2) - sn * (y1 - y2), ly = sn * (x1 - x2) + c * (y1 - y2);
		float qx = lx < -s2->hw ? -s2->hw : lx > s2->hw ? s2->hw : lx, qy = ly < -s2->hh ? -s2->hh : ly > s2->hh ? s2->hh : ly;
		float dx = lx - qx, dy = ly - qy, r = s1->hw + DX_BOX_RADIUS;
		return dx * dx + dy * dy < r * r;
	}
	float u1x, u1y, v1x, v1y, c1x, c1y, u2x, u2y, v2x, v2y, c2x, c2y;
	box_axes(s1, &u1x, &u1y, &v1x, &v1y, &c1x, &c1y); box_axes(s2, &u2x, &u2y, &v2x, &v2y, &c2x, &c2y);
	float axes[4][2] = { { u1x, u1y }, { v1x, v1y }, { u2x, u2y }, { v2x, v2y } };
	float dx = c2x - c1x, dy = c2y - c1y;
	for (int k = 0; k < 4; k++)
	{
		float ax = axes[k][0], ay = axes[k][1];
		float r1 = s1->hw * fabsf(u1x * ax + u1y * ay) + s1->hh * fabsf(v1x * ax + v1y * ay) + DX_BOX_RADIUS;
		float r2 = s2->hw * fabsf(u2x * ax + u2y * ay) + s2->hh * fabsf(v2x * ax + v2y * ay) + DX_BOX_RADIUS;
		if (fabsf(dx * ax + dy * ay) >= r1 + r2) return 0;
	}
	return 1;
}

/* Chipmunk wrapper's rule, collide when either category/mask direction matches, is Rapier's OR test mode. */
static struct R2InteractionGroups groups_of(LONG cat, LONG mask) { struct R2InteractionGroups g; g.memberships = (uint32_t)cat; g.filter = (uint32_t)mask; g.test_mode = 1; return g; }
static int query_pass(const DxShape *s, unsigned mask) { return ((unsigned)s->cat & mask) != 0; }

/* ---------- shape lookup by collider handle ---------- */
static void index_shape(DxShape *s)
{
	if (s->h.index >= g_capIndex)
	{
		unsigned c = g_capIndex ? g_capIndex : 1024; while (c <= s->h.index) c *= 2;
		DxShape **q = (DxShape **)realloc(g_byIndex, c * sizeof(DxShape *)); if (!q) abort();
		memset(q + g_capIndex, 0, (c - g_capIndex) * sizeof(DxShape *)); g_byIndex = q; g_capIndex = c;
	}
	g_byIndex[s->h.index] = s;
}
static unsigned long long hkey(struct R2ColliderHandle h) { return ((unsigned long long)h.index << 32) | h.generation; }
static DxShape *shape_of_key(unsigned long long k)
{
	unsigned idx = (unsigned)(k >> 32), gen = (unsigned)k;
	if (idx >= g_capIndex) return NULL;
	DxShape *s = g_byIndex[idx];
	return s && s->h.generation == gen ? s : NULL;
}
static DxShape *shape_of(struct R2ColliderHandle h) { return shape_of_key(hkey(h)); }
static int pair_cmp(const void *x, const void *y)
{
	const DxPair *a = (const DxPair *)x, *b = (const DxPair *)y;
	if (a->a != b->a) return a->a < b->a ? -1 : 1;
	if (a->b != b->b) return a->b < b->b ? -1 : 1;
	return 0;
}
static DxPair *pair_find(DxPair *arr, int n, unsigned long long a, unsigned long long b)
{
	DxPair k; k.a = a; k.b = b; k.ignore = 0;
	return n ? (DxPair *)bsearch(&k, arr, (size_t)n, sizeof(DxPair), pair_cmp) : NULL;
}
/* Orders a candidate (1,0) callback pair: returns 1 and fills keys when exactly one shape has type 1, the other 0. */
static int callback_pair(const DxShape *s1, const DxShape *s2, struct R2ColliderHandle h1, struct R2ColliderHandle h2, unsigned long long *a, unsigned long long *b)
{
	if (s1->ctype == 1 && s2->ctype == 0) { *a = hkey(h1); *b = hkey(h2); return 1; }
	if (s2->ctype == 1 && s1->ctype == 0) { *a = hkey(h2); *b = hkey(h1); return 1; }
	return 0;
}

/* ---------- contact-filter hook: only colliders with an ignored pair have it on (run inside r2Step, possibly in parallel; read-only) ---------- */
static int32_t RAPIER_CALL hook_contact(void *ud, const struct R2ReadContext *read, struct R2ColliderHandle c1, struct R2ColliderHandle c2, struct R2RigidBodyHandle b1, struct R2RigidBodyHandle b2)
{
	(void)ud; (void)read; (void)b1; (void)b2;
	DxShape *s1 = shape_of(c1), *s2 = shape_of(c2);
	if (!s1 || !s2) return 1;
	unsigned long long a, b;
	/* 0 keeps the contacts (the pair still reports touching and separates later) but applies no impulses. */
	if (callback_pair(s1, s2, c1, c2, &a, &b)) { DxPair *p = pair_find(g_pairs, g_npairs, a, b); if (p && p->ignore) return 0; }
	return 1;
}
static void shape_ignore(DxShape *s, int delta)
{
	if (!s || s->dead) return;
	int was = s->nignore > 0; s->nignore += delta; if (s->nignore < 0) s->nignore = 0;
	if (was != (s->nignore > 0)) r2Collider_SetActiveHooks(s->h, s->nignore > 0 ? R2_FILTER_CONTACT_PAIRS : 0);
}
/* Groups and events follow the game's lock-free cat/mask/ctype writes; applied before each step. */
static void sync_flags(DxBody *b)
{
	for (DxShape *s = b->shapes; s; s = s->next)
	{
		LONG cat = s->cat, mask = s->mask, ev = s->ctype == 1 ? R2_COLLISION_EVENTS : 0;
		if (cat != s->appCat || mask != s->appMask) { r2Collider_SetCollisionGroups(s->h, groups_of(cat, mask)); s->appCat = cat; s->appMask = mask; }
		if (ev != s->appEvents)
		{
			/* Chipmunk also reports kinematic-static and kinematic-kinematic touches; Rapier skips non-dynamic pairs unless
			   one collider asks for them, so callback-type-1 colliders do. */
			r2Collider_SetActiveEvents(s->h, (uint32_t)ev);
			r2Collider_SetActiveCollisionTypes(s->h, ev ? R2_COLLISION_TYPES_DEFAULT | R2_COLLISION_TYPES_KINEMATIC_FIXED | R2_COLLISION_TYPES_KINEMATIC_KINEMATIC : R2_COLLISION_TYPES_DEFAULT);
			s->appEvents = ev;
		}
	}
}

/* ---------- query index ---------- */
static void index_free(DxIndex *x)
{
	if (!x) return;
	free(x->it); free(x->bb); free(x->ix0); free(x->iy0); free(x->cs); free(x->ci); free(x->big); free(x);
}
struct bbjob { float *bb; };
static void bb_range(void *ctx, int begin, int end, int worker)
{
	(void)worker; float *out = ((struct bbjob *)ctx)->bb;
	for (int i = begin; i < end; i++) { DxShape *s = g_allShapes[i]; float px, py, a; body_pose(s->body, &px, &py, &a); shape_bb(s, px, py, a, &out[4 * i]); }
}
static DxIndex *index_build(void)
{
	DxIndex *x = (DxIndex *)calloc(1, sizeof(DxIndex)); if (!x) abort();
	int n = 0;
	x->it = (DxShape **)malloc(sizeof(DxShape *) * (g_nshapes + 1)); x->bb = (float *)malloc(sizeof(float) * 4 * (g_nshapes + 1));
	x->ix0 = (int *)malloc(sizeof(int) * (g_nshapes + 1)); x->iy0 = (int *)malloc(sizeof(int) * (g_nshapes + 1));
	if (!x->it || !x->bb || !x->ix0 || !x->iy0) abort();
	int live = 0;
	for (int i = 0; i < g_nshapes; i++) { DxShape *s = g_allShapes[i]; if (!s->dead) g_allShapes[live++] = s; }
	g_nshapes = live;
	/* boxes in parallel (pose + trig per shape), then keep the finite ones in order */
	{ struct bbjob j; j.bb = x->bb; dx_parallel_for(live, 2048, 8192, bb_range, &j); }
	float minx = INFINITY, miny = INFINITY, maxx = -INFINITY, maxy = -INFINITY;
	for (int i = 0; i < live; i++)
	{
		DxShape *s = g_allShapes[i]; const float *src = &x->bb[4 * i];
		if (!(isfinite(src[0]) && isfinite(src[1]) && isfinite(src[2]) && isfinite(src[3]))) continue;
		float *bb = &x->bb[4 * n]; if (bb != src) memcpy(bb, src, 4 * sizeof(float));
		memcpy(s->bb, bb, sizeof s->bb); s->pending = 0;
		if (bb[0] < minx) minx = bb[0]; if (bb[1] < miny) miny = bb[1]; if (bb[2] > maxx) maxx = bb[2]; if (bb[3] > maxy) maxy = bb[3];
		x->it[n++] = s;
	}
	x->n = n;
	if (!n) { x->gw = x->gh = 0; return x; }
	float cell = 4.0f;
	for (;;)
	{
		double gw = floor(maxx / cell) - floor(minx / cell) + 1, gh = floor(maxy / cell) - floor(miny / cell) + 1;
		if (gw * gh <= (double)(1 << 22)) break;
		cell *= 2.0f;
	}
	x->cell = cell; x->gx0 = (int)floorf(minx / cell); x->gy0 = (int)floorf(miny / cell);
	x->gw = (int)floorf(maxx / cell) - x->gx0 + 1; x->gh = (int)floorf(maxy / cell) - x->gy0 + 1;
	int cells = x->gw * x->gh;
	x->cs = (int *)calloc((size_t)cells + 1, sizeof(int)); x->big = (int *)malloc(sizeof(int) * n); if (!x->cs || !x->big) abort();
	long long total = 0;
	for (int i = 0; i < n; i++)
	{
		float *bb = &x->bb[4 * i];
		int x0 = (int)floorf(bb[0] / cell) - x->gx0, y0 = (int)floorf(bb[1] / cell) - x->gy0, x1 = (int)floorf(bb[2] / cell) - x->gx0, y1 = (int)floorf(bb[3] / cell) - x->gy0;
		x->ix0[i] = x0; x->iy0[i] = y0;
		long long span = (long long)(x1 - x0 + 1) * (y1 - y0 + 1);
		if (span > 256) { x->big[x->nbig++] = i; x->ix0[i] = -1; continue; }
		for (int gx = x0; gx <= x1; gx++) for (int gy = y0; gy <= y1; gy++) x->cs[gx * x->gh + gy + 1]++;
		total += span;
	}
	for (int c = 0; c < cells; c++) x->cs[c + 1] += x->cs[c];
	x->ci = (int *)malloc(sizeof(int) * (size_t)(total + 1)); if (!x->ci) abort();
	int *fill = (int *)calloc((size_t)cells, sizeof(int)); if (!fill) abort();
	for (int i = 0; i < n; i++)
	{
		if (x->ix0[i] < 0) continue;
		float *bb = &x->bb[4 * i];
		int x0 = x->ix0[i], y0 = x->iy0[i], x1 = (int)floorf(bb[2] / cell) - x->gx0, y1 = (int)floorf(bb[3] / cell) - x->gy0;
		for (int gx = x0; gx <= x1; gx++) for (int gy = y0; gy <= y1; gy++) { int c = gx * x->gh + gy; x->ci[x->cs[c] + fill[c]++] = i; }
	}
	free(fill);
	return x;
}
typedef void (*dx_hit_fn)(DxShape *s, const float *bb, void *ctx);
/* Calls fn once per live shape whose box overlaps q. Caller holds g_index shared. */
static void index_query(const float *q, dx_hit_fn fn, void *ctx)
{
	DxIndex *x = g_idx;
	if (x && x->n)
	{
		float cell = x->cell;
		int qx0 = (int)floorf(q[0] / cell) - x->gx0, qy0 = (int)floorf(q[1] / cell) - x->gy0, qx1 = (int)floorf(q[2] / cell) - x->gx0, qy1 = (int)floorf(q[3] / cell) - x->gy0;
		int cx0 = qx0 < 0 ? 0 : qx0, cy0 = qy0 < 0 ? 0 : qy0, cx1 = qx1 >= x->gw ? x->gw - 1 : qx1, cy1 = qy1 >= x->gh ? x->gh - 1 : qy1;
		for (int gx = cx0; gx <= cx1; gx++) for (int gy = cy0; gy <= cy1; gy++)
		{
			int c = gx * x->gh + gy;
			for (int k = x->cs[c]; k < x->cs[c + 1]; k++)
			{
				int i = x->ci[k]; DxShape *s = x->it[i];
				int fx = x->ix0[i] > cx0 ? x->ix0[i] : cx0, fy = x->iy0[i] > cy0 ? x->iy0[i] : cy0;
				if (gx != fx || gy != fy) continue;
				const float *bb = &x->bb[4 * i];
				if (s->dead || s->pending || !bb_hit(bb, q)) continue;
				fn(s, bb, ctx);
			}
		}
		for (int k = 0; k < x->nbig; k++) { int i = x->big[k]; DxShape *s = x->it[i]; const float *bb = &x->bb[4 * i]; if (!s->dead && !s->pending && bb_hit(bb, q)) fn(s, bb, ctx); }
	}
	for (int k = 0; k < g_npending; k++) { DxShape *s = g_pending[k]; if (!s->dead && bb_hit(s->bb, q)) fn(s, s->bb, ctx); }
}
/* New or reshaped shapes are visible to queries right away (Chipmunk indexes them on insertion). Caller holds g_world. */
static void index_touch(DxShape *s)
{
	AcquireSRWLockExclusive(&g_index);
	float px, py, a; body_pose(s->body, &px, &py, &a); shape_bb(s, px, py, a, s->bb);
	if (!s->pending) { APPEND(g_pending, g_npending, g_cappending, s); s->pending = 1; }
	ReleaseSRWLockExclusive(&g_index);
}

/* ---------- world bookkeeping (callers hold g_world) ---------- */
static void push_shape(DxShape *s) { APPEND(g_allShapes, g_nshapes, g_capshapes, s); index_shape(s); }
static void grave(void *p) { APPEND(g_grave, g_ngrave, g_capgrave, p); }

static struct R2ColliderDesc collider_desc(int kind, float w, float h)
{
	struct R2ColliderDesc d;
	if (kind == DX_CIRCLE) d = r2BallColliderDesc(w);
	/* A plain cuboid grown by the border: round cuboids are Rapier's slow case, and the queries above treat box corners as square too. */
	else { struct R2Vector he; he.x = w + DX_BOX_RADIUS; he.y = h + DX_BOX_RADIUS; d = r2CuboidColliderDesc(he); }
	d.massMode = R2_MASS_DENSITY; d.density = 0.0f;
	d.friction = 0.0f; d.restitution = 0.0f; d.frictionCombineRule = R2_COMBINE_MULTIPLY; d.restitutionCombineRule = R2_COMBINE_MULTIPLY;
	d.collisionGroups = groups_of(0, 0);   /* real groups / events are synced before the next step */
	d.activeHooks = 0; d.activeEvents = 0;
	return d;
}
static DxShape *add_shape(DxBody *b, int kind, float w, float h, int sensor)
{
	struct R2ColliderDesc d = collider_desc(kind, w, h);
	d.isSensor = (R2Bool)sensor;
	struct R2ColliderHandle ch = r2InsertCollider(b->h, &d);
	if (r2LastStatus() != R2_OK) return NULL;
	DxShape *s = (DxShape *)calloc(1, sizeof(DxShape)); if (!s) abort();
	s->h = ch; s->body = b; s->kind = kind; s->hw = w; s->hh = h; s->sensor = sensor;
	s->next = b->shapes; b->shapes = s;
	InterlockedOr(&b->dirty, D_FLAGS);
	push_shape(s);
	index_touch(s);
	return s;
}
static void apply_mass(DxBody *b)
{
	struct R2MassProperties mp; memset(&mp, 0, sizeof mp);
	mp.mass = b->mass > 0.0f ? b->mass : 1.0f; mp.principal_inertia = b->moment > 0.0f ? b->moment : mp.mass;
	r2RigidBody_SetAdditionalMassProperties(b->h, mp, 1);
}
static DxBody *new_body(float mass, float moment, float x, float y)
{
	struct R2RigidBodyDesc d = r2DynamicRigidBodyDesc();
	d.position.translation.x = x; d.position.translation.y = y; d.position.rotation.angle = 0.0f;
	d.canSleep = (R2Bool)g_sleep; d.linearDamping = g_damping; d.angularDamping = g_damping;
	d.useAdditionalMassProperties = 1;
	memset(&d.additionalMassProperties, 0, sizeof d.additionalMassProperties);
	d.additionalMassProperties.mass = mass > 0.0f ? mass : 1.0f;
	d.additionalMassProperties.principal_inertia = moment > 0.0f ? moment : d.additionalMassProperties.mass;
	DxBody *b = (DxBody *)calloc(1, sizeof(DxBody)); if (!b) abort();
	d.userData.low = (uint64_t)(uintptr_t)b;   /* bulk state reads of the active set map back to the record */
	struct R2RigidBodyHandle h = r2InsertRigidBody(g_rw, &d);
	if (r2LastStatus() != R2_OK) { free(b); return NULL; }
	b->h = h; b->pos = b->rpos = pk(x, y); b->type = 2; b->mass = mass; b->moment = moment;
	b->slot = g_nbodies; APPEND(g_bodies, g_nbodies, g_capbodies, b);
	return b;
}
static void remove_body_record(DxBody *b)
{
	int i = b->slot; DxBody *last = g_bodies[--g_nbodies];
	g_bodies[i] = last; last->slot = i;
	for (DxShape *s = b->shapes; s; s = s->next)
	{
		s->dead = 1;
		if (s->h.index < g_capIndex && g_byIndex[s->h.index] == s) g_byIndex[s->h.index] = NULL;
	}
	/* g_allShapes drops dead shapes in index_build, before the graveyard frees them. */
}

/* ---------- exports: space ---------- */
DX_EXPORT DXPointF Sumar(int a, int b) { return dxp((float)a, (float)b); }
DX_EXPORT void Algo(void) {}
DX_EXPORT float Space_IsLocked(void *space) { (void)space; return 0.0f; }
DX_EXPORT void Space_SetCollisionCallback(void *space, DXCollisionCallback func) { (void)space; g_collision = func; }
DX_EXPORT void Space_SetSeparationCallback(void *space, DXSeparationCallback func) { (void)space; g_separation = func; }

/* Chipmunk damping 0.001: v *= 0.001^dt per step. Rapier: v *= 1/(1 + dt*d). */
static float damping_for(float dt) { return (powf(0.001f, -dt) - 1.0f) / dt; }

/* Rapier tunes contact softness (spring frequency) for its defaults, 1/60 s steps of 4 substeps. The game steps
   1/12 s with 10, so each substep is twice as long and the same frequency is twice as stiff: contacts overshoot
   (more bodies pushed through walls, more begin/separate flicker) and the solver works harder. Keep the stiffness
   per substep (frequency x substep time) at Rapier's tuning. */
static void scale_softness(struct R2IntegrationParameters *ip)
{
	if (!env_int("DX_RAPIER_SOFTNESS", 0, 1, 1)) return;   /* 0: keep Rapier's frequencies as they are */
	struct R2IntegrationParameters def = r2DefaultIntegrationParameters();
	float k = (def.dt / (float)def.numSolverIterations) / (ip->dt / (float)ip->numSolverIterations);
	ip->contactSoftness.natural_frequency = def.contactSoftness.natural_frequency * k;
	ip->staticContactSoftness.natural_frequency = def.staticContactSoftness.natural_frequency * k;
}

static void apply_threads(void)
{
	int n = g_threads;
	if (!g_threads_explicit) { n = env_int("DX_PHYS_THREADS", 0, 1024, -1); if (n < 0) n = dx_pool_auto_threads(); }
	g_threads = n;
	dx_pool_set_threads(n);   /* the wrapper's own loops (query index, pull); Rapier has its own pool */
	if (g_rw) r2SetNumThreads(g_rw, (size_t)n + 1);   /* workers + the stepping thread */
}

DX_EXPORT void *Space_Create(float gravityX, float gravityY, int iterations)
{
	DXD(2, 0, gravityX, gravityY, iterations, 0);
	dx_check_patches("Space_Create");
	dx_diag_init();
	if (!load_rapier()) return NULL;
	AcquireSRWLockExclusive(&g_world);
	if (g_rw)
	{
		/* The previous level's records stay allocated (the game may still hold their pointers), detached. */
		for (int i = 0; i < g_nbodies; i++) { DxBody *b = g_bodies[i]; b->dead = 1; for (DxShape *s = b->shapes; s; s = s->next) s->dead = 1; }
		g_nbodies = 0; g_nshapes = 0; g_npairs = 0; g_nawake = 0;
		if (g_byIndex) memset(g_byIndex, 0, g_capIndex * sizeof(DxShape *));
		AcquireSRWLockExclusive(&g_index); index_free(g_idx); g_idx = NULL; g_npending = 0; ReleaseSRWLockExclusive(&g_index);
		r2FreeWorld(g_rw);
	}
	g_rw = r2NewWorld();
	if (!g_events) g_events = r2NewEventCollector(); else r2EventCollector_Clear(g_events);
	g_sleep = env_int("DX_RAPIER_SLEEP", 0, 1, 1);
	struct R2Vector gr; gr.x = gravityX; gr.y = gravityY; r2SetGravity(g_rw, gr);
	struct R2IntegrationParameters ip = r2IntegrationParameters(g_rw);
	ip.dt = g_dt; ip.lengthUnit = 1.0f;
	/* The game asks for Chipmunk iterations (10); DX_RAPIER_ITERATIONS overrides the count for Rapier's solver. */
	iterations = env_int("DX_RAPIER_ITERATIONS", 1, 30, iterations);
	ip.numSolverIterations = (size_t)(iterations > 0 ? iterations : 1);
	ip.normalizedAllowedLinearError = 0.1f;     /* Chipmunk collisionSlop 0.1 */
	/* The CCD pass sweeps fast bodies every step. DX_RAPIER_CCD=0 turns it off for the world (0 substeps): about 17%
	   less r2Step with 60,000 crowding bodies, but in the dense test scenes 85-126 bodies get pushed through walls
	   instead of 0-4, so it stays on by default. */
	if (!env_int("DX_RAPIER_CCD", 0, 1, 1)) ip.maxCcdSubsteps = 0;
	scale_softness(&ip);
	r2SetIntegrationParameters(g_rw, &ip);
	g_damping = damping_for(g_dt);
	apply_threads();
	ReleaseSRWLockExclusive(&g_world);
	return g_space_token;
}

static DxPair *g_newpairs; static int g_capnew;
static DxPair *g_starts, *g_stops; static int g_capstarts, g_capstops;
static DxBody **g_moving, **g_extra; static int g_capmoving, g_capawake, g_capextra;
static unsigned g_stamp;
static struct R2RigidBodyHandle *g_qh; static int g_capqh;
static struct R2RigidBodyHandle *g_mh; static int g_capmh;
static struct R2RigidBodyState *g_ms; static int g_capms;
typedef struct { void *a, *b; DxPair key; } DxCall;
static DxCall *g_begins, *g_seps; static int g_nbegins, g_nseps, g_capbegins, g_capseps;
static struct R2CollisionEvent *g_ebuf; static int g_ecap;

static void add_call(DxCall **arr, int *n, int *cap, const DxPair *key)
{
	DxShape *sa = shape_of_key(key->a), *sb = shape_of_key(key->b);
	DxCall c; c.a = sa->body->user; c.b = sb->body->user; c.key = *key;
	APPEND(*arr, *n, *cap, c);
}
/* A tracked pair stays while both shapes live and still have callback types 1 and 0. */
static int pair_live(const DxPair *p, DxShape **sa_out)
{
	DxShape *sa = shape_of_key(p->a), *sb = shape_of_key(p->b);
	*sa_out = sa;
	return sa && sb && !sa->dead && !sb->dead && sa->ctype == 1 && sb->ctype == 0;
}
static void push_key(DxPair **arr, int *cap, int *n, unsigned long long a, unsigned long long b)
{
	DxPair k; k.a = a; k.b = b; k.ignore = 0; APPEND(*arr, *n, *cap, k);
}

static void pull_body(DxBody *b, const struct R2RigidBodyState *st)
{
	b->rpos = pk(st->position.translation.x, st->position.translation.y); b->rangle = fbits(st->position.rotation.angle);
	b->rvel = pk(st->linvel.x, st->linvel.y); b->rangvel = fbits(st->angvel);
	LONG d = b->dirty;
	if (!(d & D_POS)) b->pos = b->rpos;
	if (!(d & D_ANGLE)) b->angle = b->rangle;
	if (!(d & D_VEL)) b->vel = b->rvel;
	if (!(d & D_ANGVEL)) b->angvel = b->rangvel;
}

struct pulljob { unsigned stamp; };
static void pull_range(void *ctx, int begin, int end, int worker)
{
	(void)worker; unsigned stamp = ((struct pulljob *)ctx)->stamp;
	for (int i = begin; i < end; i++)
	{
		DxBody *b = (DxBody *)(uintptr_t)g_ms[i].userData.low;
		g_moving[i] = NULL;
		if (!b || b->dead || b->pulled == stamp) continue;
		pull_body(b, &g_ms[i]); b->pulled = stamp; g_moving[i] = b;
	}
}

DX_EXPORT void Space_Step(void *space, float timeStep)
{
	DXD(1, space, timeStep, g_nbodies, 0, 0);
	if (space != g_space_token || !g_rw || timeStep == 0.0f) return;
	if (g_steps < 200 && (InterlockedIncrement(&g_steps) % 20) == 1) dx_check_patches("Space_Step");
	dx_diag_thread();
	AcquireSRWLockExclusive(&g_world);
	if (timeStep != g_dt)
	{
		g_dt = timeStep; g_damping = damping_for(g_dt);
		struct R2IntegrationParameters ip = r2IntegrationParameters(g_rw); ip.dt = g_dt; scale_softness(&ip); r2SetIntegrationParameters(g_rw, &ip);
		for (int i = 0; i < g_nbodies; i++) { r2RigidBody_SetLinearDamping(g_bodies[i]->h, g_damping); r2RigidBody_SetAngularDamping(g_bodies[i]->h, g_damping); }
	}

	double t0 = now_us();
	/* push game-side state; values equal to what Rapier already holds are skipped so sleeping bodies stay asleep */
	for (int i = 0; i < g_nbodies; i++)
	{
		DxBody *b = g_bodies[i];
		LONG d = b->dirty ? InterlockedExchange(&b->dirty, 0) : 0;   /* plain read first: most bodies are untouched */
		if (d & D_FLAGS) sync_flags(b);
		if (d & (D_POS | D_ANGLE))
		{
			LONG64 np = (d & D_POS) ? b->pos : b->rpos; LONG na = (d & D_ANGLE) ? b->angle : b->rangle;
			Pk p = upk(np);
			if ((np != b->rpos || na != b->rangle) && isfinite(p.f.x) && isfinite(p.f.y) && isfinite(bitsf(na)))
			{
				struct R2Pose pose; pose.translation.x = p.f.x; pose.translation.y = p.f.y; pose.rotation.angle = bitsf(na);
				r2RigidBody_SetPosition(b->h, pose, 1); b->rpos = np; b->rangle = na;
			}
		}
		if (d & D_MASS) apply_mass(b);
		if (b->type == 1) continue;
		/* Chipmunk applies the force as v += f/m*dt and clears it every step: take it and fold it into the velocity we push. */
		LONG64 force = 0; LONG torque = 0;
		if (b->type == 2)
		{
			if (b->force) force = InterlockedExchange64(&b->force, 0);
			if (b->torque) torque = InterlockedExchange(&b->torque, 0);
		}
		if ((d & D_VEL) || force)
		{
			LONG64 nv = b->vel;
			if (force && b->mass > 0.0f) { Pk p = upk(nv), f = upk(force); float k = g_dt / b->mass; nv = pk(p.f.x + f.f.x * k, p.f.y + f.f.y * k); }
			/* Waking resets Rapier's sleep timer, so a (near) zero velocity is set without waking the body. */
			Pk p = upk(nv);
			if (nv != b->rvel && isfinite(p.f.x) && isfinite(p.f.y)) { struct R2Vector v; v.x = p.f.x; v.y = p.f.y; r2RigidBody_SetLinvel(b->h, v, v.x * v.x + v.y * v.y > DX_WAKE_SPEED * DX_WAKE_SPEED); b->rvel = nv; }
		}
		if ((d & D_ANGVEL) || torque)
		{
			LONG nw = b->angvel;
			if (torque && b->moment > 0.0f) nw = fbits(bitsf(nw) + bitsf(torque) * (g_dt / b->moment));
			if (nw != b->rangvel && isfinite(bitsf(nw))) { r2RigidBody_SetAngvel(b->h, bitsf(nw), fabsf(bitsf(nw)) > DX_WAKE_SPEED); b->rangvel = nw; }
		}
	}

	struct R2PhysicsHooks hooks; memset(&hooks, 0, sizeof hooks);
	hooks.filter_contact_pair = hook_contact;
	double t1 = now_us();
	r2Step(g_rw, &hooks, g_events);
	double t2 = now_us();

	/* Bodies Rapier quarantined (non-finite pose or velocity) come back at their last valid pose, at rest. */
	size_t nq; FETCH(nq, g_qh, g_capqh, r2QuarantinedRigidBodies(g_rw, g_qh, (size_t)g_capqh));
	int nx = 0;
	for (size_t k = 0; k < nq; k++)
	{
		if (!r2RigidBody_Contains(g_qh[k])) continue;
		r2RigidBody_SetEnabled(g_qh[k], 1);
		DxBody *b = (DxBody *)(uintptr_t)r2RigidBody_UserData(g_qh[k]).low;
		if (b && !b->dead) APPEND(g_extra, nx, g_capextra, b);
	}
	g_prof[6] += (double)nq;

	/* Pull the island manager's active (awake) bodies in one bulk read, plus the ones awake last step that fell
	   asleep in this one. A value the game wrote during the step wins and is pushed next step. */
	unsigned stamp = ++g_stamp;
	size_t na; FETCH(na, g_mh, g_capmh, r2ActiveRigidBodies(g_rw, g_mh, (size_t)g_capmh));
	g_ms = (struct R2RigidBodyState *)grow(g_ms, &g_capms, (int)na + 1, sizeof(struct R2RigidBodyState));
	g_moving = (DxBody **)grow(g_moving, &g_capmoving, (int)na + 1, sizeof(DxBody *));
	int nm = 0;
	if (na && r2RigidBodyReadStates(g_rw, g_mh, na, g_ms, na) == na)
	{
		/* active handles are distinct, so each record is written by one worker; compact in order afterwards */
		struct pulljob j; j.stamp = stamp; dx_parallel_for((int)na, 1024, 8192, pull_range, &j);
		for (size_t i = 0; i < na; i++) if (g_moving[i]) g_moving[nm++] = g_moving[i];
	}
	for (int k = 0; k < g_nawake; k++)
	{
		DxBody *b = g_awake[k];
		if (!b->dead && b->type != 1 && b->pulled != stamp) APPEND(g_extra, nx, g_capextra, b);
	}
	if (nx)
	{
		g_mh = (struct R2RigidBodyHandle *)grow(g_mh, &g_capmh, nx, sizeof(struct R2RigidBodyHandle));
		g_ms = (struct R2RigidBodyState *)grow(g_ms, &g_capms, nx, sizeof(struct R2RigidBodyState));
		int m = 0;
		for (int k = 0; k < nx; k++) if (g_extra[k]->pulled != stamp) { g_extra[k]->pulled = stamp; g_extra[m] = g_extra[k]; g_mh[m++] = g_extra[k]->h; }
		if (m && r2RigidBodyReadStates(g_rw, g_mh, (size_t)m, g_ms, (size_t)m) == (size_t)m)
			for (int k = 0; k < m; k++) pull_body(g_extra[k], &g_ms[k]);
	}
	{ DxBody **t = g_awake; int tc = g_capawake; g_awake = g_moving; g_capawake = g_capmoving; g_nawake = nm; g_moving = t; g_capmoving = tc; }

	double t3 = now_us();
	/* collision events (on for type-1 colliders only) -> begin / separate of (1,0) pairs */
	size_t ne; FETCH(ne, g_ebuf, g_ecap, r2EventCollector_CollisionEvents(g_events, g_ebuf, (size_t)g_ecap));
	r2EventCollector_Clear(g_events);
	int nstart = 0, nstop = 0;
	for (size_t k = 0; k < ne; k++)
	{
		const struct R2CollisionEvent *e = &g_ebuf[k];
		if (e->started)
		{
			DxShape *s1 = shape_of(e->collider1), *s2 = shape_of(e->collider2); unsigned long long a, b;
			if (s1 && s2 && !s1->dead && !s2->dead && callback_pair(s1, s2, e->collider1, e->collider2, &a, &b)) push_key(&g_starts, &g_capstarts, &nstart, a, b);
		}
		else
		{
			/* the type-1 side may be gone or retyped: drop both orders */
			push_key(&g_stops, &g_capstops, &nstop, hkey(e->collider1), hkey(e->collider2));
			push_key(&g_stops, &g_capstops, &nstop, hkey(e->collider2), hkey(e->collider1));
		}
	}
	if (nstart > 1) qsort(g_starts, (size_t)nstart, sizeof(DxPair), pair_cmp);
	if (nstop > 1) qsort(g_stops, (size_t)nstop, sizeof(DxPair), pair_cmp);
	g_nbegins = g_nseps = 0;
	int nn = 0, j = 0;
	g_newpairs = (DxPair *)grow(g_newpairs, &g_capnew, g_npairs + nstart + 1, sizeof(DxPair));
	for (int i = 0; ; i++)
	{
		DxPair *p = i < g_npairs ? &g_pairs[i] : NULL;
		for (; j < nstart && (!p || pair_cmp(&g_starts[j], p) < 0); j++)
			if (!nn || pair_cmp(&g_newpairs[nn - 1], &g_starts[j]) != 0) { g_newpairs[nn++] = g_starts[j]; add_call(&g_begins, &g_nbegins, &g_capbegins, &g_starts[j]); }
		if (!p) break;
		while (j < nstart && pair_cmp(&g_starts[j], p) == 0) j++;   /* already tracked */
		DxShape *sa; int live = pair_live(p, &sa);
		int stopped = nstop && bsearch(p, g_stops, (size_t)nstop, sizeof(DxPair), pair_cmp) != NULL;
		if (live && !stopped) { g_newpairs[nn++] = *p; continue; }
		/* Pairs of disposed or retyped shapes go silently: the game clears userData before Body_Dispose anyway. */
		if (live) add_call(&g_seps, &g_nseps, &g_capseps, p);
		if (p->ignore) shape_ignore(sa, -1);
	}
	{ DxPair *t = g_pairs; int tc = g_cappairs; g_pairs = g_newpairs; g_cappairs = g_capnew; g_npairs = nn; g_newpairs = t; g_capnew = tc; }

	double t4 = now_us();
	/* rebuild the query index and free what the old one could still reference */
	DxIndex *ni = index_build();
	AcquireSRWLockExclusive(&g_index);
	DxIndex *old = g_idx; g_idx = ni; g_npending = 0;
	for (int i = 0; i < g_ngrave; i++) free(g_grave[i]);
	g_ngrave = 0;
	ReleaseSRWLockExclusive(&g_index);
	index_free(old);
	double t5 = now_us();
	g_prof[0] += t1 - t0; g_prof[1] += t2 - t1; g_prof[2] += t3 - t2; g_prof[3] += t4 - t3; g_prof[4] += t5 - t4; g_prof[5] += 1;
	ReleaseSRWLockExclusive(&g_world);

	/* callbacks into the game, no lock held */
	for (int i = 0; i < g_nbegins; i++)
	{
		DxCall *c = &g_begins[i];
		int ok = c->a && c->b && g_collision ? g_collision(c->a, c->b) : 0;
		if (!ok)
		{
			AcquireSRWLockExclusive(&g_world);
			DxPair *p = pair_find(g_pairs, g_npairs, c->key.a, c->key.b);
			if (p && !p->ignore) { p->ignore = 1; shape_ignore(shape_of_key(p->a), 1); }
			ReleaseSRWLockExclusive(&g_world);
		}
	}
	for (int i = 0; i < g_nseps; i++) { DxCall *c = &g_seps[i]; if (c->a && c->b && g_separation) g_separation(c->a, c->b); }
}

/* ---------- exports: queries ---------- */
struct qarea { DXEntityCallback func; };
static void cb_area(DxShape *s, const float *bb, void *ctx) { (void)bb; ((struct qarea *)ctx)->func(s->body->user); }
struct qmask { unsigned mask; dx_hit_fn fn; void *ctx; };
static void cb_mask(DxShape *s, const float *bb, void *ctx) { struct qmask *m = (struct qmask *)ctx; if (query_pass(s, m->mask)) m->fn(s, bb, m->ctx); }
static void query(float l, float b, float r, float t, unsigned mask, dx_hit_fn fn, void *ctx)
{
	float q[4] = { l, b, r, t }; struct qmask m; m.mask = mask; m.fn = fn; m.ctx = ctx;
	AcquireSRWLockShared(&g_index); index_query(q, cb_mask, &m); ReleaseSRWLockShared(&g_index);
}

DX_EXPORT void Space_GetBodiesInArea(void *space, float x1, float y1, float x2, float y2, unsigned mask, DXEntityCallback func)
{
	(void)space; struct qarea a; a.func = func; query(x1, y1, x2, y2, mask, cb_area, &a);
}
struct qradius { float x, y, r2; DXEntityCallback func; };
static void cb_radius(DxShape *s, const float *bb, void *ctx)
{
	(void)bb; struct qradius *d = (struct qradius *)ctx; Pk p = upk(s->body->pos);
	float dy = d->y - p.f.y, dx = d->x - p.f.x; if (dy * dy + dx * dx > d->r2) return;
	d->func(s->body->user);
}
DX_EXPORT void Space_GetBodiesInRadius(void *space, float x, float y, float radius, unsigned mask, DXEntityCallback func)
{
	(void)space; struct qradius d; d.x = x; d.y = y; d.r2 = radius * radius; d.func = func;
	query(x - radius, y - radius, radius + x, radius + y, mask, cb_radius, &d);
}
struct qfast { float x, y, r2, maxElements; void **elements; int count; };
static void cb_fast(DxShape *s, const float *bb, void *ctx)
{
	(void)bb; struct qfast *d = (struct qfast *)ctx; if ((float)d->count >= d->maxElements) return;
	Pk p = upk(s->body->pos); float dy = d->y - p.f.y, dx = d->x - p.f.x; if (dy * dy + dx * dx > d->r2) return;
	d->elements[d->count++] = s->body->user;
}
DX_EXPORT int Space_GetBodiesInRadiusFast(void *space, float x, float y, float radius, unsigned mask, int maxElements, void **elements)
{
	(void)space; struct qfast d; d.x = x; d.y = y; d.r2 = radius * radius; d.maxElements = (float)maxElements; d.elements = elements; d.count = 0;
	query(x - radius, y - radius, radius + x, radius + y, mask, cb_fast, &d);
	return d.count;
}
struct qnear { float best; DxShape *shape; float x, y; };
static void cb_near(DxShape *s, const float *bb, void *ctx)
{
	(void)bb; struct qnear *d = (struct qnear *)ctx; Pk p = upk(s->body->pos);
	float dy = p.f.y - d->y, dx = p.f.x - d->x, d2 = dy * dy + dx * dx;
	if (d2 < d->best) { d->best = d2; d->shape = s; }
}
DX_EXPORT void *Space_GetNearestBody(void *space, float x, float y, float radius, unsigned mask)
{
	(void)space; struct qnear d; d.best = 9999999.0f; d.shape = NULL; d.x = x; d.y = y;
	/* Same (asymmetric) box as the original wrapper. */
	AcquireSRWLockShared(&g_index);
	{ float q[4] = { x - radius, y - radius, (x + radius) + radius, (y + radius) + radius }; struct qmask m; m.mask = mask; m.fn = cb_near; m.ctx = &d; index_query(q, cb_mask, &m); }
	void *res = NULL; DxShape *s = d.shape;
	if (s)
	{
		Pk p = upk(s->body->pos); float dx = p.f.x - x, dy = p.f.y - y, d2 = dy * dy + dx * dx;
		if (s->kind == DX_CIRCLE) { float rr = radius + s->hw; rr = rr * rr; res = d2 < rr ? s->user : NULL; }
		else res = d2 < radius * radius ? s->user : NULL;
	}
	ReleaseSRWLockShared(&g_index);
	return res;
}
struct qray { float ax, ay, bx, by, grow, best; DxShape *hit; int sensors; float acc; };
static void cb_ray_first(DxShape *s, const float *bb, void *ctx)
{
	(void)bb; struct qray *q = (struct qray *)ctx; float t;
	if (s->sensor) return;
	if (seg_hit(s, q->ax, q->ay, q->bx, q->by, q->grow, &t) && t < q->best) { q->best = t; q->hit = s; }
}
static void segment_box(float x1, float y1, float x2, float y2, float r, float *q)
{
	q[0] = (x1 < x2 ? x1 : x2) - r; q[1] = (y1 < y2 ? y1 : y2) - r; q[2] = (x1 > x2 ? x1 : x2) + r; q[3] = (y1 > y2 ? y1 : y2) + r;
}
DX_EXPORT void *Space_RayCast(void *space, float x1, float y1, float x2, float y2, float radius, unsigned mask)
{
	(void)space; struct qray q; q.ax = x1; q.ay = y1; q.bx = x2; q.by = y2; q.grow = radius; q.best = 1.0f + 1e-6f; q.hit = NULL;
	float bb[4]; segment_box(x1, y1, x2, y2, radius, bb);
	AcquireSRWLockShared(&g_index);
	{ struct qmask m; m.mask = mask; m.fn = cb_ray_first; m.ctx = &q; index_query(bb, cb_mask, &m); }
	void *res = q.hit ? q.hit->body->user : NULL;
	ReleaseSRWLockShared(&g_index);
	return res;
}
static void cb_ray_all(DxShape *s, const float *bb, void *ctx)
{
	struct qray *q = (struct qray *)ctx; float t;
	if (seg_hit(s, q->ax, q->ay, q->bx, q->by, q->grow, &t)) q->acc = (bb[2] + q->acc) - bb[0];
}
DX_EXPORT float Space_GetAvailableSpace(void *space, float x1, float y1, float x2, float y2, float radius, unsigned mask)
{
	(void)space; struct qray q; q.ax = x1; q.ay = y1; q.bx = x2; q.by = y2; q.grow = radius; q.acc = 0.0f;
	float bb[4]; segment_box(x1, y1, x2, y2, radius, bb);
	query(bb[0], bb[1], bb[2], bb[3], mask, cb_ray_all, &q);
	float dy = y1 - y2, dx = x1 - x2;
	return sqrtf(dy * dy + dx * dx) - q.acc;
}

/* ---------- exports: bodies ---------- */
DX_EXPORT DxBody *Body_Create(void *space, float mass, float x, float y, float radius)
{
	DXD(3, space, mass, x, y, radius);
	if (!g_rw) return NULL;
	AcquireSRWLockExclusive(&g_world);
	DxBody *b = new_body(mass, mass * radius * radius * 0.5f, x, y);
	ReleaseSRWLockExclusive(&g_world);
	return b;
}
DX_EXPORT DxBody *Body_CreateCircle(void *space, float mass, float x, float y, float radius)
{
	DXD(4, space, mass, x, y, radius);
	if (!g_rw) return NULL;
	AcquireSRWLockExclusive(&g_world);
	DxBody *b = new_body(mass, mass * radius * radius * 0.5f, x, y);
	if (b) { DxShape *s = add_shape(b, DX_CIRCLE, radius, radius, 0); if (s) s->ctype = 0; }
	ReleaseSRWLockExclusive(&g_world);
	return b;
}
DX_EXPORT DxBody *Body_CreateRectangle(void *space, float mass, float x, float y, float width, float height)
{
	DXD(5, space, mass, x, width, height);
	if (!g_rw) return NULL;
	AcquireSRWLockExclusive(&g_world);
	DxBody *b = new_body(mass, mass * (width * width + height * height) / 12.0f, x, y);
	if (b) add_shape(b, DX_BOX, width * 0.5f, height * 0.5f, 0);
	ReleaseSRWLockExclusive(&g_world);
	return b;
}
DX_EXPORT DxShape *Body_AddSensor(DxBody *body, int shapeType, float width, float height, unsigned categoryMask, unsigned collisionMask)
{
	DXD(6, body, shapeType, width, height, categoryMask);
	if (!body || body->dead || (shapeType != 1 && shapeType != 2)) return NULL;
	AcquireSRWLockExclusive(&g_world);
	DxShape *s = NULL;
	if (!body->dead)
	{
		s = shapeType == 1 ? add_shape(body, DX_CIRCLE, width, width, 1) : add_shape(body, DX_BOX, width * 0.5f, height * 0.5f, 1);
		if (s) { s->cat = (LONG)categoryMask; s->mask = (LONG)collisionMask; }
	}
	ReleaseSRWLockExclusive(&g_world);
	return s;
}
DX_EXPORT void Body_Dispose(DxBody *body)
{
	DXD(7, body, body ? body->dead : 0, g_nbodies, 0, 0);
	if (!body || body->dead) return;
	AcquireSRWLockExclusive(&g_world);
	if (!body->dead)
	{
		r2RemoveRigidBody(body->h, 1);
		body->dead = 1;
		remove_body_record(body);
		for (DxShape *s = body->shapes, *n; s; s = n) { n = s->next; grave(s); }
		grave(body);
	}
	ReleaseSRWLockExclusive(&g_world);
}

DX_EXPORT DxShape *Body_GetShape(DxBody *body) { return body ? body->shapes : NULL; }
DX_EXPORT void *Shape_GetUserData(DxShape *shape) { return shape ? shape->user : NULL; }

DX_EXPORT void Body_SetSize(DxBody *body, float width, float height)
{
	DXD(8, body, width, height, 0, 0);
	if (!body || body->dead) return;
	AcquireSRWLockExclusive(&g_world);
	DxShape *s = body->shapes;
	if (s && !body->dead)
	{
		if (s->kind == DX_CIRCLE) { s->hw = s->hh = width * 0.5f; }
		else { s->hw = width * 0.5f; s->hh = height * 0.5f; }
		struct R2ColliderDesc d = collider_desc(s->kind, s->hw, s->hh);
		R2SharedShape *sh = r2ShapeDesc_Build(&d.shape);
		if (sh) { r2Collider_SetShape(s->h, sh); r2FreeSharedShape(sh); }
		index_touch(s);
	}
	ReleaseSRWLockExclusive(&g_world);
}
DX_EXPORT DXPointF Body_GetSize(DxBody *body)
{
	DxShape *s = body ? body->shapes : NULL; if (!s) return dxp(0.0f, 0.0f);
	if (s->kind == DX_CIRCLE) { float d = s->hw + s->hw; return dxp(d, d); }
	return dxp(s->hw * 2.0f, s->hh * 2.0f);
}

DX_EXPORT void Body_SetAngle(DxBody *body, float angle)
{
	DXD(9, body, angle, 0, 0, 0);
	if (!body) return;
	body->angle = fbits(angle); InterlockedOr(&body->dirty, D_ANGLE);
	if (body->dead) return;
	AcquireSRWLockExclusive(&g_world);
	if (!body->dead) for (DxShape *s = body->shapes; s; s = s->next) index_touch(s);   /* Chipmunk reindexes here */
	ReleaseSRWLockExclusive(&g_world);
}
DX_EXPORT float Body_GetAngle(DxBody *body) { return body ? bitsf(body->angle) : 0.0f; }
DX_EXPORT void Body_SetPosition(DxBody *body, float x, float y) { DXD(10, body, x, y, 0, 0); if (!body) return; body->pos = pk(x, y); InterlockedOr(&body->dirty, D_POS); }
DX_EXPORT DXPointF Body_GetPosition(DxBody *body) { if (!body) return dxp(0, 0); Pk p = upk(body->pos); return dxp(p.f.x, p.f.y); }
DX_EXPORT void Body_SetVelocity(DxBody *body, float x, float y) { if (!body) return; body->vel = pk(x, y); InterlockedOr(&body->dirty, D_VEL); }
DX_EXPORT DXPointF Body_GetVelocity(DxBody *body) { if (!body) return dxp(0, 0); Pk p = upk(body->vel); return dxp(p.f.x, p.f.y); }
DX_EXPORT void Body_SetCollisionCallbackType(DxBody *body, unsigned type) { if (body && body->shapes) { body->shapes->ctype = (LONG)type; InterlockedOr(&body->dirty, D_FLAGS); } }
DX_EXPORT unsigned Body_GetCollisionCallbackType(DxBody *body) { return body && body->shapes ? (unsigned)body->shapes->ctype : 0; }
DX_EXPORT void Body_SetMass(DxBody *body, float mass)
{
	DXD(12, body, mass, 0, 0, 0);
	if (!body || !(mass > 0.0f)) return;
	body->mass = mass; InterlockedOr(&body->dirty, D_MASS);
}
DX_EXPORT float Body_GetMass(DxBody *body) { if (!body) return 0.0f; return body->type == 2 ? body->mass : INFINITY; }
DX_EXPORT void Body_SetForce(DxBody *body, float x, float y) { if (body) body->force = pk(x, y); }
DX_EXPORT DXPointF Body_GetForce(DxBody *body) { if (!body) return dxp(0, 0); Pk p = upk(body->force); return dxp(p.f.x, p.f.y); }

/* Impulses change the velocity right away, as in Chipmunk; the new velocity is pushed at the next step. */
DX_EXPORT void Body_ApplyImpulseAtWorldPoint(DxBody *body, float ix, float iy, float wx, float wy)
{
	if (!body || body->type != 2 || !(body->mass > 0.0f)) return;
	for (;;)
	{
		LONG64 o = body->vel; Pk v = upk(o);
		LONG64 n = pk(v.f.x + ix / body->mass, v.f.y + iy / body->mass);
		if (InterlockedCompareExchange64(&body->vel, n, o) == o) break;
	}
	Pk p = upk(body->pos); float rx = wx - p.f.x, ry = wy - p.f.y;
	if (body->moment > 0.0f)
		for (;;) { LONG o = body->angvel; LONG n = fbits(bitsf(o) + (rx * iy - ry * ix) / body->moment); if (InterlockedCompareExchange(&body->angvel, n, o) == o) break; }
	InterlockedOr(&body->dirty, D_VEL | D_ANGVEL);
}
DX_EXPORT void Body_ApplyForceAtWorldPoint(DxBody *body, float fx, float fy, float wx, float wy)
{
	if (!body) return;
	for (;;) { LONG64 o = body->force; Pk f = upk(o); LONG64 n = pk(f.f.x + fx, f.f.y + fy); if (InterlockedCompareExchange64(&body->force, n, o) == o) break; }
	Pk p = upk(body->pos); float rx = wx - p.f.x, ry = wy - p.f.y;
	for (;;) { LONG o = body->torque; LONG n = fbits(bitsf(o) + rx * fy - ry * fx); if (InterlockedCompareExchange(&body->torque, n, o) == o) break; }
}

DX_EXPORT void Body_SetType(DxBody *body, unsigned char type)
{
	DXD(14, body, type, 0, 0, 0);
	if (!body || body->dead) return;
	AcquireSRWLockExclusive(&g_world);
	if (!body->dead)
	{
		DxShape *s = body->shapes;
		if (s && s->sensor) { s->sensor = 0; r2Collider_SetSensor(s->h, 0); }
		LONG nt = type == 1 ? 1 : type == 3 ? 3 : (type == 2 || type == 4) ? 2 : body->type;
		if (nt != body->type)
		{
			r2RigidBody_SetBodyType(body->h, nt == 1 ? R2_FIXED : nt == 3 ? R2_KINEMATIC_VELOCITY_BASED : R2_DYNAMIC, 1);
			if (nt == 2) apply_mass(body);
			else { body->vel = body->rvel = 0; body->angvel = body->rangvel = 0; struct R2Vector z; z.x = z.y = 0.0f; r2RigidBody_SetLinvel(body->h, z, 0); r2RigidBody_SetAngvel(body->h, 0.0f, 0); }
			body->type = nt;
		}
		if (type == 4 && s) { s->sensor = 1; r2Collider_SetSensor(s->h, 1); }
	}
	ReleaseSRWLockExclusive(&g_world);
}
DX_EXPORT unsigned char Body_GetType(DxBody *body)
{
	if (!body) return 2;
	if (body->shapes && body->shapes->sensor) return 4;
	return (unsigned char)body->type;
}

DX_EXPORT void *Body_GetUserData(DxBody *body) { return body ? body->user : NULL; }
DX_EXPORT void Body_SetUserData(DxBody *body, void *data) { DXD(15, body, 0, 0, 0, 0); if (!body) return; body->user = data; if (body->shapes) body->shapes->user = data; }
DX_EXPORT unsigned Body_GetCategoryMask(DxBody *body) { return body && body->shapes ? (unsigned)body->shapes->cat : 0; }
DX_EXPORT unsigned Body_GetCollisionMask(DxBody *body) { return body && body->shapes ? (unsigned)body->shapes->mask : 0; }
DX_EXPORT void Body_SetCollisionMask(DxBody *body, unsigned mask) { if (body && body->shapes) { body->shapes->mask = (LONG)mask; InterlockedOr(&body->dirty, D_FLAGS); } }
DX_EXPORT void Body_SetCategoryMask(DxBody *body, unsigned mask) { if (body && body->shapes) { body->shapes->cat = (LONG)mask; InterlockedOr(&body->dirty, D_FLAGS); } }
DX_EXPORT unsigned Body_GetCategory(DxBody *body) { return Body_GetCategoryMask(body); }
DX_EXPORT void Body_SetCategory(DxBody *body, unsigned category) { Body_SetCategoryMask(body, category); }

/* Original quirk kept: returns nonzero when the two head shapes do NOT touch. */
DX_EXPORT int Body_Collides(DxBody *body1, DxBody *body2)
{
	if (!body1 || !body2 || !body1->shapes || !body2->shapes) return 1;
	return !shapes_overlap(body1->shapes, body2->shapes);
}

DX_EXPORT void Space_Dispose(void *space) { (void)space; }

/* 0: worker threads (-1 = auto), 200: engine id (2 = Rapier). Other Chipmunk switches do not apply. */
DX_EXPORT int DX_SetOption(int name, int value)
{
	if (name == 0)
	{
		AcquireSRWLockExclusive(&g_world);
		g_threads_explicit = value >= 0; g_threads = value; apply_threads();
		ReleaseSRWLockExclusive(&g_world);
		return g_threads;
	}
	if (name == 100) return 0;
	if (name >= 300 && name < 308) return (int)g_prof[name - 300];
	if (name == 399) { memset(g_prof, 0, sizeof g_prof); return 0; }
	if (name == 200) return 2;
	if (name == 201 || name == 202) load_rapier();   /* asked before the first Space_Create */
	if (name == 201) return g_rapier_avx2;   /* 1: the AVX2 build of the Rapier library is loaded */
	if (name == 202) return g_rapier_release; /* 1: the Rapier library is a release (optimized) build */
	return -1;
}
