#include "drawingimmediate.hpp"

#include <RBX/environment/env.hpp>
#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

// â”€â”€â”€ Globals â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace DrawingImmediate
{
    std::vector<PaintCallback> g_PaintCallbacks;
    std::mutex g_PaintMutex;

    std::atomic<bool> g_InPaint{ false };
    PaintPaintPaint g_CurrentPaint{};

    int g_ScreenWidth = 0;
    int g_ScreenHeight = 0;

    static const char* kPaintHandleMeta = ("DrawingImmediate_PaintHandle");
    static int g_NextConnId = 1;

    bool IsInPaint()
    {
        return g_InPaint.load(std::memory_order_relaxed);
    }
}

// â”€â”€â”€ Helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace DrawingImmediate
{
    inline ImU32 PackColor(float r, float g, float b, float a)
    {
        auto ch = [](float v) -> int {
            int x = (int)(v * 255.0f + 0.5f);
            return x < 0 ? 0 : (x > 255 ? 255 : x);
        };
        return IM_COL32(ch(r), ch(g), ch(b), ch(a));
    }

    inline bool ReadColor3(lua_State* L, int idx, float& r, float& g, float& b)
    {
        if (lua_isnil(L, idx)) return false;
        idx = lua_absindex(L, idx);
        lua_getfield(L, idx, ("R"));
        lua_getfield(L, idx, ("G"));
        lua_getfield(L, idx, ("B"));
        if (lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1))
        {
            r = (float)lua_tonumber(L, -3);
            g = (float)lua_tonumber(L, -2);
            b = (float)lua_tonumber(L, -1);
            lua_pop(L, 3);
            return true;
        }
        lua_pop(L, 3);
        return false;
    }

    inline bool ReadVector2(lua_State* L, int idx, float& x, float& y)
    {
        if (lua_isnil(L, idx)) return false;
        idx = lua_absindex(L, idx);
        lua_getfield(L, idx, ("X"));
        lua_getfield(L, idx, ("Y"));
        if (lua_isnumber(L, -2) && lua_isnumber(L, -1))
        {
            x = (float)lua_tonumber(L, -2);
            y = (float)lua_tonumber(L, -1);
            lua_pop(L, 2);
            return true;
        }
        lua_pop(L, 2);
        return false;
    }

    inline bool CheckPaint(lua_State* L)
    {
        if (!IsInPaint())
        {
            luaL_error(L, ("drawing function called outside of paint callback"));
            return false;
        }
        return true;
    }

    inline ImDrawList* GetDL()
    {
        return g_CurrentPaint.drawList;
    }

    PaintHandle* GetPaintHandle(lua_State* L, int idx)
    {
        if (!lua_isuserdata(L, idx))
            return nullptr;
        void* ud = lua_touserdata(L, idx);
        if (!ud) return nullptr;
        if (!lua_getmetatable(L, idx))
            return nullptr;
        luaL_getmetatable(L, kPaintHandleMeta);
        bool ok = lua_rawequal(L, -1, -2) != 0;
        lua_pop(L, 2);
        return ok ? static_cast<PaintHandle*>(ud) : nullptr;
    }

    void EnsurePaintHandleMeta(lua_State* L)
    {
        luaL_getmetatable(L, kPaintHandleMeta);
        if (!lua_isnil(L, -1)) { lua_pop(L, 1); return; }
        lua_pop(L, 1);

        luaL_newmetatable(L, kPaintHandleMeta);

        lua_newtable(L);
        lua_pushcfunction(L, L_PH_Connect, ("Connect"));
        lua_setfield(L, -2, ("Connect"));
        lua_pushcfunction(L, L_PH_Disconnect, ("Disconnect"));
        lua_setfield(L, -2, ("Disconnect"));
        lua_pushcfunction(L, L_PH_Clear, ("Clear"));
        lua_setfield(L, -2, ("Clear"));
        lua_setfield(L, -2, ("__index"));

        lua_pushstring(L, ("PaintHandle"));
        lua_setfield(L, -2, ("__type"));
        lua_pushstring(L, ("The metatable is locked"));
        lua_setfield(L, -2, ("__metatable"));

        lua_pop(L, 1);
    }
}

