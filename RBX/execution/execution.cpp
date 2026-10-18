#include <Utilitys/Globals.hpp>
#include <Utilitys/RingStatus.hpp>
#include <RBX/execution/execution.hpp>
#include <RBX/task_scheduler/task_scheduler.hpp>
#include <cstring>
#include <cstdio>

// Execution tracing to boot.log (temporary diagnostics): every ExecuteScript
// milestone lands in %LOCALAPPDATA%\Viridian\boot.log so stalled scripts can
// be pinpointed. No-op these again once execution is verified healthy.
static void ExecFileLog(const char* fmt, ...)
{
    char bb[MAX_PATH]{};
    DWORD bn = GetEnvironmentVariableA("LOCALAPPDATA", bb, MAX_PATH);
    if (!bn || bn >= MAX_PATH) return;
    std::string bp = std::string(bb) + "\\Viridian\\boot.log";
    FILE* bf = nullptr;
    if (fopen_s(&bf, bp.c_str(), "a") != 0 || !bf) return;
    SYSTEMTIME st{}; GetLocalTime(&st);
    fprintf(bf, "%02u:%02u:%02u [Viridian] ", st.wHour, st.wMinute, st.wSecond);
    va_list ap; va_start(ap, fmt); vfprintf(bf, fmt, ap); va_end(ap);
    fprintf(bf, "\n");
    fclose(bf);
    ViridianRing::RefreshLogTail();
}
#define NAM_LOG(msg) do { ExecFileLog("%s", msg); } while(0)
#define NAM_LOGFMT(fmt, ...) do { ExecFileLog(fmt, __VA_ARGS__); } while(0)

// Script output/error mirroring to boot.log (flows to the ring tail via
// RefreshLogTail, which the UI tails). Headless-safe: file I/O only, no
// window/console creation, no mutex held (callers hold none), no Lua API
// inside the file-I/O section, never throws (all noexcept + catch-all) so a
// logging failure can never fail the execution, and error logging never
// recurses (it never calls ReportExecutionError).
static constexpr size_t kViridianPrintCap = 4096u;
static constexpr size_t kViridianErrCap = 1024u;
static const char kViridianTruncMarker[] = "... [truncated]";

static void AppendBootLogTagged(const char* tag, const std::string& line) noexcept
{
    try
    {
        if (!tag || !tag[0])
            tag = "exec";
        char base[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return;
        char bp[MAX_PATH + 32]{};
        _snprintf_s(bp, sizeof(bp), _TRUNCATE, "%s\\Viridian\\boot.log", base);
        FILE* bf = nullptr;
        if (fopen_s(&bf, bp, "a") != 0 || !bf)
            return;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(bf, "%02u:%02u:%02u [%s] %s\n", st.wHour, st.wMinute, st.wSecond, tag, line.c_str());
        fclose(bf);
        try { ViridianRing::RefreshLogTail(); } catch (...) {}
    }
    catch (...) {}
}

static std::string SanitizeOneLine(const std::string& in)
{
    try
    {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            char c = in[i];
            if (c == '\r' && i + 1 < in.size() && in[i + 1] == '\n')
            {
                out.push_back(' ');
                ++i;
            }
            else if (c == '\r' || c == '\n' || c == '\0')
                out.push_back(' ');
            else
                out.push_back(c);
        }
        return out;
    }
    catch (...)
    {
        return std::string();
    }
}

static std::string TruncateWithMarker(std::string s, size_t cap)
{
    try
    {
        if (s.size() > cap)
        {
            s.resize(cap);
            s += kViridianTruncMarker;
        }
        return s;
    }
    catch (...)
    {
        return std::string();
    }
}

static void MirrorPrintLine(const std::string& text) noexcept
{
    try
    {
        std::string one = SanitizeOneLine(text);
        one = TruncateWithMarker(one, kViridianPrintCap);
        AppendBootLogTagged("print", one);
    }
    catch (...) {}
}

