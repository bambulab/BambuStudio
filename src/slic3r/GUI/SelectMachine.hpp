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

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"

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


class SelectMachineDialog : public DPIDialog
{
private:
    void init_model();
    void init_bind();
    void init_timer();
public:
    SelectMachineDialog();
    ~SelectMachineDialog();

    /* model */
    wxObjectDataPtr<MachineListModel> machine_model;
    wxString machine_sn;

protected:
    wxDataViewCtrl* m_dataViewListCtrl_machines;
    wxStaticText* m_staticText_left;
    wxHyperlinkCtrl* m_hyperlink_add_machine;
    wxButton* m_button_cancel;
    wxButton* m_button_ensure;

    wxTimer* m_refresh_timer;

    // Virtual event handlers, overide them in your derived class
    void on_cancel(wxCommandEvent& event);
    void on_ok(wxCommandEvent& event);
    void on_timer(wxTimerEvent& event);
    void on_selection_changed(wxDataViewEvent& event);

    void on_dpi_changed(const wxRect& suggested_rect) override;

    //TODO timer to update list
};

} // namespace GUI
} // namespace Slic3r

#endif
