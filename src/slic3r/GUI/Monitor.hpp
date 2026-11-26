#ifndef slic3r_Monitor_hpp_
#define slic3r_Monitor_hpp_

#include "Tabbook.hpp"
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/grid.h>
#include <wx/dataview.h>
#include <wx/panel.h>
#include <wx/statline.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/gbsizer.h>
#include <wx/statbox.h>
#include <wx/tglbtn.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/webrequest.h>
#include <map>
#include <vector>
#include <memory>
#include "Event.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "wxExtensions.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/MonitorBasePanel.h"
#include "slic3r/GUI/StatusPanel.hpp"
#include "slic3r/GUI/UpgradePanel.hpp"
#include "slic3r/GUI/HMSPanel.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/AmsWidgets.hpp"
#include "Widgets/SideTools.hpp"
#include "SelectMachinePop.hpp"
#include "libslic3r_version.h"

namespace Slic3r {
namespace GUI {

class MediaFilePanel;
class DeviceWebHost;

class AddMachinePanel : public wxPanel
{
protected:
	Button* m_button_add_machine;
	wxStaticText* m_staticText_add_machine;
	wxStaticBitmap* m_bitmap_empty;

	void on_add_machine(wxCommandEvent& event);

public:

	AddMachinePanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);
	~AddMachinePanel();

	void msw_rescale();
};

class MonitorPanel : public wxPanel
{
private:
    Tabbook*		m_tabpanel{ nullptr };
    wxSizer*        m_main_sizer{ nullptr };

    AddMachinePanel*    m_status_add_machine_panel{ nullptr };
    StatusPanel*        m_status_info_panel{ nullptr };
    MediaFilePanel*     m_media_file_panel{ nullptr };
    UpgradePanel*       m_upgrade_panel{ nullptr };
    HMSPanel*           m_hms_panel{ nullptr };
    DeviceWebHost*      m_print_queue_panel { nullptr };

	/* side tools */
    SideTools*      m_side_tools{nullptr};
    wxStaticBitmap* m_bitmap_printer_type{ nullptr };
    wxStaticBitmap* m_bitmap_arrow{ nullptr };
    wxStaticText*   m_staticText_printer_name{ nullptr };
    wxStaticBitmap* m_bitmap_wifi_signal{ nullptr };
    wxBoxSizer *    m_side_tools_sizer{ nullptr };
    SelectMachinePopup m_select_machine;

	/* images */
    wxBitmap m_signal_strong_img;
    wxBitmap m_signal_middle_img;
    wxBitmap m_signal_weak_img;
    wxBitmap m_signal_no_img;
    wxBitmap m_printer_img;
    wxBitmap m_arrow_img;

    int last_wifi_signal = -1;
    int last_status{ 0 };
    bool m_initialized { false };
    bool update_flag{false};
    // Remaining traced update_all samples after ensure_content (avoid 1Hz log spam).
    int  m_perf_update_all_traces_left{ 0 };
    wxTimer* m_refresh_timer = nullptr;
    // One-shot. Fires the first load_last_machine/update_all after the shell paints.
    wxTimer* m_first_update_timer = nullptr;

    // Start or stop the refresh timer. `shown` is the visibility after this Show(),
    // which IsShown() does not reflect until wxPanel::Show returns.
    // `first` arms the one-shot machine bind instead of running it synchronously.
    void resume_on_show(bool shown, bool first = false);
    void on_deferred_first_update();

public:
    MonitorPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~MonitorPanel();

    enum PrinterTab {
        // Must stay in the order the pages are added in init_tabpanel().
        PT_STATUS      = 0,
#if STUDIO_ENABLE_PRINT_QUENE
        PT_PRINT_QUEUE,
#endif
        PT_MEDIA,
        PT_UPDATE,
        PT_HMS,
        PT_DEBUG,
        PT_MAX_NUM
    };

	void init_bitmap();
    // Allocate the refresh timer only. Show()/resume_on_show() starts it when the page is visible.
    void init_timer();
    void init_tabpanel();
    // Build tabbook / StatusPanel / etc. on first Show (empty shell is cheap at MainFrame init).
    void ensure_content();
    // Does not create widgets. May return nullptr before the first ensure_content().
    Tabbook* get_tabpanel();
    void set_default();
    wxWindow* create_side_tools();

    void on_sys_color_changed();
    void msw_rescale();
    void on_activate();

    // Does not force creation; may return nullptr before first ensure_content().
    StatusPanel* get_status_panel() { return m_status_info_panel; }
	void select_machine(std::string machine_sn);
    void on_timer(wxTimerEvent& event);
    void on_select_printer(wxCommandEvent& event);
    void on_printer_clicked(wxMouseEvent &event);
    void on_size(wxSizeEvent &event);

    /* update apis */
    //void update_ams(MachineObject* obj);
    void update_all();

    void update_print_queue_device(MachineObject* obj);

    void update_hms_tag();
    bool Show(bool show);

    void show_status(int status);

    std::string get_string_from_tab(PrinterTab tab);

    MachineObject *obj { nullptr };
    std::string last_conn_type = "undedefined";

    void stop_update() {update_flag = false;};
    void start_update() {update_flag = true;};


    void jump_to_HMS();
    void jump_to_Upgrade();
    void jump_to_LiveView();
    void jump_to_Rack();
    void jump_to_print_queue();
};


} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
