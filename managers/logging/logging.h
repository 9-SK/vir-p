#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <imgui/imgui.h>

struct LogEntry
{
    std::string content;
    ImU32 color;
    bool visible;
};

class LogManager
{
public:
    void add_log(const std::string& msg, ImU32 color = IM_COL32(255, 255, 255, 255))
    {
        std::lock_guard<std::mutex> lk(mtx);
        entries.push_back({ msg, color, true });
        if (entries.size() > 5000)
            entries.erase(entries.begin(), entries.begin() + 1000);
    }

    void clear_log_info()
    {
        std::lock_guard<std::mutex> lk(mtx);
        entries.clear();
    }

    std::vector<LogEntry> get_log_info()
    {
        std::lock_guard<std::mutex> lk(mtx);
        return entries;
    }

private:
    std::vector<LogEntry> entries;
    std::mutex mtx;
};

inline LogManager log_mgr;
