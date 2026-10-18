#include "FileDialog.hpp"

#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>
#include <RBX/environment/env.hpp>

#include <shlobj.h>
#include <commdlg.h>
#include <objbase.h>


namespace FileDialog {

    int openfiledialog(lua_State* L)
    {
        const char* filter = lua_isstring(L, 1) ? lua_tostring(L, 1) : ("All Files\0*.*\0");
        const char* title = lua_isstring(L, 2) ? lua_tostring(L, 2) : ("Open File");

        char filename[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetForegroundWindow();
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = title;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameA(&ofn)) {
            lua_pushstring(L, filename);
        }
        else {
            lua_pushnil(L);
        }
        return 1;
    }

    int openfilesdialog(lua_State* L)
    {
        const char* filter = lua_isstring(L, 1) ? lua_tostring(L, 1) : ("All Files\0*.*\0");
        const char* title = lua_isstring(L, 2) ? lua_tostring(L, 2) : ("Open Files");

        char filenames[32768] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetForegroundWindow();
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = filenames;
        ofn.nMaxFile = sizeof(filenames);
        ofn.lpstrTitle = title;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

        lua_newtable(L);
        if (GetOpenFileNameA(&ofn)) {
            char* dir = filenames;
            char* file = filenames + strlen(filenames) + 1;
            if (*file == '\0') {
                lua_pushstring(L, dir);
                lua_rawseti(L, -2, 1);
            }
            else {
                int idx = 1;
                while (*file) {
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

    int openfolderdialog(lua_State* L)
    {
        const char* title = lua_isstring(L, 1) ? lua_tostring(L, 1) : ("Select Folder");

        char path[MAX_PATH] = {};
        BROWSEINFOA bi = {};
        bi.hwndOwner = GetForegroundWindow();
        bi.lpszTitle = title;
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl) {
            SHGetPathFromIDListA(pidl, path);
            CoTaskMemFree(pidl);
            lua_pushstring(L, path);
        }
        else {
            lua_pushnil(L);
        }
        return 1;
    }

    int savefiledialog(lua_State* L)
    {
        const char* filter = lua_isstring(L, 1) ? lua_tostring(L, 1) : ("All Files\0*.*\0");
        const char* title = lua_isstring(L, 2) ? lua_tostring(L, 2) : ("Save File");

        char filename[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetForegroundWindow();
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = title;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

        if (GetSaveFileNameA(&ofn)) {
            lua_pushstring(L, filename);
        }
        else {
            lua_pushnil(L);
        }
        return 1;
    }

    void register_lib(lua_State* L)
    {
        Environment->make_global(L, ("openfiledialog"), openfiledialog, FileDialog::funcnameinc++);
        Environment->make_global(L, ("openfilesdialog"), openfilesdialog, FileDialog::funcnameinc++);
        Environment->make_global(L, ("openfolderdialog"), openfolderdialog, FileDialog::funcnameinc++);
        Environment->make_global(L, ("savefiledialog"), savefiledialog, FileDialog::funcnameinc++);
    }
}
