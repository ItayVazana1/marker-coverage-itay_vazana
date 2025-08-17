#pragma once
#include <string>

namespace mce
{
    struct DetectionResult
    {
        bool found{false};
        int coveragePct{0};
        std::string debugPath;
    };
}

namespace app
{
    struct State
    {
        std::string inputPath;
        bool hasValidPath{false};
        bool isDirectory{false};
        bool debug{false};
        bool saveDebug{false};
    };
    class Application
    {
    public:
        int run();

    private:
        State state_{};
        int main_loop();
    };
}