// â”€â”€â”€ PaintHandle methods â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace DrawingImmediate
{
    int L_PH_Connect(lua_State* L)
    {
        PaintHandle* ph = GetPaintHandle(L, 1);
        if (!ph) { luaL_error(L, ("Connect: invalid PaintHandle")); return 0; }

        luaL_checktype(L, 2, LUA_TFUNCTION);

        lua_pushvalue(L, 2);
        int ref = lua_ref(L, LUA_REGISTRYINDEX);

        int id = g_NextConnId++;
        ph->connections.push_back({ id, ref });

        {
            std::lock_guard<std::mutex> lk(g_PaintMutex);
            g_PaintCallbacks.push_back({ ref, ph->zIndex });
        }

        lua_pushinteger(L, id);
        return 1;
    }

    int L_PH_Disconnect(lua_State* L)
    {
        PaintHandle* ph = GetPaintHandle(L, 1);
        if (!ph) { luaL_error(L, ("Disconnect: invalid PaintHandle")); return 0; }

        int id = (int)luaL_checkinteger(L, 2);

        for (auto it = ph->connections.begin(); it != ph->connections.end(); ++it)
        {
            if (it->id == id)
            {
                lua_unref(L, it->ref);

                {
                    std::lock_guard<std::mutex> lk(g_PaintMutex);
                    auto& vec = g_PaintCallbacks;
                    for (auto jt = vec.begin(); jt != vec.end(); ++jt)
                    {
                        if (jt->ref == it->ref && jt->zIndex == ph->zIndex)
                        {
                            vec.erase(jt);
                            break;
                        }
                    }
                }

                ph->connections.erase(it);
                break;
            }
        }

        return 0;
    }

    int L_PH_Clear(lua_State* L)
    {
        PaintHandle* ph = GetPaintHandle(L, 1);
        if (!ph) { luaL_error(L, ("Clear: invalid PaintHandle")); return 0; }

        for (auto& c : ph->connections)
        {
            lua_unref(L, c.ref);

            std::lock_guard<std::mutex> lk(g_PaintMutex);
            auto& vec = g_PaintCallbacks;
            for (auto jt = vec.begin(); jt != vec.end(); ++jt)
            {
                if (jt->ref == c.ref)
                {
                    vec.erase(jt);
                    break;
                }
            }
        }
        ph->connections.clear();

        return 0;
    }
}

// â”€â”€â”€ GetPaint â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace DrawingImmediate
{
    int L_GetPaint(lua_State* L)
    {
        int zIndex = (int)luaL_optinteger(L, 1, 0);

        EnsurePaintHandleMeta(L);

        PaintHandle* ph = static_cast<PaintHandle*>(lua_newuserdata(L, sizeof(PaintHandle)));
        new (ph) PaintHandle();
        ph->zIndex = zIndex;

        luaL_getmetatable(L, kPaintHandleMeta);
        lua_setmetatable(L, -2);

        return 1;
    }
}

