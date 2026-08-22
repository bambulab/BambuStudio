#ifndef slic3r_PrintStatusFrame_hpp_
#define slic3r_PrintStatusFrame_hpp_

#include <string>
#include <vector>

#include <wx/frame.h>
#include <wx/geometry.h>
#include <wx/timer.h>

class wxBoxSizer;
class wxChoice;
class wxFlexGridSizer;
class wxPanel;
class wxStaticText;
class Button;

namespace Slic3r {

class MachineObject;

namespace GUI {

class MainFrame;
class ProgressBarPanel;

class PrintStatusFrame final : public wxFrame
{
public:
    explicit PrintStatusFrame(MainFrame* parent, std::string initial_dev_id = {}, bool is_primary_window = false);
    ~PrintStatusFrame() override;

    void show_window();
    void show_window_safe_on_minimize();
    void refresh_from_preferences();
    void destroy_for_shutdown();
    const std::string& selected_device_id() const { return m_selected_dev_id; }

private:
    enum class BadgeTone {
        Neutral,
        Positive,
        Warning,
        Error
    };

    struct Config {
        bool        always_on_top     { true };
        bool        remember_position { true };
        std::string theme             { "follow_app" };
        int         opacity           { 100 };
        bool        has_position      { false };
        wxPoint     position          { wxDefaultPosition };
    };

    struct Palette {
        wxColour background;
        wxColour panel_background;
        wxColour input_background;
        wxColour border;
        wxColour text_primary;
        wxColour text_secondary;
        wxColour text_muted;
        wxColour progress_track;
        wxColour progress_fill;
        wxColour badge_neutral_background;
        wxColour badge_neutral_text;
        wxColour badge_positive_background;
        wxColour badge_positive_text;
        wxColour badge_warning_background;
        wxColour badge_warning_text;
        wxColour badge_error_background;
        wxColour badge_error_text;
    };

    struct FieldBlock {
        wxPanel*      panel { nullptr };
        wxStaticText* label { nullptr };
        wxStaticText* value { nullptr };
    };

    void      build_ui();
    FieldBlock create_field_block(wxWindow* parent, const wxString& label, bool compact_label = false);
    Config    load_config() const;
    Palette   build_palette(const Config& config) const;
    void      apply_theme(const Config& config);
    void      apply_opacity(const Config& config);
    void      apply_window_flags(const Config& config);
    void      apply_geometry(const Config& config);
    void      persist_geometry(bool save_immediately);
    void      finish_safe_show_after_minimize();
    void      refresh_content();
    void      sync_printer_selector();
    void      update_snapshot_view(MachineObject* obj);
    void      update_header(MachineObject* obj, bool online, const wxString& warning_text, bool has_active_print);
    void      update_job_row(MachineObject* obj, bool online);
    void      update_progress_row(MachineObject* obj, bool online, bool has_active_print);
    void      update_status_eta_row(MachineObject* obj, bool online, bool has_active_print);
    void      update_temperature_rows(MachineObject* obj, bool online);
    void      update_warnings_row(const wxString& warning_text);
    void      update_field_block(FieldBlock& block, const wxString& value, bool compact = false, int fallback_width = 160);
    void      set_label_text(wxStaticText* label, const wxString& text, const wxString& tooltip = wxEmptyString);
    void      apply_badge_style(const wxString& text, BadgeTone tone);
    wxString  compact_label_text(wxStaticText* control, const wxString& value, int fallback_width = 220) const;
    bool      is_machine_online(MachineObject* obj) const;
    int       build_progress_percent(MachineObject* obj, bool online) const;
    BadgeTone build_badge_tone(MachineObject* obj, bool online, const wxString& warning_text, bool has_active_print) const;
    wxString  build_badge_text(MachineObject* obj, bool online, const wxString& warning_text, bool has_active_print) const;
    std::string build_printer_choice_signature(const std::vector<std::string>& machine_ids) const;

    std::vector<std::string> get_machine_ids() const;
    MachineObject*           resolve_machine();
    MachineObject*           get_machine_by_id(const std::string& dev_id) const;
    std::string              get_default_machine_id() const;

    wxString build_machine_name(MachineObject* obj) const;
    wxString build_stage_text(MachineObject* obj, bool online) const;
    wxString build_job_name_text(MachineObject* obj, bool online) const;
    wxString build_progress_text(MachineObject* obj, bool online) const;
    wxString build_remaining_time_text(MachineObject* obj, bool online) const;
    wxString build_estimated_finish_time_text(MachineObject* obj, bool online, bool has_active_print) const;
    wxString build_layers_text(MachineObject* obj, bool online) const;
    wxString build_nozzle_temp_text(MachineObject* obj, bool online) const;
    wxString build_bed_temp_text(MachineObject* obj, bool online) const;
    wxString build_chamber_temp_text(MachineObject* obj, bool online) const;
    wxString build_warning_text(MachineObject* obj) const;

    void on_timer(wxTimerEvent& event);
    void on_close(wxCloseEvent& event);
    void on_printer_changed(wxCommandEvent& event);
    void on_move(wxMoveEvent& event);
    void on_size(wxSizeEvent& event);

private:
    MainFrame*               m_mainframe { nullptr };
    wxTimer                  m_refresh_timer;
    wxPanel*                 m_root_panel { nullptr };
    wxPanel*                 m_header_panel { nullptr };
    wxChoice*                m_printer_choice { nullptr };
    Button*                  m_new_window_button { nullptr };
    wxPanel*                 m_badge_panel { nullptr };
    wxStaticText*            m_badge_label { nullptr };
    wxStaticText*            m_job_label { nullptr };
    wxPanel*                 m_progress_row_panel { nullptr };
    wxStaticText*            m_percent_label { nullptr };
    wxStaticText*            m_layer_summary_label { nullptr };
    wxStaticText*            m_remaining_summary_label { nullptr };
    ProgressBarPanel*        m_progress_bar { nullptr };
    wxPanel*                 m_status_eta_panel { nullptr };
    wxStaticText*            m_status_summary_label { nullptr };
    wxStaticText*            m_eta_summary_label { nullptr };
    wxFlexGridSizer*         m_temperature_grid { nullptr };
    FieldBlock               m_nozzle_block;
    FieldBlock               m_bed_block;
    FieldBlock               m_chamber_block;
    wxPanel*                 m_warnings_panel { nullptr };
    wxStaticText*            m_warnings_label { nullptr };
    wxStaticText*            m_warnings_value { nullptr };
    Palette                  m_palette;
    std::vector<std::string> m_choice_dev_ids;
    std::string              m_selected_dev_id;
    std::string              m_printer_choice_signature;
    bool                     m_is_shutting_down { false };
    bool                     m_is_initializing { true };
    bool                     m_ui_built { false };
    bool                     m_safe_show_pending { false };
    bool                     m_ignore_geometry_events { false };
    bool                     m_updating_printer_choice { false };
    bool                     m_is_primary_window { false };
};

}} // namespace Slic3r::GUI

#endif
