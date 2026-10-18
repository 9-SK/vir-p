#pragma once

#include <lstate.h>
#include <lualib.h>
#include <Windows.h>

#include <string>
#include <deque>
#include <vector>
#include <queue>

namespace RConsole
{
    static int funcnameinc = 0;

    // ── Real conhost state (latestv3 observable) ──
    // A real AllocConsole conhost is the primary output. Overlay scrollback
    // below is retained ONLY so Renderer/ui/ui.cpp still compiles; the Lua
    // path never depends on it (bypassed) — see rconsole.cpp.
    inline HANDLE ConsoleHandle = INVALID_HANDLE_VALUE;
    inline HANDLE ConsoleInputHandle = INVALID_HANDLE_VALUE;
    inline HMENU ConsoleMenu = nullptr; // latestv3 compat (unused)
    inline bool ConsoleCreated = false; // mirrors handle validity (overlay compat)
    inline bool ConsoleVisible = true;  // conhost ShowWindow state + overlay flag
    inline bool ConsoleTopmost = false; // conhost topmost + overlay draw order
    inline std::string ConsoleTitle = "rconsole";
    // ── Overlay compat (Renderer build only, bypassed by Lua path) ──
    struct ConsoleSpan
    {
        std::string text; // plain text, @@color@@ tokens already stripped
        float r = 1.0f, g = 1.0f, b = 1.0f;
    };
    struct ConsoleLine
    {
        std::vector<ConsoleSpan> spans;
    };

    inline std::deque<ConsoleLine> Scrollback;     // capped at kMaxScrollback
    inline std::queue<std::string> SubmittedInput; // legacy overlay queue (bypassed)
    inline constexpr size_t kMaxScrollback = 500;
    inline constexpr size_t kMaxInput = 4096; // incl. room for nul; 4095 chars max

    // Registration
    int rconsolecreate(lua_State* L);
    int rconsoledestroy(lua_State* L);
    int rconsoleclear(lua_State* L);
    int rconsoleprint(lua_State* L);
    int rconsoleinfo(lua_State* L);
    int rconsolewarn(lua_State* L);
    int rconsoleerr(lua_State* L);
    int rconsoleinput(lua_State* L);
    int rconsolename(lua_State* L);
    int rconsoleshow(lua_State* L);
    int rconsolehide(lua_State* L);
    int rconsoletoggle(lua_State* L);
    int rconsoletop(lua_State* L);

    // Helpers — conhost (primary Lua path, all guarded, see .cpp).
    // Same @@TOKEN@@ names as latestv3.
    void WriteToConsole(const char* text, WORD color);
    DWORD ParseColorToken(const char*& text, DWORD& currentColor);
    bool EnsureConsole();
    // Helpers — overlay compat (Renderer build only; Lua path bypasses these:
    // no rconsole* Lua function depends on Scrollback/SubmittedInput queues.
    // Retained so Renderer/ui/ui.cpp keeps compiling untouched.)
    // Parses one @@TOKEN@@ at `text`, advancing past it. Returns true when a
    // token was consumed (r/g/b updated), false otherwise (text untouched).
    bool ParseColorToken(const char*& text, float& r, float& g, float& b);
    // Token-parses `text` into spans and appends one line to Scrollback,
    // evicting oldest lines past kMaxScrollback. Brief lock, never blocks,
    // never calls Lua. Safe from any thread, including the Present thread.
    // NOTE: Lua print path does NOT call this (bypassed); Renderer calls it
    // for its own echo + Clear/Copy buttons.
    void AppendColoredLine(const std::string& text, float dr, float dg, float db);
    void ClearScrollback();
    // Overlay draw support (called from Renderer/ui/ui.cpp's draw flow).
    // Shown == explicitly created AND not hidden.
    bool IsConsoleShown();
    bool IsConsoleTopmost();
    std::string GetConsoleTitle();
    // Bounded snapshot copy of Scrollback for the draw thread (mirrors the
    // Output tab's `get_log_info()` copy discipline: no lock held while
    // drawing). At most kMaxScrollback lines.
    size_t CopyScrollback(std::vector<ConsoleLine>& out);
    // Legacy overlay InputText queue (bypassed: rconsoleinput reads conhost
    // via peek-first, never this queue). Kept for build only.
    void SubmitInputLine(const std::string& line);
    // Legacy non-blocking pop (bypassed). Returns false when empty.
    bool PollInputLine(std::string& out);
    // Death path: forget conhost handles without blocking. Called only from
    // DllMain DLL_PROCESS_DETACH. Lock-free, no I/O, no FreeConsole: the OS
    // reclaims memory/handles on process death.
    void OnProcessDetach();

    void register_lib(lua_State* L);
    void ResetLibraryState();
}