// â”€â”€â”€ Drawing Primitives â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace DrawingImmediate
{
    // Line(from: Vector2, to: Vector2, color: Color3, thickness: number?)
    int L_Line(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float x1, y1, x2, y2;
        if (!ReadVector2(L, 1, x1, y1) || !ReadVector2(L, 2, x2, y2))
        {
            luaL_error(L, ("Line: invalid Vector2 arguments"));
            return 0;
        }

        float r = 1, g = 1, b = 1;
        ReadColor3(L, 3, r, g, b);
        float thickness = (float)luaL_optnumber(L, 4, 1.0f);

        GetDL()->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), PackColor(r, g, b, 1.0f), thickness);
        return 0;
    }

    // Circle(position: Vector2, radius: number, color: Color3, thickness: number?, numSides: int?)
    int L_Circle(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float px, py;
        if (!ReadVector2(L, 1, px, py))
        {
            luaL_error(L, ("Circle: invalid Vector2 position"));
            return 0;
        }

        float radius = (float)luaL_checknumber(L, 2);
        float r = 1, g = 1, b = 1;
        ReadColor3(L, 3, r, g, b);
        float thickness = (float)luaL_optnumber(L, 4, 1.0f);
        int segments = (int)luaL_optinteger(L, 5, 24);

        GetDL()->AddCircle(ImVec2(px, py), radius, PackColor(r, g, b, 1.0f), segments, thickness);
        return 0;
    }

    // FilledCircle(position: Vector2, radius: number, color: Color3, numSides: int?)
    int L_FilledCircle(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float px, py;
        if (!ReadVector2(L, 1, px, py))
        {
            luaL_error(L, ("FilledCircle: invalid Vector2 position"));
            return 0;
        }

        float radius = (float)luaL_checknumber(L, 2);
        float r = 1, g = 1, b = 1;
        ReadColor3(L, 3, r, g, b);
        int segments = (int)luaL_optinteger(L, 4, 24);

        GetDL()->AddCircleFilled(ImVec2(px, py), radius, PackColor(r, g, b, 1.0f), segments);
        return 0;
    }

    // Triangle(pointA: Vector2, pointB: Vector2, pointC: Vector2, color: Color3, filled: bool?, thickness: number?)
    int L_Triangle(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float ax, ay, bx, by, cx, cy;
        if (!ReadVector2(L, 1, ax, ay) || !ReadVector2(L, 2, bx, by) || !ReadVector2(L, 3, cx, cy))
        {
            luaL_error(L, ("Triangle: invalid Vector2 arguments"));
            return 0;
        }

        float r = 1, g = 1, b = 1;
        ReadColor3(L, 4, r, g, b);
        bool filled = lua_toboolean(L, 5) != 0;
        float thickness = (float)luaL_optnumber(L, 6, 1.0f);

        if (filled)
            GetDL()->AddTriangleFilled(ImVec2(ax, ay), ImVec2(bx, by), ImVec2(cx, cy), PackColor(r, g, b, 1.0f));
        else
            GetDL()->AddTriangle(ImVec2(ax, ay), ImVec2(bx, by), ImVec2(cx, cy), PackColor(r, g, b, 1.0f), thickness);
        return 0;
    }

    // FilledTriangle(pointA: Vector2, pointB: Vector2, pointC: Vector2, color: Color3)
    int L_FilledTriangle(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float ax, ay, bx, by, cx, cy;
        if (!ReadVector2(L, 1, ax, ay) || !ReadVector2(L, 2, bx, by) || !ReadVector2(L, 3, cx, cy))
        {
            luaL_error(L, ("FilledTriangle: invalid Vector2 arguments"));
            return 0;
        }

        float r = 1, g = 1, b = 1;
        ReadColor3(L, 4, r, g, b);

        GetDL()->AddTriangleFilled(ImVec2(ax, ay), ImVec2(bx, by), ImVec2(cx, cy), PackColor(r, g, b, 1.0f));
        return 0;
    }

    // Rectangle(position: Vector2, size: Vector2, color: Color3, filled: bool?, thickness: number?, rounding: number?)
    int L_Rectangle(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float px, py, sx, sy;
        if (!ReadVector2(L, 1, px, py) || !ReadVector2(L, 2, sx, sy))
        {
            luaL_error(L, ("Rectangle: invalid Vector2 arguments"));
            return 0;
        }

        float r = 1, g = 1, b = 1;
        ReadColor3(L, 3, r, g, b);
        bool filled = lua_toboolean(L, 4) != 0;
        float thickness = (float)luaL_optnumber(L, 5, 1.0f);
        float rounding = (float)luaL_optnumber(L, 6, 0.0f);

        ImVec2 pmin(px, py), pmax(px + sx, py + sy);
        if (filled)
            GetDL()->AddRectFilled(pmin, pmax, PackColor(r, g, b, 1.0f), rounding);
        else
            GetDL()->AddRect(pmin, pmax, PackColor(r, g, b, 1.0f), rounding, 0, thickness);
        return 0;
    }

    // FilledRectangle(position: Vector2, size: Vector2, color: Color3, rounding: number?)
    int L_FilledRectangle(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float px, py, sx, sy;
        if (!ReadVector2(L, 1, px, py) || !ReadVector2(L, 2, sx, sy))
        {
            luaL_error(L, ("FilledRectangle: invalid Vector2 arguments"));
            return 0;
        }

        float r = 1, g = 1, b = 1;
        ReadColor3(L, 3, r, g, b);
        float rounding = (float)luaL_optnumber(L, 4, 0.0f);

        GetDL()->AddRectFilled(ImVec2(px, py), ImVec2(px + sx, py + sy), PackColor(r, g, b, 1.0f), rounding);
        return 0;
    }

    // Quad(pointA-D: Vector2, color: Color3, filled: bool?, thickness: number?)
    int L_Quad(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float ax, ay, bx, by, cx, cy, dx, dy;
        if (!ReadVector2(L, 1, ax, ay) || !ReadVector2(L, 2, bx, by) ||
            !ReadVector2(L, 3, cx, cy) || !ReadVector2(L, 4, dx, dy))
        {
            luaL_error(L, ("Quad: invalid Vector2 arguments"));
            return 0;
        }

        float r = 1, g = 1, b = 1;
        ReadColor3(L, 5, r, g, b);
        bool filled = lua_toboolean(L, 6) != 0;
        float thickness = (float)luaL_optnumber(L, 7, 1.0f);

        if (filled)
            GetDL()->AddQuadFilled(ImVec2(ax, ay), ImVec2(bx, by), ImVec2(cx, cy), ImVec2(dx, dy), PackColor(r, g, b, 1.0f));
        else
            GetDL()->AddQuad(ImVec2(ax, ay), ImVec2(bx, by), ImVec2(cx, cy), ImVec2(dx, dy), PackColor(r, g, b, 1.0f), thickness);
        return 0;
    }

    // FilledQuad(pointA-D: Vector2, color: Color3)
    int L_FilledQuad(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float ax, ay, bx, by, cx, cy, dx, dy;
        if (!ReadVector2(L, 1, ax, ay) || !ReadVector2(L, 2, bx, by) ||
            !ReadVector2(L, 3, cx, cy) || !ReadVector2(L, 4, dx, dy))
        {
            luaL_error(L, ("FilledQuad: invalid Vector2 arguments"));
            return 0;
        }

        float r = 1, g = 1, b = 1;
        ReadColor3(L, 5, r, g, b);

        GetDL()->AddQuadFilled(ImVec2(ax, ay), ImVec2(bx, by), ImVec2(cx, cy), ImVec2(dx, dy), PackColor(r, g, b, 1.0f));
        return 0;
    }

    // DrawingText(position: Vector2, text: string, color: Color3, size: number?)
    int L_Text(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float px, py;
        if (!ReadVector2(L, 1, px, py))
        {
            luaL_error(L, ("DrawingText: invalid Vector2 position"));
            return 0;
        }

        const char* text = luaL_checkstring(L, 2);
        float r = 1, g = 1, b = 1;
        ReadColor3(L, 3, r, g, b);
        float fontSize = (float)luaL_optnumber(L, 4, 14.0f);

        ImFont* font = ImGui::GetIO().Fonts->Fonts.Size > 0 ? ImGui::GetIO().Fonts->Fonts[0] : nullptr;
        if (font)
            GetDL()->AddText(font, fontSize, ImVec2(px, py), PackColor(r, g, b, 1.0f), text);
        else
            GetDL()->AddText(ImVec2(px, py), PackColor(r, g, b, 1.0f), text);
        return 0;
    }

    // OutlinedText(position: Vector2, text: string, color: Color3, outlineColor: Color3, size: number?)
    int L_OutlinedText(lua_State* L)
    {
        if (!CheckPaint(L)) return 0;

        float px, py;
        if (!ReadVector2(L, 1, px, py))
        {
            luaL_error(L, ("OutlinedText: invalid Vector2 position"));
            return 0;
        }

        const char* text = luaL_checkstring(L, 2);

        float r = 1, g = 1, b = 1;
        ReadColor3(L, 3, r, g, b);
        ImU32 col = PackColor(r, g, b, 1.0f);

        float or_ = 0, og = 0, ob = 0;
        ReadColor3(L, 4, or_, og, ob);
        ImU32 ocol = PackColor(or_, og, ob, 1.0f);

        float fontSize = (float)luaL_optnumber(L, 5, 14.0f);
        ImFont* font = ImGui::GetIO().Fonts->Fonts.Size > 0 ? ImGui::GetIO().Fonts->Fonts[0] : nullptr;

        const float ox = 1.0f, oy = 1.0f;

        if (font)
        {
            GetDL()->AddText(font, fontSize, ImVec2(px - ox, py), ocol, text);
            GetDL()->AddText(font, fontSize, ImVec2(px + ox, py), ocol, text);
            GetDL()->AddText(font, fontSize, ImVec2(px, py - oy), ocol, text);
            GetDL()->AddText(font, fontSize, ImVec2(px, py + oy), ocol, text);
            GetDL()->AddText(font, fontSize, ImVec2(px, py), col, text);
        }
        else
        {
            GetDL()->AddText(ImVec2(px - ox, py), ocol, text);
            GetDL()->AddText(ImVec2(px + ox, py), ocol, text);
            GetDL()->AddText(ImVec2(px, py - oy), ocol, text);
            GetDL()->AddText(ImVec2(px, py + oy), ocol, text);
            GetDL()->AddText(ImVec2(px, py), col, text);
        }
        return 0;
    }
}

