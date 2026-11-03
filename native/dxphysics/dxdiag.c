#include <windows.h>
#include <stdio.h>
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")

#define DX_RING 256
static struct { unsigned id; DWORD tid; void *p; float a, b, c, d; } g_ring[DX_RING];
static volatile LONG g_ring_pos;
void dx_diag_call(unsigned id, void *p, float a, float b, float c, float d)
{
	LONG i = InterlockedIncrement(&g_ring_pos) & (DX_RING - 1);
	g_ring[i].id = id; g_ring[i].tid = GetCurrentThreadId(); g_ring[i].p = p; g_ring[i].a = a; g_ring[i].b = b; g_ring[i].c = c; g_ring[i].d = d;
}

static HMODULE g_self; static ULONG_PTR g_lo, g_hi; static volatile LONG g_written;
static void dx_snapshot(HINSTANCE inst);

static void module_of(ULONG_PTR addr, char *out, size_t n)
{
	HMODULE m = NULL; char path[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m) && m)
	{ GetModuleFileNameA(m, path, MAX_PATH); const char *s = strrchr(path, '\\'); _snprintf(out, n, "%s+0x%llX", s ? s + 1 : path, (unsigned long long)(addr - (ULONG_PTR)m)); }
	else _snprintf(out, n, "0x%llX", (unsigned long long)addr);
}

static int diag_path(char *path, const char *name)
{
	static const char *const sub[] = { "TABPerf", "Billions" };
	char dir[MAX_PATH], d[MAX_PATH];
	if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, dir) != S_OK) return 0;
	for (int k = 0; k < 2; k++)
	{
		_snprintf(d, MAX_PATH, "%s\\%s", dir, sub[k]); d[MAX_PATH - 1] = 0;
		DWORD a = GetFileAttributesA(d);
		if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) { _snprintf(path, MAX_PATH, "%s\\%s", d, name); path[MAX_PATH - 1] = 0; return 1; }
	}
	return 0;
}

// The crash path is resolved ahead of time (outside DllMain and outside the exception handler): the handler may run
// on an exhausted stack, where shell32 calls are not safe.
static char g_crash_path[MAX_PATH]; static volatile LONG g_crash_ready;
void dx_diag_init(void)
{
	char p[MAX_PATH];
	if (diag_path(p, "dxphysics-crash.txt")) { memcpy(g_crash_path, p, sizeof p); InterlockedExchange(&g_crash_ready, 1); }
}
// Reserve stack for the handler itself, so a stack overflow inside this DLL can still be reported.
void dx_diag_thread(void)
{
	static __declspec(thread) int done;
	if (done) return;
	done = 1;
	ULONG g = 32 * 1024; SetThreadStackGuarantee(&g);
}

static void write_report(FILE *f, PEXCEPTION_POINTERS ep, DWORD code);

static LONG CALLBACK veh(PEXCEPTION_POINTERS ep)
{
	DWORD code = ep->ExceptionRecord->ExceptionCode;
	if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != STATUS_HEAP_CORRUPTION)
		return EXCEPTION_CONTINUE_SEARCH;
	ULONG_PTR ip = (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress;
	if (ip < g_lo || ip >= g_hi) return EXCEPTION_CONTINUE_SEARCH;
	if (!g_crash_ready) return EXCEPTION_CONTINUE_SEARCH;
	if (InterlockedExchange(&g_written, 1)) return EXCEPTION_CONTINUE_SEARCH;
	FILE *f = fopen(g_crash_path, "w"); if (!f) return EXCEPTION_CONTINUE_SEARCH;
	setvbuf(f, NULL, _IONBF, 0);
	// Reading the faulting stack/code can fault again; keep whatever was written so far.
	__try { write_report(f, ep, code); } __except (EXCEPTION_EXECUTE_HANDLER) { fprintf(f, "(진단 중 예외, 이후 생략)\n"); }
	fclose(f);
	return EXCEPTION_CONTINUE_SEARCH;
}

