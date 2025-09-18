#ifndef slic3r_GUI_StaticGroup_hpp_
#define slic3r_GUI_StaticGroup_hpp_

#include "../wxExtensions.hpp"

#include <wx/statbox.h>

class HoverLabel;

class StaticGroup : public wxStaticBox
{
public:
    StaticGroup(wxWindow *parent, wxWindowID id, const wxString &label);

public:
    void ShowBadge(bool show);
    void SetBorderColor(const wxColour &color);
    void SetEditEnabled(bool enable);
    void SetOnHoverClick(std::function<void()> on_click);
    void SetCount(int count);
    bool Show(bool show=true) override;
    void ResetLabelPos();
private:
#ifdef __WXMSW__
    void OnPaint(wxPaintEvent &evt);
    void PaintForeground(wxDC &dc, const struct tagRECT &rc) override;
#endif

private:
#ifdef __WXMSW__
    ScalableBitmap badge;
#endif
#ifdef __WXOSX__
    ScalableButton * badge { nullptr };
#endif
    wxColour       borderColor_;
    HoverLabel*    hoverLabel_;
};

#endif // !slic3r_GUI_StaticGroup_hpp_