// â”€â”€â”€ Render / Clear â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace DrawingImmediate
{
    void ClearAll()
    {
        std::lock_guard<std::mutex> lk(g_PaintMutex);
        g_PaintCallbacks.clear();
    }

    void Render(lua_State* L)
    {
        if (!L) return;

        // Snapshot under lock, then release before calling Lua callbacks
        // This prevents deadlock if a callback calls Connect/Disconnect
        std::vector<PaintCallback> snapshot;
        {
            std::lock_guard<std::mutex> lk(g_PaintMutex);
            if (g_PaintCallbacks.empty()) return;
            snapshot = g_PaintCallbacks;
        }

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        if (!dl) return;

        g_ScreenWidth = (int)ImGui::GetIO().DisplaySize.x;
        g_ScreenHeight = (int)ImGui::GetIO().DisplaySize.y;

        // Sort by zIndex
        std::sort(snapshot.begin(), snapshot.end(),
            [](const PaintCallback& a, const PaintCallback& b) { return a.zIndex < b.zIndex; });

        g_InPaint.store(true, std::memory_order_relaxed);

        for (const auto& cb : snapshot)
        {
            if (cb.ref == LUA_NOREF) continue;

            g_CurrentPaint.zIndex = cb.zIndex;
            g_CurrentPaint.drawList = dl;

            lua_rawgeti(L, LUA_REGISTRYINDEX, cb.ref);
            if (lua_isfunction(L, -1))
            {
                if (lua_pcall(L, 0, 0, 0) != 0)
                    lua_pop(L, 1);
            }
            else
            {
                lua_pop(L, 1);
            }
        }

        g_InPaint.store(false, std::memory_order_relaxed);
    }
}

