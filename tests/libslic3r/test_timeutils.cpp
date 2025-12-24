#include <catch2/catch.hpp>

#include "libslic3r/Time.hpp"
#include "libslic3r/Utils.hpp"

#include <sstream>
#include <iomanip>
#include <locale>

using namespace Slic3r;

static void test_time_fmt(Slic3r::Utils::TimeFormat fmt) {
    using namespace Slic3r::Utils;
    time_t t = get_current_time_utc();

    std::string tstr = time2str(t, TimeZone::local, fmt);
    time_t parsedtime = str2time(tstr, TimeZone::local, fmt);
    REQUIRE(t == parsedtime);

    tstr = time2str(t, TimeZone::utc, fmt);
    parsedtime = str2time(tstr, TimeZone::utc, fmt);
    REQUIRE(t == parsedtime);

    parsedtime = str2time("not valid string", TimeZone::local, fmt);
    REQUIRE(parsedtime == time_t(-1));

    parsedtime = str2time("not valid string", TimeZone::utc, fmt);
    REQUIRE(parsedtime == time_t(-1));
}

TEST_CASE("ISO8601Z", "[Timeutils]") {
    test_time_fmt(Slic3r::Utils::TimeFormat::iso8601Z);

    std::string mydate = "20190710T085000Z";
    time_t t = Slic3r::Utils::parse_iso_utc_timestamp(mydate);
    std::string date = Slic3r::Utils::iso_utc_timestamp(t);

    REQUIRE(date == mydate);
}

TEST_CASE("Slic3r_UTC_Time_Format", "[Timeutils]") {
    using namespace Slic3r::Utils;
    test_time_fmt(TimeFormat::gcode);

    std::string mydate = "2019-07-10 at 08:50:00 UTC";
    time_t t = Slic3r::Utils::str2time(mydate, TimeZone::utc, TimeFormat::gcode);
    std::string date = Slic3r::Utils::utc_timestamp(t);

    REQUIRE(date == mydate);
}

TEST_CASE("12_Hour_Time_Format", "[Timeutils]") {
    // Test format_time_hm centralized function with std::tm

    std::tm tm = {};

    // Test midnight (0:00) -> 12:00AM
    tm.tm_hour = 0;
    tm.tm_min = 0;
    auto result = format_time_hm(&tm, true);
    REQUIRE(result == "12:00AM");

    // Test noon (12:00) -> 12:00PM
    tm.tm_hour = 12;
    tm.tm_min = 0;
    result = format_time_hm(&tm, true);
    REQUIRE(result == "12:00PM");

    // Test morning (9:30) -> 09:30AM
    tm.tm_hour = 9;
    tm.tm_min = 30;
    result = format_time_hm(&tm, true);
    REQUIRE(result == "09:30AM");

    // Test afternoon (15:45) -> 03:45PM
    tm.tm_hour = 15;
    tm.tm_min = 45;
    result = format_time_hm(&tm, true);
    REQUIRE(result == "03:45PM");

    // Test evening (23:59) -> 11:59PM
    tm.tm_hour = 23;
    tm.tm_min = 59;
    result = format_time_hm(&tm, true);
    REQUIRE(result == "11:59PM");

    // Test 24-hour format (should not add AM/PM)
    tm.tm_hour = 15;
    tm.tm_min = 45;
    result = format_time_hm(&tm, false);
    REQUIRE(result == "15:45");

    // Test get_bbl_finish_time_dhm with 12-hour format
    // Test with 1 hour remaining (3600 seconds)
    std::string finish_time_24h = get_bbl_finish_time_dhm(3600.0f, false);
    std::string finish_time_12h = get_bbl_finish_time_dhm(3600.0f, true);

    // 24-hour format should have HH:MM format
    REQUIRE(finish_time_24h.find(":") != std::string::npos);
    REQUIRE(finish_time_24h.find("AM") == std::string::npos);
    REQUIRE(finish_time_24h.find("PM") == std::string::npos);

    // 12-hour format should have either AM or PM
    REQUIRE((finish_time_12h.find("AM") != std::string::npos || finish_time_12h.find("PM") != std::string::npos));
}
