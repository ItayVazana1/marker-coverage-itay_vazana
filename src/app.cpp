#include "app.hpp"
#include "ui.hpp"
#include "ansi.hpp"
#include "progress.hpp"

namespace app
{

    int Application::run()
    {
        return main_loop();
    }

    int Application::main_loop()
    {
        for (;;)
        {
            ui::main_menu(state_);
            const int choice = ui::read_menu_choice();
            switch (choice)
            {
            case 1:
                ui::input(state_);
                break;
            case 2:
                if (!state_.hasValidPath)
                {
                    ui::title("Process");
                    std::cout << ansi::warn << "Set an input path first (option 1)." << ansi::reset << "\n\n";
                    ui::wait_for_enter();
                }
                else
                {
                    progress::process_pipeline(state_.inputPath, state_.isDirectory);
                }
                break;
            case 3:
                ui::help();
                break;
            case 4:
                ui::about();
                break;
            case 0:
                ansi::clear_screen();
                std::cout << ansi::muted << "Bye! " << ansi::reset << "\n";
                return 0;
            default:
                std::cout << ansi::warn << "Invalid choice." << ansi::reset << "\n";
            }
        }
    }

} // namespace app