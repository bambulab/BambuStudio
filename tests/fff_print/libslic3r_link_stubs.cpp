#ifndef _MSC_VER

#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/BBLUtil.hpp"
#include "nanosvg/nanosvg.h"

#include <cstdlib>

// Link-only stubs for GUI-layer symbols referenced by libslic3r; tests never call them.
// Abort on accidental use rather than silently substituting production behaviour.
namespace Slic3r {
struct Http::priv {};
Http::~Http() { std::abort(); }
Http Http::get(std::string) { std::abort(); }
Http &Http::on_complete(CompleteFn) { std::abort(); }
Http &Http::on_error(ErrorFn) { std::abort(); }
Http &Http::timeout_max(long) { std::abort(); }
void Http::perform_sync() { std::abort(); }

bool BBL_Encrypt::AES256CBC_Encrypt(unsigned char *, unsigned, unsigned char *, unsigned &,
                                    const std::string &, const std::string &) { std::abort(); }
bool BBL_Encrypt::AES256CBC_Decrypt(unsigned char *, unsigned, unsigned char *, unsigned &,
                                    const std::string &, const std::string &) { std::abort(); }
} // namespace Slic3r

extern "C" {
NSVGimage *nsvgParse(char *, const char *, float) { std::abort(); }
NSVGimage *nsvgParseFromFile(const char *, const char *, float) { std::abort(); }
void nsvgDelete(NSVGimage *) { std::abort(); }
}

#endif
