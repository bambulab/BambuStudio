#include "PrintStatusFrame.hpp"

#include <algorithm>
#include <map>
#include <utility>

#include <boost/log/trivial.hpp>

#include <wx/choice.h>
#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "DeviceManager.hpp"
#include "GUI_App.hpp"
#include "HMS.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "DeviceCore/DevBed.h"
#include "DeviceCore/DevChamber.h"
#include "DeviceCore/DevExtruderSystem.h"
#include "DeviceCore/DevHMS.h"
#include "DeviceCore/DevManager.h"
#include "Widgets/Button.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Slic3r {
namespace GUI {

class ProgressBarPanel final : public wxPanel
{
public:
    explicit ProgressBarPanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(wxSize(-1, FromDIP(10)));
        Bind(wxEVT_PAINT, &ProgressBarPanel::on_paint, this);
        Bind(wxEVT_SIZE, &ProgressBarPanel::on_size, this);
    }

    void SetValue(int value)
    {
        value = std::clamp(value, 0, 100);
        if (m_value == value)
            return;

        m_value = value;
        Refresh();
    }

    void SetColors(const wxColour& background, const wxColour& track, const wxColour& fill)
    {
        m_background = background;
        m_track      = track;
        m_fill       = fill;
        SetBackgroundColour(background);
        Refresh();
    }

private:
    void on_paint(wxPaintEvent& /*event*/)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(m_background));
        dc.Clear();

        const wxRect rect = GetClientRect();
        if (rect.GetWidth() <= 0 || rect.GetHeight() <= 0)
            return;

        const int radius = std::max(2, rect.GetHeight() / 2);

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(m_track));
        dc.DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);

        const int fill_width = std::clamp((rect.GetWidth() * m_value) / 100, 0, rect.GetWidth());
        if (fill_width <= 0)
            return;

        wxRect fill_rect = rect;
        fill_rect.SetWidth(fill_width);
        dc.SetBrush(wxBrush(m_fill));
        dc.DrawRoundedRectangle(fill_rect.x, fill_rect.y, fill_rect.width, fill_rect.height, radius);
    }

    void on_size(wxSizeEvent& event)
    {
        Refresh();
        Update();
        event.Skip();
    }

private:
    int      m_value { 0 };
    wxColour m_background { *wxWHITE };
    wxColour m_track { wxColour(220, 224, 229) };
    wxColour m_fill { wxColour(0, 174, 66) };
};

namespace {

wxString na_text()
{
    return _L("N/A");
}

wxString temperature_unit()
{
    return wxString::FromUTF8("\xC2\xB0" "C");
}

bool cfg_bool(AppConfig* config, const char* key, bool fallback = false)
{
    if (config == nullptr)
        return fallback;
    const auto value = config->get(key);
    if (value.empty())
        return fallback;
    return value == "true";
}

int cfg_int(AppConfig* config, const char* key, int fallback)
{
    if (config == nullptr)
        return fallback;
    long parsed = fallback;
    if (wxString::FromUTF8(config->get(key)).ToLong(&parsed))
        return static_cast<int>(parsed);
    return fallback;
}

bool cfg_has_value(AppConfig* config, const char* key)
{
    return config != nullptr && !config->get(key).empty();
}

wxString short_machine_suffix(const std::string& dev_id)
{
    if (dev_id.size() <= 4)
        return GUI::from_u8(dev_id);
    return GUI::from_u8(dev_id.substr(dev_id.size() - 4));
}

wxRect sanitize_rect_for_displays(const wxRect& rect)
{
    if (wxDisplay::GetCount() <= 0)
        return rect;

    int display_idx = wxDisplay::GetFromPoint(rect.GetTopLeft());
    if (display_idx == wxNOT_FOUND) {
        const wxPoint center(rect.GetLeft() + rect.GetWidth() / 2, rect.GetTop() + rect.GetHeight() / 2);
        display_idx = wxDisplay::GetFromPoint(center);
    }
    if (display_idx == wxNOT_FOUND)
        display_idx = 0;

    const wxRect display = wxDisplay(static_cast<unsigned int>(display_idx)).GetClientArea();
    const int    width   = std::min(rect.GetWidth(), display.GetWidth());
    const int    height  = std::min(rect.GetHeight(), display.GetHeight());
    const int    max_x   = display.GetRight() - width + 1;
    const int    max_y   = display.GetBottom() - height + 1;
    return wxRect(std::clamp(rect.GetLeft(), display.GetLeft(), max_x),
                  std::clamp(rect.GetTop(), display.GetTop(), max_y),
                  width,
                  height);
}

bool contains_case_insensitive(const wxString& text, const wxString& token)
{
    return text.Lower().Find(token.Lower()) != wxNOT_FOUND;
}

wxString prefixed_value(const wxString& prefix, const wxString& value)
{
    return prefix + ": " + value;
}

void ensure_min_text_width(wxWindow* window, const wxFont& font, const wxString& sample_text, int horizontal_padding_dip = 0, int min_height = -1)
{
    if (window == nullptr)
        return;

    int width = 0;
    int height = 0;
    window->GetTextExtent(sample_text, &width, &height, nullptr, nullptr, &font);
    const wxSize current_min = window->GetMinSize();
    window->SetMinSize(wxSize(std::max(current_min.GetWidth(), width + window->FromDIP(horizontal_padding_dip)),
                              std::max(current_min.GetHeight(), std::max(min_height, height))));
}

} // namespace

