#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include <imgui.h>
#include <lstate.h>
#include <lualib.h>

namespace DrawingImmediate
{
    // ── Paint callback entry ──────────────────────────────────
    struct PaintCallback
    {
        int ref = LUA_NOREF;        // lua registry ref to callback function
        int zIndex = 0;
    };

    // ── Per-frame paint context ───────────────────────────────
    struct PaintPaintPaint
    {
        int zIndex = 0;
        ImDrawList* drawList = nullptr;
    };

    // ── PaintHandle userdata ──────────────────────────────────
    struct PaintHandle
    {
        int zIndex = 0;
        struct Conn { int id; int ref; };
        std::vector<Conn> connections;
    };

    // ── Globals ───────────────────────────────────────────────
    extern std::vector<PaintCallback> g_PaintCallbacks;
    extern std::mutex g_PaintMutex;

    extern std::atomic<bool> g_InPaint;
    extern PaintPaintPaint g_CurrentPaint;

    extern int g_ScreenWidth;
    extern int g_ScreenHeight;

    // ── Public ────────────────────────────────────────────────
    bool IsInPaint();
    void Render(lua_State* L);
    void ClearAll();

    // ── PaintHandle metatable methods ────────────────────────
    int L_PH_Connect(lua_State* L);
    int L_PH_Disconnect(lua_State* L);
    int L_PH_Clear(lua_State* L);

    // ── Lua API: GetPaint ────────────────────────────────────
    int L_GetPaint(lua_State* L);

    // ── Drawing primitives (only valid inside paint callback) ─
    int L_Line(lua_State* L);
    int L_Circle(lua_State* L);
    int L_FilledCircle(lua_State* L);
    int L_Triangle(lua_State* L);
    int L_FilledTriangle(lua_State* L);
    int L_Rectangle(lua_State* L);
    int L_FilledRectangle(lua_State* L);
    int L_Quad(lua_State* L);
    int L_FilledQuad(lua_State* L);
    int L_Text(lua_State* L);
    int L_OutlinedText(lua_State* L);

    void register_lib(lua_State* L);
}
