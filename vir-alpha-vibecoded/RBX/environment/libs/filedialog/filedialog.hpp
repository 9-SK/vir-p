#pragma once

#include <lstate.h>

namespace FileDialog {

    static int funcnameinc = 0;

    int openfiledialog(lua_State* L);
    int openfilesdialog(lua_State* L);
    int openfolderdialog(lua_State* L);
    int savefiledialog(lua_State* L);

    void register_lib(lua_State* L);
}
