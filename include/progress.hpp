#pragma once
#include <string>
#include <vector>

namespace app::progress
{

    void process_pipeline(const std::string &inputPath, bool isDir);

    // Utility if you want custom steps elsewhere
    void simulate_step(const std::string &label, int msTotal, int barWidth = 42);

} // namespace app::progress
