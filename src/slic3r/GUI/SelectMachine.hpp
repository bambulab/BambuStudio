#ifndef slic3r_GUI_SelectMachine_hpp_
#define slic3r_GUI_SelectMachine_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>
#include <wx/dataview.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/dataview.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/hyperlink.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"
#include "Plater.hpp"
#include "BBLStatusBar.hpp"


namespace Slic3r { 
namespace GUI {

// move to seperate file
class MachineListModel : public wxDataViewVirtualListModel
{
public:
    enum
    {
        Col_MachineName,
        Col_MachineSN,
        Col_MachineBind,
        Col_MachineConnection,
        Col_Max
    };
    MachineListModel();

    virtual unsigned int GetColumnCount() const wxOVERRIDE
    {
        return Col_Max;
    }

    virtual wxString GetColumnType(unsigned int col) const wxOVERRIDE
    {
        return "string";
    }

    virtual void GetValueByRow(wxVariant& variant,
        unsigned int row, unsigned int col) const wxOVERRIDE;
    virtual bool GetAttrByRow(unsigned int row, unsigned int col,
        wxDataViewItemAttr& attr) const wxOVERRIDE;
    virtual bool SetValueByRow(const wxVariant& variant,
        unsigned int row, unsigned int col) wxOVERRIDE;


    void display_machines(std::map<std::string, MachineObject*> list);
    void add_machine(MachineObject* obj);
    int find_row_by_sn(wxString sn);

private:
    wxArrayString    m_nameColValues;
    wxArrayString    m_snColValues;
    wxArrayString    m_bindColValues;
    wxArrayString    m_connectionColValues;
};


class MachineObjectPanel : public wxPanel
{
	private:
        wxColour m_bg_colour;
        wxColour m_hover_colour;

        MachineObject* obj_;
        wxBitmap ams_placeholder_img;
        wxBitmap printing_img;
        wxBitmap owner_img;
        void init_bitmap();

	protected:
		wxStaticBitmap* m_bitmap_type;
		wxStaticText* m_staticText_printer;
		wxStaticBitmap* m_bitmap_info;
		wxStaticText* m_staticText_printing;
		wxStaticBitmap* m_bitmap_bind;
		wxStaticText* m_staticText_bind_info;
		wxStaticBitmap* m_bitmap_ams;

	public:
		MachineObjectPanel( wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 240,100 ), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString );

		~MachineObjectPanel();

        void update_machine_info(MachineObject* obj);
        void on_mouse_enter(wxMouseEvent& evt);
        void on_mouse_leave(wxMouseEvent& evt);
        void on_mouse_left_up(wxMouseEvent& evt);
};

class SelectMachinePopup : public wxPopupTransientWindow
{
public:
    SelectMachinePopup(wxWindow* parent, bool scrolled);
    ~SelectMachinePopup() {}

    // wxPopupTransientWindow virtual methods are all overridden to log them
    virtual void Popup(wxWindow *focus = NULL) wxOVERRIDE;
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent& event) wxOVERRIDE;
    virtual bool Show( bool show = true ) wxOVERRIDE;

    void update_machine_list(std::vector<MachineObject*> obj_list);

private:
    const int POPUP_WIDTH   = 211;
    const int POPUP_HEIGHT  = 326;
    wxColour m_bg_colour;
    wxColour m_hover_colour;

    wxScrolledWindow*    m_panel;
    wxBoxSizer*          topSizer;
    wxStaticText*        m_staticText_select;
    wxTimer*             m_refresh_timer;
    std::vector<MachineObjectPanel*> obj_panels;
    std::vector<MachineObject*>     m_obj_list;

private:
    void OnMouse( wxMouseEvent &event );
    void OnSize( wxSizeEvent &event );
    void OnSetFocus( wxFocusEvent &event );
    void OnKillFocus( wxFocusEvent &event );
    void OnButton(wxCommandEvent& event);

    void on_timer(wxTimerEvent& event);

private:
    wxDECLARE_ABSTRACT_CLASS(SelectMachinePopup);
    wxDECLARE_EVENT_TABLE();
};


class SelectMachineDialog : public DPIDialog
{
private:
    void init_model();
    void init_bind();
    void init_timer();
public:
    SelectMachineDialog(Plater* plater = nullptr, int print_plate_idx = 0);
    ~SelectMachineDialog();

    /* model */
    wxObjectDataPtr<MachineListModel> machine_model;
    wxString        machine_sn;
    wxString        current_dev_id;
    Plater*         m_plater;
    std::shared_ptr<BBLStatusBar> m_status_bar;
    int             m_print_plate_idx;

protected:
    wxDataViewCtrl* m_dataViewListCtrl_machines;
	wxStaticText* m_staticText_left;
	wxHyperlinkCtrl* m_hyperlink_add_machine;
	wxGauge* m_gauge_job_progress;
	wxPanel* m_panel_status;
	wxButton* m_button_cancel;
	wxButton* m_button_ensure;

    wxTimer* m_refresh_timer;

    std::shared_ptr<PrintJob> m_print_job;

    // Virtual event handlers, overide them in your derived class
    void on_cancel(wxCommandEvent& event);
    void on_ok(wxCommandEvent& event);
    void on_timer(wxTimerEvent& event);
    void on_selection_changed(wxDataViewEvent& event);
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

} // namespace GUI
} // namespace Slic3r

#endif
