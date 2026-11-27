// Linux implementation of the xrDebug error/crash path. Portable parts (gather_info, the fail/error
// wrappers, fatal, the OOM report) are copied unchanged from xrDebugNew.cpp; the Windows-only parts
// (StackWalker, minidumps, SEH filters, CRT handlers) are replaced by execinfo backtraces and signals.
#include "stdafx.h"
#pragma hdrstop

#include "xrDebug.h"
#include "os_clipboard.h"

#include <csignal>
#include <fcntl.h>
#include <ctime>
#include <string>
#include <cxxabi.h>
#include <execinfo.h>
#include <new>
#include <unistd.h>

extern bool shared_str_initialized;

XRCORE_API xrDebug Debug;

static bool error_after_dialog = false;

namespace crash_saving
{
void (*save_impl)() = nullptr;
BOOL enabled = TRUE;

void save()
{
	if (enabled && save_impl != nullptr)
		(*save_impl)();
}
} // namespace crash_saving

extern void printLuaStack();

namespace
{
void save_crash_log(); // below
// Logs the native call stack. Symbol names need -rdynamic at link time; addresses always print.
void log_native_stack(int skip)
{
	void* frames[128];
	const int n = backtrace(frames, 128);
	char** syms = backtrace_symbols(frames, n);
	for (int i = skip; i < n; ++i)
	{
		const char* line = syms ? syms[i] : "?";
		// "module(mangled+0x1f) [addr]" -> demangle the part between '(' and '+'
		const char* open = strchr(line, '(');
		const char* plus = open ? strchr(open, '+') : nullptr;
		if (open && plus && plus > open + 1)
		{
			std::string mangled(open + 1, plus);
			int status = 0;
			char* name = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
			Msg("%s", status == 0 && name ? (std::string(line, open + 1) + name + plus).c_str() : line);
			free(name);
		}
		else
			Msg("%s", line);
	}
	free(syms);
}
} // namespace

void LogStackTrace(LPCSTR header = nullptr, bool printStack = false)
{
	if (!shared_str_initialized)
		return;
	if (header)
		Msg("%s", header);
	if (printStack)
	{
		printLuaStack();
		Msg("\n");
		log_native_stack(2);
	}
}

void xrDebug::gather_info(const char* expression, const char* description, const char* argument0, const char* argument1,
                          const char* file, int line, const char* function, LPSTR assertion_info,
                          u32 const assertion_info_size)
{
	if (!expression)
		expression = "<no expression>";
	LPSTR buffer_base = assertion_info;
	LPSTR buffer = assertion_info;
	int assertion_size = (int)assertion_info_size;
	LPCSTR endline = "\n";
	LPCSTR prefix = "[error]";
	bool extended_description = (description && !argument0 && strchr(description, '\n'));
	for (int i = 0; i < 2; ++i)
	{
		if (!i)
			buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sFATAL ERROR%s%s", endline,
			                     endline, endline);
		buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sExpression    : %s%s", prefix,
		                     expression, endline);
		buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sFunction      : %s%s", prefix,
		                     function, endline);
		buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sFile          : %s%s", prefix, file,
		                     endline);
		buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sLine          : %d%s", prefix, line,
		                     endline);

		if (extended_description)
		{
			buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s%s%s", endline, description,
			                     endline);
			if (argument0)
			{
				if (argument1)
				{
					buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s%s", argument0,
					                     endline);
					buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s%s", argument1,
					                     endline);
				}
				else
					buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s%s", argument0,
					                     endline);
			}
		}
		else
		{
			buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sDescription   : %s%s", prefix,
			                     description, endline);
			if (argument0)
			{
				if (argument1)
				{
					buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sArgument 0    : %s%s",
					                     prefix, argument0, endline);
					buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sArgument 1    : %s%s",
					                     prefix, argument1, endline);
				}
				else
					buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sArguments     : %s%s",
					                     prefix, argument0, endline);
			}
		}

		buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s", endline);
		if (!i)
		{
			if (shared_str_initialized)
			{
				Msg("%s", assertion_info);
				FlushLog();
			}
			buffer = assertion_info;
			endline = "\r\n";
			prefix = "";
		}
	}

