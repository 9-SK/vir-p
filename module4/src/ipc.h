

#pragma once
#include <string>

namespace ipc {
    void ensure_dir();
    void write_bootlog(const std::string& line);   
    void write_status(const std::string& line);    
    bool read_execute(std::string& out);           
    std::string runtime_fullpath(const char* leaf);
}
