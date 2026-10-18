#pragma once

#include <lstate.h>
#include <lualib.h>
#include <Roblox/Shared/Shared.hpp>

#include <shlobj.h>
#include <commdlg.h>
#include <objbase.h>
#include <cstdio>
#include <cstring>

namespace FileDialog
{
    inline int openfiledialog(lua_State* L)
    {
        const char* filter = lua_isstring(L, 1) ? lua_tostring(L, 1) : ("All Files\0*.*\0");
        const char* title = lua_isstring(L, 2) ? lua_tostring(L, 2) : ("Open File");

        char filename[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = title;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameA(&ofn))
            lua_pushstring(L, filename);
        else
            lua_pushnil(L);

        return 1;
    }

    inline int openfilesdialog(lua_State* L)
    {
        const char* filter = lua_isstring(L, 1) ? lua_tostring(L, 1) : ("All Files\0*.*\0");
        const char* title = lua_isstring(L, 2) ? lua_tostring(L, 2) : ("Open Files");

        char filenames[32768] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = filenames;
        ofn.nMaxFile = sizeof(filenames);
        ofn.lpstrTitle = title;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

        lua_newtable(L);
        if (GetOpenFileNameA(&ofn))
        {
            char* dir = filenames;
            char* file = filenames + strlen(filenames) + 1;
            if (*file == '\0')
            {
                lua_pushstring(L, dir);
                lua_rawseti(L, -2, 1);
            }
            else
            {
                int idx = 1;
                while (*file)
                {
                    char fullpath[MAX_PATH];
                    snprintf(fullpath, MAX_PATH, ("%s\\%s"), dir, file);
                    lua_pushstring(L, fullpath);
                    lua_rawseti(L, -2, idx++);
                    file += strlen(file) + 1;
                }
            }
        }
        return 1;
    }

    inline int openfolderdialog(lua_State* L)
    {
        const char* title = lua_isstring(L, 1) ? lua_tostring(L, 1) : ("Select Folder");

        char path[MAX_PATH] = {};
        BROWSEINFOA bi = {};
        bi.lpszTitle = title;
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl)
        {
            SHGetPathFromIDListA(pidl, path);
            CoTaskMemFree(pidl);
            lua_pushstring(L, path);
        }
        else
        {
            lua_pushnil(L);
        }
        return 1;
    }

    inline int savefiledialog(lua_State* L)
    {
        const char* filter = lua_isstring(L, 1) ? lua_tostring(L, 1) : ("All Files\0*.*\0");
        const char* title = lua_isstring(L, 2) ? lua_tostring(L, 2) : ("Save File");

        char filename[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = title;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

        if (GetSaveFileNameA(&ofn))
            lua_pushstring(L, filename);
        else
            lua_pushnil(L);

        return 1;
    }

    inline void RegisterLibrary(lua_State* L)
    {
        Utils::AddFunction(L, ("openfiledialog"), openfiledialog);
        Utils::AddFunction(L, ("openfilesdialog"), openfilesdialog);
        Utils::AddFunction(L, ("openfolderdialog"), openfolderdialog);
        Utils::AddFunction(L, ("savefiledialog"), savefiledialog);
    }
}