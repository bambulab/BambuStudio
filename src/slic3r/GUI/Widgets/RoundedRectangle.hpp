#pragma once

#include "../wxExtensions.hpp"

/**
 * @brief A rounded-rectangle surface with an optional fill and an optional 1px border.
 *
 * Anti-aliased and flicker-free, with corners that blend into the parent so it reads
 * correctly in both light and dark themes. Can hold children like any wxWindow.
 * An invalid wxColour() means "omit that part" (no fill, or no border).
 */
class RoundedRectangle : public wxWindow
{
public:
    RoundedRectangle(wxWindow *parent, const wxColour &fill, const wxColour &border, double radius, wxPoint pos = wxDefaultPosition, wxSize size = wxDefaultSize);
    ~RoundedRectangle(){};

    void SetFillColour(const wxColour &fill);

private:
    double   m_radius;
    wxColour m_fill;
    wxColour m_border;

public:
    void OnPaint(wxPaintEvent &evt);
    DECLARE_EVENT_TABLE()
};
