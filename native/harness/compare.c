#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

typedef struct { float x, y; } PF;
typedef int(__cdecl *CollCb)(void *, void *);
typedef void(__cdecl *SepCb)(void *, void *);
typedef void(__cdecl *EntCb)(void *);

typedef struct {
	HMODULE h; const char *name;
	void *(*Space_Create)(float, float, int);
	void (*Space_Step)(void *, float);
	float (*Space_IsLocked)(void *);
	void (*Space_SetCollisionCallback)(void *, CollCb);
	void (*Space_SetSeparationCallback)(void *, SepCb);
	void (*Space_GetBodiesInRadius)(void *, float, float, float, unsigned, EntCb);
	int (*Space_GetBodiesInRadiusFast)(void *, float, float, float, unsigned, int, void **);
	void (*Space_GetBodiesInArea)(void *, float, float, float, float, unsigned, EntCb);
	void *(*Space_GetNearestBody)(void *, float, float, float, unsigned);
	void *(*Space_RayCast)(void *, float, float, float, float, float, unsigned);
	float (*Space_GetAvailableSpace)(void *, float, float, float, float, float, unsigned);
	void *(*Body_Create)(void *, float, float, float, float);
	void *(*Body_CreateCircle)(void *, float, float, float, float);
	void *(*Body_CreateRectangle)(void *, float, float, float, float, float);
	void *(*Body_AddSensor)(void *, int, float, float, unsigned, unsigned);
	void (*Body_Dispose)(void *);
	void (*Body_SetType)(void *, unsigned char);
	unsigned char (*Body_GetType)(void *);
	void (*Body_SetUserData)(void *, void *);
	void *(*Body_GetUserData)(void *);
	void (*Body_SetCategoryMask)(void *, unsigned);
	void (*Body_SetCollisionMask)(void *, unsigned);
	unsigned (*Body_GetCategoryMask)(void *);
	unsigned (*Body_GetCollisionMask)(void *);
	void (*Body_SetCollisionCallbackType)(void *, unsigned);
	PF (*Body_GetPosition)(void *);
	void (*Body_SetPosition)(void *, float, float);
	PF (*Body_GetVelocity)(void *);
	void (*Body_SetVelocity)(void *, float, float);
	PF (*Body_GetForce)(void *);
	void (*Body_SetForce)(void *, float, float);
	float (*Body_GetAngle)(void *);
	void (*Body_SetAngle)(void *, float);
	PF (*Body_GetSize)(void *);
	void (*Body_SetSize)(void *, float, float);
	float (*Body_GetMass)(void *);
	void (*Body_SetMass)(void *, float);
	void (*Body_ApplyImpulseAtWorldPoint)(void *, float, float, float, float);
	void (*Body_ApplyForceAtWorldPoint)(void *, float, float, float, float);
	int (*Body_Collides)(void *, void *);
	void *(*Body_GetShape)(void *);
	void *(*Shape_GetUserData)(void *);
	PF (*Sumar)(int, int);
} Api;

#define LOAD(a, f) do { *(FARPROC *)&(a)->f = GetProcAddress((a)->h, #f); if (!(a)->f) { fprintf(stderr, "%s: 내보내기 없음 %s\n", (a)->name, #f); exit(2); } } while (0)

