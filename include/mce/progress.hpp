#pragma once
#include <string>
#include <vector>
#include "mce/app.hpp" // for app::State

namespace app::progress
{

    // Run detection, print to console, and save outputs neatly.
    // Default root is ./mce_output (inside the container), override with env MCE_OUTPUT_ROOT.
    // - CSV:   <root>/results/<YYYYMMDD-HHMMSS>.csv
    // - Debug: <root>/debug/<YYYYMMDD-HHMMSS>/<index>_<name>_{quad,warp,mask}.png
    void process_and_report(const std::vector<std::string> &images,
                            const app::State &state);

} // namespace app::progress
