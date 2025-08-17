#include <iostream> // for std::cout
#include "mce/app.hpp"
#include "mce/ui.hpp"
#include "mce/ansi.hpp"
#include "mce/progress.hpp"

namespace app
{

    int Application::run() { return main_loop(); }

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
                ui::settings(state_);
                break; // toggle debug flags
            case 3:
                ui::help();
                break;
            case 4:
                ui::about();
                break;
            case 5:
            {
                // Process current selection: iterate files, run detector, print results
                auto imgs = ui::collect_images(state_.inputPath, state_.isDirectory);
                progress::process_and_report(imgs, state_); // <-- pass the whole state
                break;
            }
            case 0:
                mce::ansi::clear_screen();
                std::cout << mce::ansi::muted << "Bye! " << mce::ansi::reset << "\n";
                return 0;

            default:
                std::cout << mce::ansi::warn << "Invalid choice." << mce::ansi::reset << "\n";
            }
        }
    }

} // namespace app
