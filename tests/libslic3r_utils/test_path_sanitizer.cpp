#include <catch2/catch.hpp>

#include "libslic3r/Utils.hpp"

#include <cstdlib>
#include <initializer_list>
#include <string>

using Slic3r::PathSanitizer;

namespace {

#ifdef _WIN32
constexpr const char *profile_env = "USERPROFILE";
constexpr const char *profile     = "C:\\Users\\bob";
constexpr char        sep         = '\\';
    #if BBL_RELEASE_TO_PUBLIC
constexpr const char *app_user_dir = "AppData\\Roaming\\BambuStudio\\user";
    #else
constexpr const char *app_user_dir = BBL_INTERNAL_TESTING == 1 ? "AppData\\Roaming\\BambuStudioInternal\\user" :
                                                                 "AppData\\Roaming\\BambuStudioBeta\\user";
    #endif
#elif __APPLE__
constexpr const char *profile_env = "HOME";
constexpr const char *profile     = "/Users/bob";
constexpr char        sep         = '/';
    #if BBL_RELEASE_TO_PUBLIC
constexpr const char *app_user_dir = "Library/Application Support/BambuStudio/user";
    #else
constexpr const char *app_user_dir = BBL_INTERNAL_TESTING == 1 ? "Library/Application Support/BambuStudioInternal/user" :
                                                                 "Library/Application Support/BambuStudioBeta/user";
    #endif
#else
constexpr const char *profile_env = "HOME";
constexpr const char *profile     = "/home/bob";
constexpr char        sep         = '/';
    #if BBL_RELEASE_TO_PUBLIC
constexpr const char *app_user_dir = ".config/BambuStudio/user";
    #else
constexpr const char *app_user_dir = BBL_INTERNAL_TESTING == 1 ? ".config/BambuStudioInternal/user" : ".config/BambuStudioBeta/user";
    #endif
#endif

// PathSanitizer reads the profile directory once, on first use, so it has to be in place before any test runs.
const bool profile_installed = [] {
#ifdef _WIN32
    return _putenv_s(profile_env, profile) == 0;
#else
    return setenv(profile_env, profile, 1) == 0;
#endif
}();

/**
 * \brief Joins path components with the native separator
 */
std::string join(std::initializer_list<std::string> parts)
{
    std::string out;
    bool        first = true;
    for (const std::string &part : parts) {
        if (!first)
            out += sep;
        out += part;
        first = false;
    }
    return out;
}

bool is_valid_utf8(const std::string &s)
{
    for (size_t i = 0; i < s.size();) {
        const auto   c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size())
            return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
                return false;
        i += n;
    }
    return true;
}

} // namespace

TEST_CASE("PathSanitizer test setup", "[PathSanitizer]")
{
    REQUIRE(profile_installed);
}

TEST_CASE("PathSanitizer returns only the file name of ordinary paths", "[PathSanitizer]")
{
    CHECK(PathSanitizer::sanitize(join({profile, "Documents", "cube.3mf"})) == "cube.3mf");
    CHECK(PathSanitizer::sanitize(join({"", "models", "a_rather_long_folder", "cube.stl"})) == "cube.stl");
    CHECK(PathSanitizer::sanitize("cube.stl") == "cube.stl");
    CHECK(PathSanitizer::sanitize(join({profile, "Documents", ""})) == "");
}

TEST_CASE("PathSanitizer handles empty and null input", "[PathSanitizer]")
{
    CHECK(PathSanitizer::sanitize(std::string()) == "");
    CHECK(PathSanitizer::sanitize(static_cast<const char *>(nullptr)) == "");
    CHECK(PathSanitizer::sanitize("") == "");
}

TEST_CASE("PathSanitizer masks the user name", "[PathSanitizer]")
{
    const std::string profile_dir = profile;
    const std::string users_dir   = profile_dir.substr(0, profile_dir.size() - 3);

    // The profile directory itself ends in the user name.
    CHECK(PathSanitizer::sanitize(profile_dir) == "***");

    // A longer name starting with the capitalised user name is masked with a fixed width.
    CHECK(PathSanitizer::sanitize(users_dir + "Bobby") == "*****");

    // A longer name that is not the capitalised user name is someone else's directory.
    CHECK(PathSanitizer::sanitize(users_dir + "bobby") == "bobby");
}

