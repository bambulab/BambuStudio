#ifndef slic3r_MultiCameraPage_hpp_
#define slic3r_MultiCameraPage_hpp_

#include "GUI_Utils.hpp"
#include "MediaPlayCtrl.h"

#include <wx/panel.h>
#include <wx/timer.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

class Button;
class CheckBox;
class ComboBox;
class Label;
class ProgressBar;

namespace Slic3r {

class MachineObject;

namespace GUI {

#define MULTI_CAMERA_SLOT_COUNT 4

enum class MultiCameraLayout : int
{
    Single = 0,    // one printer, info beside the video
    Double,        // two printers side by side
    TripleColumns, // three printers side by side
    TripleFocus,   // one large printer and two small ones
    Quad,          // 2 x 2 grid
    Count
};

enum class MultiCameraSource : int
{
    Auto = 0,  // cloud first, LAN when the cloud refuses the live view
    LanFirst,  // LAN first, cloud if the printer ip is unknown
    CloudOnly, // behave like the Device page
    Count
};

// One cell of the camera wall: printer picker, live view and the printer status.
class CameraSlotPanel : public wxPanel
{
public:
    CameraSlotPanel(wxWindow *parent, int slot);

    const std::string &get_dev_id() const { return m_dev_id; }
    void               set_dev_id(const std::string &dev_id);
    void               request_autoplay() { m_autoplay_pending = true; }

    void set_horizontal(bool horizontal);
    void set_camera_source(MultiCameraSource source);
    void set_keep_alive(bool keep_alive);
    void play();
    void stop();

    void refresh_printer_list(const std::map<std::string, MachineObject *> &machines);
    void update(MachineObject *obj);
    void msw_rescale();

    // called when the user picks another printer in this slot
    std::function<void(int slot)> on_printer_changed;

private:
    struct InfoItem
    {
        Label *  title{nullptr};
        wxSizer *value_sizer{nullptr};
        Label *  value{nullptr};
    };

    void create_info_panel();
    void layout_body();
    void layout_info();
    Label *add_info_item(const wxString &title);

    void on_printer_selected(wxCommandEvent &evt);
    void update_status(MachineObject *obj);
    void update_task(MachineObject *obj);
    void update_temperatures(MachineObject *obj);
    void update_filament(MachineObject *obj);
    void update_others(MachineObject *obj);
    void clear_info();

private:
    int                      m_slot;
    std::string              m_dev_id;
    std::string              m_printer_list_key;
    std::vector<std::string> m_combo_dev_ids;
    bool                     m_autoplay_pending{true};
    bool                     m_horizontal{false};

    ComboBox *     m_printer_combo{nullptr};
    Label *        m_status_label{nullptr};
    Button *       m_detail_button{nullptr};
    wxMediaCtrl3 * m_media_ctrl{nullptr};
    MediaPlayCtrl *m_media_play_ctrl{nullptr};

    wxPanel *             m_info_panel{nullptr};
    std::vector<InfoItem> m_info_items;
    Label *               m_task_label{nullptr};
    Label *               m_plate_label{nullptr};
    ProgressBar *         m_progress_bar{nullptr};
    Label *               m_percent_label{nullptr};
    wxPanel *             m_filament_swatch{nullptr};

    Label *m_layer_label{nullptr};
    Label *m_left_time_label{nullptr};
    Label *m_finish_time_label{nullptr};
    Label *m_total_time_label{nullptr};
    Label *m_weight_label{nullptr};
    Label *m_speed_label{nullptr};
    Label *m_nozzle_label{nullptr};
    Label *m_bed_label{nullptr};
    Label *m_chamber_label{nullptr};
    Label *m_fan_label{nullptr};
    Label *m_filament_label{nullptr};
    Label *m_ams_label{nullptr};
    Label *m_nozzle_info_label{nullptr};
    Label *m_lamp_label{nullptr};
    Label *m_wifi_label{nullptr};
    Label *m_hms_label{nullptr};
};

// Device tab showing the live view and status of up to 4 printers at once.
class MultiCameraPage : public wxPanel
{
public:
    MultiCameraPage(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~MultiCameraPage();

    bool Show(bool show) override;
    void msw_rescale();
    void on_sys_color_changed();

private:
    void create_toolbar();
    void apply_layout();
    void apply_camera_source();
    int  visible_slot_count() const;

    void on_timer(wxTimerEvent &event);
    void on_slot_changed(int slot);
    void update_subscribe();
    void request_full_status();

private:
    wxTimer *                      m_refresh_timer{nullptr};
    wxPanel *                      m_toolbar{nullptr};
    ComboBox *                     m_layout_combo{nullptr};
    ComboBox *                     m_source_combo{nullptr};
    CheckBox *                     m_keep_alive_check{nullptr};
    Button *                       m_play_all_button{nullptr};
    Button *                       m_stop_all_button{nullptr};
    wxPanel *                      m_grid_panel{nullptr};
    std::vector<CameraSlotPanel *> m_slots;
    std::vector<std::string>       m_subscribed;
    MultiCameraLayout              m_layout{MultiCameraLayout::Quad};
    MultiCameraSource              m_source{MultiCameraSource::Auto};
    bool                           m_user_stopped{false};
    int                            m_tick{0};
    int                            m_push_all_retry{0};
};

} // namespace GUI
} // namespace Slic3r

#endif
