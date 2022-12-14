//
//  ImageGrid.h
//  libslic3r_gui
//
//  Created by cmguo on 2021/12/7.
//

#ifndef ImageGrid_h
#define ImageGrid_h

#include <wx/window.h>
#include <boost/shared_ptr.hpp>

#include "Widgets/StateColor.hpp"
#include "wxExtensions.hpp"

class Button;
class Label;

class PrinterFileSystem;

namespace Slic3r {

class MachineObject;

namespace GUI {

class ImageGrid : public wxWindow
{
public:
    ImageGrid(wxWindow * parent);

    void SetFileSystem(boost::shared_ptr<PrinterFileSystem> file_sys);

    void SetStatus(ScalableBitmap const & icon, wxString const &msg);

    boost::shared_ptr<PrinterFileSystem> GetFileSystem() { return m_file_sys; }

    void SetFileType(int type);

    void SetGroupMode(int mode);

    void SetSelecting(bool selecting);

    bool IsSelecting() { return m_selecting; }

    void DoActionOnSelection(int action);

public:
    void Rescale();

protected:
    void Select(size_t index);

    void DoAction(size_t index, int action);

    void UpdateFileSystem();

    void UpdateLayout();

    void UpdateFocusRange();

    std::pair<int, size_t> HitTest(wxPoint const &pt);

protected:

    void changedEvent(wxCommandEvent& evt);

    void paintEvent(wxPaintEvent& evt);

    size_t firstItem(wxSize const &size, wxPoint &off);

    wxBitmap createAlphaBitmap(wxSize size, wxColour color, int alpha1, int alpha2);

    wxBitmap createCircleBitmap(wxSize size, int borderWidth, int percent, wxColour fillColor, wxColour borderColor = wxTransparentColour);

    void render(wxDC &dc);

    void renderButtons(wxDC &dc, wxStringList const &texts, wxRect const &rect, size_t hit, int states);

    void renderText(wxDC &dc, wxString const & text, wxRect const & rect, int states);

    // some useful events
    void mouseMoved(wxMouseEvent& event);
    void mouseWheelMoved(wxMouseEvent& event);
    void mouseEnterWindow(wxMouseEvent& event);
    void mouseLeaveWindow(wxMouseEvent& event);
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void resize(wxSizeEvent& event);

    DECLARE_EVENT_TABLE()

private:
    boost::shared_ptr<PrinterFileSystem> m_file_sys;
    ScalableBitmap m_status_icon;
    wxString m_status_msg;

    ScalableBitmap m_checked_icon;
    ScalableBitmap m_unchecked_icon;
    StateColor m_buttonBackgroundColor;
    StateColor m_buttonTextColor;

    bool m_hovered = false;
    bool m_pressed = false;

    wxTimer m_timer;
    wxBitmap   m_mask;
    wxBitmap m_buttons_background;
    wxBitmap m_buttons_background_checked;
    // wxBitmap   m_button_background;

    bool m_selecting = false;

    enum HitType {
        HIT_NONE,
        HIT_ITEM,
        HIT_ACTION, // implicit HTI_ITEM
        HIT_MODE,
        HIT_STATUS
    };
    int     m_hit_type = HIT_NONE;
    size_t  m_hit_item = size_t(-1);

    int m_scroll_offset = 0;
    int m_row_offset = 0; // 1/4 row height
    int m_row_count = 0; // 1/4 row height
    int m_col_count = 1;
    wxSize m_image_size;
    wxSize m_cell_size;
};

}}

#endif /* ImageGrid_h */