static void MirrorExecErrorLine(const char* msg) noexcept
{
    try
    {
        std::string s = (msg && msg[0]) ? std::string(msg) : std::string("unknown error");
        size_t pos = s.find_first_of("\r\n");
        if (pos != std::string::npos)
            s.resize(pos);
        s = TruncateWithMarker(s, kViridianErrCap);
        std::string line = "error: " + s;
        AppendBootLogTagged("exec", line);
    }
    catch (...) {}
}

// Convert Lua value at idx to string without ever longjmp-ing while a lock
// is held (callers hold no lock at all). Fast path for string/number;
// otherwise pcall(tostring) with fallback strings. Stack is balanced on all
// paths (net 0): pushes are always popped.
static std::string LuaToStringNoThrow(lua_State* L, int idx)
{
    try
    {
        if (lua_isstring(L, idx) || lua_isnumber(L, idx))
        {
            size_t len = 0;
            const char* s = lua_tolstring(L, idx, &len);
            if (!s)
                return std::string();
            return std::string(s, len);
        }
        int topBefore = lua_gettop(L);
        lua_getglobal(L, "tostring");
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 1);
            const char* tn = lua_typename(L, lua_type(L, idx));
            return tn ? std::string(tn) : std::string("<unprintable>");
        }
        lua_pushvalue(L, idx);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        {
            lua_pop(L, 1);
            (void)topBefore;
            return std::string("<tostring error>");
        }
        size_t len = 0;
        const char* s = lua_tolstring(L, -1, &len);
        std::string out = s ? std::string(s, len) : std::string();
        lua_pop(L, 1);
        return out;
    }
    catch (...)
    {
        return std::string("<unprintable>");
    }
}

// Global print override (the one scripts actually call). There was no prior
// override: scripts fell through to the game's print (dev console only, never
// boot.log). This wrapper logs "[print] <text>" (one line, tab-joined like
// stock print, 4KB-capped) then forwards to the original so dev-console
// behavior is preserved. No mutex held; file I/O happens after arg
// extraction with no Lua API inside, inside try/catch so logging can never
// throw back into the script.
static int ViridianPrint(lua_State* L)
{
    std::string combined;
    int n = 0;
    try
    {
        n = lua_gettop(L);
        if (n < 0)
            n = 0;
        for (int i = 1; i <= n; ++i)
        {
            std::string part;
            try { part = LuaToStringNoThrow(L, i); }
            catch (...) { part = "<unprintable>"; }
            if (i > 1)
                combined.push_back('\t');
            combined += part;
            if (combined.size() > kViridianPrintCap)
                break;
        }
    }
    catch (...) {}
    try
    {
        std::string toLog;
        try { toLog = combined; } catch (...) { toLog.clear(); }
        MirrorPrintLine(toLog);
    }
    catch (...) {}
    try
    {
        if (lua_isnil(L, lua_upvalueindex(1)))
            return 0;
        if (lua_iscfunction(L, lua_upvalueindex(1)))
        {
            lua_CFunction f = lua_tocfunction(L, lua_upvalueindex(1));
            if (f == ViridianPrint)
                return 0;
        }
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, n, 0);
    }
    catch (...)
    {
        return 0;
    }
    return 0;
}

// Hidden error-logging leaf used by the deferred wrapper chunk. Takes the
// raw error object, stringifies without invoking user metamethods (fast path
// for string/number, type-name fallback otherwise, so no Lua longjmp and no
// recursion), mirrors "[exec] error: ..." (first line, 1KB-capped), returns 0.
static int ViridianLogError(lua_State* L)
{
    std::string msg;
    try
    {
        int top = lua_gettop(L);
        if (top < 1)
            msg = "unknown error";
        else if (lua_isstring(L, 1) || lua_isnumber(L, 1))
        {
            size_t len = 0;
            const char* s = lua_tolstring(L, 1, &len);
            if (s)
                msg.assign(s, len);
            else
                msg = "unknown error";
        }
        else
        {
            const char* tn = nullptr;
            try { tn = lua_typename(L, lua_type(L, 1)); } catch (...) { tn = nullptr; }
            if (tn)
                msg = std::string("non-string error: ") + tn;
            else
                msg = "non-string error";
        }
    }
    catch (...)
    {
        msg = "unknown error";
    }
    MirrorExecErrorLine(msg.c_str());
    return 0;
}

