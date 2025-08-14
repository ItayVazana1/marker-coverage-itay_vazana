#pragma once
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace app::ansi
{
    // Palette
    inline constexpr const char *reset = "\x1b[0m";
    inline constexpr const char *bold = "\x1b[1m";
    inline constexpr const char *dim = "\x1b[2m";
    inline constexpr const char *italic = "\x1b[3m";
    inline constexpr const char *underline = "\x1b[4m";

    // Theme colors
    inline constexpr const char *title = "\x1b[38;5;208m"; // orange
    inline constexpr const char *ok = "\x1b[38;5;82m";     // green
    inline constexpr const char *warn = "\x1b[38;5;214m";  // yellow
    inline constexpr const char *err = "\x1b[38;5;196m";   // red
    inline constexpr const char *info = "\x1b[38;5;45m";   // cyan
    inline constexpr const char *muted = "\x1b[90m";       // grey

    inline void enable_virtual_terminal_on_windows()
    {
#if defined(_WIN32)
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut == INVALID_HANDLE_VALUE)
            return;
        DWORD mode = 0;
        if (!GetConsoleMode(hOut, &mode))
            return;
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, mode);
#endif
    }

    inline void clear_screen()
    {
        std::cout << "\x1b[2J\x1b[H";
    }
}
