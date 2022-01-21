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

    boost::shared_ptr<PrinterFileSystem> GetFileSystem() { return m_file_sys; }

    void SetGroupMode(int mode);

public:
    void Rescale();

protected:
    void Select(int index);

    void UpdateFileSystem();

    void UpdateLayout();

protected:

    void changedEvent(wxCommandEvent& evt);

    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

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

    bool m_hovered = false;
    bool m_pressed = false;
    wxTimer m_timer;
    wxBitmap m_mask;

    int m_row_offset = 0; // 1/4 row height
    int m_row_count = 0; // 1/4 row height
    int m_col_count = 1;
    wxSize m_image_size;
    wxSize m_cell_size;
};

}}

#endif /* ImageGrid_h */