static void EnsurePrintOverride(lua_State* L)
{
    if (!L)
        return;
    lua_getglobal(L, "print");
    bool already = false;
    if (lua_iscfunction(L, -1))
    {
        lua_CFunction f = lua_tocfunction(L, -1);
        if (f == ViridianPrint)
            already = true;
    }
    if (already)
    {
        lua_pop(L, 1);
        return;
    }
    lua_pushcclosure(L, ViridianPrint, nullptr, 1);
    lua_setglobal(L, "print");
}

static void EnsureErrorLogFunc(lua_State* L)
{
    if (!L)
        return;
    lua_getglobal(L, "__viridian_log_error");
    bool already = false;
    if (lua_iscfunction(L, -1))
    {
        lua_CFunction f = lua_tocfunction(L, -1);
        if (f == ViridianLogError)
            already = true;
    }
    lua_pop(L, 1);
    if (already)
        return;
    lua_pushcclosure(L, ViridianLogError, nullptr, 0);
    lua_setglobal(L, "__viridian_log_error");
}

// Lua wrapper deferred instead of the raw chunk so async runtime errors are
// observed: pcall(f) inside Lua stays yieldable (task.wait works), unlike a
// C-level pcall across the C-call boundary. On failure it pcall()s the log
// leaf (never throws) then error(err, 0) rethrows intact for the dev console.
static const char kViridianWrapSrc[] =
    "local f, logFn = ...\n"
    "local ok, err = pcall(f)\n"
    "if not ok then\n"
    "  if type(logFn) == \"function\" then\n"
    "    pcall(logFn, err)\n"
    "  end\n"
    "  error(err, 0)\n"
    "end\n";

// Ring guard: marks status=executing on entry; on scope exit (every return
// path) records exec_count+1 / last_exec_* and returns to status=ready.
// ok=false unless the script reaches the DONE marker, so compile/load/
// schedule failures are reported as failed executions.
struct ExecRingGuard
{
    uint32_t Bytes;
    DWORD T0;
    bool OkFlag = false;
    explicit ExecRingGuard(size_t bytes)
        : Bytes(static_cast<uint32_t>(bytes)), T0(GetTickCount())
    {
        ViridianRing::OnExecBegin();
    }
    ~ExecRingGuard()
    {
        ViridianRing::OnExecEnd(OkFlag, Bytes, GetTickCount() - T0);
    }
};

class BytecodeEncoder : public Luau::BytecodeEncoder
{
	inline void encode(uint32_t* data, size_t count) override
	{
		for (auto i = 0; i < count;)
		{
			uint8_t Opcode = LUAU_INSN_OP(data[i]);
			const auto LookupTable = reinterpret_cast<BYTE*>(Offsets::OpcodeLookupTable);
			uint8_t FinalOpcode = Opcode * 227;
			FinalOpcode = LookupTable[FinalOpcode];

			data[i] = (FinalOpcode) | (data[i] & ~0xFF);
			i += Luau::getOpLength(static_cast<LuauOpcode>(Opcode));
		}
	}
};





namespace
{
	void ReportExecutionError(const char* Stage, const char* Message)
	{
		const char* Text = Message && Message[0] ? Message : "unknown error";
		Roblox::Print(3, "[dsfy-System] %s: %s", Stage, Text);

		char Buffer[1024]{};
		std::snprintf(Buffer, sizeof(Buffer), "[sdfyftws-System] %s: %s\n", Stage, Text);
		OutputDebugStringA(Buffer);

		// Mirror to boot.log (ring tail) as "[exec] error: <first line, 1KB-capped>".
		// No Lua API, no lock, never throws, never recurses (mirror never calls back here).
		MirrorExecErrorLine(Text);
	}

	bool IsIdentifierChar(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
	}