PrintStatusFrame::PrintStatusFrame(MainFrame* parent, std::string initial_dev_id, bool is_primary_window)
    : wxFrame(parent,
              wxID_ANY,
              _L("Print Status Window"),
              wxDefaultPosition,
              wxDefaultSize,
              (wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX)) | wxFRAME_TOOL_WINDOW),
      m_mainframe(parent),
      m_refresh_timer(this),
      m_selected_dev_id(std::move(initial_dev_id)),
      m_is_primary_window(is_primary_window)
{
    build_ui();
    Bind(wxEVT_TIMER, &PrintStatusFrame::on_timer, this);
    Bind(wxEVT_CLOSE_WINDOW, &PrintStatusFrame::on_close, this);
    Bind(wxEVT_MOVE, &PrintStatusFrame::on_move, this);
    Bind(wxEVT_SIZE, &PrintStatusFrame::on_size, this);
    m_printer_choice->Bind(wxEVT_CHOICE, &PrintStatusFrame::on_printer_changed, this);

    const wxSize fixed_size(FromDIP(430), FromDIP(352));
    SetMinSize(fixed_size);
    SetSize(fixed_size);
    m_is_initializing = false;
}

PrintStatusFrame::~PrintStatusFrame()
{
    m_refresh_timer.Stop();
}

void PrintStatusFrame::show_window()
{
    Show();
    refresh_from_preferences();
    if (!m_refresh_timer.IsRunning())
        m_refresh_timer.Start(1000);
    refresh_content();
    Raise();
}

void PrintStatusFrame::show_window_safe_on_minimize()
{
    Show();

    if (m_safe_show_pending)
        return;

    m_safe_show_pending = true;
    CallAfter(&PrintStatusFrame::finish_safe_show_after_minimize);
}

void PrintStatusFrame::refresh_from_preferences()
{
    if (!m_ui_built)
        return;

    const auto config = load_config();
    apply_window_flags(config);
    apply_theme(config);
    apply_opacity(config);
    apply_geometry(config);
    if (config.remember_position && !config.has_position)
        persist_geometry(true);
    if (!m_is_initializing && IsShownOnScreen())
        refresh_content();
    Layout();
}

void PrintStatusFrame::finish_safe_show_after_minimize()
{
    m_safe_show_pending = false;

    if (m_is_shutting_down || !m_ui_built || !GetHandle())
        return;

    refresh_from_preferences();
    if (!m_refresh_timer.IsRunning())
        m_refresh_timer.Start(1000);
    refresh_content();
    Raise();
}

void PrintStatusFrame::destroy_for_shutdown()
{
    if (m_is_shutting_down)
        return;

    m_is_shutting_down = true;
    m_refresh_timer.Stop();
    persist_geometry(true);
    Destroy();
}

