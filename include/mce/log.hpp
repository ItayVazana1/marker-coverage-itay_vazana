#pragma once
#include <iostream>
#include <string>

namespace mce::log
{
    inline bool g_debug = false;
    inline bool g_save_debug = false;

    inline void set(bool debug, bool save_debug)
    {
        g_debug = debug;
        g_save_debug = save_debug;
    }

    inline void d(const std::string &msg)
    {
        if (g_debug)
            std::cerr << "[DBG] " << msg << "\n";
    }
    inline void i(const std::string &msg) { std::cerr << "[INF] " << msg << "\n"; }
    inline void w(const std::string &msg) { std::cerr << "[WRN] " << msg << "\n"; }
    inline void e(const std::string &msg) { std::cerr << "[ERR] " << msg << "\n"; }
}
