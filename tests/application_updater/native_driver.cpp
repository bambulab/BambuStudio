#include "slic3r/Utils/DesktopUpdater.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

// Invoke production platform tools from disposable integration fixtures.
int main(int argc, char **argv)
{
    try {
        if (argc == 2 && std::string(argv[1]) == "mac-script")
            std::cout << Slic3r::DesktopUpdater::mac_install_script();
        else if (argc == 2 && std::string(argv[1]) == "windows-script")
            std::cout << Slic3r::DesktopUpdater::windows_install_script();
#ifdef __APPLE__
        else if (argc == 4 && std::string(argv[1]) == "prepare-mac")
            Slic3r::DesktopUpdater::prepare_mac_bundle(argv[2], argv[3]);
#endif
        else if (argc == 7 && std::string(argv[1]) == "download") {
            Slic3r::ReleaseUpdater::Release release{"02.09.00.01", "fixture", argv[2], argv[3], std::stoull(argv[4])};
            std::atomic<bool> cancel{false};
            std::atomic<int> percent{0};
            std::cout << Slic3r::DesktopUpdater::download_package(argv[5], release, argv[6], cancel, percent);
        } else throw std::runtime_error("Invalid integration driver arguments");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
