#include "ansi.hpp"
#include "app.hpp"

int main()
{
    app::ansi::enable_virtual_terminal_on_windows();
    app::Application app;
    return app.run();
}