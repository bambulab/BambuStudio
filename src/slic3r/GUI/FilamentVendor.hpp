#ifndef slic3r_FilamentVendor_hpp_
#define slic3r_FilamentVendor_hpp_

#include <wx/string.h>

namespace Slic3r { namespace GUI {

// Group vendors without changing their stored spelling, presets, or IDs.
struct FilamentVendorLess {
    bool operator()(const wxString& left, const wxString& right) const
    {
        return left.CmpNoCase(right) < 0;
    }
};

}} // namespace Slic3r::GUI

#endif
