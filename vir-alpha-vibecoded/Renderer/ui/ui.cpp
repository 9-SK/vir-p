#include "ui.h"
#include "../graphics.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <chrono>
#include <cstdio>

#include <globals.h>
#include <managers/logging/logging.h>

#include <RBX/environment/libs/rconsole/rconsole.hpp>

std::deque<module::core::graphics::ui::ui_instruction_t> module::core::graphics::ui::ui_queue;

static std::unordered_map<std::string, bool> checkbox_states;
static std::unordered_map<std::string, float> slider_states;
static std::unordered_map<std::string, int> combo_states;
static std::unordered_map<std::string, int> listbox_states;

std::vector<module::core::graphics::ui::editor_tab_t> module::core::graphics::ui::c_ui::editor_tabs;
int module::core::graphics::ui::c_ui::active_tab = 0;
int module::core::graphics::ui::c_ui::tab_counter = 0;
int module::core::graphics::ui::c_ui::current_script = 0;

static const char* script_labels[] = {
    (("InfiniteYield")), (("UNC")), (("Vulnerability Test")), (("Unnamed-ESP")), (("Myriad")), (("Sunc")), (("Cobalt")), (("Dex"))
};

static const char* script_sources[] = {
    ("loadstring(game:HttpGet(\"https://raw.githubusercontent.com/EdgeIY/infiniteyield/master/source\"))()"),
    ("loadstring(game:HttpGet(\"https://raw.githubusercontent.com/unified-naming-convention/NamingStandard/refs/heads/main/UNCCheckEnv.lua\"))()"),
    ("loadstring(game:HttpGet(\"https://raw.githubusercontent.com/zryr/Vulnerability-Check/refs/heads/main/Script\"))()"),
    ("pcall(function() loadstring(game:HttpGet('https://raw.githubusercontent.com/ic3w0lf22/Unnamed-ESP/master/UnnamedESP.lua'))() end)"),
    ("loadstring(game:HttpGetAsync(\"https://github.com/Proton-Utilities/Myriad/releases/latest/download/myriad.luau\"))()"),
    ("getgenv().sUNCDebug = {[\"printcheckpoints\"] = false, [\"delaybetweentests\"] = 0, [\"printtesttimetaken\"] = false} loadstring(game:HttpGet(\"https://script.sunc.su/\"))()"),
    ("loadstring(game:HttpGet(\"https://github.com/notpoiu/cobalt/releases/latest/download/Cobalt.luau\"))()"),
    ("loadstring(game:HttpGet(\"https://raw.githubusercontent.com/infyiff/backup/main/dex.lua\"))()")
};

void module::core::graphics::ui::c_ui::fire_callback_void(int ref)
{
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return;
    lua_State* L = globals.exploit_thread;
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pcall(L, 0, 0, 0);
}

void module::core::graphics::ui::c_ui::fire_callback_bool(int ref, bool val)
{
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return;
    lua_State* L = globals.exploit_thread;
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushboolean(L, val);
    lua_pcall(L, 1, 0, 0);
}

void module::core::graphics::ui::c_ui::fire_callback_float(int ref, float val)
{
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return;
    lua_State* L = globals.exploit_thread;
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnumber(L, val);
    lua_pcall(L, 1, 0, 0);
}

void module::core::graphics::ui::c_ui::fire_callback_str(int ref, const std::string& val)
{
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return;
    lua_State* L = globals.exploit_thread;
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushstring(L, val.c_str());
    lua_pcall(L, 1, 0, 0);
}

void module::core::graphics::ui::c_ui::fire_callback_int(int ref, int val)
{
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return;
    lua_State* L = globals.exploit_thread;
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushinteger(L, val);
    lua_pcall(L, 1, 0, 0);
}

void module::core::graphics::ui::c_ui::fire_callback_color(int ref, float r, float g, float b, float a)
{
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return;
    lua_State* L = globals.exploit_thread;
    if (!L) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnumber(L, r);
    lua_pushnumber(L, g);
    lua_pushnumber(L, b);
    lua_pushnumber(L, a);
    lua_pcall(L, 4, 0, 0);
}