static int load(Api *a, const char *path, const char *name)
{
	memset(a, 0, sizeof(*a)); a->name = name;
	wchar_t w[MAX_PATH]; MultiByteToWideChar(CP_UTF8, 0, path, -1, w, MAX_PATH);
	a->h = LoadLibraryExW(w, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!a->h) { fprintf(stderr, "불러오기 실패 %s (%lu)\n", path, GetLastError()); return 0; }
	LOAD(a, Space_Create); LOAD(a, Space_Step); LOAD(a, Space_IsLocked); LOAD(a, Space_SetCollisionCallback); LOAD(a, Space_SetSeparationCallback);
	LOAD(a, Space_GetBodiesInRadius); LOAD(a, Space_GetBodiesInRadiusFast); LOAD(a, Space_GetBodiesInArea); LOAD(a, Space_GetNearestBody);
	LOAD(a, Space_RayCast); LOAD(a, Space_GetAvailableSpace);
	LOAD(a, Body_Create); LOAD(a, Body_CreateCircle); LOAD(a, Body_CreateRectangle); LOAD(a, Body_AddSensor); LOAD(a, Body_Dispose);
	LOAD(a, Body_SetType); LOAD(a, Body_GetType); LOAD(a, Body_SetUserData); LOAD(a, Body_GetUserData);
	LOAD(a, Body_SetCategoryMask); LOAD(a, Body_SetCollisionMask); LOAD(a, Body_GetCategoryMask); LOAD(a, Body_GetCollisionMask); LOAD(a, Body_SetCollisionCallbackType);
	LOAD(a, Body_GetPosition); LOAD(a, Body_SetPosition); LOAD(a, Body_GetVelocity); LOAD(a, Body_SetVelocity); LOAD(a, Body_GetForce); LOAD(a, Body_SetForce);
	LOAD(a, Body_GetAngle); LOAD(a, Body_SetAngle); LOAD(a, Body_GetSize); LOAD(a, Body_SetSize); LOAD(a, Body_GetMass); LOAD(a, Body_SetMass);
	LOAD(a, Body_ApplyImpulseAtWorldPoint); LOAD(a, Body_ApplyForceAtWorldPoint); LOAD(a, Body_Collides); LOAD(a, Body_GetShape); LOAD(a, Shape_GetUserData); LOAD(a, Sumar);
	return 1;
}

typedef struct { uint32_t *v; char *tag; size_t n, cap; } Trace;
static Trace *g_tr;
static void put(uint32_t x, char tag) { Trace *t = g_tr; if (t->n == t->cap) { t->cap = t->cap ? t->cap * 2 : 1 << 16; t->v = realloc(t->v, t->cap * 4); t->tag = realloc(t->tag, t->cap); } t->v[t->n] = x; t->tag[t->n++] = tag; }
static void putf(float f, char tag) { uint32_t u; memcpy(&u, &f, 4); put(u, tag); }
static void puti(intptr_t i, char tag) { put((uint32_t)i, tag); }

static int __cdecl coll_cb(void *a, void *b) { intptr_t x = (intptr_t)a, y = (intptr_t)b; puti(x, 'C'); puti(y, 'C'); return ((x + y) % 5) != 0; }
static void __cdecl sep_cb(void *a, void *b) { puti((intptr_t)a, 'S'); puti((intptr_t)b, 'S'); }
static void __cdecl ent_cb(void *u) { puti((intptr_t)u, 'Q'); }

static uint32_t g_rng;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static float rndf(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }

#define CAT_ZOMBIE (2u | 2097152u)
#define MASK_UNIT 14339u
#define CAT_UNIT (2u | 1024u | 32u)
#define CAT_WALL (1u | 2048u | 8192u)
#define MASK_WALL 2u

typedef struct { void *b; float speed, m; int alive, kind; float gx, gy; } Ent;

