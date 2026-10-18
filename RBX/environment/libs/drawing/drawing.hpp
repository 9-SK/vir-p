#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <lstate.h>
#include <lualib.h>

namespace Drawing
{
    enum class Kind : int
    {
        Line = 0,
        Text,
        Circle,
        Square,
        Triangle,
        Quad,
        Image,
        Invalid
    };

    struct Object
    {
        std::uint64_t id = 0;
        std::size_t slot = 0;
        Kind kind = Kind::Invalid;
        bool visible = false;
        bool destroyed = false;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        float transparency = 1.0f;
        float thickness = 1.0f;
        int zindex = 0;

        float fromX = 0.0f, fromY = 0.0f;
        float toX = 0.0f, toY = 0.0f;

        std::string text;
        float textSize = 13.0f;
        bool center = false;
        bool outline = false;
        float outR = 0.0f, outG = 0.0f, outB = 0.0f;
        float posX = 0.0f, posY = 0.0f;
        int font = 0;
        float textBoundsX = 0.0f, textBoundsY = 0.0f;

        float radius = 0.0f;
        int numSides = 24;
        bool filled = false;

        float sizeX = 0.0f, sizeY = 0.0f;

        float pAx = 0.0f, pAy = 0.0f;
        float pBx = 0.0f, pBy = 0.0f;
        float pCx = 0.0f, pCy = 0.0f;
        float pDx = 0.0f, pDy = 0.0f;

        std::string imageData;
        std::string imageUri;
        float rounding = 0.0f;
        float rotation = 0.0f;

        void* texSrv = nullptr;
        int   texW = 0;
        int   texH = 0;
        bool  texDirty = false;
    };

    using ObjectRef = std::shared_ptr<Object>;

    using UploadImageFn = void* (*)(const void* data, size_t size, int* outW, int* outH);
    using ReleaseImageFn = void (*)(void* srv);
    using FetchUriFn = void (*)(Object* obj, const char* uri);

    extern UploadImageFn  g_UploadImageFn;
    extern ReleaseImageFn g_ReleaseImageFn;
    extern FetchUriFn     g_FetchUriFn;

    extern std::recursive_mutex g_mutex;
    extern std::vector<ObjectRef> g_objects;
    extern std::unordered_map<std::uint64_t, ObjectRef> g_lookup;

    extern float g_dpiScale;

    extern ImFont* g_Fonts[4];

    struct PendingImage
    {
        std::uint64_t id;
        std::string data;
    };
    extern std::mutex g_pendingMutex;
    extern std::vector<PendingImage> g_pending;

    extern std::atomic<std::uint64_t> g_nextId;

    extern const char* kMetaName;

    struct UdataPayload
    {
        std::uint64_t id;
    };

    static int funcnameinc = 0;

    // Public interface
    bool ImGuiReady();
    void ClearAll();
    void DrainPendingImages();
    void PushPendingImage(std::uint64_t id, std::string bytes);
    void RenderAll(ImDrawList* dl);

    // Lua-facing functions
    int L_New(lua_State* L);
    int L_Destroy(lua_State* L);
    int L_ToString(lua_State* L);
    int L_Index(lua_State* L);
    int L_NewIndex(lua_State* L);
    int L_ClearCache(lua_State* L);
    int L_IsRenderObj(lua_State* L);
    int L_GetRenderProperty(lua_State* L);
    int L_SetRenderProperty(lua_State* L);

    void register_lib(lua_State* L);
}
