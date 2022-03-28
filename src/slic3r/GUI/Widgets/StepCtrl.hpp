#ifndef slic3r_GUI_StepCtrl_hpp_
#define slic3r_GUI_StepCtrl_hpp_

#include "StaticBox.hpp"

wxDECLARE_EVENT( EVT_STEP_CHANGING, wxCommandEvent );
wxDECLARE_EVENT( EVT_STEP_CHANGED, wxCommandEvent );

class StepCtrl : public StaticBox
{
    wxFont font_tip;
    StateColor clr_bar;
    StateColor clr_text;
    StateColor clr_tip;
    int radius = 7;
    int bar_width = 4;
    ScalableBitmap bmp_thumb;

    std::vector<wxString> steps;
    std::vector<wxString> tips;

    int step = -1;

    wxPoint drag_offset;
    wxPoint pos_thumb;

public:
    StepCtrl(wxWindow *      parent,
             wxWindowID      id,
             const wxPoint & pos       = wxDefaultPosition,
             const wxSize &  size      = wxDefaultSize,
             long            style     = 0);

    ~StepCtrl();

public:
    bool SetTipFont(wxFont const & font);

public:
    int AppendItem(const wxString &item, wxString const & tip = {});

    void DeleteAllItems();

    unsigned int GetCount() const;

    int  GetSelection() const;

    void SelectItem(int item);

    virtual void Rescale();

    wxString GetItemText(unsigned int item) const;
    void     SetItemText(unsigned int item, wxString const &value);

private:

    void mouseDown(wxMouseEvent &event);
    void mouseMove(wxMouseEvent &event);
    void mouseUp(wxMouseEvent &event);

    void doRender(wxDC & dc) override;

    // some useful events
    bool sendStepCtrlEvent(bool changing = false);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_StepCtrl_hpp_
