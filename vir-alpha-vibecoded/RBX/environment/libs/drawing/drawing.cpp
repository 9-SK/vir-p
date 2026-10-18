#include "Drawing.hpp"

#include <RBX/environment/env.hpp>
#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>
#include <RBX/execution/execution.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui.h>


// â”€â”€â”€ Global Definitions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace Drawing
{
    UploadImageFn  g_UploadImageFn = nullptr;
    ReleaseImageFn g_ReleaseImageFn = nullptr;
    FetchUriFn     g_FetchUriFn = nullptr;

    std::recursive_mutex g_mutex;
    std::vector<ObjectRef> g_objects;
    std::unordered_map<std::uint64_t, ObjectRef> g_lookup;

    float g_dpiScale = 1.0f;

    ImFont* g_Fonts[4] = { nullptr, nullptr, nullptr, nullptr };

    std::mutex g_pendingMutex;
    std::vector<PendingImage> g_pending;

    std::atomic<std::uint64_t> g_nextId{ 1 };

    const char* kMetaName = ("ShitaroDrawing");
}

// â”€â”€â”€ Internal Helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace Drawing
{
    // Internal helpers and implementation

    bool ImGuiReady()
    {
        if (!ImGui::GetCurrentContext())
            return false;
        ImGuiIO& io = ImGui::GetIO();
        return io.Fonts != nullptr && io.Fonts->IsBuilt() && io.Fonts->Fonts.Size > 0;
    }

    ImFont* PickFont(int idx)
    {
        if (!ImGuiReady()) return nullptr;
        if (idx < 0 || idx > 3) idx = 0;
        if (g_Fonts[idx] && g_Fonts[idx]->IsLoaded()) return g_Fonts[idx];
        return ImGui::GetIO().Fonts->Fonts[0];
    }

    void RecalcTextBounds(Object* o)
    {
        if (!o || o->kind != Kind::Text)
            return;
        if (o->text.empty())
        {
            o->textBoundsX = 0.0f;
            o->textBoundsY = o->textSize;
            return;
        }
        ImFont* f = PickFont(o->font);
        if (!f)
        {
            o->textBoundsX = (float)o->text.size() * o->textSize * 0.55f;
            o->textBoundsY = o->textSize;
            return;
        }
        float sz = o->textSize < 1.0f ? 1.0f : o->textSize;
        sz *= (g_dpiScale > 0.0f ? g_dpiScale : 1.0f);
        ImVec2 v = f->CalcTextSizeA(sz, FLT_MAX, 0.0f, o->text.c_str());
        o->textBoundsX = v.x;
        o->textBoundsY = v.y;
    }

    int ParseKind(const char* s)
    {
        if (!s) return (int)Kind::Invalid;
        if (!strcmp(s, ("Line"))) return (int)Kind::Line;
        if (!strcmp(s, ("Text"))) return (int)Kind::Text;
        if (!strcmp(s, ("Circle"))) return (int)Kind::Circle;
        if (!strcmp(s, ("Square"))) return (int)Kind::Square;
        if (!strcmp(s, ("Triangle"))) return (int)Kind::Triangle;
        if (!strcmp(s, ("Quad"))) return (int)Kind::Quad;
        if (!strcmp(s, ("Image"))) return (int)Kind::Image;
        return (int)Kind::Invalid;
    }

    const char* KindName(Kind k)
    {
        switch (k)
        {
        case Kind::Line: return ("Line");
        case Kind::Text: return ("Text");
        case Kind::Circle: return ("Circle");
        case Kind::Square: return ("Square");
        case Kind::Triangle: return ("Triangle");
        case Kind::Quad: return ("Quad");
        case Kind::Image: return ("Image");
        default: return ("Invalid");
        }
    }

    void PushColor3(lua_State* L, float r, float g, float b)
    {
        lua_getglobal(L, ("Color3"));
        if (!lua_istable(L, -1) && !lua_isuserdata(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushnumber(L, r); lua_setfield(L, -2, ("R"));
            lua_pushnumber(L, g); lua_setfield(L, -2, ("G"));
            lua_pushnumber(L, b); lua_setfield(L, -2, ("B"));
            return;
        }
        lua_getfield(L, -1, ("new"));
        lua_remove(L, -2);
        lua_pushnumber(L, r);
        lua_pushnumber(L, g);
        lua_pushnumber(L, b);
        if (lua_pcall(L, 3, 1, 0) != 0)
        {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
    }

    void PushVector2(lua_State* L, float x, float y)
    {
        lua_getglobal(L, ("Vector2"));
        if (!lua_istable(L, -1) && !lua_isuserdata(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushnumber(L, x); lua_setfield(L, -2, ("X"));
            lua_pushnumber(L, y); lua_setfield(L, -2, ("Y"));
            return;
        }
        lua_getfield(L, -1, ("new"));
        lua_remove(L, -2);
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        if (lua_pcall(L, 2, 1, 0) != 0)
        {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
    }

    bool ReadColor3(lua_State* L, int idx, float& r, float& g, float& b)
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

    bool ReadVector2(lua_State* L, int idx, float& x, float& y)
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

    bool IsDrawingUdata(lua_State* L, int idx)
    {
        if (!lua_isuserdata(L, idx))
            return false;
        if (!lua_getmetatable(L, idx))
            return false;
        luaL_getmetatable(L, kMetaName);
        int eq = lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
        return eq != 0;
    }

    ObjectRef ResolveObject(lua_State* L, int idx)
    {
        if (!IsDrawingUdata(L, idx))
            return nullptr;

        UdataPayload* p = static_cast<UdataPayload*>(lua_touserdata(L, idx));
        if (!p)
            return nullptr;

        std::lock_guard<std::recursive_mutex> lk(g_mutex);
        auto it = g_lookup.find(p->id);
        if (it == g_lookup.end())
            return nullptr;
        return it->second;
    }

    void ReleaseTexture(Object* o)
    {
        if (!o || !o->texSrv) return;
        if (g_ReleaseImageFn) g_ReleaseImageFn(o->texSrv);
        o->texSrv = nullptr;
        o->texW = 0;
        o->texH = 0;
    }

    void EraseObject(const ObjectRef& target)
    {
        if (!target)
            return;

        std::lock_guard<std::recursive_mutex> lk(g_mutex);

        const std::size_t slot = target->slot;
        if (slot < g_objects.size() && g_objects[slot] == target)
        {
            g_objects[slot] = g_objects.back();
            g_objects[slot]->slot = slot;
            g_objects.pop_back();
        }

        ReleaseTexture(target.get());
        target->destroyed = true;
        target->visible = false;
        target->text.clear();
        target->text.shrink_to_fit();
        target->imageData.clear();
        target->imageData.shrink_to_fit();
        target->imageUri.clear();
        target->imageUri.shrink_to_fit();

        g_lookup.erase(target->id);
    }

    void EnsureMetatable(lua_State* L)
    {
        luaL_getmetatable(L, kMetaName);
        if (!lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            return;
        }
        lua_pop(L, 1);

        luaL_newmetatable(L, kMetaName);

        lua_pushcfunction(L, Drawing::L_Index, ("__index"));
        lua_setfield(L, -2, ("__index"));

        lua_pushcfunction(L, Drawing::L_NewIndex, ("__newindex"));
        lua_setfield(L, -2, ("__newindex"));

        lua_pushcfunction(L, Drawing::L_ToString, ("__tostring"));
        lua_setfield(L, -2, ("__tostring"));

        lua_pushstring(L, ("Drawing"));
        lua_setfield(L, -2, ("__type"));

        lua_pushstring(L, ("The metatable is locked"));
        lua_setfield(L, -2, ("__metatable"));

        lua_pop(L, 1);
    }

    int PushProperty(lua_State* L, const ObjectRef& ref, const char* key)
    {
        if (!strcmp(key, ("Remove")) || !strcmp(key, ("Destroy")))
        {
            lua_pushcfunction(L, Drawing::L_Destroy, key);
            return 1;
        }

        if (!ref)
        {
            if (!strcmp(key, ("__OBJECT_EXISTS"))) { lua_pushboolean(L, 0); return 1; }
            lua_pushnil(L);
            return 1;
        }

        std::lock_guard<std::recursive_mutex> lk(g_mutex);
        Object* o = ref.get();

        if (!strcmp(key, ("Visible"))) { lua_pushboolean(L, o->visible); return 1; }
        if (!strcmp(key, ("Transparency"))) { lua_pushnumber(L, o->transparency); return 1; }
        if (!strcmp(key, ("ZIndex"))) { lua_pushinteger(L, o->zindex); return 1; }
        if (!strcmp(key, ("Thickness"))) { lua_pushnumber(L, o->thickness); return 1; }
        if (!strcmp(key, ("Color"))) { PushColor3(L, o->r, o->g, o->b); return 1; }
        if (!strcmp(key, ("__OBJECT_EXISTS"))) { lua_pushboolean(L, !o->destroyed); return 1; }
        if (!strcmp(key, ("Class")) || !strcmp(key, ("ClassName"))) { lua_pushstring(L, KindName(o->kind)); return 1; }

        switch (o->kind)
        {
        case Kind::Line:
            if (!strcmp(key, ("From"))) { PushVector2(L, o->fromX, o->fromY); return 1; }
            if (!strcmp(key, ("To"))) { PushVector2(L, o->toX, o->toY); return 1; }
            break;
        case Kind::Text:
            if (!strcmp(key, ("Text"))) { lua_pushlstring(L, o->text.data(), o->text.size()); return 1; }
            if (!strcmp(key, ("Size"))) { lua_pushnumber(L, o->textSize); return 1; }
            if (!strcmp(key, ("Center"))) { lua_pushboolean(L, o->center); return 1; }
            if (!strcmp(key, ("Outline"))) { lua_pushboolean(L, o->outline); return 1; }
            if (!strcmp(key, ("OutlineColor"))) { PushColor3(L, o->outR, o->outG, o->outB); return 1; }
            if (!strcmp(key, ("Position"))) { PushVector2(L, o->posX, o->posY); return 1; }
            if (!strcmp(key, ("Font"))) { lua_pushinteger(L, o->font); return 1; }
            if (!strcmp(key, ("TextBounds")))
            {
                RecalcTextBounds(o);
                PushVector2(L, o->textBoundsX, o->textBoundsY);
                return 1;
            }
            break;
        case Kind::Circle:
            if (!strcmp(key, ("Position"))) { PushVector2(L, o->posX, o->posY); return 1; }
            if (!strcmp(key, ("Radius"))) { lua_pushnumber(L, o->radius); return 1; }
            if (!strcmp(key, ("NumSides"))) { lua_pushinteger(L, o->numSides); return 1; }
            if (!strcmp(key, ("Filled"))) { lua_pushboolean(L, o->filled); return 1; }
            break;
        case Kind::Square:
            if (!strcmp(key, ("Position"))) { PushVector2(L, o->posX, o->posY); return 1; }
            if (!strcmp(key, ("Size"))) { PushVector2(L, o->sizeX, o->sizeY); return 1; }
            if (!strcmp(key, ("Filled"))) { lua_pushboolean(L, o->filled); return 1; }
            break;
        case Kind::Triangle:
            if (!strcmp(key, ("PointA"))) { PushVector2(L, o->pAx, o->pAy); return 1; }
            if (!strcmp(key, ("PointB"))) { PushVector2(L, o->pBx, o->pBy); return 1; }
            if (!strcmp(key, ("PointC"))) { PushVector2(L, o->pCx, o->pCy); return 1; }
            if (!strcmp(key, ("Filled"))) { lua_pushboolean(L, o->filled); return 1; }
            break;
        case Kind::Quad:
            if (!strcmp(key, ("PointA"))) { PushVector2(L, o->pAx, o->pAy); return 1; }
            if (!strcmp(key, ("PointB"))) { PushVector2(L, o->pBx, o->pBy); return 1; }
            if (!strcmp(key, ("PointC"))) { PushVector2(L, o->pCx, o->pCy); return 1; }
            if (!strcmp(key, ("PointD"))) { PushVector2(L, o->pDx, o->pDy); return 1; }
            if (!strcmp(key, ("Filled"))) { lua_pushboolean(L, o->filled); return 1; }
            break;
        case Kind::Image:
            if (!strcmp(key, ("Position"))) { PushVector2(L, o->posX, o->posY); return 1; }
            if (!strcmp(key, ("Size"))) { PushVector2(L, o->sizeX, o->sizeY); return 1; }
            if (!strcmp(key, ("Rounding"))) { lua_pushnumber(L, o->rounding); return 1; }
            if (!strcmp(key, ("Rotation"))) { lua_pushnumber(L, o->rotation); return 1; }
            if (!strcmp(key, ("Data"))) { lua_pushlstring(L, o->imageData.data(), o->imageData.size()); return 1; }
            if (!strcmp(key, ("Uri")) || !strcmp(key, ("Url"))) { lua_pushlstring(L, o->imageUri.data(), o->imageUri.size()); return 1; }
            break;
        default: break;
        }

        lua_pushnil(L);
        return 1;
    }

    void AssignProperty(lua_State* L, const ObjectRef& ref, const char* key, int vidx)
    {
        if (!ref || ref->destroyed)
            return;

        vidx = lua_absindex(L, vidx);

        std::lock_guard<std::recursive_mutex> lk(g_mutex);
        Object* o = ref.get();

        if (!strcmp(key, ("Visible"))) { o->visible = lua_toboolean(L, vidx) != 0; return; }
        if (!strcmp(key, ("Transparency"))) { o->transparency = (float)luaL_checknumber(L, vidx); return; }
        if (!strcmp(key, ("ZIndex"))) { o->zindex = (int)luaL_checkinteger(L, vidx); return; }
        if (!strcmp(key, ("Thickness"))) { o->thickness = (float)luaL_checknumber(L, vidx); return; }
        if (!strcmp(key, ("Color")))
        {
            float r = o->r, g = o->g, b = o->b;
            if (ReadColor3(L, vidx, r, g, b))
            {
                o->r = r; o->g = g; o->b = b;
            }
            return;
        }

        switch (o->kind)
        {
        case Kind::Line:
            if (!strcmp(key, ("From"))) { ReadVector2(L, vidx, o->fromX, o->fromY); return; }
            if (!strcmp(key, ("To"))) { ReadVector2(L, vidx, o->toX, o->toY); return; }
            break;
        case Kind::Text:
            if (!strcmp(key, ("Text")))
            {
                size_t n = 0;
                const char* s = luaL_checklstring(L, vidx, &n);
                o->text.assign(s ? s : (""), n);
                return;
            }
            if (!strcmp(key, ("Size")))
            {
                o->textSize = (float)luaL_checknumber(L, vidx);
                return;
            }
            if (!strcmp(key, ("Center"))) { o->center = lua_toboolean(L, vidx) != 0; return; }
            if (!strcmp(key, ("Outline"))) { o->outline = lua_toboolean(L, vidx) != 0; return; }
            if (!strcmp(key, ("OutlineColor")))
            {
                float r = o->outR, g = o->outG, b = o->outB;
                if (ReadColor3(L, vidx, r, g, b))
                {
                    o->outR = r; o->outG = g; o->outB = b;
                }
                return;
            }
            if (!strcmp(key, ("Position"))) { ReadVector2(L, vidx, o->posX, o->posY); return; }
            if (!strcmp(key, ("Font")))
            {
                int f = (int)luaL_checkinteger(L, vidx);
                if (f < 0) f = 0;
                if (f > 3) f = 3;
                o->font = f;
                return;
            }
            break;
        case Kind::Circle:
            if (!strcmp(key, ("Position"))) { ReadVector2(L, vidx, o->posX, o->posY); return; }
            if (!strcmp(key, ("Radius"))) { o->radius = (float)luaL_checknumber(L, vidx); return; }
            if (!strcmp(key, ("NumSides")))
            {
                int n = (int)luaL_checkinteger(L, vidx);
                if (n < 3) n = 3;
                if (n > 128) n = 128;
                o->numSides = n;
                return;
            }
            if (!strcmp(key, ("Filled"))) { o->filled = lua_toboolean(L, vidx) != 0; return; }
            break;
        case Kind::Square:
            if (!strcmp(key, ("Position"))) { ReadVector2(L, vidx, o->posX, o->posY); return; }
            if (!strcmp(key, ("Size"))) { ReadVector2(L, vidx, o->sizeX, o->sizeY); return; }
            if (!strcmp(key, ("Filled"))) { o->filled = lua_toboolean(L, vidx) != 0; return; }
            break;
        case Kind::Triangle:
            if (!strcmp(key, ("PointA"))) { ReadVector2(L, vidx, o->pAx, o->pAy); return; }
            if (!strcmp(key, ("PointB"))) { ReadVector2(L, vidx, o->pBx, o->pBy); return; }
            if (!strcmp(key, ("PointC"))) { ReadVector2(L, vidx, o->pCx, o->pCy); return; }
            if (!strcmp(key, ("Filled"))) { o->filled = lua_toboolean(L, vidx) != 0; return; }
            break;
        case Kind::Quad:
            if (!strcmp(key, ("PointA"))) { ReadVector2(L, vidx, o->pAx, o->pAy); return; }
            if (!strcmp(key, ("PointB"))) { ReadVector2(L, vidx, o->pBx, o->pBy); return; }
            if (!strcmp(key, ("PointC"))) { ReadVector2(L, vidx, o->pCx, o->pCy); return; }
            if (!strcmp(key, ("PointD"))) { ReadVector2(L, vidx, o->pDx, o->pDy); return; }
            if (!strcmp(key, ("Filled"))) { o->filled = lua_toboolean(L, vidx) != 0; return; }
            break;
        case Kind::Image:
            if (!strcmp(key, ("Position"))) { ReadVector2(L, vidx, o->posX, o->posY); return; }
            if (!strcmp(key, ("Size"))) { ReadVector2(L, vidx, o->sizeX, o->sizeY); return; }
            if (!strcmp(key, ("Rounding"))) { o->rounding = (float)luaL_checknumber(L, vidx); return; }
            if (!strcmp(key, ("Rotation"))) { o->rotation = (float)luaL_checknumber(L, vidx); return; }
            if (!strcmp(key, ("Data")))
            {
                size_t n = 0;
                const char* s = luaL_checklstring(L, vidx, &n);
                o->imageData.assign(s ? s : (""), n);
                o->texDirty = true;
                return;
            }
            if (!strcmp(key, ("Uri")) || !strcmp(key, ("Url")))
            {
                size_t n = 0;
                const char* s = luaL_checklstring(L, vidx, &n);
                o->imageUri.assign(s ? s : (""), n);
                if (g_FetchUriFn && !o->imageUri.empty())
                    g_FetchUriFn(o, o->imageUri.c_str());
                return;
            }
            break;
        default: break;
        }
    }

    bool Finite2(float x, float y)
    {
        return std::isfinite(x) && std::isfinite(y);
    }

    bool Culled(float minX, float minY, float maxX, float maxY, const ImVec2& display)
    {
        const float pad = 128.0f;
        if (maxX < -pad || maxY < -pad) return true;
        if (minX > display.x + pad || minY > display.y + pad) return true;
        return false;
    }

    ImU32 PackColor(float r, float g, float b, float a)
    {
        auto channel = [](float v) -> int {
            int x = (int)(v * 255.0f + 0.5f);
            if (x < 0) x = 0;
            if (x > 255) x = 255;
            return x;
            };
        return IM_COL32(channel(r), channel(g), channel(b), channel(a));
    }
}

// â”€â”€â”€ Drawing Public Interface â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

namespace Drawing
{
    void ClearAll()
    {
        {
            std::lock_guard<std::mutex> lk(g_pendingMutex);
            g_pending.clear();
        }

        std::lock_guard<std::recursive_mutex> lk(g_mutex);
        for (auto& u : g_objects)
        {
            if (!u) continue;
            ReleaseTexture(u.get());
            u->destroyed = true;
            u->visible = false;
        }
        g_objects.clear();
        g_lookup.clear();
    }

    void DrainPendingImages()
    {
        std::vector<PendingImage> local;
        {
            std::lock_guard<std::mutex> lk(g_pendingMutex);
            if (g_pending.empty()) return;
            local = std::move(g_pending);
            g_pending.clear();
        }

        std::lock_guard<std::recursive_mutex> lk(g_mutex);
        for (auto& p : local)
        {
            auto it = g_lookup.find(p.id);
            if (it == g_lookup.end() || !it->second) continue;
            Object* o = it->second.get();
            if (o->destroyed || o->kind != Kind::Image) continue;
            o->imageData = std::move(p.data);
            o->texDirty = true;
        }
    }

    void PushPendingImage(std::uint64_t id, std::string bytes)
    {
        if (bytes.empty()) return;
        std::lock_guard<std::mutex> lk(g_pendingMutex);
        g_pending.push_back({ id, std::move(bytes) });
    }

    // â”€â”€â”€ Lua Function Implementations â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    int L_New(lua_State* L)
    {
        const char* clsName = luaL_checkstring(L, 1);
        int kv = ParseKind(clsName);
        if (kv == (int)Kind::Invalid)
            luaL_error(L, ("invalid drawing class '%s'"), clsName ? clsName : (""));

        ObjectRef obj = std::make_shared<Object>();
        obj->id = g_nextId.fetch_add(1, std::memory_order_relaxed);
        obj->kind = (Kind)kv;

        {
            std::lock_guard<std::recursive_mutex> lk(g_mutex);
            obj->slot = g_objects.size();
            g_objects.push_back(obj);
            g_lookup.emplace(obj->id, obj);
        }

        EnsureMetatable(L);

        UdataPayload* pd = static_cast<UdataPayload*>(lua_newuserdata(L, sizeof(UdataPayload)));
        pd->id = obj->id;

        luaL_getmetatable(L, kMetaName);
        lua_setmetatable(L, -2);

        return 1;
    }

    int L_Destroy(lua_State* L)
    {
        ObjectRef o = ResolveObject(L, 1);
        if (o)
            EraseObject(o);
        return 0;
    }

    int L_ToString(lua_State* L)
    {
        ObjectRef o = ResolveObject(L, 1);
        if (!o) { lua_pushstring(L, ("Drawing<destroyed>")); return 1; }
        lua_pushfstring(L, ("Drawing<%s>"), KindName(o->kind));
        return 1;
    }

    int L_Index(lua_State* L)
    {
        const char* key = luaL_checkstring(L, 2);
        return PushProperty(L, ResolveObject(L, 1), key);
    }

    int L_NewIndex(lua_State* L)
    {
        const char* key = luaL_checkstring(L, 2);
        AssignProperty(L, ResolveObject(L, 1), key, 3);
        return 0;
    }

    int L_ClearCache(lua_State* L)
    {
        (void)L;
        ClearAll();
        return 0;
    }

    int L_IsRenderObj(lua_State* L)
    {
        lua_pushboolean(L, IsDrawingUdata(L, 1) ? 1 : 0);
        return 1;
    }

    int L_GetRenderProperty(lua_State* L)
    {
        const char* key = luaL_checkstring(L, 2);
        return PushProperty(L, ResolveObject(L, 1), key);
    }

    int L_SetRenderProperty(lua_State* L)
    {
        const char* key = luaL_checkstring(L, 2);
        AssignProperty(L, ResolveObject(L, 1), key, 3);
        return 0;
    }

    // â”€â”€â”€ Render All â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    void RenderAll(ImDrawList* dl)
    {
        if (!dl || !ImGuiReady())
            return;

        DrainPendingImages();

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x <= 0.0f || display.y <= 0.0f)
            return;

        const float s = g_dpiScale > 0.0f ? g_dpiScale : 1.0f;

        static std::vector<Object*> ordered;
        ordered.clear();

        std::lock_guard<std::recursive_mutex> lk(g_mutex);

        if (g_objects.empty())
            return;

        ordered.reserve(g_objects.size());
        for (auto& u : g_objects)
            if (u && u->visible && !u->destroyed)
                ordered.push_back(u.get());

        if (ordered.empty())
            return;

        std::sort(ordered.begin(), ordered.end(), [](const Object* a, const Object* b) {
            if (a->zindex != b->zindex) return a->zindex < b->zindex;
            return a->id < b->id;
            });

        for (Object* o : ordered)
        {
            float alpha = o->transparency;
            if (!std::isfinite(alpha)) continue;
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;
            if (alpha <= 0.0f) continue;

            const ImU32 col = PackColor(o->r, o->g, o->b, alpha);
            const float thickness = (o->thickness < 1.0f || !std::isfinite(o->thickness)) ? 1.0f : o->thickness;

            switch (o->kind)
            {
            case Kind::Line:
            {
                if (!Finite2(o->fromX, o->fromY) || !Finite2(o->toX, o->toY)) break;
                const float fx = o->fromX * s, fy = o->fromY * s;
                const float tx = o->toX * s, ty = o->toY * s;
                if (Culled((std::min)(fx, tx), (std::min)(fy, ty),
                    (std::max)(fx, tx), (std::max)(fy, ty), display)) break;
                dl->AddLine(ImVec2(fx, fy), ImVec2(tx, ty), col, thickness * s);
                break;
            }
            case Kind::Text:
            {
                if (o->text.empty()) break;
                if (!Finite2(o->posX, o->posY)) break;

                ImFont* font = PickFont(o->font);
                if (!font) break;

                float renderSize = o->textSize * s;
                if (!std::isfinite(renderSize) || renderSize < 1.0f) renderSize = 1.0f;
                if (renderSize > 128.0f) renderSize = 128.0f;

                const ImVec2 measured = font->CalcTextSizeA(renderSize, FLT_MAX, 0.0f, o->text.c_str());
                o->textBoundsX = measured.x;
                o->textBoundsY = measured.y;

                const float bx = o->posX * s, by = o->posY * s;
                float px = o->center ? (bx - measured.x * 0.5f) : bx;
                float py = by;
                if (Culled(px, py, px + measured.x, py + measured.y, display)) break;

                px = floorf(px + 0.5f);
                py = floorf(py + 0.5f);

                if (o->outline)
                {
                    const ImU32 oc = PackColor(o->outR, o->outG, o->outB, alpha);
                    const float ox = 1.0f * s;
                    dl->AddText(font, renderSize, ImVec2(px - ox, py), oc, o->text.c_str());
                    dl->AddText(font, renderSize, ImVec2(px + ox, py), oc, o->text.c_str());
                    dl->AddText(font, renderSize, ImVec2(px, py - ox), oc, o->text.c_str());
                    dl->AddText(font, renderSize, ImVec2(px, py + ox), oc, o->text.c_str());
                }
                dl->AddText(font, renderSize, ImVec2(px, py), col, o->text.c_str());
                break;
            }
            case Kind::Circle:
            {
                if (!Finite2(o->posX, o->posY) || !std::isfinite(o->radius)) break;
                if (o->radius <= 0.0f) break;
                const float cx = o->posX * s, cy = o->posY * s;
                const float rad = o->radius * s;
                if (Culled(cx - rad, cy - rad, cx + rad, cy + rad, display)) break;

                int n = o->numSides;
                if (n < 3) n = 3;
                if (n > 128) n = 128;

                if (o->filled)
                    dl->AddCircleFilled(ImVec2(cx, cy), rad, col, n);
                else
                    dl->AddCircle(ImVec2(cx, cy), rad, col, n, thickness * s);
                break;
            }
            case Kind::Square:
            {
                if (!Finite2(o->posX, o->posY) || !Finite2(o->sizeX, o->sizeY)) break;
                const ImVec2 a(o->posX * s, o->posY * s);
                const ImVec2 b((o->posX + o->sizeX) * s, (o->posY + o->sizeY) * s);
                if (Culled((std::min)(a.x, b.x), (std::min)(a.y, b.y),
                    (std::max)(a.x, b.x), (std::max)(a.y, b.y), display)) break;

                if (o->filled)
                    dl->AddRectFilled(a, b, col);
                else
                    dl->AddRect(a, b, col, 0.0f, 0, thickness * s);
                break;
            }
            case Kind::Triangle:
            {
                if (!Finite2(o->pAx, o->pAy) || !Finite2(o->pBx, o->pBy) || !Finite2(o->pCx, o->pCy)) break;
                const float minX = (std::min)(o->pAx, (std::min)(o->pBx, o->pCx)) * s;
                const float minY = (std::min)(o->pAy, (std::min)(o->pBy, o->pCy)) * s;
                const float maxX = (std::max)(o->pAx, (std::max)(o->pBx, o->pCx)) * s;
                const float maxY = (std::max)(o->pAy, (std::max)(o->pBy, o->pCy)) * s;
                if (Culled(minX, minY, maxX, maxY, display)) break;

                const ImVec2 A(o->pAx * s, o->pAy * s), B(o->pBx * s, o->pBy * s), C(o->pCx * s, o->pCy * s);
                if (o->filled)
                    dl->AddTriangleFilled(A, B, C, col);
                else
                    dl->AddTriangle(A, B, C, col, thickness * s);
                break;
            }
            case Kind::Quad:
            {
                if (!Finite2(o->pAx, o->pAy) || !Finite2(o->pBx, o->pBy)
                    || !Finite2(o->pCx, o->pCy) || !Finite2(o->pDx, o->pDy)) break;
                const float minX = (std::min)((std::min)(o->pAx, o->pBx), (std::min)(o->pCx, o->pDx)) * s;
                const float minY = (std::min)((std::min)(o->pAy, o->pBy), (std::min)(o->pCy, o->pDy)) * s;
                const float maxX = (std::max)((std::max)(o->pAx, o->pBx), (std::max)(o->pCx, o->pDx)) * s;
                const float maxY = (std::max)((std::max)(o->pAy, o->pBy), (std::max)(o->pCy, o->pDy)) * s;
                if (Culled(minX, minY, maxX, maxY, display)) break;

                const ImVec2 A(o->pAx * s, o->pAy * s), B(o->pBx * s, o->pBy * s),
                    C(o->pCx * s, o->pCy * s), D(o->pDx * s, o->pDy * s);
                if (o->filled)
                    dl->AddQuadFilled(A, B, C, D, col);
                else
                    dl->AddQuad(A, B, C, D, col, thickness * s);
                break;
            }
            case Kind::Image:
            {
                if (!Finite2(o->posX, o->posY) || !Finite2(o->sizeX, o->sizeY)) break;
                if (o->sizeX <= 0.0f || o->sizeY <= 0.0f) break;

                const ImVec2 a(o->posX * s, o->posY * s);
                const ImVec2 b((o->posX + o->sizeX) * s, (o->posY + o->sizeY) * s);
                if (Culled(a.x, a.y, b.x, b.y, display)) break;

                if (o->texDirty && g_UploadImageFn && !o->imageData.empty())
                {
                    if (o->texSrv && g_ReleaseImageFn)
                        g_ReleaseImageFn(o->texSrv);
                    int w = 0, h = 0;
                    o->texSrv = g_UploadImageFn(o->imageData.data(), o->imageData.size(), &w, &h);
                    o->texW = w;
                    o->texH = h;
                    o->texDirty = false;
                }

                ImU32 tint = PackColor(1.0f, 1.0f, 1.0f, alpha);
                if (o->r != 1.0f || o->g != 1.0f || o->b != 1.0f)
                    tint = PackColor(o->r, o->g, o->b, alpha);

                if (o->texSrv)
                {
                    ImTextureID tid = (ImTextureID)o->texSrv;
                    if (o->rotation != 0.0f && std::isfinite(o->rotation))
                    {
                        const float cx = (a.x + b.x) * 0.5f;
                        const float cy = (a.y + b.y) * 0.5f;
                        const float rad = o->rotation * 3.14159265358979323846f / 180.0f;
                        const float cs = cosf(rad);
                        const float sn = sinf(rad);
                        auto rot = [&](float x, float y) -> ImVec2 {
                            const float dx = x - cx;
                            const float dy = y - cy;
                            return ImVec2(cx + dx * cs - dy * sn, cy + dx * sn + dy * cs);
                            };
                        dl->AddImageQuad(tid, rot(a.x, a.y), rot(b.x, a.y), rot(b.x, b.y), rot(a.x, b.y),
                            ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), tint);
                    }
                    else if (o->rounding > 0.0f)
                    {
                        dl->AddImageRounded(tid, a, b, ImVec2(0, 0), ImVec2(1, 1), tint, o->rounding * s);
                    }
                    else
                    {
                        dl->AddImage(tid, a, b, ImVec2(0, 0), ImVec2(1, 1), tint);
                    }
                }
                else if (!o->imageData.empty() || !o->imageUri.empty())
                {
                    const ImU32 loading = PackColor(0.7f, 0.7f, 0.7f, alpha * 0.25f);
                    if (o->rounding > 0.0f)
                        dl->AddRectFilled(a, b, loading, o->rounding * s);
                    else
                        dl->AddRectFilled(a, b, loading);
                }
                break;
            }
            default: break;
            }
        }
    }

    // â”€â”€â”€ Registration â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    void ResetLibraryState()
    {
        ClearAll();
    }

    void register_lib(lua_State* L)
    {
        funcnameinc = 0;

        ClearAll();
        EnsureMetatable(L);

        lua_newtable(L);
        lua_pushcfunction(L, L_New, ("new"));
        lua_setfield(L, -2, ("new"));

        lua_newtable(L);
        lua_pushinteger(L, 0); lua_setfield(L, -2, ("UI"));
        lua_pushinteger(L, 1); lua_setfield(L, -2, ("System"));
        lua_pushinteger(L, 2); lua_setfield(L, -2, ("Plex"));
        lua_pushinteger(L, 3); lua_setfield(L, -2, ("Monospace"));
        lua_setfield(L, -2, ("Fonts"));

        lua_setglobal(L, ("Drawing"));

        Environment->make_global(L, ("cleardrawcache"), L_ClearCache, funcnameinc++);
        Environment->make_global(L, ("isrenderobj"), L_IsRenderObj, funcnameinc++);
        Environment->make_global(L, ("getrenderproperty"), L_GetRenderProperty, funcnameinc++);
        Environment->make_global(L, ("setrenderproperty"), L_SetRenderProperty, funcnameinc++);
    }
}
