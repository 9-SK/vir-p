#pragma once

#include <string>
#include <vector>
#include <memory>
#include <deque>
#include <unordered_map>

#include <imgui/imgui.h>
#include <imgui/addons/TextEditor.h>

#include <globals.h>
#include <lstate.h>
#include <lualib.h>


namespace module::core::graphics::ui
{
    struct ui_instruction_t
    {
        enum Type
        {
            NONE, TEXT, BUTTON, CHECKBOX, INPUT, SLIDER, LISTBOX, COMBO,
            COLOR_PICKER, PROGRESS_BAR, SEPARATOR, SAME_LINE, SPACING,
            TREE_NODE, TOOLTIP, COLUMNS, NEXT_COLUMN, TABBAR, TABITEM
        };
        Type type = NONE;
        std::string ui_title;
        std::string parent_tabbar;
        std::string text_title;
        std::string text_content;
        std::string button_title;
        int button_callback = -2;
        std::string checkbox_title;
        int checkbox_callback = -2;
        std::string input_title;
        std::string input_text;
        int input_callback = -2;
        std::string slider_title;
        float slider_min = 0.f;
        float slider_max = 1.f;
        int slider_callback = -2;
        std::string listbox_title;
        std::vector<std::string> items;
        int current_item = 1;
        int listbox_callback = -2;
        std::string combo_title;
        int combo_callback = -2;
        float color[4] = { 1.f, 1.f, 1.f, 1.f };
        int color_callback = -2;
        float progress_value = 0.f;
        std::string treename_title;
        int treenode_callback = -2;
        std::string tooltip_text;
        int columns_count = 1;
        std::string tabbar_title;
        std::string tabitem_title;
    };

    extern std::deque<ui_instruction_t> ui_queue;

    struct editor_tab_t
    {
        std::string name;
        TextEditor editor;
        editor_tab_t(const std::string& n = ("tab")) : name(n) {}
    };

    class c_ui
    {
    public:
        static std::vector<editor_tab_t> editor_tabs;
        static int active_tab;
        static int tab_counter;
        static int current_script;

        static void fire_callback_void(int ref);
        static void fire_callback_bool(int ref, bool val);
        static void fire_callback_float(int ref, float val);
        static void fire_callback_str(int ref, const std::string& val);
        static void fire_callback_int(int ref, int val);
        static void fire_callback_color(int ref, float r, float g, float b, float a);

        static void add_tab(const std::string& name = (""), const std::string& content = (""));
        static void close_tab(int index);
        static void initialize();
        static void draw();
        static void draw_lua_ui();
    };

    inline std::unique_ptr<c_ui> ui = std::make_unique<c_ui>();
}