void PrintStatusFrame::build_ui()
{
    m_root_panel = new wxPanel(this, wxID_ANY);

    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    m_header_panel = new wxPanel(m_root_panel, wxID_ANY);
    auto* header_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_printer_choice = new wxChoice(m_header_panel, wxID_ANY);
    m_printer_choice->SetMinSize(wxSize(FromDIP(220), FromDIP(22)));
    header_sizer->Add(m_printer_choice, 1, wxEXPAND, 0);

    m_new_window_button = new Button(m_header_panel, _L("+"));
    m_new_window_button->SetMinSize(wxSize(FromDIP(26), FromDIP(26)));
    m_new_window_button->SetToolTip(_L("Open another print status window"));
    m_new_window_button->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& event) {
        if (m_mainframe)
            m_mainframe->open_additional_print_status_frame(m_selected_dev_id);
        event.Skip();
    });
    header_sizer->Add(m_new_window_button, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(8));

    m_badge_panel = new wxPanel(m_header_panel, wxID_ANY);
    auto* badge_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_badge_label = new wxStaticText(m_badge_panel, wxID_ANY, _L("Idle"));
    wxFont badge_font = m_badge_label->GetFont();
    badge_font.SetWeight(wxFONTWEIGHT_BOLD);
    badge_font.SetPointSize(std::max(badge_font.GetPointSize() - 1, 8));
    m_badge_label->SetFont(badge_font);
    badge_sizer->Add(m_badge_label, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(6));
    ensure_min_text_width(m_badge_panel, badge_font, _L("Finished"), 20, 26);
    m_badge_panel->SetSizer(badge_sizer);
    header_sizer->Add(m_badge_panel, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    m_header_panel->SetSizer(header_sizer);
    root_sizer->Add(m_header_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    m_job_label = new wxStaticText(m_root_panel, wxID_ANY, na_text());
    wxFont job_font = m_job_label->GetFont();
    job_font.SetWeight(wxFONTWEIGHT_BOLD);
    job_font.SetPointSize(std::max(job_font.GetPointSize() + 3, 13));
    m_job_label->SetFont(job_font);
    root_sizer->Add(m_job_label, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    m_progress_row_panel = new wxPanel(m_root_panel, wxID_ANY);
    auto* progress_row_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_percent_label = new wxStaticText(m_progress_row_panel, wxID_ANY, na_text());
    wxFont percent_font = m_percent_label->GetFont();
    percent_font.SetWeight(wxFONTWEIGHT_BOLD);
    percent_font.SetPointSize(std::max(percent_font.GetPointSize() + 10, 20));
    m_percent_label->SetFont(percent_font);
    progress_row_sizer->Add(m_percent_label, 0, wxALIGN_CENTER_VERTICAL, 0);
    progress_row_sizer->AddStretchSpacer();

    m_layer_summary_label = new wxStaticText(m_progress_row_panel, wxID_ANY, prefixed_value(_L("Layer"), na_text()));
    progress_row_sizer->Add(m_layer_summary_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

    m_remaining_summary_label = new wxStaticText(m_progress_row_panel, wxID_ANY, na_text());
    ensure_min_text_width(m_remaining_summary_label, m_remaining_summary_label->GetFont(), _L("Finished"), 8);
    progress_row_sizer->Add(m_remaining_summary_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));
    m_progress_row_panel->SetSizer(progress_row_sizer);
    root_sizer->Add(m_progress_row_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    m_progress_bar = new ProgressBarPanel(m_root_panel);
    root_sizer->Add(m_progress_bar, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    m_status_eta_panel = new wxPanel(m_root_panel, wxID_ANY);
    auto* status_eta_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_status_summary_label = new wxStaticText(m_status_eta_panel, wxID_ANY, prefixed_value(_L("Status"), na_text()));
    status_eta_sizer->Add(m_status_summary_label, 0, wxALIGN_CENTER_VERTICAL, 0);
    status_eta_sizer->AddStretchSpacer();
    m_eta_summary_label = new wxStaticText(m_status_eta_panel, wxID_ANY, _L("Estimated finish time: ") + na_text());
    ensure_min_text_width(m_eta_summary_label, m_eta_summary_label->GetFont(), _L("Estimated finish time: Finished"), 8);
    status_eta_sizer->Add(m_eta_summary_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
    m_status_eta_panel->SetSizer(status_eta_sizer);
    root_sizer->Add(m_status_eta_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(12));

    m_temperature_grid = new wxFlexGridSizer(0, 2, FromDIP(8), FromDIP(18));
    m_temperature_grid->AddGrowableCol(0, 1);
    m_temperature_grid->AddGrowableCol(1, 1);

    m_nozzle_block  = create_field_block(m_root_panel, _L("Nozzle"), true);
    m_bed_block     = create_field_block(m_root_panel, _L("Bed"), true);
    m_chamber_block = create_field_block(m_root_panel, _L("Chamber"), true);

    m_temperature_grid->Add(m_nozzle_block.panel, 1, wxEXPAND, 0);
    m_temperature_grid->Add(m_bed_block.panel, 1, wxEXPAND, 0);
    m_temperature_grid->Add(m_chamber_block.panel, 1, wxEXPAND, 0);
    m_temperature_grid->AddSpacer(0);
    root_sizer->Add(m_temperature_grid, 0, wxEXPAND | wxBOTTOM, FromDIP(12));

    m_warnings_panel = new wxPanel(m_root_panel, wxID_ANY);
    auto* warnings_sizer = new wxBoxSizer(wxVERTICAL);
    m_warnings_label = new wxStaticText(m_warnings_panel, wxID_ANY, _L("Warnings"));
    wxFont warning_label_font = m_warnings_label->GetFont();
    warning_label_font.SetPointSize(std::max(warning_label_font.GetPointSize() - 1, 8));
    m_warnings_label->SetFont(warning_label_font);
    warnings_sizer->Add(m_warnings_label, 0, wxBOTTOM, FromDIP(4));

    m_warnings_value = new wxStaticText(m_warnings_panel, wxID_ANY, _L("No warnings"));
    wxFont warning_value_font = m_warnings_value->GetFont();
    warning_value_font.SetWeight(wxFONTWEIGHT_BOLD);
    m_warnings_value->SetFont(warning_value_font);
    warnings_sizer->Add(m_warnings_value, 0, wxEXPAND, 0);
    m_warnings_panel->SetMinSize(wxSize(-1, FromDIP(42)));
    m_warnings_panel->SetSizer(warnings_sizer);
    root_sizer->Add(m_warnings_panel, 0, wxEXPAND, 0);

    auto* frame_sizer = new wxBoxSizer(wxVERTICAL);
    frame_sizer->Add(m_root_panel, 1, wxALL | wxEXPAND, FromDIP(14));
    m_root_panel->SetSizer(root_sizer);
    SetSizer(frame_sizer);
    m_ui_built = true;
}

PrintStatusFrame::FieldBlock PrintStatusFrame::create_field_block(wxWindow* parent, const wxString& label, bool compact_label)
{
    FieldBlock block;
    block.panel = new wxPanel(parent, wxID_ANY);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    block.label = new wxStaticText(block.panel, wxID_ANY, label);
    wxFont label_font = block.label->GetFont();
    label_font.SetPointSize(std::max(label_font.GetPointSize() - (compact_label ? 1 : 0), 8));
    block.label->SetFont(label_font);
    sizer->Add(block.label, 0, wxBOTTOM, FromDIP(2));

    block.value = new wxStaticText(block.panel, wxID_ANY, na_text());
    wxFont value_font = block.value->GetFont();
    value_font.SetWeight(wxFONTWEIGHT_BOLD);
    value_font.SetPointSize(std::max(value_font.GetPointSize() + 1, 10));
    block.value->SetFont(value_font);
    sizer->Add(block.value, 0, wxEXPAND, 0);

    block.panel->SetSizer(sizer);
    return block;
}

PrintStatusFrame::Config PrintStatusFrame::load_config() const
{
    Config config;
    auto*  app_config = wxGetApp().app_config;
    config.always_on_top     = cfg_bool(app_config, "print_status_window_always_on_top", true);
    config.remember_position = cfg_bool(app_config, "print_status_window_remember_position", true);
    config.theme             = app_config ? app_config->get("print_status_window_theme") : "follow_app";
    if (config.theme.empty())
        config.theme = "follow_app";
    config.opacity = std::clamp(cfg_int(app_config, "print_status_window_opacity", 100), 40, 100);

    if (cfg_has_value(app_config, "print_status_window_pos_x") && cfg_has_value(app_config, "print_status_window_pos_y")) {
        config.position     = wxPoint(cfg_int(app_config, "print_status_window_pos_x", wxDefaultPosition.x),
                                      cfg_int(app_config, "print_status_window_pos_y", wxDefaultPosition.y));
        config.has_position = true;
    }
    return config;
}

PrintStatusFrame::Palette PrintStatusFrame::build_palette(const Config& config) const
{
    const bool dark_mode = config.theme == "dark" || (config.theme == "follow_app" && wxGetApp().dark_mode());
    const wxColour accent(0, 174, 66);

    if (dark_mode) {
        return Palette{
            wxColour(34, 37, 42),
            wxColour(34, 37, 42),
            wxColour(44, 48, 53),
            wxColour(70, 75, 82),
            wxColour(242, 245, 247),
            wxColour(197, 203, 208),
            wxColour(148, 154, 160),
            wxColour(66, 71, 77),
            accent,
            wxColour(82, 87, 94),
            wxColour(230, 234, 237),
            wxColour(16, 96, 51),
            wxColour(220, 248, 229),
            wxColour(114, 83, 17),
            wxColour(255, 236, 188),
            wxColour(118, 38, 52),
            wxColour(255, 221, 228)
        };
    }

    return Palette{
        *wxWHITE,
        *wxWHITE,
        wxColour(246, 248, 250),
        wxColour(216, 222, 227),
        wxColour(32, 38, 43),
        wxColour(84, 93, 101),
        wxColour(117, 125, 133),
        wxColour(223, 227, 231),
        accent,
        wxColour(235, 239, 242),
        wxColour(82, 91, 98),
        wxColour(219, 245, 228),
        wxColour(16, 96, 51),
        wxColour(255, 242, 213),
        wxColour(149, 96, 19),
        wxColour(255, 227, 232),
        wxColour(173, 34, 59)
    };
}

void PrintStatusFrame::apply_theme(const Config& config)
{
    m_palette = build_palette(config);

    SetBackgroundColour(m_palette.background);
    if (m_root_panel)
        m_root_panel->SetBackgroundColour(m_palette.background);

    const auto apply_panel = [&](wxPanel* panel) {
        if (panel == nullptr)
            return;
        panel->SetBackgroundColour(m_palette.panel_background);
        panel->SetForegroundColour(m_palette.text_primary);
    };

    apply_panel(m_header_panel);
    apply_panel(m_progress_row_panel);
    apply_panel(m_status_eta_panel);
    apply_panel(m_nozzle_block.panel);
    apply_panel(m_bed_block.panel);
    apply_panel(m_chamber_block.panel);
    apply_panel(m_warnings_panel);

    if (m_printer_choice) {
        m_printer_choice->SetBackgroundColour(m_palette.input_background);
        m_printer_choice->SetForegroundColour(m_palette.text_primary);
        m_printer_choice->SetToolTip(_L("Printer"));
    }
    if (m_new_window_button) {
        m_new_window_button->SetBackgroundColorNormal(m_palette.input_background);
        m_new_window_button->SetBorderColorNormal(m_palette.border);
        m_new_window_button->SetTextColorNormal(m_palette.text_primary);
    }

    if (m_job_label)
        m_job_label->SetForegroundColour(m_palette.text_primary);
    if (m_percent_label)
        m_percent_label->SetForegroundColour(m_palette.progress_fill);
    if (m_layer_summary_label)
        m_layer_summary_label->SetForegroundColour(m_palette.text_secondary);
    if (m_remaining_summary_label)
        m_remaining_summary_label->SetForegroundColour(m_palette.text_secondary);
    if (m_status_summary_label)
        m_status_summary_label->SetForegroundColour(m_palette.text_secondary);
    if (m_eta_summary_label)
        m_eta_summary_label->SetForegroundColour(m_palette.text_secondary);
    if (m_warnings_label)
        m_warnings_label->SetForegroundColour(m_palette.text_muted);
    if (m_warnings_value)
        m_warnings_value->SetForegroundColour(m_palette.text_primary);

    const auto apply_block_theme = [&](FieldBlock& block) {
        if (block.label)
            block.label->SetForegroundColour(m_palette.text_muted);
        if (block.value)
            block.value->SetForegroundColour(m_palette.text_primary);
    };

    apply_block_theme(m_nozzle_block);
    apply_block_theme(m_bed_block);
    apply_block_theme(m_chamber_block);

    if (m_progress_bar)
        m_progress_bar->SetColors(m_palette.panel_background, m_palette.progress_track, m_palette.progress_fill);

    if (IsShownOnScreen())
        Refresh();
}

void PrintStatusFrame::apply_opacity(const Config& config)
{
    const int alpha = std::clamp((config.opacity * 255) / 100, 102, 255);
    if (!SetTransparent(static_cast<wxByte>(alpha)))
        SetTransparent(wxALPHA_OPAQUE);
}

void PrintStatusFrame::apply_window_flags(const Config& config)
{
    const long current_style = GetWindowStyleFlag();
    const long desired_style = config.always_on_top ? (current_style | wxSTAY_ON_TOP) : (current_style & ~wxSTAY_ON_TOP);
    if (desired_style != current_style)
        SetWindowStyleFlag(desired_style);

#ifdef _WIN32
    if (GetHandle() != nullptr) {
        ::SetWindowPos(static_cast<HWND>(GetHandle()),
                       config.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                       0,
                       0,
                       0,
                       0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
#endif

    if (config.always_on_top && IsShown())
        Raise();
}

void PrintStatusFrame::apply_geometry(const Config& config)
{
    if (!m_is_primary_window || !config.remember_position)
        return;

    if (!config.has_position)
        return;

    const wxRect sanitized = sanitize_rect_for_displays(wxRect(config.position, GetSize()));
    if (sanitized.GetPosition() == GetPosition())
        return;

    m_ignore_geometry_events = true;
    SetPosition(sanitized.GetPosition());
    m_ignore_geometry_events = false;
}

void PrintStatusFrame::persist_geometry(bool save_immediately)
{
    const auto config     = load_config();
    auto*      app_config = wxGetApp().app_config;
    if (app_config == nullptr || !m_is_primary_window || !config.remember_position || IsIconized())
        return;

    const wxPoint position = GetPosition();
    if (position.x != wxDefaultPosition.x && position.y != wxDefaultPosition.y) {
        app_config->set("print_status_window_pos_x", std::to_string(position.x));
        app_config->set("print_status_window_pos_y", std::to_string(position.y));
    }

    if (save_immediately)
        app_config->save();
}

void PrintStatusFrame::refresh_content()
{
    if (m_is_shutting_down || m_is_initializing || !m_ui_built)
        return;

    sync_printer_selector();
    update_snapshot_view(resolve_machine());
}

void PrintStatusFrame::sync_printer_selector()
{
    if (!m_ui_built || m_printer_choice == nullptr)
        return;

    const std::vector<std::string> machine_ids = get_machine_ids();

    if (machine_ids.empty()) {
        m_choice_dev_ids.clear();
        m_selected_dev_id.clear();
        const std::string empty_signature = "__empty__";
        if (m_printer_choice_signature != empty_signature) {
            m_updating_printer_choice = true;
            m_printer_choice->Clear();
            m_printer_choice->Append(_L("No printer available"));
            m_printer_choice->SetSelection(0);
            m_printer_choice->Enable(false);
            m_updating_printer_choice = false;
            m_printer_choice_signature = empty_signature;
        }
        if (m_new_window_button)
            m_new_window_button->Enable(false);
        return;
    }

    if (m_selected_dev_id.empty())
        m_selected_dev_id = get_default_machine_id();
    if (std::find(machine_ids.begin(), machine_ids.end(), m_selected_dev_id) == machine_ids.end()) {
        m_selected_dev_id = get_default_machine_id();
        if (m_selected_dev_id.empty() || std::find(machine_ids.begin(), machine_ids.end(), m_selected_dev_id) == machine_ids.end())
            m_selected_dev_id = machine_ids.front();
    }

    const std::string signature = build_printer_choice_signature(machine_ids);
    std::map<wxString, int> label_counts;
    for (const auto& dev_id : machine_ids) {
        if (auto* obj = get_machine_by_id(dev_id))
            label_counts[build_machine_name(obj)]++;
    }

    if (m_printer_choice_signature != signature) {
        m_updating_printer_choice = true;
        m_printer_choice->Clear();
        m_choice_dev_ids = machine_ids;

        for (const auto& dev_id : machine_ids) {
            wxString label = GUI::from_u8(dev_id);
            if (auto* obj = get_machine_by_id(dev_id))
                label = build_machine_name(obj);
            if (label_counts[label] > 1)
                label += " (" + short_machine_suffix(dev_id) + ")";
            m_printer_choice->Append(label);
        }

        const auto it = std::find(m_choice_dev_ids.begin(), m_choice_dev_ids.end(), m_selected_dev_id);
        if (it != m_choice_dev_ids.end())
            m_printer_choice->SetSelection(static_cast<int>(std::distance(m_choice_dev_ids.begin(), it)));

        m_printer_choice->Enable(machine_ids.size() > 1);
        m_updating_printer_choice = false;
        m_printer_choice_signature = signature;
        if (m_new_window_button)
            m_new_window_button->Enable(machine_ids.size() > 1);
        return;
    }

    m_choice_dev_ids = machine_ids;
    m_printer_choice->Enable(machine_ids.size() > 1);
    if (m_new_window_button)
        m_new_window_button->Enable(machine_ids.size() > 1);

    const auto it = std::find(m_choice_dev_ids.begin(), m_choice_dev_ids.end(), m_selected_dev_id);
    if (it == m_choice_dev_ids.end())
        return;

    const int expected_selection = static_cast<int>(std::distance(m_choice_dev_ids.begin(), it));
    if (m_printer_choice->GetSelection() != expected_selection) {
        m_updating_printer_choice = true;
        m_printer_choice->SetSelection(expected_selection);
        m_updating_printer_choice = false;
    }
}

void PrintStatusFrame::update_snapshot_view(MachineObject* obj)
{
    if (!m_ui_built)
        return;

    const bool     online           = is_machine_online(obj);
    const bool     has_active_print = obj != nullptr && online &&
        (obj->is_in_printing() || obj->is_in_prepare() || obj->print_status == "SLICING" || obj->print_status == "FINISH");
    const wxString warning_text     = build_warning_text(obj);

    update_header(obj, online, warning_text, has_active_print);
    update_job_row(obj, online);
    update_progress_row(obj, online, has_active_print);
    update_status_eta_row(obj, online, has_active_print);
    update_temperature_rows(obj, online);
    update_warnings_row(warning_text);
}

void PrintStatusFrame::update_header(MachineObject* obj, bool online, const wxString& warning_text, bool has_active_print)
{
    if (m_printer_choice)
        m_printer_choice->SetToolTip(obj != nullptr ? build_machine_name(obj) : _L("No printer available"));
    if (obj != nullptr)
        SetTitle(_L("Print Status Window") + " - " + build_machine_name(obj));
    else
        SetTitle(_L("Print Status Window"));
    apply_badge_style(build_badge_text(obj, online, warning_text, has_active_print),
                      build_badge_tone(obj, online, warning_text, has_active_print));
}

void PrintStatusFrame::update_job_row(MachineObject* obj, bool online)
{
    const wxString full_text    = build_job_name_text(obj, online);
    const wxString display_text = compact_label_text(m_job_label, full_text, 320);
    set_label_text(m_job_label, display_text, display_text != full_text ? full_text : wxEmptyString);
}

void PrintStatusFrame::update_progress_row(MachineObject* obj, bool online, bool has_active_print)
{
    set_label_text(m_percent_label, build_progress_text(obj, online));
    set_label_text(m_layer_summary_label, prefixed_value(_L("Layer"), build_layers_text(obj, online)));
    set_label_text(m_remaining_summary_label, build_remaining_time_text(obj, online));
    m_progress_bar->SetValue(has_active_print ? build_progress_percent(obj, online) : 0);
}

void PrintStatusFrame::update_status_eta_row(MachineObject* obj, bool online, bool has_active_print)
{
    set_label_text(m_status_summary_label, prefixed_value(_L("Status"), build_stage_text(obj, online)));
    set_label_text(m_eta_summary_label, build_estimated_finish_time_text(obj, online, has_active_print));
}

void PrintStatusFrame::update_temperature_rows(MachineObject* obj, bool online)
{
    update_field_block(m_nozzle_block, build_nozzle_temp_text(obj, online));
    update_field_block(m_bed_block, build_bed_temp_text(obj, online));
    update_field_block(m_chamber_block, build_chamber_temp_text(obj, online));
}

void PrintStatusFrame::update_warnings_row(const wxString& warning_text)
{
    set_label_text(m_warnings_value, warning_text.empty() ? _L("No warnings") : warning_text,
                   warning_text.empty() ? wxEmptyString : warning_text);
}

void PrintStatusFrame::update_field_block(FieldBlock& block, const wxString& value, bool compact, int fallback_width)
{
    const wxString full_value = value.empty() ? na_text() : value;
    if (compact) {
        const wxString display_value = compact_label_text(block.value, full_value, fallback_width);
        set_label_text(block.value, display_value, display_value != full_value ? full_value : wxEmptyString);
    } else {
        set_label_text(block.value, full_value);
    }
}

void PrintStatusFrame::set_label_text(wxStaticText* label, const wxString& text, const wxString& tooltip)
{
    if (label == nullptr)
        return;

    if (label->GetLabelText() != text)
        label->SetLabel(text);
    label->SetToolTip(tooltip);
}

void PrintStatusFrame::apply_badge_style(const wxString& text, BadgeTone tone)
{
    wxColour background = m_palette.badge_neutral_background;
    wxColour foreground = m_palette.badge_neutral_text;

    switch (tone) {
    case BadgeTone::Positive:
        background = m_palette.badge_positive_background;
        foreground = m_palette.badge_positive_text;
        break;
    case BadgeTone::Warning:
        background = m_palette.badge_warning_background;
        foreground = m_palette.badge_warning_text;
        break;
    case BadgeTone::Error:
        background = m_palette.badge_error_background;
        foreground = m_palette.badge_error_text;
        break;
    case BadgeTone::Neutral:
    default:
        break;
    }

    if (m_badge_panel) {
        m_badge_panel->SetBackgroundColour(background);
        m_badge_panel->SetForegroundColour(foreground);
    }
    if (m_badge_label) {
        m_badge_label->SetBackgroundColour(background);
        m_badge_label->SetForegroundColour(foreground);
        set_label_text(m_badge_label, text);
    }

    if (m_badge_label)
        m_badge_label->Refresh();
    if (m_badge_panel)
        m_badge_panel->Refresh();
}

wxString PrintStatusFrame::compact_label_text(wxStaticText* control, const wxString& value, int fallback_width) const
{
    if (control == nullptr || value.empty())
        return value;
    if (control->GetHandle() == nullptr || !control->IsShownOnScreen())
        return value;

    wxClientDC dc(control);
    dc.SetFont(control->GetFont());
    const int width = std::max(control->GetClientSize().GetWidth(), FromDIP(fallback_width));
    return wxControl::Ellipsize(value, dc, wxELLIPSIZE_END, width);
}

std::string PrintStatusFrame::build_printer_choice_signature(const std::vector<std::string>& machine_ids) const
{
    std::string signature;
    signature.reserve(machine_ids.size() * 32);

    for (const auto& dev_id : machine_ids) {
        signature += dev_id;
        signature += '|';
        if (auto* obj = get_machine_by_id(dev_id))
            signature += build_machine_name(obj).ToUTF8().data();
        signature += ';';
    }

    return signature;
}

bool PrintStatusFrame::is_machine_online(MachineObject* obj) const
{
    // MachineObject::is_online() is already driven by the device updates / dev_online state,
    // which is the smallest reliable signal we have here for the widget.
    return obj != nullptr && obj->is_online();
}

int PrintStatusFrame::build_progress_percent(MachineObject* obj, bool online) const
{
    if (obj == nullptr || !online)
        return 0;

    if (obj->subtask_ != nullptr && obj->subtask_->task_progress >= 0)
        return std::clamp(obj->subtask_->task_progress, 0, 100);
    if ((obj->is_in_prepare() || obj->print_status == "SLICING") && obj->gcode_file_prepare_percent >= 0 && obj->gcode_file_prepare_percent <= 100)
        return obj->gcode_file_prepare_percent;
    if (obj->mc_print_percent >= 0 && obj->mc_print_percent <= 100)
        return obj->mc_print_percent;
    return 0;
}

PrintStatusFrame::BadgeTone PrintStatusFrame::build_badge_tone(MachineObject* obj, bool online, const wxString& warning_text, bool has_active_print) const
{
    if (obj == nullptr || !online)
        return BadgeTone::Neutral;
    if (obj->print_error > 0)
        return BadgeTone::Error;
    if (!warning_text.empty())
        return BadgeTone::Warning;
    if (contains_case_insensitive(build_stage_text(obj, online), _L("Paused")))
        return BadgeTone::Warning;
    if (has_active_print)
        return BadgeTone::Positive;
    return BadgeTone::Neutral;
}

wxString PrintStatusFrame::build_badge_text(MachineObject* obj, bool online, const wxString& warning_text, bool has_active_print) const
{
    if (obj == nullptr || !online)
        return _L("Offline");
    if (obj->print_error > 0)
        return _L("Error");
    if (!warning_text.empty())
        return _L("Warning");

    const wxString stage_text = build_stage_text(obj, online);
    if (contains_case_insensitive(stage_text, _L("Paused")))
        return _L("Paused");
    if (obj->print_status == "FINISH")
        return _L("Finished");
    if (has_active_print)
        return _L("Printing");
    return _L("Idle");
}

std::vector<std::string> PrintStatusFrame::get_machine_ids() const
{
    std::vector<std::string> result;
    auto* dev = wxGetApp().getDeviceManager();
    if (dev == nullptr)
        return result;

    const auto machines = dev->get_my_machine_list();
    result.reserve(machines.size());
    for (const auto& item : machines) {
        if (item.second != nullptr)
            result.emplace_back(item.first);
    }
    return result;
}

MachineObject* PrintStatusFrame::resolve_machine()
{
    if (!m_selected_dev_id.empty()) {
        if (auto* obj = get_machine_by_id(m_selected_dev_id))
            return obj;
    }

    m_selected_dev_id = get_default_machine_id();
    if (m_selected_dev_id.empty())
        return nullptr;
    return get_machine_by_id(m_selected_dev_id);
}

MachineObject* PrintStatusFrame::get_machine_by_id(const std::string& dev_id) const
{
    auto* dev = wxGetApp().getDeviceManager();
    return dev ? dev->get_my_machine(dev_id) : nullptr;
}

std::string PrintStatusFrame::get_default_machine_id() const
{
    auto* dev = wxGetApp().getDeviceManager();
    if (dev == nullptr)
        return {};

    if (auto* selected = dev->get_selected_machine()) {
        if (dev->get_my_machine(selected->get_dev_id()) != nullptr)
            return selected->get_dev_id();
    }

    const auto machines = dev->get_my_machine_list();
    if (!machines.empty())
        return machines.begin()->first;
    return {};
}

wxString PrintStatusFrame::build_machine_name(MachineObject* obj) const
{
    if (obj == nullptr)
        return na_text();

    const wxString dev_name = GUI::from_u8(obj->get_dev_name());
    return dev_name.empty() ? GUI::from_u8(obj->get_dev_id()) : dev_name;
}

wxString PrintStatusFrame::build_stage_text(MachineObject* obj, bool online) const
{
    if (obj == nullptr)
        return na_text();
    if (!online)
        return _L("Offline");

    if (obj->is_in_prepare() || obj->print_status == "SLICING") {
        wxString prepare_text;
        bool     show_percent = true;

        if (obj->is_in_prepare()) {
            prepare_text = _L("Downloading...");
        } else if (obj->print_status == "SLICING") {
            if (obj->queue_number <= 0) {
                prepare_text = _L("Cloud Slicing...");
            } else {
                prepare_text = wxString::Format(_L("In Cloud Slicing Queue, there are %d tasks ahead."), obj->queue_number);
                show_percent = false;
            }
        }

        if (obj->gcode_file_prepare_percent >= 0 && obj->gcode_file_prepare_percent <= 100 && show_percent)
            prepare_text += wxString::Format(" (%d%%)", obj->gcode_file_prepare_percent);

        return obj->get_curr_stage().IsEmpty() ? prepare_text : obj->get_curr_stage();
    }

    wxString stage = obj->get_curr_stage();
    if (!stage.IsEmpty())
        return stage;
    if (obj->print_status == "FINISH")
        return _L("Finished");
    if (obj->is_in_printing())
        return get_stage_string(obj->mc_print_stage);
    return _L("Idle");
}

wxString PrintStatusFrame::build_job_name_text(MachineObject* obj, bool online) const
{
    if (obj == nullptr || !online)
        return na_text();
    if (!obj->subtask_name.empty())
        return GUI::from_u8(obj->subtask_name);
    if (!obj->m_gcode_file.empty())
        return GUI::from_u8(obj->m_gcode_file);
    return na_text();
}

wxString PrintStatusFrame::build_progress_text(MachineObject* obj, bool online) const
{
    if (obj == nullptr || !online)
        return na_text();

    const int progress = build_progress_percent(obj, online);
    if (progress <= 0 && !(obj->is_in_printing() || obj->is_in_prepare() || obj->print_status == "SLICING" || obj->print_status == "FINISH"))
        return na_text();
    if (obj->print_status == "FINISH")
        return "100%";
    return wxString::Format("%d%%", progress);
}

wxString PrintStatusFrame::build_remaining_time_text(MachineObject* obj, bool online) const
{
    if (obj == nullptr || !online)
        return na_text();
    if (obj->print_status == "FINISH")
        return _L("Finished");
    if (obj->mc_left_time > 0) {
        try {
            const auto left_time = get_bbl_monitor_time_dhm(obj->mc_left_time);
            if (!left_time.empty())
                return "-" + GUI::from_u8(left_time);
        } catch (...) {
            ;
        }
    }
    return na_text();
}

wxString PrintStatusFrame::build_estimated_finish_time_text(MachineObject* obj, bool online, bool has_active_print) const
{
    if (obj == nullptr || !online)
        return _L("Estimated finish time: ") + na_text();
    if (obj->print_status == "FINISH")
        return _L("Estimated finish time: ") + _L("Finished");
    if (!has_active_print || obj->mc_left_time <= 0)
        return _L("Estimated finish time: ") + na_text();

    try {
        const bool use_12h_format = wxGetApp().app_config && wxGetApp().app_config->get("use_12h_time_format") == "true";
        const auto finish_time = get_bbl_finish_time_dhm(obj->mc_left_time, use_12h_format);
        if (!finish_time.empty())
            return _L("Estimated finish time: ") + GUI::from_u8(finish_time);
    } catch (...) {
        ;
    }

    return _L("Estimated finish time: ") + na_text();
}

wxString PrintStatusFrame::build_layers_text(MachineObject* obj, bool online) const
{
    if (obj == nullptr || !online)
        return na_text();
    if (obj->is_support_layer_num && obj->total_layers > 0 && obj->curr_layer >= 0)
        return wxString::Format("%d/%d", obj->curr_layer, obj->total_layers);
    return na_text();
}

wxString PrintStatusFrame::build_nozzle_temp_text(MachineObject* obj, bool online) const
{
    if (obj == nullptr || !online || obj->GetExtderSystem() == nullptr)
        return na_text();

    auto ext = obj->GetExtderSystem()->GetCurrentExtder();
    if (!ext.has_value())
        ext = obj->GetExtderSystem()->GetExtderById(MAIN_EXTRUDER_ID);
    if (!ext.has_value())
        return na_text();

    return wxString::Format("%d / %d", ext->GetCurrentTemp(), ext->GetTargetTemp()) + temperature_unit();
}

wxString PrintStatusFrame::build_bed_temp_text(MachineObject* obj, bool online) const
{
    if (obj == nullptr || !online || obj->GetBed() == nullptr)
        return na_text();

    auto* bed = obj->GetBed();
    return wxString::Format("%d / %d", static_cast<int>(bed->GetBedTemp()), static_cast<int>(bed->GetBedTempTarget())) + temperature_unit();
}

wxString PrintStatusFrame::build_chamber_temp_text(MachineObject* obj, bool online) const
{
    if (obj == nullptr || !online || obj->GetChamber() == nullptr)
        return na_text();

    const auto& chamber = obj->GetChamber();
    if (!chamber->SupportChamberTempDisplay())
        return na_text();

    if (chamber->SupportChamberEdit())
        return wxString::Format("%d / %d", static_cast<int>(chamber->GetChamberTemp()), static_cast<int>(chamber->GetChamberTempTarget())) + temperature_unit();

    return wxString::Format("%d", static_cast<int>(chamber->GetChamberTemp())) + temperature_unit();
}

wxString PrintStatusFrame::build_warning_text(MachineObject* obj) const
{
    if (obj == nullptr)
        return wxEmptyString;

    if (obj->print_error > 0) {
        if (auto* query = wxGetApp().get_hms_query()) {
            wxString error_text = query->query_print_error_msg(obj, obj->print_error);
            if (!error_text.empty())
                return error_text;
        }
        return _L("Error code") + ": " + GUI::from_u8(obj->get_print_error_str());
    }

    if (auto* hms = obj->GetHMS()) {
        for (const auto& item : hms->GetHMSItems()) {
            if (auto* query = wxGetApp().get_hms_query()) {
                wxString warning_text = query->query_hms_msg(obj, item.get_long_error_code());
                if (!warning_text.empty())
                    return warning_text;
            }
            if (!item.get_long_error_code().empty())
                return _L("Warning code") + ": " + GUI::from_u8(item.get_long_error_code());
        }
    }

    return wxEmptyString;
}

void PrintStatusFrame::on_timer(wxTimerEvent& /*event*/)
{
    if (m_is_shutting_down || m_is_initializing || !m_ui_built)
        return;

    refresh_content();
}

void PrintStatusFrame::on_close(wxCloseEvent& event)
{
    if (m_is_shutting_down || !event.CanVeto()) {
        event.Skip();
        return;
    }

    persist_geometry(true);
    m_refresh_timer.Stop();
    Hide();
}

void PrintStatusFrame::on_printer_changed(wxCommandEvent& event)
{
    if (m_updating_printer_choice) {
        event.Skip();
        return;
    }

    const int selection = m_printer_choice->GetSelection();
    if (selection >= 0 && selection < static_cast<int>(m_choice_dev_ids.size())) {
        m_selected_dev_id = m_choice_dev_ids[selection];
        refresh_content();
    }
    event.Skip();
}

void PrintStatusFrame::on_move(wxMoveEvent& event)
{
    if (!m_ignore_geometry_events)
        persist_geometry(false);
    event.Skip();
}

void PrintStatusFrame::on_size(wxSizeEvent& event)
{
    event.Skip();
}

}} // namespace Slic3r::GUI
