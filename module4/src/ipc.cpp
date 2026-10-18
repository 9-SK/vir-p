
#include "ipc.h"
#include "contract.h"
#include <Windows.h>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <filesystem>

namespace {
    std::mutex g_log_mutex;

    std::string runtime_dir() {
        char* local = nullptr;
        size_t n = 0;
        _dupenv_s(&local, &n, "LOCALAPPDATA");
        std::string path = local ? local : "C:\\Users\\Public";
        free(local);
        path += "\\";
        path += rv::runtime_dir;
        return path;
    }
}

namespace ipc {

    std::string runtime_fullpath(const char* leaf) {
        return runtime_dir() + "\\" + leaf;
    }

    
    void ensure_dir() {
        try { std::filesystem::create_directories(runtime_dir()); }
        catch (...) {  }
    }

    void write_bootlog(const std::string& line) {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        FILE* f = nullptr;
        if (fopen_s(&f, runtime_fullpath(rv::file_bootlog).c_str(), "a") == 0 && f) {
            time_t now = time(nullptr);
            tm t{};
            localtime_s(&t, &now);
            char stamp[64];
            strftime(stamp, sizeof(stamp), "[%H:%M:%S] ", &t);
            fputs(stamp, f);
            fputs(line.c_str(), f);
            fputc('\n', f);
            fclose(f);
        }
    }

    
    void write_status(const std::string& line) {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        std::string tmp = runtime_fullpath(rv::file_status) + ".tmp";
        std::string dst = runtime_fullpath(rv::file_status);
        FILE* f = nullptr;
        if (fopen_s(&f, tmp.c_str(), "w") == 0 && f) {
            fputs(line.c_str(), f);
            fclose(f);
            MoveFileExA(tmp.c_str(), dst.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
    }

    bool read_execute(std::string& out) {
        FILE* f = nullptr;
        if (fopen_s(&f, runtime_fullpath(rv::file_execute).c_str(), "rb") != 0 || !f)
            return false;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        out.resize(static_cast<size_t>(n));
        fread(out.data(), 1, static_cast<size_t>(n), f);
        fclose(f);
        return true;
    }
}
