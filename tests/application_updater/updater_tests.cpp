#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include <catch2/catch.hpp>
#include "slic3r/Utils/DesktopUpdater.hpp"
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <filesystem>
#include <fstream>
#include <random>
namespace fs = std::filesystem;
using namespace Slic3r::ReleaseUpdater;
using Slic3r::DesktopUpdater::StagedPackage;
namespace {
const std::string bytes(4096, 'x');
Release fixture_release() {
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned size = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("EVP_Digest failed");
    std::string hex;
    for (unsigned i = 0; i < size; ++i) {
        hex += "0123456789abcdef"[digest[i] >> 4]; hex += "0123456789abcdef"[digest[i] & 15];
    }
    return {"02.09.00.01", "notes", "https://example.invalid/update", hex, bytes.size()};
}
struct Fixture {
    fs::path directory;
    Fixture() {
        std::random_device random;
        for (int i = 0; i < 10; ++i) {
            auto candidate = fs::temp_directory_path() / fs::u8path(u8"bambu test ' $ \u00e9 \u6f22 " + std::to_string(random()));
            if (fs::create_directory(candidate)) { directory = candidate; return; }
        }
        throw std::runtime_error("Cannot create fixture");
    }
    ~Fixture() { std::error_code ignored; fs::remove_all(directory, ignored); }
};
}
TEST_CASE("Each platform selects only its exact release asset") {
    for (const std::string asset : {"BambuStudio_windows-x86_64.exe", "BambuStudio_windows-arm64.exe", "BambuStudio_macos-universal.dmg", "BambuStudio_linux-x86_64.AppImage"}) {
        CAPTURE(asset);
        auto release = fixture_release();
        nlohmann::json metadata = nlohmann::json::array({{{"tag_name", "v02.09.00.01"}, {"prerelease", false}, {"assets", nlohmann::json::array({
            {{"name", asset}, {"size", release.size}, {"digest", "sha256:" + release.sha256},
             {"browser_download_url", "https://github.com/example/fork/releases/download/v02.09.00.01/" + asset}}})}}});
        REQUIRE(select_release(metadata.dump(), "example/fork", asset, "02.08.04.57", false)->sha256 == release.sha256);
        REQUIRE_FALSE(select_release(metadata.dump(), "example/fork", "other.exe", "02.08.04.57", false));
        REQUIRE_FALSE(select_release(metadata.dump(), "example/fork", asset, "02.09.00.01", false));
        REQUIRE_THROWS(select_release(metadata.dump(), "evil/fork", asset, "02.08.04.57", false));
    }
    REQUIRE_THROWS(select_release("[]", "example/fork", "../update.exe", "02.08.04.57", false));
    REQUIRE_THROWS(select_release("[]", "example/fork", "update.zip", "02.08.04.57", false));
}
TEST_CASE("Verified desktop packages survive staging and preserve existing files") {
    Fixture f;
    const auto existing = f.directory / "existing.app";
    std::ofstream(existing) << "current";
    for (const std::string name : {"update.exe", "update.dmg"}) {
        std::string path;
        {
            StagedPackage staged(f.directory.u8string(), fixture_release(), name);
            staged.append(bytes.data(), 100);
            staged.append(bytes.data() + 100, bytes.size() - 100);
            path = staged.finish();
            REQUIRE_THROWS(staged.finish());
            REQUIRE_THROWS(staged.append("x", 1));
        }
        REQUIRE(fs::file_size(fs::u8path(path)) == bytes.size());
        REQUIRE(fs::exists(existing));
        fs::remove_all(fs::u8path(path).parent_path());
    }
}
TEST_CASE("Corrupted and unfinished desktop downloads leave no staging files") {
    Fixture f;
    auto release = fixture_release();
    {
        StagedPackage staged(f.directory.u8string(), release, "update.exe");
        SECTION("Truncated") { staged.append(bytes.data(), 100); REQUIRE_THROWS(staged.finish()); }
        SECTION("Corrupt") { auto corrupt = bytes; corrupt[10] = 'y'; staged.append(corrupt.data(), corrupt.size()); REQUIRE_THROWS(staged.finish()); }
        SECTION("Too large") { REQUIRE_THROWS(staged.append(bytes.data(), bytes.size() + 1)); }
    }
    REQUIRE(fs::is_empty(f.directory));
    REQUIRE_THROWS(StagedPackage(f.directory.u8string(), release, "../../update.exe"));
    REQUIRE(fs::is_empty(f.directory));
}
TEST_CASE("Desktop transport cancellation and forbidden schemes clean up") {
    Fixture f;
    auto release = fixture_release();
    std::atomic<bool> cancel{true}; std::atomic<int> percent{0};
    release.url = "file:///etc/passwd";
    REQUIRE_THROWS(Slic3r::DesktopUpdater::download_package(f.directory.u8string(), release, "update.exe", cancel, percent));
    REQUIRE(fs::is_empty(f.directory));
    cancel = false;
    REQUIRE_THROWS(Slic3r::DesktopUpdater::download_package(f.directory.u8string(), release, "update.dmg", cancel, percent));
    REQUIRE(fs::is_empty(f.directory));
}
