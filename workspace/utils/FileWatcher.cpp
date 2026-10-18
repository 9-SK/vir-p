#include "FileWatcher.h"
#include "BootLog.hpp"
#include "core/Scheduler/Scheduler.h"
#include <Windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace FileWatcher
{
    namespace
    {
        std::string ViridianDir()
        {
            char base[MAX_PATH]{};
            const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
            if (!n || n >= MAX_PATH)
                return {};
            return std::string(base, n) + "\\viridian";
        }

        std::vector<std::string> Targets()
        {
            char base[MAX_PATH]{};
            const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
            std::vector<std::string> paths;
            if (n && n < MAX_PATH)
            {
                const std::filesystem::path local(base);
                paths.push_back((local / "Hello.txt").string());
                paths.push_back((local / "viridian" / "execute.lua").string());
            }
            return paths;
        }

        void WriteStatus(const char* state, const std::string& detail)
        {
            const std::string dir = ViridianDir();
            if (dir.empty())
                return;
            CreateDirectoryA(dir.c_str(), nullptr);
            std::ofstream out(dir + "\\status.txt", std::ios::binary | std::ios::trunc);
            if (out.is_open())
                out << state << "\n" << detail << "\n";
        }

        std::string ReadFileContent(const std::string& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
                return {};

            std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return std::string(buffer.begin(), buffer.end());
        }

        void WatchThread()
        {
            const std::vector<std::string> targets = Targets();
            std::vector<std::string> last(targets.size());

            // Baseline whatever is already on disk: a stale script from a
            // previous session must never fire on inject.
            for (size_t i = 0; i < targets.size(); ++i)
                last[i] = ReadFileContent(targets[i]);

            for (;;)
            {
                Sleep(2);

                for (size_t i = 0; i < targets.size(); ++i)
                {
                    const std::string current = ReadFileContent(targets[i]);
                    if (current == last[i])
                        continue;
                    last[i] = current;

                    if (current.empty())
                        continue;

                    const std::string name = std::filesystem::path(targets[i]).filename().string();

                    char line[256]{};
                    snprintf(line, sizeof(line), "exec %s (%zu bytes)", name.c_str(), current.size());
                    BootLog::WriteLine("exec", line);
                    WriteStatus("executing", name);

                    Scheduler::ExecuteY(current);

                    snprintf(line, sizeof(line), "queued %zu bytes from %s", current.size(), name.c_str());
                    BootLog::WriteLine("exec", line);
                    WriteStatus("queued", name);
                }
            }
        }
    }

    void Initialize()
    {
        std::thread(WatchThread).detach();
    }
} // namespace FileWatcher