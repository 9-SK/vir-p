#include "rconsole.hpp"

#include <RBX/environment/env.hpp>
#include <Utilitys/Globals.hpp>

#include <cstring>
#include <string>
#include <mutex>
#include <queue>
#include <vector>
#include <deque>

namespace RConsole
{
    static std::mutex ConsoleMutex;

    // ── SEH guards (C2712 fix): __try lives ONLY in POD-only helpers ──
    // Each helper takes ONLY PODs (handles, pointers, ints, plain buffers)
    // and owns no RAII object, so __try is legal here. RAII-owning callers
    // call these and branch on status — no __try in RAII functions.
    static bool alloc_console_guarded(BOOL* out)
    {
        __try { *out = AllocConsole(); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool get_std_handles_guarded(HANDLE* hOut, HANDLE* hIn)
    {
        __try
        {
            *hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            *hIn = GetStdHandle(STD_INPUT_HANDLE);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool set_console_title_guarded(const char* title)
    {
        __try { SetConsoleTitleA(title); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool set_console_output_cp_guarded(UINT cp)
    {
        __try { SetConsoleOutputCP(cp); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool configure_console_mode_guarded(HANDLE hOut)
    {
        __try
        {
            DWORD mode = 0;
            if (GetConsoleMode(hOut, &mode))
                SetConsoleMode(hOut, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool set_console_attr_guarded(HANDLE h, WORD attr)
    {
        __try { SetConsoleTextAttribute(h, attr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool write_console_guarded(HANDLE h, const char* text, DWORD len, DWORD* written)
    {
        __try { WriteConsoleA(h, text, len, written, nullptr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool free_console_guarded()
    {
        __try { FreeConsole(); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool clear_console_guarded(HANDLE hOut)
    {
        __try
        {
            CONSOLE_SCREEN_BUFFER_INFO csbi{};
            if (GetConsoleScreenBufferInfo(hOut, &csbi))
            {
                DWORD written = 0;
                DWORD cells = (DWORD)(csbi.dwSize.X * csbi.dwSize.Y);
                COORD origin = {0, 0};
                FillConsoleOutputCharacterA(hOut, ' ', cells, origin, &written);
                FillConsoleOutputAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, cells, origin, &written);
                SetConsoleCursorPosition(hOut, origin);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool get_num_input_events_guarded(HANDLE hIn, DWORD* numEvents, BOOL* ok)
    {
        __try { *ok = GetNumberOfConsoleInputEvents(hIn, numEvents); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool peek_console_input_guarded(HANDLE hIn, INPUT_RECORD* buf, DWORD toPeek, DWORD* peeked, BOOL* ok)
    {
        __try { *ok = PeekConsoleInputA(hIn, buf, toPeek, peeked); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool read_console_guarded(HANDLE hIn, char* buffer, DWORD bufSize, DWORD* read, BOOL* ok)
    {
        __try { *ok = ReadConsoleA(hIn, buffer, bufSize, read, nullptr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool get_console_window_guarded(HWND* out)
    {
        __try { *out = GetConsoleWindow(); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { *out = NULL; return false; }
    }

    static bool show_window_guarded(HWND hwnd, int cmd)
    {
        __try { ShowWindow(hwnd, cmd); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool set_window_pos_guarded(HWND hwnd, HWND insertAfter)
    {
        __try { SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ── Color token language (same names as latestv3) — conhost WORD attrs ──
    // Supports @@RED@@, @@BLUE@@, @@GREEN@@, @@YELLOW@@, @@WHITE@@, @@BLACK@@,
    // @@CYAN@@, @@MAGENTA@@, @@RESET@@, @@GRAY@@, @@BROWN@@, @@ORANGE@@, @@LIGHT_BLUE@@,
    // @@LIGHT_GREEN@@, @@LIGHT_CYAN@@, @@LIGHT_RED@@, @@LIGHT_MAGENTA@@

    static const struct { const char* name; WORD attr; } ColorTokens[] = {
        {"@@RESET@@",        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE},
        {"@@RED@@",          FOREGROUND_RED},
        {"@@GREEN@@",        FOREGROUND_GREEN},
        {"@@BLUE@@",         FOREGROUND_BLUE},
        {"@@WHITE@@",        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE},
        {"@@BLACK@@",        0},
        {"@@YELLOW@@",       FOREGROUND_RED | FOREGROUND_GREEN},
        {"@@CYAN@@",         FOREGROUND_GREEN | FOREGROUND_BLUE},
        {"@@MAGENTA@@",      FOREGROUND_RED | FOREGROUND_BLUE},
        {"@@GRAY@@",         FOREGROUND_INTENSITY},
        {"@@BROWN@@",        FOREGROUND_RED | FOREGROUND_INTENSITY},
        {"@@ORANGE@@",       FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY},
        {"@@LIGHT_BLUE@@",   FOREGROUND_BLUE | FOREGROUND_INTENSITY},
        {"@@LIGHT_GREEN@@",  FOREGROUND_GREEN | FOREGROUND_INTENSITY},
        {"@@LIGHT_CYAN@@",   FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY},
        {"@@LIGHT_RED@@",    FOREGROUND_RED | FOREGROUND_INTENSITY},
        {"@@LIGHT_MAGENTA@@", FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY},
        {nullptr, 0}
    };

    DWORD ParseColorToken(const char*& text, DWORD& currentColor)
    {
        for (int i = 0; ColorTokens[i].name; ++i)
        {
            size_t len = strlen(ColorTokens[i].name);
            if (strncmp(text, ColorTokens[i].name, len) == 0)
            {
                currentColor = ColorTokens[i].attr;
                text += len;
                return currentColor;
            }
        }
        return currentColor;
    }

    // ── Overlay float tokens (Renderer compat only, bypassed by Lua path) ──
    // Same names, ImGui float colors (dark blues brightened for dark theme).
    static const struct { const char* name; float r, g, b; } OverlayColorTokens[] = {
        {"@@RESET@@",        1.00f, 1.00f, 1.00f},
        {"@@RED@@",          1.00f, 0.00f, 0.00f},
        {"@@GREEN@@",        0.00f, 1.00f, 0.00f},
        {"@@BLUE@@",         0.35f, 0.60f, 1.00f},
        {"@@WHITE@@",        1.00f, 1.00f, 1.00f},
        {"@@BLACK@@",        0.00f, 0.00f, 0.00f},
        {"@@YELLOW@@",       1.00f, 1.00f, 0.00f},
        {"@@CYAN@@",         0.00f, 1.00f, 1.00f},
        {"@@MAGENTA@@",      1.00f, 0.00f, 1.00f},
        {"@@GRAY@@",         0.65f, 0.65f, 0.65f},
        {"@@BROWN@@",        0.65f, 0.40f, 0.15f},
        {"@@ORANGE@@",       1.00f, 0.60f, 0.00f},
        {"@@LIGHT_BLUE@@",   0.50f, 0.75f, 1.00f},
        {"@@LIGHT_GREEN@@",  0.50f, 1.00f, 0.50f},
        {"@@LIGHT_CYAN@@",   0.50f, 1.00f, 1.00f},
        {"@@LIGHT_RED@@",    1.00f, 0.50f, 0.50f},
        {"@@LIGHT_MAGENTA@@", 1.00f, 0.50f, 1.00f},
        {nullptr, 0.0f, 0.0f, 0.0f}
    };

    bool ParseColorToken(const char*& text, float& r, float& g, float& b)
    {
        for (int i = 0; OverlayColorTokens[i].name; ++i)
        {
            size_t len = strlen(OverlayColorTokens[i].name);
            if (strncmp(text, OverlayColorTokens[i].name, len) == 0)
            {
                r = OverlayColorTokens[i].r;
                g = OverlayColorTokens[i].g;
                b = OverlayColorTokens[i].b;
                text += len;
                return true;
            }
        }
        return false;
    }

    bool EnsureConsole()
    {
        // Fast path: already have a console (brief lock, no blocking).
        {
            std::lock_guard<std::mutex> lock(ConsoleMutex);
            if (ConsoleHandle != INVALID_HANDLE_VALUE && ConsoleHandle != NULL)
                return true;
        }

        // Alloc without holding the lock (can be slow; never block holders).
        BOOL allocOk = FALSE;
        if (!alloc_console_guarded(&allocOk))
            return false;
        if (!allocOk)
        {
            DWORD err = GetLastError();
            if (err != ERROR_ACCESS_DENIED)
            {
                // Alloc failed for a reason other than "already have one".
                // Fall through to salvage: if GetStdHandle yields something
                // usable (e.g., launched with a console), use it; else fail.
            }
        }

        HANDLE hOut = INVALID_HANDLE_VALUE;
        HANDLE hIn = INVALID_HANDLE_VALUE;
        if (!get_std_handles_guarded(&hOut, &hIn))
            return false;

        bool outUsable = (hOut != INVALID_HANDLE_VALUE && hOut != NULL);
        bool inUsable = (hIn != INVALID_HANDLE_VALUE && hIn != NULL);
        if (!allocOk && !outUsable && !inUsable)
            return false;

        // Setup: title/CP/mode (latestv3 observable). Guarded, handle-checked,
        // no locks held (no shared state touched yet except the OS console).
        if (outUsable)
        {
            set_console_title_guarded("justafatguy67");
            set_console_output_cp_guarded(CP_UTF8);
            configure_console_mode_guarded(hOut);
        }

        // Publish under lock (re-check: another thread may have won the race).
        {
            std::lock_guard<std::mutex> lock(ConsoleMutex);
            if (ConsoleHandle != INVALID_HANDLE_VALUE && ConsoleHandle != NULL)
                return true;
            ConsoleHandle = hOut;
            ConsoleInputHandle = hIn;
            ConsoleCreated = true;
            ConsoleTitle = "justafatguy67";
            if (!allocOk && !outUsable && !inUsable)
                return false;
            return true;
        }
    }

    void WriteToConsole(const char* text, WORD color)
    {
        if (!text)
            return;
        // Auto-create (latestv3 semantics: Ensure at print start). No lock
        // held here; Ensure locks internally and releases before we lock.
        if (!EnsureConsole())
            return;

        // Serialize output. No Lua under this lock (caller converted args via
        // LuaToStringSafe before calling).
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        HANDLE hOut = ConsoleHandle;
        if (hOut == INVALID_HANDLE_VALUE || hOut == NULL)
            return;

        const char* cursor = text;
        const char* segmentStart = text;
        DWORD written = 0;
        WORD curColor = color;

        set_console_attr_guarded(hOut, curColor);

        while (*cursor)
        {
            if (cursor[0] == '@' && cursor[1] == '@')
            {
                const char* before = cursor;
                DWORD dummy = curColor;
                DWORD newColor = ParseColorToken(cursor, dummy);
                if (cursor != before)
                {
                    if (before > segmentStart)
                    {
                        if (!write_console_guarded(hOut, segmentStart, (DWORD)(before - segmentStart), &written))
                            return;
                    }
                    curColor = (WORD)newColor;
                    set_console_attr_guarded(hOut, curColor);
                    segmentStart = cursor;
                    continue;
                }
            }
            ++cursor;
        }

        if (cursor > segmentStart)
        {
            if (!write_console_guarded(hOut, segmentStart, (DWORD)(cursor - segmentStart), &written))
                return;
        }

        write_console_guarded(hOut, "\n", 1, &written);
    }

    // Overlay backing store: token-parse `text` into colored spans, append
    // one line, evict oldest past the cap. Brief scoped lock only — no Lua,
    // no I/O, no blocking calls under it.
    void AppendColoredLine(const std::string& text, float dr, float dg, float db)
    {
        ConsoleLine line;
        float r = dr, g = dg, b = db;
        std::string cur;
        const char* p = text.c_str();
        while (*p)
        {
            if (p[0] == '@' && p[1] == '@')
            {
                float nr = r, ng = g, nb = b;
                if (ParseColorToken(p, nr, ng, nb))
                {
                    if (!cur.empty())
                    {
                        ConsoleSpan s;
                        s.text = cur;
                        s.r = r; s.g = g; s.b = b;
                        line.spans.push_back(std::move(s));
                        cur.clear();
                    }
                    r = nr; g = ng; b = nb;
                    continue;
                }
            }
            cur.push_back(*p++);
        }
        if (!cur.empty())
        {
            ConsoleSpan s;
            s.text = cur;
            s.r = r; s.g = g; s.b = b;
            line.spans.push_back(std::move(s));
        }
        if (line.spans.empty())
        {
            // Blank print still occupies a line so spacing is preserved.
            ConsoleSpan s;
            s.r = r; s.g = g; s.b = b;
            line.spans.push_back(std::move(s));
        }

        std::lock_guard<std::mutex> lock(ConsoleMutex);
        Scrollback.push_back(std::move(line));
        while (Scrollback.size() > kMaxScrollback)
            Scrollback.pop_front();
    }

    void ClearScrollback()
    {
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        Scrollback.clear();
    }

    bool IsConsoleShown()
    {
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        return ConsoleCreated && ConsoleVisible;
    }

    bool IsConsoleTopmost()
    {
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        return ConsoleTopmost;
    }

    std::string GetConsoleTitle()
    {
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        return ConsoleTitle;
    }

    size_t CopyScrollback(std::vector<ConsoleLine>& out)
    {
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        out.assign(Scrollback.begin(), Scrollback.end());
        return out.size();
    }

    void SubmitInputLine(const std::string& line)
    {
        std::string clipped = line;
        if (clipped.size() > kMaxInput - 1)
            clipped.resize(kMaxInput - 1);
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        SubmittedInput.push(std::move(clipped));
    }

    bool PollInputLine(std::string& out)
    {
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        if (SubmittedInput.empty())
            return false;
        out = std::move(SubmittedInput.front());
        SubmittedInput.pop();
        // Mirror the old cooked-line behavior: strip trailing
        // \r\n and enforce the 4096 cap.
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        if (out.size() > kMaxInput - 1)
            out.resize(kMaxInput - 1);
        return true;
    }

    // ── Lua Functions ──

    // Convert Lua value at `idx` to string WITHOUT holding ConsoleMutex.
    // Never lets a Lua error escape via longjmp: uses lua_pcall for the
    // `tostring` fallback so a hostile/broken __tostring (or a shadowed
    // `tostring` global) yields a fallback string instead of throwing.
    // All Lua API used here runs with NO mutex held, so even an unexpected
    // longjmp could never leave ConsoleMutex locked.
    static std::string LuaToStringSafe(lua_State* L, int idx)
    {
        if (lua_isstring(L, idx) || lua_isnumber(L, idx))
        {
            const char* s = lua_tostring(L, idx);
            return s ? s : "";
        }
        lua_getglobal(L, "tostring");
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 1);
            return "<unprintable>";
        }
        lua_pushvalue(L, idx);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        {
            // Discard the error object; do NOT re-raise while locked.
            // Callers invoke this helper before taking the lock, but the
            // fallback keeps behavior deterministic for UNC suites that
            // deliberately break __tostring.
            lua_pop(L, 1);
            return "<tostring error>";
        }
        const char* s = lua_tostring(L, -1);
        std::string out = s ? s : "";
        lua_pop(L, 1);
        return out;
    }

    int rconsolecreate(lua_State* L)
    {
        (void)L;
        // No Lua args, no lock held across Ensure (Ensure locks internally).
        // Auto-creates the real conhost (latestv3 semantics).
        EnsureConsole();
        return 0;
    }

    int rconsoledestroy(lua_State* L)
    {
        (void)L;
        // Safe no-op when no console exists. No Lua under lock (no args).
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        if (ConsoleHandle == INVALID_HANDLE_VALUE || ConsoleHandle == NULL)
        {
            ConsoleCreated = false;
            ConsoleInputHandle = INVALID_HANDLE_VALUE;
            ConsoleVisible = true;
            ConsoleTopmost = false;
            return 0;
        }
        free_console_guarded();
        ConsoleHandle = INVALID_HANDLE_VALUE;
        ConsoleInputHandle = INVALID_HANDLE_VALUE;
        ConsoleCreated = false;
        ConsoleVisible = true;
        ConsoleTopmost = false;
        // NOTE: Scrollback/SubmittedInput intentionally untouched (bypassed
        // legacy overlay queues kept only for Renderer build).
        return 0;
    }

    int rconsoleclear(lua_State* L)
    {
        (void)L;
        // Auto-create like latestv3 (Ensure at start), then clear conhost.
        // No Lua under lock (no args).
        if (!EnsureConsole())
            return 0;
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        HANDLE hOut = ConsoleHandle;
        if (hOut == INVALID_HANDLE_VALUE || hOut == NULL)
            return 0;
        clear_console_guarded(hOut);
        return 0;
    }

    int rconsoleprint(lua_State* L)
    {
        // Validate + convert ALL Lua args BEFORE any lock. luaL_checkany
        // longjmps on missing args; LuaToStringSafe uses pcall internally, so
        // no Lua error path can escape while ConsoleMutex is held.
        // WriteToConsole auto-creates (Ensure at print start, latestv3
        // semantics) and locks internally for writes with no Lua under it.
        luaL_checkany(L, 1);
        std::string text = LuaToStringSafe(L, 1);

        // Third arg compat (latestv3 reads but ignores): extract before lock.
        bool escape = true;
        if (!lua_isnone(L, 3))
            escape = lua_toboolean(L, 3) != 0;
        (void)escape;

        WriteToConsole(text.c_str(), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        return 0;
    }

    int rconsoleinfo(lua_State* L)
    {
        // Same lock discipline as print: convert first (no lock), then write.
        luaL_checkany(L, 1);
        std::string text = LuaToStringSafe(L, 1);
        std::string output = "@@CYAN@@@@LIGHT_BLUE@@[INFO]@@RESET@@ " + text;
        WriteToConsole(output.c_str(), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        return 0;
    }

    int rconsolewarn(lua_State* L)
    {
        luaL_checkany(L, 1);
        std::string text = LuaToStringSafe(L, 1);
        std::string output = "@@YELLOW@@@@LIGHT_GREEN@@[WARN]@@RESET@@ " + text;
        WriteToConsole(output.c_str(), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        return 0;
    }

    int rconsoleerr(lua_State* L)
    {
        luaL_checkany(L, 1);
        std::string text = LuaToStringSafe(L, 1);
        std::string output = "@@RED@@@@LIGHT_RED@@[ERROR]@@RESET@@ " + text;

        // Still returns the error string even when no console exists
        // (WriteToConsole degrades to no-op, latestv3 contract preserved).
        WriteToConsole(output.c_str(), FOREGROUND_RED | FOREGROUND_INTENSITY);

        // Push AFTER lock release: lua_pushstring can throw on OOM, which must
        // never happen while ConsoleMutex is held (WriteToConsole already
        // released its internal lock before returning).
        lua_pushstring(L, text.c_str());
        return 1;
    }

    // Non-blocking conhost line input. NEVER blocks the calling thread:
    // peek-first (GetNumberOfConsoleInputEvents + PeekConsoleInputA), return
    // "" unless a full Enter-terminated line is present; only then
    // ReadConsoleA (which returns immediately). Never holds the mutex across
    // the read. Redirected stdin degrades to "" (no crash).
    int rconsoleinput(lua_State* L)
    {
        if (!EnsureConsole())
        {
            lua_pushstring(L, "");
            return 1;
        }

        HANDLE hIn = INVALID_HANDLE_VALUE;
        {
            std::lock_guard<std::mutex> lock(ConsoleMutex);
            hIn = ConsoleInputHandle;
        }
        if (hIn == INVALID_HANDLE_VALUE || hIn == NULL)
        {
            lua_pushstring(L, "");
            return 1;
        }

        DWORD numEvents = 0;
        BOOL eventsOk = FALSE;
        if (!get_num_input_events_guarded(hIn, &numEvents, &eventsOk) || !eventsOk)
        {
            lua_pushstring(L, ""); // redirected/pipe, invalid mode, or SEH
            return 1;
        }
        if (numEvents == 0)
        {
            lua_pushstring(L, "");
            return 1;
        }

        // Peek for an Enter-terminated (cooked) line. No consume, no block.
        const DWORD kPeekCap = 8192;
        DWORD toPeek = numEvents < kPeekCap ? numEvents : kPeekCap;
        std::vector<INPUT_RECORD> peekBuf;
        peekBuf.resize(toPeek);
        DWORD peeked = 0;
        bool hasEnter = false;
        BOOL peekOk = FALSE;
        if (!peek_console_input_guarded(hIn, peekBuf.data(), toPeek, &peeked, &peekOk))
        {
            lua_pushstring(L, "");
            return 1;
        }
        if (!peekOk || peeked == 0)
        {
            lua_pushstring(L, "");
            return 1;
        }
        for (DWORD i = 0; i < peeked; ++i)
        {
            const INPUT_RECORD& ir = peekBuf[i];
            if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown)
            {
                WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
                char ch = ir.Event.KeyEvent.uChar.AsciiChar;
                if (vk == VK_RETURN || ch == '\r' || ch == '\n')
                {
                    hasEnter = true;
                    break;
                }
            }
        }
        if (!hasEnter)
        {
            lua_pushstring(L, "");
            return 1;
        }

        // Full line present: ReadConsoleA returns immediately (no block).
        // Mutex is NOT held here (snapshot above, released before peek/read).
        char buffer[4096] = {0};
        DWORD read = 0;
        BOOL readOk = FALSE;
        if (!read_console_guarded(hIn, buffer, sizeof(buffer) - 1, &read, &readOk) || !readOk)
        {
            lua_pushstring(L, "");
            return 1;
        }
        while (read > 0 && (buffer[read - 1] == '\n' || buffer[read - 1] == '\r'))
            buffer[--read] = '\0';
        if (read > (DWORD)(kMaxInput - 1))
        {
            buffer[kMaxInput - 1] = '\0';
            read = (DWORD)(kMaxInput - 1);
        }
        // lua_pushstring AFTER (can throw on OOM, never under lock).
        lua_pushstring(L, buffer);
        return 1;
    }

    int rconsolename(lua_State* L)
    {
        // luaL_checklstring longjmps on missing/wrong-type args: must run
        // with NO lock held. Copy to std::string immediately; the raw Lua
        // string pointer must not be used under the lock.
        size_t len = 0;
        const char* name = luaL_checklstring(L, 1, &len);
        std::string title(name ? name : "", len);
        if (title.size() > 255)
            title.resize(255);

        // Auto-create like latestv3, then set OS title (guarded) + mirror to
        // overlay title flag so Renderer shows the same name. No Lua under lock.
        bool created = EnsureConsole();
        if (created)
        {
            std::string forTitle = title; // SetConsoleTitleA needs nul-terminated
            set_console_title_guarded(forTitle.c_str());
        }
        {
            std::lock_guard<std::mutex> lock(ConsoleMutex);
            ConsoleTitle = std::move(title);
            if (created)
                ConsoleCreated = true;
        }

        return 0;
    }

    int rconsoleshow(lua_State* L)
    {
        (void)L;
        // Auto-create like latestv3, then ShowWindow (both guarded).
        if (!EnsureConsole())
            return 0;
        HWND hwnd = NULL;
        get_console_window_guarded(&hwnd);
        if (hwnd)
        {
            show_window_guarded(hwnd, SW_SHOW);
            std::lock_guard<std::mutex> lock(ConsoleMutex);
            ConsoleCreated = true;
            ConsoleVisible = true;
        }
        return 0;
    }

    int rconsolehide(lua_State* L)
    {
        (void)L;
        // No auto-create (latestv3 semantics): hide only if a window exists.
        HWND hwnd = NULL;
        get_console_window_guarded(&hwnd);
        if (hwnd)
        {
            show_window_guarded(hwnd, SW_HIDE);
            std::lock_guard<std::mutex> lock(ConsoleMutex);
            ConsoleVisible = false;
        }
        return 0;
    }

    int rconsoletoggle(lua_State* L)
    {
        (void)L;
        // No auto-create (latestv3 semantics). Guarded, no Lua under lock.
        HWND hwnd = NULL;
        get_console_window_guarded(&hwnd);
        if (hwnd)
        {
            std::lock_guard<std::mutex> lock(ConsoleMutex);
            if (ConsoleVisible)
            {
                show_window_guarded(hwnd, SW_HIDE);
                ConsoleVisible = false;
            }
            else
            {
                show_window_guarded(hwnd, SW_SHOW);
                ConsoleVisible = true;
            }
        }
        return 0;
    }

    int rconsoletop(lua_State* L)
    {
        // lua_isnone/lua_toboolean never longjmp, but extract before locking
        // for consistency: no Lua API inside the locked region at all.
        bool topmost = lua_isnone(L, 1) ? true : lua_toboolean(L, 1) != 0;

        HWND hwnd = NULL;
        get_console_window_guarded(&hwnd);
        if (hwnd)
        {
            HWND insertAfter = topmost ? HWND_TOPMOST : HWND_NOTOPMOST;
            set_window_pos_guarded(hwnd, insertAfter);
            std::lock_guard<std::mutex> lock(ConsoleMutex);
            ConsoleTopmost = topmost;
        }
        return 0;
    }

    // ── Teardown ──

    // Called ONLY from DllMain DLL_PROCESS_DETACH. Lock-free and
    // blocking-free by construction: no mutexes (a worker may hold one
    // mid-print while the process dies — waiting would hang detach), no
    // FreeConsole (can block on a dead conhost and recreate the zombie
    // from entry.cpp), no file/ring I/O. Handles + created flag are merely
    // forgotten; the OS reclaims conhost + scrollback memory on process
    // death. (ViridianRing's mapping is likewise never unmapped.)
    void OnProcessDetach()
    {
        ConsoleHandle = INVALID_HANDLE_VALUE;
        ConsoleInputHandle = INVALID_HANDLE_VALUE;
        ConsoleCreated = false;
    }

    // ── Reset / Register ──

    void ResetLibraryState()
    {
        // Don't destroy the conhost on teleport reset — just clear state
        // flags (latestv3 semantics). The console survives DataModel re-init
        // (same process); scrollback holds only std::strings (no per-DataModel
        // pointers), so re-init can neither dangle nor leak it.
        std::lock_guard<std::mutex> lock(ConsoleMutex);
        ConsoleVisible = true;
        ConsoleTopmost = false;
    }

    void register_lib(lua_State* L)
    {
        funcnameinc = 0;

        // rconsolecreate / consolecreate
        Environment->make_aliases(L, rconsolecreate, funcnameinc++, {
            "rconsolecreate", "consolecreate"
            });

        // rconsoledestroy / consoledestroy
        Environment->make_aliases(L, rconsoledestroy, funcnameinc++, {
            "rconsoledestroy", "consoledestroy"
            });

        // rconsoleclear / consoleclear
        Environment->make_aliases(L, rconsoleclear, funcnameinc++, {
            "rconsoleclear", "consoleclear"
            });

        // rconsoleprint / consoleprint
        Environment->make_aliases(L, rconsoleprint, funcnameinc++, {
            "rconsoleprint", "consoleprint"
            });

        // rconsoleinfo
        Environment->make_global(L, "rconsoleinfo", rconsoleinfo, funcnameinc++);

        // rconsolewarn
        Environment->make_global(L, "rconsolewarn", rconsolewarn, funcnameinc++);

        // rconsoleerr
        Environment->make_global(L, "rconsoleerr", rconsoleerr, funcnameinc++);

        // rconsoleinput / consoleinput
        Environment->make_aliases(L, rconsoleinput, funcnameinc++, {
            "rconsoleinput", "consoleinput"
            });

        // rconsolename / rconsolesettitle / consolesettitle
        Environment->make_aliases(L, rconsolename, funcnameinc++, {
            "rconsolename", "rconsolesettitle", "consolesettitle"
            });

        // rconsoleshow
        Environment->make_global(L, "rconsoleshow", rconsoleshow, funcnameinc++);

        // rconsolehide
        Environment->make_global(L, "rconsolehide", rconsolehide, funcnameinc++);

        // rconsoletoggle
        Environment->make_global(L, "rconsoletoggle", rconsoletoggle, funcnameinc++);

        // rconsoletop
        Environment->make_global(L, "rconsoletop", rconsoletop, funcnameinc++);

        // rconsolehidden (alias for rconsolehide)
        Environment->make_global(L, "rconsolehidden", rconsolehide, funcnameinc++);
    }
}
