//
//  MediaFilePanel.h
//  libslic3r_gui
//
//  Created by cmguo on 2021/12/7.
//

#ifndef MediaFilePanel_h
#define MediaFilePanel_h

#include "GUI_Utils.hpp"

#include <wx/frame.h>

class Button;
class SwitchButton;
class Label;
class StaticBox;

namespace Slic3r {

class MachineObject;

namespace GUI {

class ImageGrid;

class MediaFilePanel : public wxPanel
{
public:
    MediaFilePanel(wxWindow * parent);

    void SetMachineObject(MachineObject * obj);

public:
    void Rescale();

private:
    void fileChanged(wxCommandEvent & e);

private:
    ::StaticBox *m_tab_panel = nullptr;
    ::Button    *m_tab_button_year = nullptr;
    ::Button    *m_tab_button_month = nullptr;
    ::Button    *m_tab_button_all = nullptr;
    ::Label     *m_switch_label = nullptr;
    ::SwitchButton * m_switch_button = nullptr;

    MachineObject * m_machine = nullptr;
    ImageGrid * m_image_grid = nullptr;

    int m_last_mode = 0;
};


class MediaFileFrame : public DPIFrame
{
public:
    MediaFileFrame(wxWindow * parent);

    MediaFilePanel * filePanel() { return m_panel; }

    virtual void on_dpi_changed(const wxRect& suggested_rect);

private:
    MediaFilePanel* m_panel;
};

}}
#endif /* MediaFilePanel_h */
