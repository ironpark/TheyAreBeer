#include <windows.h>
#include <intrin.h>
#include "dxpool.h"

#define DX_MAX_WORKERS 31
#define DX_SPIN_US 60
#define DX_SLEEP_MS 50
#ifndef DX_POOL_PROBE
#define DX_POOL_PROBE()
#endif
#define DX_JOIN_YIELD_US 200

typedef BOOL (WINAPI *WaitOnAddressFn)(volatile VOID *, PVOID, SIZE_T, DWORD);
typedef VOID (WINAPI *WakeByAddressAllFn)(PVOID);
static WaitOnAddressFn pWaitOnAddress; static WakeByAddressAllFn pWakeByAddressAll; static int synchResolved;

static void resolve_synch(void)
{
	if (synchResolved) return;
	HMODULE m = GetModuleHandleW(L"api-ms-win-core-synch-l1-2-0.dll");
	if (!m) m = LoadLibraryW(L"api-ms-win-core-synch-l1-2-0.dll");
	if (!m) m = GetModuleHandleW(L"kernelbase.dll");
	if (m) { pWaitOnAddress = (WaitOnAddressFn)GetProcAddress(m, "WaitOnAddress"); pWakeByAddressAll = (WakeByAddressAllFn)GetProcAddress(m, "WakeByAddressAll"); }
	if (!pWaitOnAddress || !pWakeByAddressAll) { pWaitOnAddress = NULL; pWakeByAddressAll = NULL; }
	synchResolved = 1;
}

static double qpc_us;
static long long now_ticks(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart; }
static void init_clock(void) { if (qpc_us == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpc_us = f.QuadPart / 1e6; } }

static struct {
	volatile LONG generation;
	volatile LONG64 work;
	volatile LONG remaining;
	volatile LONG quit;
	dx_range_fn fn; void *ctx; int count, chunk;
	int threads;
	int broken;
	HANDLE handles[DX_MAX_WORKERS];
} P;

static void run_chunks(int worker)
{
	for (;;)
	{
		LONG64 w = InterlockedIncrement64(&P.work) - 1;
		LONG c = (LONG)(w & 0xFFFFFFFF), chunks = (LONG)(w >> 32);
		if (c >= chunks) break;
		DX_POOL_PROBE();

		int b = c * P.chunk, e = b + P.chunk; if (e > P.count) e = P.count;
		P.fn(P.ctx, b, e, worker);
		InterlockedDecrement(&P.remaining);
	}
}

static DWORD WINAPI worker_main(LPVOID arg)
{
	int id = (int)(INT_PTR)arg;
	LONG seen = P.generation;
	long long spinTicks = (long long)(DX_SPIN_US * qpc_us);
	while (!P.quit)
	{

		long long t0 = now_ticks(); int k = 0, sleeping = 0;
		while (P.generation == seen && !P.quit)
		{
			if (sleeping)
			{
				LONG s = seen;
				if (pWaitOnAddress) pWaitOnAddress(&P.generation, &s, sizeof(LONG), DX_SLEEP_MS); else Sleep(1);
				continue;
			}
			_mm_pause();
			if ((++k & 63) == 0 && now_ticks() - t0 > spinTicks) sleeping = 1;
		}
		if (P.quit) break;
		seen = P.generation;
		run_chunks(id);
	}
	return 0;
}

static void wake_all(void) { if (pWakeByAddressAll) pWakeByAddressAll((PVOID)&P.generation); }

void dx_pool_set_threads(int n)
{
	resolve_synch(); init_clock();
	if (n < 0) n = 0; if (n > DX_MAX_WORKERS) n = DX_MAX_WORKERS;
	if (P.broken || n == P.threads) return;

	if (P.threads > 0)
	{
		P.quit = 1; InterlockedIncrement(&P.generation); wake_all();
		DWORD r = WaitForMultipleObjects(P.threads, P.handles, TRUE, 5000);
		for (int i = 0; i < P.threads; i++) CloseHandle(P.handles[i]);
		P.threads = 0;
		if (r != WAIT_OBJECT_0) { P.broken = 1; return; }
		P.quit = 0;
	}
	int made = 0;
	for (int i = 0; i < n; i++)
	{
		HANDLE h = CreateThread(NULL, 256 * 1024, worker_main, (LPVOID)(INT_PTR)(made + 1), 0, NULL);
		if (!h) break;
		P.handles[made++] = h;
	}
	P.threads = made;
}

int dx_pool_threads(void) { return P.threads; }

int dx_pool_auto_threads(void)
{
	int physical = 0, logical = 0;
	DWORD len = 0;
	GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
	if (len > 0)
	{
		BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, len);
		if (buf && GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf, &len))
		{
			for (DWORD off = 0; off < len; )
			{
				PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX p = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf + off);
				if (p->Relationship == RelationProcessorCore)
				{
					physical++;
					for (WORD g = 0; g < p->Processor.GroupCount; g++) { KAFFINITY m = p->Processor.GroupMask[g].Mask; while (m) { logical += (int)(m & 1); m >>= 1; } }
				}
				if (p->Size == 0) break;
				off += p->Size;
			}
		}
		if (buf) HeapFree(GetProcessHeap(), 0, buf);
	}

	DWORD_PTR pm = 0, sm = 0; int allowed = 0;
	if (GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm)) { DWORD_PTR m = pm; while (m) { allowed += (int)(m & 1); m >>= 1; } }
	if (logical <= 0) { SYSTEM_INFO si; GetSystemInfo(&si); logical = (int)si.dwNumberOfProcessors; }
	if (physical <= 0) physical = logical / 2 > 0 ? logical / 2 : 1;
	int n = physical - 1;
	if (physical <= 2) n = 0;
	if (allowed > 0 && allowed - 1 < n) n = allowed - 1;
	if (n > 16) n = 16;
	if (n < 0) n = 0;
	return n;
}

void dx_parallel_for(int count, int chunk, int minParallel, dx_range_fn fn, void *ctx)
{
	if (count <= 0) return;
	if (P.threads == 0 || count < minParallel) { fn(ctx, 0, count, 0); return; }
	if (chunk < 1) chunk = 1;
	LONG chunks = (count + chunk - 1) / chunk;
	P.fn = fn; P.ctx = ctx; P.count = count; P.chunk = chunk;
	P.remaining = chunks;
	InterlockedExchange64(&P.work, (LONG64)chunks << 32);
	InterlockedIncrement(&P.generation);
	wake_all();
	run_chunks(0);

	long long t0 = now_ticks(), yieldTicks = (long long)(DX_JOIN_YIELD_US * qpc_us); int k = 0;
	while (P.remaining > 0)
	{
		_mm_pause();
		if ((++k & 63) == 0 && now_ticks() - t0 > yieldTicks) SwitchToThread();
	}
	MemoryBarrier();
}
