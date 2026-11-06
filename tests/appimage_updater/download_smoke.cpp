// Optional transport smoke test: downloads a real release into a disposable
// fixture. It never launches the image or touches an installed application.
#include "slic3r/Utils/AppImageUpdater.hpp"
#include <iostream>
int main(int argc, char **argv)
{
    if (argc != 5) {
        std::cerr << "usage: download_smoke fixture.AppImage https-url sha256 size\n";
        return 2;
    }
    try {
        Slic3r::AppImageUpdater::Release release;
        release.url = argv[2]; release.sha256 = argv[3]; release.size = std::stoull(argv[4]);
        std::atomic<bool> cancel{false}; std::atomic<int> percent{0};
        Slic3r::AppImageUpdater::download_and_install(argv[1], release, cancel, percent);
        std::cout << "Verified download installed in fixture (" << percent << "%)\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
