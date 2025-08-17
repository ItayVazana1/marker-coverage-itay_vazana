#include "./mce/ansi.hpp"
#include "./mce/app.hpp"

int main()
{
    mce::ansi::enable_virtual_terminal_on_windows();
    app::Application app;
    return app.run();
}
