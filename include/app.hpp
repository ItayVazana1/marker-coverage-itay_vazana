#pragma once
#include <string>

namespace app
{

    struct State
    {
        std::string inputPath;
        bool hasValidPath{false};
        bool isDirectory{false};
    };

    class Application
    {
    public:
        int run();

    private:
        State state_{};
        int main_loop();
    };

} // namespace app