static void write_report(FILE *f, PEXCEPTION_POINTERS ep, DWORD code)
{
	SYSTEMTIME st; GetLocalTime(&st);
	fprintf(f, "%02d:%02d:%02d.%03d 예외 0x%08lX 스레드 %lu\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, code, GetCurrentThreadId());
	if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
		fprintf(f, "접근 %s 주소 0x%llX\n", ep->ExceptionRecord->ExceptionInformation[0] ? "쓰기" : "읽기", (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
	CONTEXT c = *ep->ContextRecord;
	fprintf(f, "rax=%llX rbx=%llX rcx=%llX rdx=%llX rsi=%llX rdi=%llX r8=%llX r9=%llX rsp=%llX\n", c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.R8, c.R9, c.Rsp);
	fprintf(f, "eflags=%08lX mxcsr=%08lX\n", c.EFlags, c.MxCsr);
	{
		const unsigned char *code = (const unsigned char *)c.Rip; fprintf(f, "code@rip:"); for (int k = 0; k < 16; k++) fprintf(f, " %02X", code[k]); fprintf(f, "\n");
		ULONG_PTR probes[3] = { (ULONG_PTR)c.Rsp, (ULONG_PTR)c.Rdi - 0x10, (ULONG_PTR)c.Rip };
		for (int k = 0; k < 3; k++) { MEMORY_BASIC_INFORMATION mbi; if (VirtualQuery((LPCVOID)probes[k], &mbi, sizeof mbi)) fprintf(f, "mem %llX: state %lX protect %lX type %lX base %llX size %llX\n", (unsigned long long)probes[k], mbi.State, mbi.Protect, mbi.Type, (unsigned long long)(ULONG_PTR)mbi.BaseAddress, (unsigned long long)mbi.RegionSize); }
	}

	for (int depth = 0; depth < 32 && c.Rip; depth++)
	{
		char m[MAX_PATH + 32]; module_of((ULONG_PTR)c.Rip, m, sizeof m); fprintf(f, "  #%02d %s\n", depth, m);
		DWORD64 imageBase; PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &imageBase, NULL);
		if (!fn) { c.Rip = *(DWORD64 *)c.Rsp; c.Rsp += 8; continue; }
		PVOID handlerData; DWORD64 establisher;
		RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fn, &c, &handlerData, &establisher, NULL);
	}
	fprintf(f, "최근 호출(오래된 것부터, id 는 DXPhysicsNative.c 의 DXD_ 번호):\n");
	LONG pos = g_ring_pos;
	for (int k = 1; k <= DX_RING; k++)
	{
		int i = (pos + k) & (DX_RING - 1); if (!g_ring[i].id) continue;
		fprintf(f, "  id %u 스레드 %lu p=%p %g %g %g %g\n", g_ring[i].id, g_ring[i].tid, g_ring[i].p, g_ring[i].a, g_ring[i].b, g_ring[i].c, g_ring[i].d);
	}
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
	(void)reserved;
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_self = inst;
		IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)inst; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)inst + dos->e_lfanew);
		g_lo = (ULONG_PTR)inst; g_hi = g_lo + nt->OptionalHeader.SizeOfImage;
		AddVectoredExceptionHandler(1, veh);
		dx_snapshot(inst);
	}
	return TRUE;
}

#include <intrin.h>
#define DX_N4 __nop(); __nop(); __nop(); __nop();
#define DX_N16 DX_N4 DX_N4 DX_N4 DX_N4
#define DX_N64 DX_N16 DX_N16 DX_N16 DX_N16
#define DX_N256 DX_N64 DX_N64 DX_N64 DX_N64
#define DX_N1K DX_N256 DX_N256 DX_N256 DX_N256
// The loader patches 5 bytes at DLL base + 0x13B3; this pad must stay at the very start of .text.
// /INCLUDE keeps it even if dx_keep_pad were optimised away (/Gw, /GL); scripts/build-native.ps1 checks its placement in the map file.
#pragma comment(linker, "/INCLUDE:dx_text_pad")
#pragma code_seg(".text$a00")
__declspec(noinline) void dx_text_pad(void) { DX_N1K DX_N1K DX_N1K DX_N1K DX_N1K DX_N1K DX_N1K DX_N1K __debugbreak(); }
#pragma code_seg()
void (*volatile dx_keep_pad)(void) = dx_text_pad;

static unsigned char *g_snap; static ULONG_PTR g_snap_lo, g_snap_len;
static void dx_snapshot(HINSTANCE inst)
{
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)inst; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)inst + dos->e_lfanew);
	IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);

	for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
		if (!memcmp(sec[i].Name, ".text", 5)) { g_snap_lo = (ULONG_PTR)inst + sec[i].VirtualAddress; g_snap_len = sec[i].Misc.VirtualSize; }
	if (!g_snap_len) return;
	g_snap = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, g_snap_len);
	if (g_snap) memcpy(g_snap, (void *)g_snap_lo, g_snap_len);
}
void dx_check_patches(const char *where)
{
	if (!g_snap) return;
	const unsigned char *cur = (const unsigned char *)g_snap_lo; ULONG_PTR i = 0; int found = 0; FILE *f = NULL;
	while (i < g_snap_len)
	{
		if (cur[i] == g_snap[i]) { i++; continue; }
		ULONG_PTR j = i; while (j < g_snap_len && cur[j] != g_snap[j]) j++;

		if (!f) { static int started; char path[MAX_PATH]; if (!diag_path(path, "dxphysics-patches.txt")) return; f = fopen(path, started ? "a" : "w"); if (!f) return; started = 1; SYSTEMTIME st; GetLocalTime(&st); fprintf(f, "%02d:%02d:%02d %s:\n", st.wHour, st.wMinute, st.wSecond, where); }
		fprintf(f, "  RVA 0x%llX 길이 %llu:", (unsigned long long)(g_snap_lo - g_lo + i), (unsigned long long)(j - i));
		for (ULONG_PTR k = i; k < j && k < i + 32; k++) fprintf(f, " %02X>%02X", g_snap[k], cur[k]);
		fprintf(f, "\n"); found++;
		memcpy(g_snap + i, cur + i, j - i);
		i = j;
	}
	if (f) fclose(f);
}
