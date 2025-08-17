#pragma once
#include <string>
#include <vector>

namespace app::progress
{
    void process_and_report(const std::vector<std::string> &images, bool debug, bool saveDebug);
    void simulate_step(const std::string &label, int msTotal, int barWidth = 42);
}