// â”€â”€â”€ Registration â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace DrawingImmediate
{
    static int g_FuncNameInc = 0;

    void register_lib(lua_State* L)
    {
        g_FuncNameInc = 0;

        // Create DrawingImmediate table
        lua_newtable(L);

        Environment->make_table_function(L, ("GetPaint"), L_GetPaint, g_FuncNameInc++);

        Environment->make_table_function(L, ("Line"), L_Line, g_FuncNameInc++);
        Environment->make_table_function(L, ("Circle"), L_Circle, g_FuncNameInc++);
        Environment->make_table_function(L, ("FilledCircle"), L_FilledCircle, g_FuncNameInc++);
        Environment->make_table_function(L, ("Triangle"), L_Triangle, g_FuncNameInc++);
        Environment->make_table_function(L, ("FilledTriangle"), L_FilledTriangle, g_FuncNameInc++);
        Environment->make_table_function(L, ("Rectangle"), L_Rectangle, g_FuncNameInc++);
        Environment->make_table_function(L, ("FilledRectangle"), L_FilledRectangle, g_FuncNameInc++);
        Environment->make_table_function(L, ("Quad"), L_Quad, g_FuncNameInc++);
        Environment->make_table_function(L, ("FilledQuad"), L_FilledQuad, g_FuncNameInc++);
        Environment->make_table_function(L, ("Text"), L_Text, g_FuncNameInc++);
        Environment->make_table_function(L, ("OutlinedText"), L_OutlinedText, g_FuncNameInc++);

        // Fonts sub-table
        lua_newtable(L);
        lua_pushinteger(L, 0); lua_setfield(L, -2, ("Default"));
        lua_pushinteger(L, 1); lua_setfield(L, -2, ("SansSerif"));
        lua_pushinteger(L, 2); lua_setfield(L, -2, ("Monospace"));
        lua_pushinteger(L, 3); lua_setfield(L, -2, ("UI"));
        lua_pushinteger(L, 4); lua_setfield(L, -2, ("Code"));
        lua_setfield(L, -2, ("Fonts"));

        lua_setglobal(L, ("DrawingImmediate"));
    }
}