static void run(Api *A, int scene, int n, int steps, Trace *tr)
{
	g_tr = tr; tr->n = 0; g_rng = 12345u + (uint32_t)scene * 7919u + (getenv("COMPARE_SEED") ? (uint32_t)atoi(getenv("COMPARE_SEED")) * 104729u : 0u);
	const float dt = 0.0833333358f;
	void *space = A->Space_Create(0.0f, 0.0f, 10);
	A->Space_SetCollisionCallback(space, coll_cb);
	A->Space_SetSeparationCallback(space, sep_cb);

	int nw = 0; void *walls[64];
	for (int i = 0; i < 4; i++)
	{
		float x = i == 0 ? 0 : i == 1 ? 40 : 20, y = i < 2 ? 20 : (i == 2 ? 0 : 40), w = i < 2 ? 1 : 40, h = i < 2 ? 40 : 1;
		void *b = A->Body_CreateRectangle(space, 1.0f, x, y, w, h); A->Body_SetType(b, 1); A->Body_SetCategoryMask(b, CAT_WALL); A->Body_SetCollisionMask(b, MASK_WALL);
		A->Body_SetUserData(b, (void *)(intptr_t)(100000 + nw)); walls[nw++] = b;
	}
	for (int i = 0; i < 6; i++)
	{
		void *b = A->Body_CreateRectangle(space, 1.0f, 10.0f + 4.0f * i, 20.0f + (i % 2 ? 3.0f : -3.0f), 1.0f, 1.0f); A->Body_SetType(b, 1);
		A->Body_SetCategoryMask(b, CAT_WALL); A->Body_SetCollisionMask(b, MASK_WALL); A->Body_SetUserData(b, (void *)(intptr_t)(100000 + nw)); walls[nw++] = b;
	}
	Ent *e = calloc(n, sizeof(Ent));
	for (int i = 0; i < n; i++)
	{
		int kind = (int)(rnd() % 10);
		float r = kind == 9 ? 0.9f : kind >= 7 ? 0.3f : (rnd() % 2 ? 0.25f : 0.35f);
		float m = kind == 9 ? 20.0f : kind >= 7 ? 2.0f : 1.0f;
		float x = rndf(2, 38), y = rndf(2, 38);
		if (scene == 2) { x = rndf(2, 12); y = rndf(2, 38); }
		e[i].b = A->Body_CreateCircle(space, m, x, y, r); A->Body_SetType(e[i].b, 2);
		A->Body_SetCategoryMask(e[i].b, kind >= 7 && kind < 9 ? CAT_UNIT : CAT_ZOMBIE); A->Body_SetCollisionMask(e[i].b, MASK_UNIT);
		A->Body_SetUserData(e[i].b, (void *)(intptr_t)(i + 1));
		if (i % 17 == 0) A->Body_SetCollisionCallbackType(e[i].b, 1);
		e[i].alive = 1; e[i].kind = kind; e[i].m = m; e[i].speed = kind >= 7 ? 2.0f : rndf(0.6f, 1.6f);
		e[i].gx = scene == 2 ? 36.0f : rndf(2, 38); e[i].gy = scene == 2 ? 20.0f : rndf(2, 38);
	}
	for (int s = 0; s < steps; s++)
	{
		for (int i = 0; i < n; i++)
		{
			if (!e[i].alive) continue;
			PF p = A->Body_GetPosition(e[i].b), v = A->Body_GetVelocity(e[i].b);
			float dx = e[i].gx - p.x, dy = e[i].gy - p.y, l = sqrtf(dx * dx + dy * dy), sp = e[i].speed * 1.3f, tx, ty;
			if (l <= sp / 12.0f) { tx = dx; ty = dy; if (scene != 2) { e[i].gx = rndf(2, 38); e[i].gy = rndf(2, 38); } }
			else { tx = dx / l * sp; ty = dy / l * sp; }
			A->Body_SetForce(e[i].b, (tx - v.x) * e[i].m / dt, (ty - v.y) * e[i].m / dt);
		}
		if (scene == 1)
		{
			int k = (int)(rnd() % n);
			switch (s % 25)
			{
			case 3: if (e[k].alive) { A->Body_Dispose(e[k].b); e[k].alive = 0; } break;
			case 5: if (e[k].alive) A->Body_SetPosition(e[k].b, rndf(3, 37), rndf(3, 37)); break;
			case 7: if (e[k].alive) A->Body_SetVelocity(e[k].b, rndf(-3, 3), rndf(-3, 3)); break;
			case 9: if (e[k].alive) A->Body_ApplyImpulseAtWorldPoint(e[k].b, rndf(-2, 2), rndf(-2, 2), 0, 0); break;
			case 11: if (e[k].alive) { A->Body_SetType(e[k].b, 3); A->Body_SetVelocity(e[k].b, 0.5f, 0.0f); } break;
			case 13: if (e[k].alive && A->Body_GetType(e[k].b) == 3) A->Body_SetType(e[k].b, 2); break;
			case 15: A->Body_SetAngle(walls[4 + (s / 25) % 6], rndf(0, 3.14f)); break;
			case 17: if (e[k].alive) A->Body_AddSensor(e[k].b, 1 + (int)(rnd() % 2), 1.5f, 1.0f, 2u, 2u); break;
			case 19: if (e[k].alive) A->Body_SetType(e[k].b, 4); break;
			case 21: if (e[k].alive) A->Body_SetSize(e[k].b, rndf(0.4f, 1.0f), 1.0f); break;
			case 23: if (e[k].alive) A->Body_SetMass(e[k].b, rndf(0.5f, 3.0f)); break;
			}
		}
		A->Space_Step(space, dt);
		for (int i = 0; i < n; i++)
		{
			if (!e[i].alive) continue;
			PF p = A->Body_GetPosition(e[i].b), v = A->Body_GetVelocity(e[i].b);
			putf(p.x, 'P'); putf(p.y, 'P'); putf(v.x, 'V'); putf(v.y, 'V'); putf(A->Body_GetAngle(e[i].b), 'A');
		}
		if (scene == 1 && s % 20 == 0)
		{
			float qx = rndf(5, 35), qy = rndf(5, 35), qr = rndf(1, 6);
			A->Space_GetBodiesInRadius(space, qx, qy, qr, CAT_ZOMBIE, ent_cb); puti(-1, 'q');
			void *el[64]; int c = A->Space_GetBodiesInRadiusFast(space, qx, qy, qr, 0xFFFFFFFFu, 64, el); puti(c, 'F'); for (int j = 0; j < c; j++) puti((intptr_t)el[j], 'F');
			A->Space_GetBodiesInArea(space, qx - qr, qy - qr, qx + qr, qy + qr, CAT_UNIT | CAT_WALL, ent_cb); puti(-2, 'q');
			puti((intptr_t)A->Space_GetNearestBody(space, qx, qy, qr, 0xFFFFFFFFu), 'N');
			puti((intptr_t)A->Space_RayCast(space, qx, qy, qx + 10, qy + 3, 0.2f, 0xFFFFFFFFu), 'R');
			putf(A->Space_GetAvailableSpace(space, qx, qy, qx + 10, qy + 3, 0.1f, 0xFFFFFFFFu), 'W');
			putf(A->Space_IsLocked(space), 'L');
			int a = (int)(rnd() % n), b = (int)(rnd() % n);
			if (e[a].alive && e[b].alive) puti(A->Body_Collides(e[a].b, e[b].b), 'K');
			if (e[a].alive) { PF z = A->Body_GetSize(e[a].b); putf(z.x, 'Z'); putf(z.y, 'Z'); puti(A->Body_GetType(e[a].b), 'T'); }
		}
	}
	{ int outside = 0; for (int i = 0; i < n; i++) if (e[i].alive) { PF p = A->Body_GetPosition(e[i].b); if (!(p.x > -0.5f && p.x < 40.5f && p.y > -0.5f && p.y < 40.5f)) outside++; } printf("   [%s] 방벽 상자 밖 몸체 %d\n", A->name, outside); }
	free(e);
}

