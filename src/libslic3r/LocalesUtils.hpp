#ifndef slic3r_LocalesUtils_hpp_
#define slic3r_LocalesUtils_hpp_

#include <string>
#include <clocale>
#include <iomanip>
#include <cassert>
#include <string_view>

#ifdef __APPLE__
#include <xlocale.h>
#endif

namespace Slic3r {

// RAII wrapper that sets LC_NUMERIC to "C" on construction
// and restores the old value on destruction.
class CNumericLocalesSetter {
public:
    CNumericLocalesSetter();
    ~CNumericLocalesSetter();

private:
#ifdef _WIN32
    std::string m_orig_numeric_locale;
#else
    locale_t m_original_locale;
    locale_t m_new_locale;
#endif

};

// A function to check that current C locale uses decimal point as a separator.
// Intended mostly for asserts.
bool is_decimal_separator_point();


// A substitute for std::to_string that works according to
// C++ locales, not C locale. Meant to be used when we need
// to be sure that decimal point is used as a separator.
// (We use user C locales and "C" C++ locales in most of the code.)
std::string float_to_string_decimal_point(double value, int precision = -1);
//std::string float_to_string_decimal_point(float value,  int precision = -1);
double string_to_double_decimal_point(const std::string_view str, size_t* pos = nullptr);
// strtod() as in the "C" locale, whatever the calling thread's C locale is: skips leading white space
// and one '+', reads no further than end, and sets *pend past the number (to str when nothing was converted).
// Unlike strtod(), it does not read hexadecimal floats.
double strtod_decimal_point(const char *str, const char *end, const char **pend = nullptr);

} // namespace Slic3r

#endif // slic3r_LocalesUtils_hpp_
