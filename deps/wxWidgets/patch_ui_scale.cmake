# macOS uses logical pixels, so stock FromDIP/ToDIP are identity operations.
# Keep Retina backing scale separate from the user-selected interface density.
set(header "include/wx/window.h")
file(READ "${header}" content)
if(content MATCHES "bambu-ui-scale-percent")
    return()
endif()
string(REPLACE "#include \"wx/event.h\"" "#include \"wx/sysopt.h\"\n#include \"wx/math.h\"\n#include \"wx/event.h\"" content "${content}")
set(from [=[wxWindowBase::FromDIP(const wxSize& sz, const wxWindowBase* WXUNUSED(w))
{
    return sz;
}]=])
set(from_scaled [=[wxWindowBase::FromDIP(const wxSize& sz, const wxWindowBase* WXUNUSED(w))
{
#ifdef __WXOSX__
    const int percent = wxSystemOptions::GetOptionInt("bambu-ui-scale-percent");
    if (percent >= 75 && percent <= 150 && percent != 100)
        return wxSize(sz.x == -1 ? -1 : wxMulDivInt32(sz.x, percent, 100),
                      sz.y == -1 ? -1 : wxMulDivInt32(sz.y, percent, 100));
#endif
    return sz;
}]=])
set(to [=[wxWindowBase::ToDIP(const wxSize& sz, const wxWindowBase* WXUNUSED(w))
{
    return sz;
}]=])
set(to_scaled [=[wxWindowBase::ToDIP(const wxSize& sz, const wxWindowBase* WXUNUSED(w))
{
#ifdef __WXOSX__
    const int percent = wxSystemOptions::GetOptionInt("bambu-ui-scale-percent");
    if (percent >= 75 && percent <= 150 && percent != 100)
        return wxSize(sz.x == -1 ? -1 : wxMulDivInt32(sz.x, 100, percent),
                      sz.y == -1 ? -1 : wxMulDivInt32(sz.y, 100, percent));
#endif
    return sz;
}]=])
string(FIND "${content}" "${from}" from_pos)
string(FIND "${content}" "${to}" to_pos)
if(from_pos EQUAL -1 OR to_pos EQUAL -1)
    message(FATAL_ERROR "wxWidgets DIP implementation changed; review UI scale patch")
endif()
string(REPLACE "${from}" "${from_scaled}" content "${content}")
string(REPLACE "${to}" "${to_scaled}" content "${content}")
file(WRITE "${header}" "${content}")