void module::core::graphics::ui::c_ui::add_tab(const std::string& name, const std::string& content)
{
    tab_counter++;
    std::string tab_name = name.empty() ? (("tab ") + std::to_string(tab_counter)) : name;
    editor_tabs.emplace_back(tab_name);
    if (!content.empty())
        editor_tabs.back().editor.SetText(content);
    active_tab = static_cast<int>(editor_tabs.size()) - 1;
}

void module::core::graphics::ui::c_ui::close_tab(int index)
{
    if (index < 0 || index >= (int)editor_tabs.size()) return;
    if (editor_tabs.size() <= 1) return;
    editor_tabs.erase(editor_tabs.begin() + index);
    if (active_tab >= (int)editor_tabs.size())
        active_tab = (int)editor_tabs.size() - 1;
}

void module::core::graphics::ui::c_ui::initialize()
{
    add_tab(("tab 1"), ("print('updated')"));
}

static void execute_script(const std::string& src)
{
    if (src.empty()) return;
    Shared::ExecutionRequest req;
    req.Source = src;
    globals.execution_queue.push_back(std::move(req));
}

static void render_instruction(module::core::graphics::ui::ui_instruction_t* inst, const std::string& window_name)
{
    using namespace ImGui;
    switch (inst->type)
    {
    case module::core::graphics::ui::ui_instruction_t::TEXT:
    {
        if (!inst->text_title.empty() && !inst->text_content.empty())
            Text((("%s %s")), inst->text_title.c_str(), inst->text_content.c_str());
        else if (!inst->text_content.empty())
            Text((("%s")), inst->text_content.c_str());
        else
            Text((("%s")), inst->text_title.c_str());
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::BUTTON:
    {
        if (Button(inst->button_title.c_str()))
            module::core::graphics::ui::c_ui::fire_callback_void(inst->button_callback);
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::CHECKBOX:
    {
        std::string id = window_name + ("/") + inst->checkbox_title;
        bool& s = checkbox_states[id];
        if (Checkbox(inst->checkbox_title.c_str(), &s))
            module::core::graphics::ui::c_ui::fire_callback_bool(inst->checkbox_callback, s);
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::INPUT:
    {
        if (!inst->input_title.empty())
            Text((("%s")), inst->input_title.c_str());
        char buf[512];
        strncpy_s(buf, inst->input_text.c_str(), sizeof(buf) - 1);
        std::string label = (("##input") + std::to_string(reinterpret_cast<uintptr_t>(inst)));
        if (InputText(label.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            inst->input_text = buf;
            module::core::graphics::ui::c_ui::fire_callback_str(inst->input_callback, inst->input_text);
        }
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::SLIDER:
    {
        std::string id = window_name + ("/") + inst->slider_title;
        float& v = slider_states[id];
        if (SliderFloat(inst->slider_title.c_str(), &v, inst->slider_min, inst->slider_max))
            module::core::graphics::ui::c_ui::fire_callback_float(inst->slider_callback, v);
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::LISTBOX:
    {
        std::string id = window_name + ("/") + inst->listbox_title;
        if (!listbox_states.count(id)) listbox_states[id] = inst->current_item - 1;
        int& sel = listbox_states[id];
        if (BeginListBox(inst->listbox_title.c_str(), ImVec2(200, 100)))
        {
            for (int i = 0; i < (int)inst->items.size(); i++)
            {
                if (Selectable(inst->items[i].c_str(), sel == i))
                {
                    sel = i;
                    module::core::graphics::ui::c_ui::fire_callback_int(inst->listbox_callback, sel + 1);
                }
                if (sel == i) SetItemDefaultFocus();
            }
            EndListBox();
        }
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::COMBO:
    {
        std::string id = window_name + ("/") + inst->combo_title;
        if (!combo_states.count(id)) combo_states[id] = inst->current_item - 1;
        int& sel = combo_states[id];
        std::vector<const char*> items_c;
        for (auto& item : inst->items)
            items_c.push_back(item.c_str());
        if (!items_c.empty())
        {
            if (Combo(inst->combo_title.c_str(), &sel, items_c.data(), (int)items_c.size()))
                module::core::graphics::ui::c_ui::fire_callback_int(inst->combo_callback, sel + 1);
        }
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::COLOR_PICKER:
    {
        if (ColorEdit4(inst->text_title.c_str(), inst->color))
            module::core::graphics::ui::c_ui::fire_callback_color(inst->color_callback, inst->color[0], inst->color[1], inst->color[2], inst->color[3]);
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::PROGRESS_BAR:
    {
        std::string overlay = std::to_string((int)(inst->progress_value * 100)) + ("%");
        ProgressBar(inst->progress_value, ImVec2(-1, 0), overlay.c_str());
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::SEPARATOR: { Separator(); break; }
    case module::core::graphics::ui::ui_instruction_t::SAME_LINE: { SameLine(); break; }
    case module::core::graphics::ui::ui_instruction_t::SPACING: { Spacing(); break; }
    case module::core::graphics::ui::ui_instruction_t::TREE_NODE:
    {
        if (TreeNode(inst->treename_title.c_str()))
        {
            module::core::graphics::ui::c_ui::fire_callback_void(inst->treenode_callback);
            TreePop();
        }
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::TOOLTIP:
    {
        if (IsItemHovered())
        {
            BeginTooltip();
            Text(("%s"), inst->tooltip_text.c_str());
            EndTooltip();
        }
        break;
    }
    case module::core::graphics::ui::ui_instruction_t::COLUMNS: { Columns(inst->columns_count); break; }
    case module::core::graphics::ui::ui_instruction_t::NEXT_COLUMN: { NextColumn(); break; }
    default: break;
    }
}

// ── rconsole overlay panel ──
// File-local (no ui.h decl): renders the in-memory rconsole scrollback when
// the console is "shown", nothing when hidden. Only ever called from
// c_ui::draw's Present-hook flow, so it can only exist while show_ui routes
// input to ImGui already — it never captures game input on its own, and when
// hidden it draws nothing and captures nothing. Snapshot-copy discipline
// mirrors the Output tab (no lock held while drawing); the buffer is capped
// at RConsole::kMaxScrollback so per-frame work stays bounded.
static void draw_rconsole()
{
    using namespace ImGui;
    if (!RConsole::IsConsoleShown())
        return;

    const std::string title = RConsole::GetConsoleTitle();
    const float scale = module::core::graphics::c_graphics::dpi_scale;
    SetNextWindowSize(ImVec2(520 * scale, 300 * scale), ImGuiCond_FirstUseEver);

    if (!Begin(title.c_str()))
    {
        End();
        return;
    }

    static bool auto_scroll = true;
    if (Button(("Clear")))
        RConsole::ClearScrollback();
    SameLine();
    if (Button(("Copy all")))
    {
        static std::vector<RConsole::ConsoleLine> clip_lines;
        RConsole::CopyScrollback(clip_lines);
        std::string out;
        for (const auto& l : clip_lines)
        {
            for (const auto& s : l.spans)
                out += s.text;
            out += '\n';
        }
        SetClipboardText(out.c_str());
    }
    SameLine();
    Checkbox(("Auto scroll"), &auto_scroll);
    Separator();

    // Bounded snapshot: at most kMaxScrollback lines, reused static storage
    // so capacity persists instead of reallocating every frame.
    static std::vector<RConsole::ConsoleLine> lines;
    RConsole::CopyScrollback(lines);

    ImVec2 avail = GetContentRegionAvail();
    const float input_h = GetFrameHeightWithSpacing();
    ImVec2 scroll_size(avail.x, avail.y > input_h ? avail.y - input_h : 0.0f);
    if (BeginChild(("##rconsole_scroll"), scroll_size, true, ImGuiWindowFlags_HorizontalScrollbar))
    {
        for (const auto& l : lines)
        {
            bool first = true;
            for (const auto& s : l.spans)
            {
                if (s.text.empty())
                    continue;
                if (!first)
                    SameLine(0.0f, 0.0f);
                PushStyleColor(ImGuiCol_Text, ImVec4(s.r, s.g, s.b, 1.0f));
                TextUnformatted(s.text.c_str());
                PopStyleColor();
                first = false;
            }
            if (first)
                TextUnformatted((""));
        }
        if (auto_scroll && GetScrollY() >= GetScrollMaxY() - 10.f)
            SetScrollHereY(1.0f);
    }
    EndChild();

    // Non-blocking input: persistent line box, Enter submits to the input
    // queue that rconsoleinput polls ("" while nothing submitted).
    TextUnformatted((">"));
    SameLine();
    SetNextItemWidth(-1.0f);
    static char input_buf[4096] = "";
    static bool reclaim_focus = false;
    if (reclaim_focus)
    {
        SetKeyboardFocusHere();
        reclaim_focus = false;
    }
    if (InputText(("##rconsole_input"), input_buf, sizeof(input_buf),
        ImGuiInputTextFlags_EnterReturnsTrue))
    {
        std::string submitted(input_buf);
        input_buf[0] = '\0';
        reclaim_focus = true;
        if (!submitted.empty())
        {
            RConsole::SubmitInputLine(submitted);
            // Echo the submitted line so scrollback shows what was typed.
            RConsole::AppendColoredLine(("> " + submitted), 0.65f, 0.65f, 0.65f);
        }
    }

    End();
}

void module::core::graphics::ui::c_ui::draw_lua_ui()
{
    using namespace ImGui;
    std::unordered_map<std::string, std::vector<module::core::graphics::ui::ui_instruction_t*>> window_map;
    for (auto& inst : ui_queue)
        window_map[inst.ui_title].push_back(&inst);
    for (auto& [window_name, instructions] : window_map)
    {
        if (!Begin(window_name.c_str())) { End(); continue; }
        bool has_tabbar = false;
        std::string tabbar_name;
        for (auto* inst : instructions)
        {
            if (inst->type == module::core::graphics::ui::ui_instruction_t::TABBAR)
            {
                has_tabbar = true;
                tabbar_name = inst->tabbar_title;
                break;
            }
        }
        if (has_tabbar && BeginTabBar(tabbar_name.c_str()))
        {
            std::unordered_map<std::string, std::vector<module::core::graphics::ui::ui_instruction_t*>> tab_content;
            for (auto* inst : instructions)
            {
                if (!inst->parent_tabbar.empty())
                    tab_content[inst->parent_tabbar].push_back(inst);
            }
            for (auto* inst : instructions)
            {
                if (inst->type == module::core::graphics::ui::ui_instruction_t::TABITEM && BeginTabItem(inst->tabitem_title.c_str()))
                {
                    auto it = tab_content.find(inst->tabitem_title);
                    if (it != tab_content.end())
                    {
                        for (auto* ci : it->second)
                            render_instruction(ci, window_name);
                    }
                    EndTabItem();
                }
            }
            EndTabBar();
        }
        for (auto* inst : instructions)
        {
            if (inst->parent_tabbar.empty() && inst->type != module::core::graphics::ui::ui_instruction_t::TABBAR)
                render_instruction(inst, window_name);
        }
        End();
    }
}

void module::core::graphics::ui::c_ui::draw()
{
    using namespace ImGui;
    // rconsole overlay: ConsoleTopmost is draw order (front vs back of the
    // ImGui submission order), never OS topmost. Snapshot once so both the
    // pre- and post-draw calls agree within this frame.
    const bool rconsole_shown = RConsole::IsConsoleShown();
    const bool rconsole_topmost = rconsole_shown && RConsole::IsConsoleTopmost();
    if (rconsole_shown && !rconsole_topmost)
        draw_rconsole();
    static bool is_collapsed = false;
    float scale = c_graphics::dpi_scale;
    SetNextWindowSize(ImVec2(580 * scale, 380 * scale), ImGuiCond_FirstUseEver);
    SetNextWindowSizeConstraints(ImVec2(300 * scale, 200 * scale), ImVec2(FLT_MAX, FLT_MAX));
    if (Begin(("VX licks tavares dih"), nullptr, 0))
    {
        is_collapsed = ImGui::IsWindowCollapsed();
        if (!is_collapsed)
        {
            if (BeginTabBar(("##maintabs")))
            {
                if (BeginTabItem(("Execution")))
                {
                    float button_h = 28.f * scale;
                    float tabbar_h = 26.f * scale;
                    float editor_h = GetContentRegionAvail().y - button_h - tabbar_h;
                    if (BeginTabBar(("##scripttabs"), ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable))
                    {
                        if (TabItemButton(("+"), ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
                            add_tab();
                        int tab_to_close = -1;
                        for (int i = 0; i < (int)editor_tabs.size(); i++)
                        {
                            auto& tab = editor_tabs[i];
                            bool is_open = true;
                            bool* p_open = editor_tabs.size() > 1 ? &is_open : nullptr;
                            if (BeginTabItem(tab.name.c_str(), p_open))
                            {
                                active_tab = i; EndTabItem();
                            }
                            if (p_open && !is_open)
                                tab_to_close = i;
                        }
                        EndTabBar();
                        if (tab_to_close != -1)
                            close_tab(tab_to_close);
                    }
                    if (active_tab >= 0 && active_tab < (int)editor_tabs.size())
                        editor_tabs[active_tab].editor.Render(("##editor"), ImVec2(0, editor_h), true);
                    if (active_tab >= 0 && active_tab < (int)editor_tabs.size())
                    {
                        if (Button(("Execute")))
                            execute_script(editor_tabs[active_tab].editor.GetText());
                        SameLine();
                        if (Button(("Clear")))
                            editor_tabs[active_tab].editor.SetText((""));
                        SameLine();
                        if (Button(("Clipboard")))
                        {
                            const char* clip = GetClipboardText();
                            if (clip) execute_script(clip);
                        }
                    }
                    EndTabItem();
                }
                if (BeginTabItem(("Scripts")))
                {
                    Combo(("Script"), &current_script, script_labels, 8);
                    if (Button(("Execute")))
                        execute_script(script_sources[current_script]);
                    SameLine();
                    if (Button(("Copy")))
                        SetClipboardText(script_sources[current_script]);
                    SameLine();
                    if (Button(("New tab")))
                        add_tab(script_labels[current_script], script_sources[current_script]);
                    EndTabItem();
                }
                if (BeginTabItem(("Output")))
                {
                    static bool auto_scroll = true;
                    static char filter_buf[64] = "";
                    if (Button(("Clear")))
                        log_mgr.clear_log_info();
                    SameLine();
                    if (Button(("Copy all")))
                    {
                        auto logs = log_mgr.get_log_info();
                        std::string out;
                        for (const auto& l : logs)
                        {
                            out += l.content;
                            out += '\n';
                        }
                        SetClipboardText(out.c_str());
                    }
                    SameLine();
                    Checkbox(("Auto scroll"), &auto_scroll);
                    SameLine();
                    SetNextItemWidth(180.f * scale);
                    InputTextWithHint(("##filter"), ("Filter..."), filter_buf, sizeof(filter_buf));
                    Separator();
                    ImVec2 avail = GetContentRegionAvail();
                    if (BeginChild(("##output_scroll"), avail, true, ImGuiWindowFlags_HorizontalScrollbar))
                    {
                        auto logs = log_mgr.get_log_info();
                        const char* filter = filter_buf;
                        const bool has_filter = filter[0] != '\0';
                        for (const auto& l : logs)
                        {
                            if (!l.visible) continue;
                            if (has_filter && l.content.find(filter) == std::string::npos)
                                continue;
                            PushStyleColor(ImGuiCol_Text, l.color);
                            TextUnformatted(l.content.c_str());
                            PopStyleColor();
                        }
                        if (auto_scroll && GetScrollY() >= GetScrollMaxY() - 10.f)
                            SetScrollHereY(1.0f);
                    }
                    EndChild();
                    EndTabItem();
                }
                if (BeginTabItem(("Settings")))
                {
                    if (Button(("Unload"))) exit(0);
                    EndTabItem();
                }
                EndTabBar();
            }
        }
    }
    End();
    draw_lua_ui();
    if (rconsole_shown && rconsole_topmost)
        draw_rconsole();
}