TEST_CASE("PathSanitizer masks the user id folder", "[PathSanitizer]")
{
    CHECK(PathSanitizer::sanitize(join({profile, app_user_dir, "1234567890"})) == "**********");
    CHECK(PathSanitizer::sanitize(join({profile, app_user_dir, "1234567890", "filament", "PLA.json"})) == "PLA.json");

    // Only numeric folders are user ids.
    CHECK(PathSanitizer::sanitize(join({profile, app_user_dir, "default"})) == "default");
}

TEST_CASE("PathSanitizer gives the same result for every overload", "[PathSanitizer]")
{
    const std::string path     = join({profile, app_user_dir, "1234567890"});
    const std::string expected = PathSanitizer::sanitize(path);

    CHECK(PathSanitizer::sanitize(path.c_str()) == expected);
    CHECK(PathSanitizer::sanitize(std::string(path)) == expected);
    CHECK(PathSanitizer::sanitize(boost::filesystem::path(path)) == expected);
}

TEST_CASE("PathSanitizer accepts UTF-8 paths", "[PathSanitizer]")
{
    // "模型.3mf" under "文档"
    const std::string model = "\xE6\xA8\xA1\xE5\x9E\x8B.3mf";
    const std::string docs  = "\xE6\x96\x87\xE6\xA1\xA3";

    CHECK(PathSanitizer::sanitize(join({profile, docs, model})) == model);
    CHECK(PathSanitizer::sanitize(join({"", docs, docs, model})) == model);
}

TEST_CASE("PathSanitizer never throws on bytes that are not UTF-8", "[PathSanitizer]")
{
    // "测试" and "模型.3mf" encoded in GBK, as left behind by tools writing paths in the local code page.
    const std::string gbk_dir   = "\xB2\xE2\xCA\xD4";
    const std::string gbk_model = "\xC4\xA3\xD0\xCD.3mf";

    std::string out;
    CHECK_NOTHROW(out = PathSanitizer::sanitize(join({"", gbk_dir, gbk_model})));
    CHECK(out == gbk_model);

    CHECK_NOTHROW(out = PathSanitizer::sanitize(join({profile, gbk_dir, gbk_model})));
    CHECK(out == gbk_model);
}

#ifdef _WIN32
TEST_CASE("PathSanitizer masks the user name on another drive", "[PathSanitizer]")
{
    // Not under the profile, so the whole path is returned with a fixed-width mask over the name.
    CHECK(PathSanitizer::sanitize("D:\\Users\\Bob\\x.3mf") == "D:\\Users\\*********");
    CHECK(PathSanitizer::sanitize("D:\\Users\\Bobby\\models\\x.3mf") == "D:\\Users\\************\\x.3mf");

    // Someone else's directory.
    CHECK(PathSanitizer::sanitize("D:\\Users\\alice\\x.3mf") == "x.3mf");
}

TEST_CASE("PathSanitizer never splits a UTF-8 character when masking", "[PathSanitizer]")
{
    // "Bobb模型模型.3mf": the fixed-width mask ends inside the 3rd CJK character and is widened over it.
    const std::string out = PathSanitizer::sanitize("D:\\Users\\Bobb\xE6\xA8\xA1\xE5\x9E\x8B\xE6\xA8\xA1\xE5\x9E\x8B.3mf");
    CHECK(out == "D:\\Users\\" + std::string(13, '*') + "\xE5\x9E\x8B.3mf");
    CHECK(is_valid_utf8(out));

    // Under the profile, "Bobbyy\模型.3mf": the mask ends inside "型".
    const std::string under = PathSanitizer::sanitize("C:\\Users\\Bobbyy\\\xE6\xA8\xA1\xE5\x9E\x8B.3mf");
    CHECK(under == std::string(13, '*') + ".3mf");
    CHECK(is_valid_utf8(under));
}

TEST_CASE("PathSanitizer accepts non-ASCII bytes where the user name is expected", "[PathSanitizer]")
{
    // "张三" in UTF-8 and in GBK: not an upper-case letter, so not taken for the user name.
    CHECK(PathSanitizer::sanitize("D:\\Users\\\xE5\xBC\xA0\xE4\xB8\x89\\x.3mf") == "x.3mf");
    CHECK(PathSanitizer::sanitize("D:\\Users\\\xD5\xC5\xC8\xFD\\x.3mf") == "x.3mf");
}
#endif