	bool MatchLongBracketOpen(const std::string& source, size_t position, size_t& equalsCount)
	{
		if (position >= source.size() || source[position] != '[')
			return false;

		size_t cursor = position + 1;
		while (cursor < source.size() && source[cursor] == '=')
			++cursor;

		if (cursor < source.size() && source[cursor] == '[')
		{
			equalsCount = cursor - position - 1;
			return true;
		}

		return false;
	}

	size_t FindLongBracketClose(const std::string& source, size_t position, size_t equalsCount)
	{
		std::string close = std::string("]");
		close.append(equalsCount, '=');
		close.push_back(']');

		const size_t found = source.find(close, position);
		return found == std::string::npos ? source.size() : found + close.size();
	}

	bool TryRewriteGameMethod(const std::string& source, size_t& position, std::string& output, const char* method, const char* replacement, bool passGame)
	{
		const std::string pattern = std::string("game:") + method;
		const size_t length = pattern.size();

		if (source.compare(position, length, pattern) != 0)
			return false;

		if (position > 0 && IsIdentifierChar(source[position - 1]))
			return false;

		size_t cursor = position + length;
		if (cursor < source.size() && IsIdentifierChar(source[cursor]))
			return false;

		while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t' || source[cursor] == '\r' || source[cursor] == '\n'))
			++cursor;

		if (cursor >= source.size() || source[cursor] != '(')
			return false;

		output.append(replacement);

		if (passGame)
		{
			output.append(source, position + length, cursor - (position + length));
			output.append("(game, ");
			position = cursor + 1;
		}
		else
		{
			position += length;
		}

		return true;
	}

	bool TryRewriteGameDotCall(const std::string& source, size_t& position, std::string& output, const char* method, const char* replacement, bool passGame)
	{
		const std::string pattern = std::string("game.") + method;
		const size_t length = pattern.size();

		if (source.compare(position, length, pattern) != 0)
			return false;

		if (position > 0 && IsIdentifierChar(source[position - 1]))
			return false;

		size_t cursor = position + length;
		if (cursor < source.size() && IsIdentifierChar(source[cursor]))
			return false;

		while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t' || source[cursor] == '\r' || source[cursor] == '\n'))
			++cursor;

		if (cursor >= source.size() || source[cursor] != '(')
			return false;

		output.append(replacement);

		if (passGame)
		{
			output.append(source, position + length, cursor - (position + length));
			output.append("(game, ");
			position = cursor + 1;
		}
		else
		{
			position += length;
		}

		return true;
	}

	bool TryRewriteGameMemberCall(const std::string& source, size_t& position, std::string& output, const char* method, const char* replacement)
	{
		const bool lowerGame = source.compare(position, 4, "game") == 0;
		const bool upperGame = source.compare(position, 4, "Game") == 0;
		if (!lowerGame && !upperGame)
			return false;

		if (position > 0 && IsIdentifierChar(source[position - 1]))
			return false;

		size_t cursor = position + 4;
		if (cursor < source.size() && IsIdentifierChar(source[cursor]))
			return false;

		while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t' || source[cursor] == '\r' || source[cursor] == '\n'))
			++cursor;

		if (cursor >= source.size() || (source[cursor] != ':' && source[cursor] != '.'))
			return false;

		const char separator = source[cursor++];
		while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t' || source[cursor] == '\r' || source[cursor] == '\n'))
			++cursor;

		const size_t methodLength = strlen(method);
		if (source.compare(cursor, methodLength, method) != 0)
			return false;

		const size_t methodEnd = cursor + methodLength;
		if (methodEnd < source.size() && IsIdentifierChar(source[methodEnd]))
			return false;

		cursor = methodEnd;
		while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t' || source[cursor] == '\r' || source[cursor] == '\n'))
			++cursor;

		if (cursor >= source.size() || source[cursor] != '(')
			return false;

		output.append(replacement);

		if (strcmp(method, "GetObjects") == 0 && separator == ':')
		{
			output.append(source, methodEnd, cursor - methodEnd);
			output.append("(game, ");
			position = cursor + 1;
		}
		else
		{
			position = methodEnd;
		}

		return true;
	}