static void write_cfg(void)
{
	char path[MAX_PATH]; GetModuleFileNameA(NULL, path, MAX_PATH);
	FILE *f = fopen(path, "rb"); int32_t sum = 0; unsigned char buf[65536]; size_t r;
	while ((r = fread(buf, 1, sizeof buf, f)) > 0) for (size_t i = 0; i < r; i++) sum = (int32_t)((uint32_t)sum + (uint32_t)buf[i] * 2u + 1u);
	fclose(f);
	f = fopen("DXPhysicsNative.cfg", "w"); fprintf(f, "Iterations:%d", sum); fclose(f);
}

static int compare(const char *label, Trace *a, Trace *b)
{
	size_t n = a->n < b->n ? a->n : b->n, first = (size_t)-1, diff = 0; double maxd = 0;
	for (size_t i = 0; i < n; i++)
	{
		if (a->v[i] == b->v[i] && a->tag[i] == b->tag[i]) continue;
		diff++; if (first == (size_t)-1) first = i;
		if (a->tag[i] == b->tag[i] && strchr("PVAWZL", a->tag[i])) { float x, y; memcpy(&x, &a->v[i], 4); memcpy(&y, &b->v[i], 4); double d = fabs((double)x - y); if (d > maxd) maxd = d; }
	}

	{
		size_t cnt[128] = {0}; int onlyS = 1;
		for (size_t i = 0; i < n; i++) if (a->v[i] != b->v[i] || a->tag[i] != b->tag[i]) cnt[(unsigned char)a->tag[i] & 127]++;
		for (int c = 0; c < 128; c++) if (cnt[c] && c != 'S') onlyS = 0;
		if (diff && onlyS && a->n == b->n)
		{
			uint64_t sa = 0, sb = 0;
			for (size_t i = 0; i < n; i++) if (a->tag[i] == 'S') { sa += (uint64_t)a->v[i] * 2654435761u; sb += (uint64_t)b->v[i] * 2654435761u; }
			printf("%s: 물리·질의·충돌 콜백 같음 (값 %zu개) · 떨어짐 콜백 순서만 %zu개 다름(모음 합 %s)\n", label, a->n, diff, sa == sb ? "같음" : "다름");
			return sa == sb;
		}
		if (diff) { printf("   다른 값 종류:"); for (int c = 0; c < 128; c++) if (cnt[c]) printf(" %c=%zu", c, cnt[c]); printf("\n"); }
	}
	if (diff == 0 && a->n == b->n) { printf("%s: 같음 (값 %zu개)\n", label, a->n); return 1; }
	printf("%s: 다름 — 값 원작 %zu / 대체 %zu, 다른 값 %zu개, 처음 다른 자리 %zd (종류 %c/%c), 위치·속도 최대 차 %.6g\n", label, a->n, b->n, diff, (ptrdiff_t)first,
		first != (size_t)-1 ? a->tag[first] : '-', first != (size_t)-1 ? b->tag[first] : '-', maxd);
	if (first != (size_t)-1) { for (size_t i = first; i < first + 6 && i < n; i++) { float x, y; memcpy(&x, &a->v[i], 4); memcpy(&y, &b->v[i], 4); printf("   [%zu] %c %08X (%g) | %c %08X (%g)\n", i, a->tag[i], a->v[i], x, b->tag[i], b->v[i], y); } }
	return 0;
}

