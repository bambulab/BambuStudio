/**
 * \file decimal_comma_tests.cpp
 * \brief G-code export must not depend on the calling thread's C locale (GitHub #13145 / #13287)
 *
 * The export pipeline runs its stages on whichever thread holds the token. A thread whose C locale
 * uses a decimal comma used to (1) write "; Z_HEIGHT: 1,2" comments with sprintf and (2) read G-code
 * with atof()/strtod(), which stop at the '.', so a Z move within the same millimeter looked like a
 * zero-length move and the cooling stage deleted it. Each test switches LC_NUMERIC of the test thread
 * to a decimal-comma locale, and is skipped with a warning when the system has none of them.
 */

#include <catch_main.hpp>

#include <clocale>
#include <cstdio>
#include <string>
#include <vector>

#include "libslic3r/GCode.hpp"
#include "libslic3r/GCode/GCodeEditor.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/LocalesUtils.hpp"

using namespace Slic3r;

namespace {

/**
 * \brief Switches LC_NUMERIC of the calling thread to a decimal-comma locale and restores it on destruction
 *
 * On Windows the per-thread locale is enabled first, so other threads are not affected. On POSIX
 * setlocale() is process wide, which is fine for this single-threaded test.
 */
class DecimalCommaLocale
{
public:
    DecimalCommaLocale()
    {
#ifdef _WIN32
        m_prev_thread_state = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
        static const char *names[] = { "hu-HU", "de-DE", "Hungarian_Hungary.1250", "German_Germany.1252" };
#else
        static const char *names[] = { "de_DE.UTF-8", "hu_HU.UTF-8", "de_DE.utf8", "hu_HU.utf8", "de_DE", "hu_HU" };
#endif
        if (const char *prev = std::setlocale(LC_NUMERIC, nullptr))
            m_prev = prev;
        for (const char *name : names)
            if (std::setlocale(LC_NUMERIC, name) != nullptr && !is_decimal_separator_point()) {
                m_name = name;
                return;
            }
        // None available: put the original back.
        std::setlocale(LC_NUMERIC, m_prev.c_str());
    }
    ~DecimalCommaLocale()
    {
        std::setlocale(LC_NUMERIC, m_prev.c_str());
#ifdef _WIN32
        if (m_prev_thread_state != -1)
            _configthreadlocale(m_prev_thread_state);
#endif
    }
    bool               active() const { return !m_name.empty(); }
    const std::string &name() const { return m_name; }

private:
    std::string m_prev;
    std::string m_name;
#ifdef _WIN32
    int         m_prev_thread_state{ -1 };
#endif
};

#define REQUIRE_DECIMAL_COMMA(locale)                                                                    \
    if (!(locale).active()) {                                                                            \
        WARN("SKIPPED: no decimal-comma locale (hu-HU / de-DE / de_DE.UTF-8 / hu_HU.UTF-8) can be set"); \
        return;                                                                                          \
    }

/**
 * \brief Runs one layer through the cooling stage the way GCode::process_layers() does: process_layer(), then write_layer_gcode()
 */
std::string run_cooling_stage(const std::string &layer_gcode)
{
    GCode gcodegen;
    // GCode::set_extruders() is private; the editor only reads the writer's extruder list.
    gcodegen.writer().set_extruders({ 0 });
    GCodeEditor editor(gcodegen);
    editor.set_current_extruder(0);

    std::vector<PerExtruderAdjustments> adjustments;
    bool                                not_set_additional_fan = false;
    std::string out = editor.process_layer(std::string(layer_gcode), not_set_additional_fan, 1, adjustments, {}, true, false);
    return editor.write_layer_gcode(out, not_set_additional_fan, 1, 100.f, adjustments);
}

// A lift to Z1.4 at the travel feedrate, then the descent to the layer Z1.2 at the same feedrate,
// as GCodeWriter writes them on a layer change (travel_speed_z equal to the travel speed).
const std::string LAYER =
    "G1 X144.202 Y245.81 Z1.4 F42000\n"
    "G1 Z1.2 F42000\n"
    "G1 X150 Y245.81 E.5 F3000\n";

} // namespace

TEST_CASE("Cooling stage keeps a descent within the same millimeter in the C locale", "[DecimalComma]")
{
    // Control: true with and without the fix.
    const std::string out = run_cooling_stage(LAYER);
    INFO(out);
    CHECK(out.find("G1 Z1.2\n") != std::string::npos);
}

TEST_CASE("Cooling stage keeps a descent within the same millimeter in a decimal-comma locale", "[DecimalComma]")
{
    const std::string expected = run_cooling_stage(LAYER); // C locale
    DecimalCommaLocale locale;
    REQUIRE_DECIMAL_COMMA(locale);
    INFO("locale " << locale.name());

    // Unpatched 2.9.2: atof() reads Z1.4 and Z1.2 as 1, so the descent has length 0 and the same F,
    // and write_layer_gcode() deletes the whole line. Fixed: the output is byte for byte the C-locale output,
    // where the descent stays and only its F is stripped.
    const std::string out = run_cooling_stage(LAYER);
    INFO(out);
    CHECK(out.find("G1 Z1.2\n") != std::string::npos);
    CHECK(out == expected);
}

TEST_CASE("GCodeReader::GCodeLine::has_value reads a decimal point in a decimal-comma locale", "[DecimalComma]")
{
    DecimalCommaLocale locale;
    REQUIRE_DECIMAL_COMMA(locale);
    INFO("locale " << locale.name());

    // GCodeProcessor reads G4 S/P, M104 S, M106 S with has_value() on the pipeline's output thread.
    // Unpatched 2.9.2: strtod() stops at the '.', the word is not fully read, and has_value() returns false.
    GCodeReader reader;
    bool        seen = false;
    reader.parse_buffer("G4 S1.5\n", [&seen](GCodeReader &, const GCodeReader::GCodeLine &line) {
        float value = 0.f;
        CHECK(line.has_value('S', value));
        CHECK(value == 1.5f);
        seen = true;
    });
    CHECK(seen);
}

TEST_CASE("CNumericLocalesSetter makes sprintf write a decimal point in a decimal-comma locale", "[DecimalComma]")
{
    DecimalCommaLocale locale;
    REQUIRE_DECIMAL_COMMA(locale);

    // Helper level only: GCode::process_layer() cannot be reached without a full slice, and a full slice
    // would not tell the builds apart, because GCode::do_export() already sets "C" on the calling thread.
    // This checks the mechanism the fix in process_layer() relies on; it passes with and without the fix.
    char buf[64];
    {
        CNumericLocalesSetter locales_setter;
        snprintf(buf, sizeof(buf), "; Z_HEIGHT: %g\n", 1.2);
        CHECK(std::string(buf) == "; Z_HEIGHT: 1.2\n");
    }
    snprintf(buf, sizeof(buf), "%g", 1.2);
    CHECK(std::string(buf) == "1,2");
}