#ifdef USE_MEMORY_MONITOR
    memory_monitor::flush_each_time(true);
    memory_monitor::flush_each_time(false);
#endif //-USE_MEMORY_MONITOR

	if (!strstr(GetCommandLine(), "-no_call_stack_assert"))
	{
		if (shared_str_initialized)
			Msg("stack trace:\n");

        LogStackTrace(nullptr, true);

#ifdef USE_OWN_ERROR_MESSAGE_WINDOW
		buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "stack trace:%s%s", endline, endline);
#endif //-USE_OWN_ERROR_MESSAGE_WINDOW

		//        BuildStackTrace();

		//        for (int i = 2; i < g_stackTraceCount; ++i)
		//        {
		//            if (shared_str_initialized)
		//                Msg("%s", g_stackTrace[i]);
		//
		//#ifdef USE_OWN_ERROR_MESSAGE_WINDOW
		//            buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s%s", g_stackTrace[i], endline);
		//#endif //-USE_OWN_ERROR_MESSAGE_WINDOW
		//        }

		if (shared_str_initialized)
			FlushLog();

		os_clipboard::copy_to_clipboard(assertion_info);
	}
}

void xrDebug::do_exit(const std::string& message)
{
	FlushLog();
	MessageBox(NULL, message.c_str(), "Error", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
	_exit(1);
}

// Same flow as the NO_BUG_TRAP backend on Windows: crash-save, report, show the message, terminate.
void xrDebug::backend(const char* expression, const char* description, const char* argument0, const char* argument1,
                      const char* file, int line, const char* function, bool& ignore_always)
{
	crash_saving::save();

	static xrCriticalSection CS;
	CS.Enter();

	string4096 assertion_info;
	gather_info(expression, description, argument0, argument1, file, line, function, assertion_info,
	            sizeof(assertion_info));

	LPCSTR endline = "\r\n";
	LPSTR buffer = assertion_info + xr_strlen(assertion_info);
	buffer += xr_sprintf(buffer, sizeof(assertion_info) - u32(buffer - &assertion_info[0]),
	                     "%sPress OK to abort execution%s", endline, endline);

	if (handler)
		handler();

	FlushLog();
	save_crash_log();
	MessageBox(NULL, assertion_info, "Fatal Error", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

	CS.Leave();
	_exit(1);
}

LPCSTR xrDebug::error2string(long code)
{
	static string1024 desc_storage;
	// HRESULTs from D3D/DXVK have no strerror text; print them in hex like FormatMessage's fallback.
	if (code > 0 && code < 4096)
		xr_strcpy(desc_storage, strerror((int)code));
	else
		xr_sprintf(desc_storage, "error 0x%08lx", (unsigned long)code);
	return desc_storage;
}

void xrDebug::error(long hr, const char* expr, const char* file, int line, const char* function, bool& ignore_always)
{
	backend(expr, error2string(hr), 0, 0, file, line, function, ignore_always);
}

void xrDebug::error(long hr, const char* expr, const char* e2, const char* file, int line, const char* function,
                    bool& ignore_always)
{
	backend(expr, error2string(hr), e2, 0, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* file, int line, const char* function, bool& ignore_always)
{
	backend(e1, "assertion failed", 0, 0, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const std::string& e2, const char* file, int line, const char* function,
                   bool& ignore_always)
{
	backend(e1, e2.c_str(), 0, 0, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* e2, const char* file, int line, const char* function,
                   bool& ignore_always)
{
	backend(e1, e2, 0, 0, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* e2, const char* e3, const char* file, int line, const char* function,
                   bool& ignore_always)
{
	backend(e1, e2, e3, 0, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* e2, const char* e3, const char* e4, const char* file, int line,
                   const char* function, bool& ignore_always)
{
	backend(e1, e2, e3, e4, file, line, function, ignore_always);
}

bool ignore_verify = true;

//AVO: print, dont crash
void xrDebug::soft_fail(LPCSTR e1, LPCSTR file, int line, LPCSTR function)
{
	if (!ignore_verify)
	Msg("! VERIFY_FAILED: %s[%d] {%s}  %s", file, line, function, e1);
}

void xrDebug::soft_fail(LPCSTR e1, const std::string& e2, LPCSTR file, int line, LPCSTR function)
{
	if (!ignore_verify)
	Msg("! VERIFY_FAILED: %s[%d] {%s}  %s %s", file, line, function, e1, e2.c_str());
}

void xrDebug::soft_fail(LPCSTR e1, LPCSTR e2, LPCSTR file, int line, LPCSTR function)
{
	if (!ignore_verify)
	Msg("! VERIFY_FAILED: %s[%d] {%s}  %s %s", file, line, function, e1, e2);
}

void xrDebug::soft_fail(LPCSTR e1, LPCSTR e2, LPCSTR e3, LPCSTR file, int line, LPCSTR function)
{
	if (!ignore_verify)
	Msg("! VERIFY_FAILED: %s[%d] {%s}  %s %s %s", file, line, function, e1, e2, e3);
}

void xrDebug::soft_fail(LPCSTR e1, LPCSTR e2, LPCSTR e3, LPCSTR e4, LPCSTR file, int line, LPCSTR function)
{
	if (!ignore_verify)
	Msg("! VERIFY_FAILED: %s[%d] {%s}  %s %s %s %s", file, line, function, e1, e2, e3, e4);
}

void xrDebug::soft_fail(LPCSTR e1, LPCSTR e2, LPCSTR e3, LPCSTR e4, LPCSTR e5, LPCSTR file, int line, LPCSTR function)
{
	if (!ignore_verify)
	Msg("! VERIFY_FAILED: %s[%d] {%s}  %s %s %s %s %s", file, line, function, e1, e2, e3, e4, e5);
}

//-AVO

void __cdecl xrDebug::fatal(const char* file, int line, const char* function, const char* F, ...)
{
	string1024 buffer;

	va_list p;
	va_start(p, F);
	vsprintf(buffer, F, p);
	va_end(p);

	bool ignore_always = true;

	backend(nullptr, "fatal error", buffer, 0, file, line, function, ignore_always);
}

typedef void (*full_memory_stats_callback_type)();
XRCORE_API full_memory_stats_callback_type g_full_memory_stats_callback = 0;

int out_of_memory_handler(size_t size)
{
	Msg("* [x-ray]: OOM requesting %lld bytes", size);

	if (g_full_memory_stats_callback)
		g_full_memory_stats_callback();
	else
	{
		Memory.mem_compact();
		size_t process_heap = Memory.mem_usage();
        u32 eco_strings_count = 0;
		int eco_strings = (int)g_pStringContainer->stat_economy(eco_strings_count);
		int eco_smem = (int)g_pSharedMemoryContainer->stat_economy();
		Msg("* [x-ray]: process heap[%llu K]", process_heap / 1024);
		Msg("* [x-ray]: strings: memory[%ld K], count[%lu]", eco_strings / 1024, eco_strings_count);
		Msg("* [x-ray]: shared: memory[%ld K]", eco_smem);
	}

	Debug.fatal(DEBUG_INFO, "Out of memory. Memory request: %lld K", size / 1024);
	return 1;
}

XRCORE_API string_path g_bug_report_file;

namespace
{
// Crash report: logs/crash_<unix time>.log next to the engine log. The engine log is renamed to .bkp on
// the next start and that backup is overwritten on the start after, so without this the log of a
// crashed run is gone after two launches. A signal can arrive with the heap lock held (malloc_printerr
// -> abort), so the first part of the report uses async-signal-safe calls only: open/write and
// backtrace_symbols_fd (backtrace() is primed at start-up so it does not load libgcc then). The copy
// of the engine log is appended afterwards.
int g_crash_fd = -1;

void write_str(int fd, const char* s)
{
	size_t n = 0;
	while (s[n])
		++n;
	(void)!write(fd, s, n);
}

void write_num(int fd, unsigned long long v)
{
	char buf[24];
	int i = sizeof(buf);
	buf[--i] = 0;
	do
	{
		buf[--i] = char('0' + v % 10);
		v /= 10;
	} while (v && i);
	write_str(fd, buf + i);
}

// Opens the report (once) and writes the native stack; async-signal-safe.
void open_crash_report(const char* what, int sig)
{
	if (g_crash_fd >= 0)
		return;
	const char* log = log_file_path();
	static char path[4096];
	size_t n = 0, dir_end = 0;
	for (; log && log[n] && n < sizeof(path) - 64; ++n)
	{
		path[n] = log[n] == '\\' ? '/' : log[n];
		if (path[n] == '/')
			dir_end = n + 1;
	}
	if (!dir_end)
		return;
	const char prefix[] = "crash_";
	size_t p = dir_end;
	for (const char* c = prefix; *c; ++c)
		path[p++] = *c;
	unsigned long long t = (unsigned long long)time(nullptr);
	char digits[24];
	int d = 0;
	do
	{
		digits[d++] = char('0' + t % 10);
		t /= 10;
	} while (t);
	while (d)
		path[p++] = digits[--d];
	for (const char* c = ".log"; *c; ++c)
		path[p++] = *c;
	path[p] = 0;
	g_crash_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (g_crash_fd < 0)
		return;
	write_str(g_crash_fd, "X-Ray crash report: ");
	write_str(g_crash_fd, what);
	if (sig)
	{
		write_str(g_crash_fd, " ");
		write_num(g_crash_fd, (unsigned long long)sig);
	}
	write_str(g_crash_fd, "\nnative stack:\n");
	void* frames[64];
	const int count = backtrace(frames, 64);
	backtrace_symbols_fd(frames, count, g_crash_fd);
	write_str(g_crash_fd, "\n--- engine log ---\n");
}

// Appends the (flushed) engine log to the report. Not async-signal-safe (stdio); runs after the safe part.
void save_crash_log()
{
	if (g_crash_fd < 0)
		open_crash_report("fatal error", 0);
	if (g_crash_fd < 0)
		return;
	const char* log = log_file_path();
	FILE* in = (log && *log) ? fopen(log, "rb") : nullptr;
	if (in)
	{
		char buf[65536];
		for (size_t n; (n = fread(buf, 1, sizeof(buf), in)) > 0;)
			(void)!write(g_crash_fd, buf, n);
		fclose(in);
	}
	close(g_crash_fd);
	g_crash_fd = -1;
}

void fatal_signal(int sig, siginfo_t* info, void*)
{
	// Restore the default action first so a fault inside this handler terminates normally.
	signal(sig, SIG_DFL);
	// Anything below may deadlock (e.g. malloc after heap corruption): never outlive 10 s.
	alarm(10);
	open_crash_report(sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGBUS ? "SIGBUS" : sig == SIGFPE ? "SIGFPE"
	                  : sig == SIGILL ? "SIGILL" : "signal", sig); // no strsignal: it may allocate
	if (shared_str_initialized)
	{
		Msg("! FATAL: signal %d (%s), address %p", sig, strsignal(sig), info ? info->si_addr : nullptr);
		LogStackTrace("stack trace:", true);
		FlushLog();
		save_crash_log();
	}
	crash_saving::save();
	raise(sig);
}

void out_of_memory_new_handler() { out_of_memory_handler(0); }
} // namespace

void debug_on_thread_spawn()
{
	// Signal dispositions are process-wide on Linux; nothing per thread is needed.
}

void xrDebug::_initialize(const bool& dedicated)
{
	(void)dedicated;
	*g_bug_report_file = 0;
	std::set_new_handler(&out_of_memory_new_handler);

	struct sigaction sa = {};
	sa.sa_sigaction = &fatal_signal;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&sa.sa_mask);
	for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
		sigaction(sig, &sa, nullptr);
	void* prime[1];
	backtrace(prime, 1); // loads the unwinder now, not inside a signal handler

	// An alternate stack lets the handler run after a stack overflow.
	static char altstack[64 * 1024];
	stack_t ss = {};
	ss.ss_sp = altstack;
	ss.ss_size = sizeof(altstack);
	sigaltstack(&ss, nullptr);
}