    std::string NormalizeExecutorSource(const std::string& source)
    {
        std::string output;
		output.reserve(source.size());

		for (size_t i = 0; i < source.size();)
		{
			if (TryRewriteGameMemberCall(source, i, output, "HttpGetAsync", "HttpGet") ||
				TryRewriteGameMemberCall(source, i, output, "HttpGet", "HttpGet") ||
				TryRewriteGameMemberCall(source, i, output, "GetObjects", "GetObjects") ||
				TryRewriteGameMethod(source, i, output, "HttpGetAsync", "HttpGet", false) ||
				TryRewriteGameMethod(source, i, output, "HttpGet", "HttpGet", false) ||
				TryRewriteGameMethod(source, i, output, "GetObjects", "GetObjects", true) ||
				TryRewriteGameDotCall(source, i, output, "HttpGetAsync", "HttpGet", false) ||
				TryRewriteGameDotCall(source, i, output, "HttpGet", "HttpGet", false) ||
				TryRewriteGameDotCall(source, i, output, "GetObjects", "GetObjects", true))
			{
				continue;
			}

			const char c = source[i];

			if (c == '-' && i + 1 < source.size() && source[i + 1] == '-')
			{
				size_t equalsCount = 0;
				if (i + 2 < source.size() && MatchLongBracketOpen(source, i + 2, equalsCount))
				{
					const size_t end = FindLongBracketClose(source, i + 2, equalsCount);
					output.append(source, i, end - i);
					i = end;
					continue;
				}

				const size_t end = source.find('\n', i + 2);
				const size_t count = (end == std::string::npos ? source.size() : end) - i;
				output.append(source, i, count);
				i += count;
				continue;
			}

			if (c == '\'' || c == '"' || c == '`')
			{
				const char quote = c;
				const size_t start = i++;
				bool escaped = false;

				while (i < source.size())
				{
					const char current = source[i++];
					if (escaped)
					{
						escaped = false;
						continue;
					}
					if (current == '\\')
					{
						escaped = true;
						continue;
					}
					if (current == quote)
						break;
				}

				output.append(source, start, i - start);
				continue;
			}

			if (c == '[')
			{
				size_t equalsCount = 0;
				if (MatchLongBracketOpen(source, i, equalsCount))
				{
					const size_t end = FindLongBracketClose(source, i, equalsCount);
					output.append(source, i, end - i);
					i = end;
					continue;
				}
			}

			output.push_back(c);
			++i;
		}

		return output;
	}
}

std::string Execution::CompileScript(std::string Source)
{
	Source = NormalizeExecutorSource(Source);

	auto BytecodeEncoding = BytecodeEncoder();
	static const char* CommonGlobals[] = { "Game", "Workspace", "game", "plugin", "script", "shared", "workspace", "_G", "_ENV", nullptr };

	Luau::CompileOptions Options;
	Options.debugLevel = 2;
	Options.optimizationLevel = 1;
	Options.mutableGlobals = CommonGlobals;
	Options.vectorLib = "Vector3";
	Options.vectorCtor = "new";
	Options.vectorType = "Vector3";

	return Luau::compile(Source, Options, {}, &BytecodeEncoding);
}