static int prof_main(const char *dllPath, int n, int steps, int kills)
{
	Api A; if (!load(&A, dllPath, "계측")) return 2;
	int (*get)(double *, int) = (int (*)(double *, int))GetProcAddress(A.h, "DX_ProfileGet");
	void (*reset)(void) = (void (*)(void))GetProcAddress(A.h, "DX_ProfileReset");
	if (!get || !reset) { fprintf(stderr, "DX_PROFILE 판이 아니다\n"); return 2; }
	Trace t = {0}; g_tr = &t; g_rng = 777u;
	const float dt = 0.0833333358f, C = 100.0f, H = 30.0f;
	void *space = A.Space_Create(0.0f, 0.0f, 10);
	A.Space_SetCollisionCallback(space, coll_cb); A.Space_SetSeparationCallback(space, sep_cb);
	int nw = 0;
	for (int i = 0; i <= 60; i++)
		for (int side = 0; side < 4; side++)
		{
			float x = side < 2 ? C - H + i : (side == 2 ? C - H : C + H), y = side < 2 ? (side == 0 ? C - H : C + H) : C - H + i;
			void *b = A.Body_CreateRectangle(space, 1.0f, x, y, 1.0f, 1.0f); A.Body_SetType(b, 1);
			A.Body_SetCategoryMask(b, CAT_WALL); A.Body_SetCollisionMask(b, MASK_WALL); A.Body_SetUserData(b, (void *)(intptr_t)(100000 + nw++));
		}
	Ent *e = calloc(n, sizeof(Ent));
	for (int i = 0; i < n; i++)
	{
		float a = rndf(0, 6.2831853f), d = rndf(45, 90);
		e[i].m = 1.0f; e[i].speed = rndf(0.8f, 1.6f); e[i].alive = 1;
		e[i].b = A.Body_CreateCircle(space, 1.0f, C + cosf(a) * d, C + sinf(a) * d, rnd() % 2 ? 0.25f : 0.35f); A.Body_SetType(e[i].b, 2);
		A.Body_SetCategoryMask(e[i].b, CAT_ZOMBIE); A.Body_SetCollisionMask(e[i].b, MASK_UNIT); A.Body_SetUserData(e[i].b, (void *)(intptr_t)(i + 1));
	}
	LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); double wall = 0; int measured = 0;
	for (int s = 0; s < steps; s++)
	{
		if (s == steps / 3) { reset(); wall = 0; measured = 0; t.n = 0; }
		QueryPerformanceCounter(&t0);
		for (int i = 0; i < n; i++)
		{
			PF p = A.Body_GetPosition(e[i].b), v = A.Body_GetVelocity(e[i].b);
			float dx = C - p.x, dy = C - p.y, l = sqrtf(dx * dx + dy * dy), sp = e[i].speed * 1.3f;
			float tx = l > 0.01f ? dx / l * sp : 0, ty = l > 0.01f ? dy / l * sp : 0;
			A.Body_SetForce(e[i].b, (tx - v.x) * e[i].m / dt, (ty - v.y) * e[i].m / dt);
		}
		for (int k = 0, tries = 0; k < kills && tries < 4 * n; tries++)
		{
			int i = (int)(rnd() % n); PF p = A.Body_GetPosition(e[i].b);
			if (fabsf(p.x - C) > H + 3 || fabsf(p.y - C) > H + 3) continue;
			A.Body_Dispose(e[i].b); k++;
			float a = rndf(0, 6.2831853f), d = rndf(70, 90);
			e[i].b = A.Body_CreateCircle(space, 1.0f, C + cosf(a) * d, C + sinf(a) * d, 0.3f); A.Body_SetType(e[i].b, 2);
			A.Body_SetCategoryMask(e[i].b, CAT_ZOMBIE); A.Body_SetCollisionMask(e[i].b, MASK_UNIT); A.Body_SetUserData(e[i].b, (void *)(intptr_t)(i + 1));
		}
		A.Space_Step(space, dt);
		QueryPerformanceCounter(&t1); wall += (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart; measured++;
		if (t.n > (1u << 20)) t.n = 0;
	}
	double p[32]; get(p, 32);
	double st = p[29] > 0 ? p[29] : 1;
	static const char *names[10] = { "접촉 목록 비우기", "위치 적분", "모양 갱신", "충돌 찾기(넓은+좁은)", "구성요소", "오래된 접촉 정리", "접촉 준비", "속도 적분+캐시 충격", "충격 풀이(반복)", "뒤처리" };
	printf("기지 공성: 좀비 %d, 단계당 죽음 %d, 잰 단계 %d — 단계당 평균 몸체 %.0f · 접촉 %.0f\n", n, kills, (int)st, p[31] / st, p[30] / st);
	double sum = 0; for (int i = 0; i < 10; i++) sum += p[i];
	for (int i = 0; i < 10; i++) printf("  %-22s %8.2f ms  (%4.1f%%)\n", names[i], p[i] / st * 1000, sum > 0 ? p[i] / sum * 100 : 0);
	printf("  Step 합 %.2f ms · 몸체 지우기 %.0f번 한 번 %.3f ms(단계당 %.2f ms) · 만들기 한 번 %.1f µs · 단계 전체(시험 도구 포함) %.2f ms\n",
		sum / st * 1000, p[11], p[11] > 0 ? p[10] / p[11] * 1000 : 0, p[10] / st * 1000, p[13] > 0 ? p[12] / p[13] * 1e6 : 0, wall / (measured ? measured : 1) * 1000);
	printf("  충돌 찾기 안: 잎 갱신 %.2f ms · 격자 %.2f ms · 쌍 찾기+좁은 판정 %.2f ms · 접촉 기록 %.2f ms · 기록 %.0f/단계 · 색칠 %.2f ms · (원작 경로 쌍 훑기 %.2f ms)\n", p[14] / st * 1000, p[19] / st * 1000, p[16] / st * 1000, p[17] / st * 1000, p[18] / st, p[20] / st * 1000, p[15] / st * 1000);
	return 0;
}

