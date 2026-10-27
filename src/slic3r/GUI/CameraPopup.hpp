#ifndef slic3r_CameraPopup_hpp_
#define slic3r_CameraPopup_hpp_

#include "slic3r/GUI/MonitorBasePanel.h"
#include "DeviceManager.hpp"
#include "GUI.hpp"
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/statbmp.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <chrono>
#include <wx/gbsizer.h>
#include <wx/webrequest.h>
#include <wx/hyperlink.h>
#include <functional>
#include "Widgets/SwitchButton.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/PopupWindow.hpp"

namespace Slic3r {
namespace GUI {

wxDECLARE_EVENT(EVT_VCAMERA_SWITCH, wxCommandEvent);

class CameraPopup : public PopupWindow
{
public:
    CameraPopup(wxWindow *parent);
    virtual ~CameraPopup() {}

    // PopupWindow virtual methods are all overridden to log them
    virtual void Popup(wxWindow *focus = NULL) wxOVERRIDE;
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent &event) wxOVERRIDE;
    virtual bool Show(bool show = true) wxOVERRIDE;

    void sync_vcamera_state(bool show_vcamera);
    void check_func_supported(MachineObject* obj);
    void update(bool vcamera_streaming);
    void SetOnDismiss(std::function<void()> cb) { m_on_dismiss = std::move(cb); }

    enum CameraResolution
    {
        RESOLUTION_720P = 0,
        RESOLUTION_1080P = 1,
        RESOLUTION_OPTIONS_NUM = 2
    };

    void rescale();

protected:
    struct WrapHint { wxStaticText* text; wxString original; int reserved_dip; };
    std::vector<WrapHint> m_wrap_hints;
    int m_content_width = -1;
    unsigned m_wrap_last_shown = 0xFFFFFFFFu;
    void wrap_hints();

    void on_switch_recording(wxMouseEvent& event);
    void on_switch_live_cam_rec(wxMouseEvent& event);
    void on_switch_visual_cam_rec(wxMouseEvent& event);
    void on_set_resolution();

    wxWindow *  create_item_radiobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left);
    void select_curr_radiobox(int btn_idx);
    void sync_resolution_setting(std::string resolution);
    void reset_resolution_setting();
    wxString to_resolution_label_string(CameraResolution resolution);
    std::string to_resolution_msg_string(CameraResolution resolution);

private:
    MachineObject* m_obj { nullptr };
    wxTimer* m_interval_timer{nullptr};
    bool  m_is_in_interval{ false };
    wxStaticText* m_text_recording;
    SwitchButton* m_switch_recording;
    wxStaticText* m_text_recording_hint;
    wxStaticText* m_text_live_cam_rec;
    SwitchButton* m_switch_live_cam_rec;
    wxStaticText* m_text_visual_cam_rec;
    SwitchButton* m_switch_visual_cam_rec;
    wxStaticText* m_text_cam_rec_hint;
    wxStaticText* m_text_vcamera;
    SwitchButton* m_switch_vcamera;
    wxStaticText* m_text_storage;
    wxStaticText* m_text_storage_value;
    wxStaticText* m_text_timelapse_hint;
    wxStaticText* m_text_liveview_retry;
    SwitchButton* m_switch_liveview_retry;
    wxStaticText* m_text_resolution;
    wxWindow* m_resolution_options[RESOLUTION_OPTIONS_NUM];
    wxScrolledWindow *m_panel;
    wxBoxSizer* main_sizer;
    std::vector<RadioBox*> resolution_rbtns;
    std::vector<wxStaticText*> resolution_texts;
    CameraResolution curr_sel_resolution = RESOLUTION_1080P;
    Label* vcamera_guide_link { nullptr };
    wxPanel* link_underline{ nullptr };
    bool is_vcamera_show = false;
    bool allow_alter_resolution = false;

    void start_interval();
    void stop_interval(wxTimerEvent& event);
    void OnMouse(wxMouseEvent &event);
    void OnSize(wxSizeEvent &event);
    void OnSetFocus(wxFocusEvent &event);
    void OnKillFocus(wxFocusEvent &event);
    void OnLeftUp(wxMouseEvent& event);

    // Debounce guards to prevent double-trigger on macOS
    std::chrono::steady_clock::time_point m_last_recording_click{};
    std::chrono::steady_clock::time_point m_last_live_cam_click{};
    std::chrono::steady_clock::time_point m_last_visual_cam_click{};
    std::chrono::steady_clock::time_point m_last_vcamera_click{};

    std::function<void()> m_on_dismiss;

private:
    wxDECLARE_ABSTRACT_CLASS(CameraPopup);
    wxDECLARE_EVENT_TABLE();
};


class CameraItem : public wxPanel
{
public:
    CameraItem(wxWindow *parent, std::string normal, std::string hover, int icon_dip = 20, std::string selected = "", double selected_scale = 1.0);
    ~CameraItem();

    MachineObject *m_obj{nullptr};
    bool     m_hover{false};
    ScalableBitmap m_bitmap_normal;
    ScalableBitmap m_bitmap_hover;
    ScalableBitmap m_bitmap_selected;

    void msw_rescale();
    void SetBitmaps(const std::string &normal, const std::string &hover, const std::string &selected = "", double selected_scale = 1.0);
    void on_enter_win(wxMouseEvent &evt);
    void on_leave_win(wxMouseEvent &evt);
    void reset_hover();
    void SetSelected(bool selected);

    bool Enable(bool enable = true) override;
    bool SetBackgroundColour(const wxColour &colour) override;

private:
    void update_displayed_bitmap();
    wxBitmap make_disabled_bitmap(const wxBitmap &bmp) const;
    void forward_mouse_event(wxMouseEvent &evt);
    void set_hover(bool hover);
    void apply_outer_size();

    int  m_icon_dip;
    int  m_selected_icon_dip{0};
    bool m_selected{false};
    wxStaticBitmap *m_image{nullptr};
};

}
}
#endif