void Execution::ExecuteScript(lua_State* L, std::string Script)
{
	if (!L || Script.empty())
	{
		NAM_LOG("ExecuteScript: L is NULL or script empty");
		return;
	}
	NAM_LOGFMT("ExecuteScript: starting (%zu bytes)", Script.size());
	ExecRingGuard ringGuard(Script.size());

	const int OriginalTop = lua_gettop(L);
	lua_State* ExecutionThread = lua_newthread(L);
	if (!ExecutionThread)
	{
		NAM_LOG("ExecuteScript: lua_newthread returned null");
		lua_settop(L, OriginalTop);
		ReportExecutionError("thread creation", "lua_newthread returned null");
		return;
	}
	NAM_LOG("ExecuteScript: execution thread created");

	const int ThreadRef = lua_ref(L, -1);
	lua_pop(L, 1);
	const auto Cleanup = [&]()
		{
			lua_unref(L, ThreadRef);
			lua_settop(L, OriginalTop);
		};

		luaL_sandboxthread(ExecutionThread);
		task_scheduler::SetThreadCapabilities(ExecutionThread, 8, MaxCapabilities);

		// Install script-output mirroring globals (idempotent, no window creation).
		EnsurePrintOverride(ExecutionThread);
		EnsureErrorLogFunc(ExecutionThread);

	NAM_LOG("ExecuteScript: compiling...");
	std::string Bytecode;
	try
	{
		Bytecode = Execution::CompileScript(std::move(Script));
	}
	catch (const std::exception& Error)
	{
		NAM_LOGFMT("ExecuteScript: compile exception: %s", Error.what());
		ReportExecutionError("compile", Error.what());
		Cleanup();
		return;
	}
	catch (...)
	{
		NAM_LOG("ExecuteScript: unknown compile exception");
		ReportExecutionError("compile", "compiler failure");
		Cleanup();
		return;
	}

	if (Bytecode.empty())
	{
		NAM_LOG("ExecuteScript: compiler returned empty bytecode");
		ReportExecutionError("compile", "compiler returned empty bytecode");
		Cleanup();
		return;
	}
	NAM_LOGFMT("ExecuteScript: compiled %zu bytes of bytecode", Bytecode.size());

	if (luau_load(ExecutionThread, "=Viridian", Bytecode.c_str(), Bytecode.length(), NULL) != LUA_OK)
	{
		const char* Error = lua_tostring(ExecutionThread, -1);
		NAM_LOGFMT("ExecuteScript: luau_load FAILED: %s", Error ? Error : "null");
		ReportExecutionError("load", Error);
		lua_pop(ExecutionThread, 1);
		Cleanup();
		return;
	}
	NAM_LOG("ExecuteScript: luau_load OK");

	Closure* Closure = clvalue(luaA_toobject(ExecutionThread, -1));
	if (!Closure || Closure->isC || !Closure->l.p)
	{
		NAM_LOG("ExecuteScript: loaded chunk is not a Lua closure");
		ReportExecutionError("load", "loaded chunk is not a Lua closure");
		Cleanup();
		return;
	}
	task_scheduler::SetProtoCapabilities(Closure->l.p, &MaxCapabilities);

	// Build the yield-safe error-mirroring wrapper. Silent fallback to a
	// direct defer when the wrapper cannot be built (stack left as [orig]).
	bool haveWrap = false;
	int wrapOrigRef = LUA_NOREF, wrapWrapRef = LUA_NOREF, wrapLogRef = LUA_NOREF;
	do
	{
		std::string wrapBytecode;
		try { wrapBytecode = Execution::CompileScript(std::string(kViridianWrapSrc)); }
		catch (...) { break; }
		if (wrapBytecode.empty())
			break;
		if (luau_load(ExecutionThread, "=ViridianWrap", wrapBytecode.c_str(), wrapBytecode.length(), NULL) != LUA_OK)
		{
			lua_pop(ExecutionThread, 1);
			break;
		}
		auto WrapClosureCheck = clvalue(luaA_toobject(ExecutionThread, -1));
		if (WrapClosureCheck && !WrapClosureCheck->isC && WrapClosureCheck->l.p)
			task_scheduler::SetProtoCapabilities(WrapClosureCheck->l.p, &MaxCapabilities);
		lua_getglobal(ExecutionThread, "__viridian_log_error");
		wrapOrigRef = lua_ref(ExecutionThread, 1);
		wrapWrapRef = lua_ref(ExecutionThread, 2);
		wrapLogRef = lua_ref(ExecutionThread, 3);
		lua_settop(ExecutionThread, 0);
		haveWrap = true;
	} while (0);
	if (!haveWrap)
	{
		if (wrapOrigRef > 0)
			lua_unref(ExecutionThread, wrapOrigRef);
		if (wrapWrapRef > 0)
			lua_unref(ExecutionThread, wrapWrapRef);
		if (wrapLogRef > 0)
			lua_unref(ExecutionThread, wrapLogRef);
		lua_settop(ExecutionThread, 1);
	}

	NAM_LOG("ExecuteScript: scheduling via task.defer...");
	if (haveWrap)
	{
		lua_getglobal(ExecutionThread, "task");
		if (lua_type(ExecutionThread, -1) == LUA_TNIL)
		{
			NAM_LOG("ExecuteScript: global 'task' is unavailable");
			ReportExecutionError("schedule", "global 'task' is unavailable");
			lua_pop(ExecutionThread, 1);
			if (wrapOrigRef > 0)
				lua_unref(ExecutionThread, wrapOrigRef);
			if (wrapWrapRef > 0)
				lua_unref(ExecutionThread, wrapWrapRef);
			if (wrapLogRef > 0)
				lua_unref(ExecutionThread, wrapLogRef);
			Cleanup();
			return;
		}

		lua_getfield(ExecutionThread, -1, "defer");
		if (!lua_isfunction(ExecutionThread, -1))
		{
			NAM_LOG("ExecuteScript: task.defer is unavailable");
			ReportExecutionError("schedule", "task.defer is unavailable");
			lua_pop(ExecutionThread, 2);
			if (wrapOrigRef > 0)
				lua_unref(ExecutionThread, wrapOrigRef);
			if (wrapWrapRef > 0)
				lua_unref(ExecutionThread, wrapWrapRef);
			if (wrapLogRef > 0)
				lua_unref(ExecutionThread, wrapLogRef);
			Cleanup();
			return;
		}

		lua_remove(ExecutionThread, -2);
		lua_getref(ExecutionThread, wrapWrapRef);
		lua_getref(ExecutionThread, wrapOrigRef);
		lua_getref(ExecutionThread, wrapLogRef);
		if (wrapOrigRef > 0)
			lua_unref(ExecutionThread, wrapOrigRef);
		if (wrapWrapRef > 0)
			lua_unref(ExecutionThread, wrapWrapRef);
		if (wrapLogRef > 0)
			lua_unref(ExecutionThread, wrapLogRef);
		if (lua_pcall(ExecutionThread, 3, 0, 0) != LUA_OK)
		{
			const char* Error = lua_tostring(ExecutionThread, -1);
			NAM_LOGFMT("ExecuteScript: task.defer FAILED: %s", Error ? Error : "null");
			ReportExecutionError("task.defer", Error);
			lua_pop(ExecutionThread, 1);
			Cleanup();
			return;
		}

		ringGuard.OkFlag = true;
		NAM_LOG("ExecuteScript: === DONE ===");
		Cleanup();
		return;
	}
	lua_getglobal(ExecutionThread, "task");
	if (lua_type(ExecutionThread, -1) == LUA_TNIL)
	{
		NAM_LOG("ExecuteScript: global 'task' is unavailable");
		ReportExecutionError("schedule", "global 'task' is unavailable");
		lua_pop(ExecutionThread, 1);
		Cleanup();
		return;
	}

	lua_getfield(ExecutionThread, -1, "defer");
	if (!lua_isfunction(ExecutionThread, -1))
	{
		NAM_LOG("ExecuteScript: task.defer is unavailable");
		ReportExecutionError("schedule", "task.defer is unavailable");
		lua_pop(ExecutionThread, 2);
		Cleanup();
		return;
	}

	lua_remove(ExecutionThread, -2);
	lua_insert(ExecutionThread, -2);
	if (lua_pcall(ExecutionThread, 1, 0, 0) != LUA_OK)
	{
		const char* Error = lua_tostring(ExecutionThread, -1);
		NAM_LOGFMT("ExecuteScript: task.defer FAILED: %s", Error ? Error : "null");
		ReportExecutionError("task.defer", Error);
		lua_pop(ExecutionThread, 1);
		Cleanup();
		return;
	}

	ringGuard.OkFlag = true;
	NAM_LOG("ExecuteScript: === DONE ===");
	Cleanup();
}