static int ray_main(const char *dllPath, int n, int steps, int rays, unsigned seed)
{
	Api A; if (!load(&A, dllPath, "광선")) return 2;
	Trace t = {0}; g_tr = &t; g_rng = 777u;
	const float dt = 0.0833333358f, C = 100.0f, H = 30.0f;
	void *space = A.Space_Create(0.0f, 0.0f, 10);
	A.Space_SetCollisionCallback(space, coll_cb); A.Space_SetSeparationCallback(space, sep_cb);
	int nw = 0;
	for (int i = 0; i <= 60; i++)
		for (int side = 0; side < 4; side++)
		{
			float x = side < 2 ? C - H + i : (side == 2 ? C - H : C + H), y = side < 2 ? (side == 0 ? C - H : C + H) : C - H + i;
			void *b = A.Body_CreateRectangle(space, 1.0f, x, y, 1.0f, 1.0f); A.Body_SetType(b, 1);
			A.Body_SetCategoryMask(b, CAT_WALL); A.Body_SetCollisionMask(b, MASK_WALL); A.Body_SetUserData(b, (void *)(intptr_t)(100000 + nw++));
		}

	for (int i = 0; i < 1300; i++) { void *b = A.Body_CreateRectangle(space, 1.0f, C - H + 2 + rndf(0, 2 * H - 4), C - H + 2 + rndf(0, 2 * H - 4), 2.0f, 2.0f); A.Body_SetType(b, 1); A.Body_SetCategoryMask(b, CAT_WALL); A.Body_SetCollisionMask(b, MASK_WALL); A.Body_SetUserData(b, (void *)(intptr_t)(200000 + i)); }
	Ent *e = calloc(n, sizeof(Ent));
	for (int i = 0; i < n; i++)
	{
		float a = rndf(0, 6.2831853f), d = rndf(45, 120);
		e[i].m = 1.0f; e[i].speed = rndf(0.8f, 1.6f); e[i].alive = 1;
		e[i].b = A.Body_CreateCircle(space, 1.0f, C + cosf(a) * d, C + sinf(a) * d, rnd() % 2 ? 0.25f : 0.35f); A.Body_SetType(e[i].b, 2);
		A.Body_SetCategoryMask(e[i].b, CAT_ZOMBIE); A.Body_SetCollisionMask(e[i].b, MASK_UNIT); A.Body_SetUserData(e[i].b, (void *)(intptr_t)(i + 1));
	}
	for (int s = 0; s < steps; s++)
	{
		for (int i = 0; i < n; i++)
		{
			PF p = A.Body_GetPosition(e[i].b), v = A.Body_GetVelocity(e[i].b);
			float dx = C - p.x, dy = C - p.y, l = sqrtf(dx * dx + dy * dy), sp = e[i].speed * 1.3f;
			float tx = l > 0.01f ? dx / l * sp : 0, ty = l > 0.01f ? dy / l * sp : 0;
			A.Body_SetForce(e[i].b, (tx - v.x) * e[i].m / dt, (ty - v.y) * e[i].m / dt);
		}
		A.Space_Step(space, dt);
		if (t.n > (1u << 20)) t.n = 0;
	}
	LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
	unsigned long long h = 1469598103934665603ull; int hits = 0; g_rng = seed;
	double ms[2] = {0, 0};
	for (int kind = 0; kind < 2; kind++)
	{
		QueryPerformanceCounter(&t0);
		for (int r = 0; r < rays; r++)
		{
			int i = (int)(rnd() % n); PF p = A.Body_GetPosition(e[i].b);
			float dx = C - p.x, dy = C - p.y, l = sqrtf(dx * dx + dy * dy); if (l < 0.01f) l = 0.01f;
			float ex = kind == 0 ? C : p.x + dx / l * 6.0f, ey = kind == 0 ? C : p.y + dy / l * 6.0f;
			void *u = A.Space_RayCast(space, p.x, p.y, ex, ey, 0.0f, MASK_UNIT);
			intptr_t id = u ? (intptr_t)u : 0; if (id) hits++;
			h = (h ^ (unsigned long long)id) * 1099511628211ull;
		}
		QueryPerformanceCounter(&t1); ms[kind] = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
	}
	printf("%s: 좀비 %d · 단계 %d · 광선 %d×2 — 먼 광선 한 번 %.2f µs · 짧은 광선 한 번 %.2f µs · 걸림 %d · 결과 해시 %016llx\n", dllPath, n, steps, rays, ms[0] * 1000.0 / rays, ms[1] * 1000.0 / rays, hits, h);
	return 0;
}

int main(int argc, char **argv)
{
	SetConsoleOutputCP(CP_UTF8);
	if (argc > 2 && !strcmp(argv[1], "ray")) { write_cfg(); return ray_main(argv[2], argc > 3 ? atoi(argv[3]) : 30000, argc > 4 ? atoi(argv[4]) : 200, argc > 5 ? atoi(argv[5]) : 200000, 12345u); }
	if (argc > 2 && !strcmp(argv[1], "prof")) return prof_main(argv[2], argc > 3 ? atoi(argv[3]) : 10000, argc > 4 ? atoi(argv[4]) : 300, argc > 5 ? atoi(argv[5]) : 8);
	if (argc < 3) { fprintf(stderr, "사용: compare.exe <원작 DLL> <대체본 DLL> [장면|all] [몸체 수] [단계 수]\n"); return 2; }
	write_cfg();
	Api A, B; if (!load(&A, argv[1], "원작") || !load(&B, argv[2], "대체")) return 2;

	{
		int (*opt)(int, int) = (int (*)(int, int))GetProcAddress(B.h, "DX_SetOption");
		const char *o = getenv("COMPARE_OPTS");
		if (opt && o) { char buf[256]; strncpy(buf, o, 255); buf[255] = 0; for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) { int k, v; if (sscanf(t, "%d=%d", &k, &v) == 2) { opt(k, v); printf("대체본 스위치 %d = %d\n", k, v); } } }
	}
	int s0 = 0, s1 = 2; if (argc > 3 && strcmp(argv[3], "all")) s0 = s1 = atoi(argv[3]);
	int n = argc > 4 ? atoi(argv[4]) : 300, steps = argc > 5 ? atoi(argv[5]) : 300, ok = 1;
	for (int s = s0; s <= s1; s++)
	{
		Trace ta = {0}, tb = {0};
		LARGE_INTEGER f, t0, t1, t2; QueryPerformanceFrequency(&f);
		QueryPerformanceCounter(&t0); run(&A, s, n, steps, &ta); QueryPerformanceCounter(&t1); run(&B, s, n, steps, &tb); QueryPerformanceCounter(&t2);
		char label[64]; sprintf(label, "장면 %d (몸체 %d, 단계 %d)", s, n, steps);
		ok &= compare(label, &ta, &tb);
		printf("   시간 원작 %.1f ms · 대체 %.1f ms\n", (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart, (t2.QuadPart - t1.QuadPart) * 1000.0 / f.QuadPart);
		free(ta.v); free(ta.tag); free(tb.v); free(tb.tag);
	}
	return ok ? 0 : 1;
}
