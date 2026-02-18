#include "HelioHistoryDialog.hpp"
#include "HelioReleaseNote.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/Http.hpp"
#include <boost/log/trivial.hpp>
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "BitmapCache.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticLine.hpp"
#include "Plater.hpp"
#include "BackgroundSlicingProcess.hpp"

#include <wx/dcgraph.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/scrolwin.h>
#include <wx/wupdlock.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/file.h>

#include <iomanip>
#include <sstream>
#include <regex>
#include <thread>

namespace Slic3r { namespace GUI {

// Helio dark palette theme colors (matching existing Helio dialogs)
namespace {
    // Base background: #07090C
    const wxColour HELIO_BG_BASE(7, 9, 12);
    // Panel/card background: #0E1320
    const wxColour HELIO_CARD_BG(14, 19, 32);
    // Card highlight (subtle): #121A2B
    const wxColour HELIO_CARD_HIGHLIGHT(18, 26, 43);
    // Border: rgba(255,255,255,0.10)
    const wxColour HELIO_BORDER(255, 255, 255, 25);
    // Text: #EEF2FF
    const wxColour HELIO_TEXT(238, 242, 255);
    // Muted text: #A8B0C0
    const wxColour HELIO_MUTED(168, 176, 192);
    // Purple accent: #AF7CFF (simulation)
    const wxColour HELIO_PURPLE(175, 124, 255);
    // Blue accent: #4F86FF (optimization)
    const wxColour HELIO_BLUE(79, 134, 255);
    // Success green: #22C55E
    const wxColour HELIO_SUCCESS(34, 197, 94);
    // Warning yellow: #FBBF24
    const wxColour HELIO_WARNING(251, 191, 36);
    // Error red: #EF4444
    const wxColour HELIO_ERROR(239, 68, 68);
}

HelioInputDialogTheme HelioHistoryDialog::get_theme() const
{
    HelioInputDialogTheme theme;
    theme.bg = HELIO_BG_BASE;
    theme.card = HELIO_CARD_BG;
    theme.card2 = HELIO_CARD_HIGHLIGHT;
    theme.border = HELIO_BORDER;
    theme.text = HELIO_TEXT;
    theme.muted = HELIO_MUTED;
    theme.purple = HELIO_PURPLE;
    theme.blue = HELIO_BLUE;
    return theme;
}

HelioHistoryDialog::HelioHistoryDialog(wxWindow* parent)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe),
                wxID_ANY,
                _L("Recent Helio Runs"),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    shared_ptr = std::make_shared<int>(0);

    // Set Helio icon
    try {
        wxBitmap bmp = create_scaled_bitmap("helio_icon", this, 32);
        if (bmp.IsOk()) {
            wxIcon icon;
            icon.CopyFromBitmap(bmp);
            SetIcon(icon);
        }
    } catch (...) {
        // Icon loading failed, continue anyway
    }

    // Use Helio dark background
    SetBackgroundColour(HELIO_BG_BASE);

    create_ui();

    // Set size after UI is created (FromDIP is now safe to call)
    SetMinSize(wxSize(FromDIP(700), FromDIP(600)));
    SetSize(wxSize(FromDIP(700), FromDIP(600)));

    // Load recent runs after UI is created
    load_recent_runs();
}

HelioHistoryDialog::~HelioHistoryDialog()
{
    // Signal any async callbacks to stop
    shared_ptr.reset();
}

void HelioHistoryDialog::create_ui()
{
    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    // Create header with refresh button
    create_header(m_main_sizer);

    // Create scrollable content area
    m_scroll_window = new wxScrolledWindow(this, wxID_ANY);
    m_scroll_window->SetBackgroundColour(HELIO_BG_BASE);
    m_scroll_window->SetScrollRate(0, 20);

    m_content_sizer = new wxBoxSizer(wxVERTICAL);
    m_scroll_window->SetSizer(m_content_sizer);

    m_main_sizer->Add(m_scroll_window, 1, wxEXPAND | wxALL, FromDIP(16));

    // Create loading state (initially visible)
    create_loading_state();

    // Create empty state (initially hidden)
    create_empty_state();

    // Create content panel (initially hidden)
    m_content_panel = new wxPanel(m_scroll_window, wxID_ANY);
    if (m_content_panel) {
        m_content_panel->SetBackgroundColour(HELIO_BG_BASE);
        m_content_panel->Hide();
    }

    // Close button at bottom
    auto* button_sizer = new wxBoxSizer(wxHORIZONTAL);
    button_sizer->AddStretchSpacer();

    StateColor close_btn_bg(
        std::pair<wxColour, int>(HELIO_CARD_HIGHLIGHT, StateColor::Hovered),
        std::pair<wxColour, int>(HELIO_CARD_BG, StateColor::Normal));
    StateColor close_btn_border(
        std::pair<wxColour, int>(HELIO_BORDER, StateColor::Normal));
    StateColor close_btn_text(
        std::pair<wxColour, int>(HELIO_TEXT, StateColor::Normal));

    m_button_close = new Button(this, _L("Close"));
    if (m_button_close) {
        m_button_close->SetBackgroundColor(close_btn_bg);
        m_button_close->SetBorderColor(close_btn_border);
        m_button_close->SetTextColor(close_btn_text);
        m_button_close->SetMinSize(wxSize(FromDIP(100), FromDIP(36)));
        m_button_close->SetCornerRadius(FromDIP(6));
        m_button_close->Bind(wxEVT_LEFT_DOWN, &HelioHistoryDialog::on_close, this);

        button_sizer->Add(m_button_close, 0, wxALL, FromDIP(8));
    }

    m_main_sizer->Add(button_sizer, 0, wxEXPAND);

    SetSizer(m_main_sizer);
    Layout();
    Fit();
    {
        wxWindow* parent = GetParent();
        if (parent) {
            wxPoint parentPos = parent->GetScreenPosition();
            wxSize parentSize = parent->GetSize();
            wxSize dlgSize = GetSize();
            int x = parentPos.x + (parentSize.GetWidth() - dlgSize.GetWidth()) / 2;
            int y = parentPos.y + (parentSize.GetHeight() - dlgSize.GetHeight()) / 3;
            SetPosition(wxPoint(x, y));
        }
    }

    BOOST_LOG_TRIVIAL(info) << "HelioHistoryDialog UI created successfully";
}

void HelioHistoryDialog::create_header(wxBoxSizer* parent_sizer)
{
    auto* header_panel = new wxPanel(this, wxID_ANY);
    header_panel->SetBackgroundColour(HELIO_BG_BASE);

    auto* header_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Description text
    auto* desc_label = new Label(header_panel, Label::Body_14);
    desc_label->SetLabel(_L("Showing recent completed optimizations and simulations"));
    desc_label->SetForegroundColour(HELIO_MUTED);

    header_sizer->Add(desc_label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));

    // Refresh button
    StateColor refresh_btn_bg(
        std::pair<wxColour, int>(HELIO_CARD_HIGHLIGHT, StateColor::Hovered),
        std::pair<wxColour, int>(HELIO_CARD_BG, StateColor::Normal));
    StateColor refresh_btn_border(
        std::pair<wxColour, int>(HELIO_BORDER, StateColor::Normal));
    StateColor refresh_btn_text(
        std::pair<wxColour, int>(HELIO_TEXT, StateColor::Normal));

    m_button_refresh = new Button(header_panel, _L("Refresh"));
    m_button_refresh->SetBackgroundColor(refresh_btn_bg);
    m_button_refresh->SetBorderColor(refresh_btn_border);
    m_button_refresh->SetTextColor(refresh_btn_text);
    m_button_refresh->SetMinSize(wxSize(FromDIP(100), FromDIP(32)));
    m_button_refresh->SetCornerRadius(FromDIP(6));
    m_button_refresh->Bind(wxEVT_LEFT_DOWN, &HelioHistoryDialog::on_refresh, this);

    header_sizer->Add(m_button_refresh, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(16));

    header_panel->SetSizer(header_sizer);
    parent_sizer->Add(header_panel, 0, wxEXPAND | wxTOP, FromDIP(8));
}

void HelioHistoryDialog::create_loading_state()
{
    m_loading_label = new Label(m_scroll_window, Label::Body_14);
    m_loading_label->SetLabel(_L("Loading recent runs..."));
    m_loading_label->SetForegroundColour(HELIO_MUTED);
    m_content_sizer->Add(m_loading_label, 0, wxALIGN_CENTER | wxALL, FromDIP(40));
}

void HelioHistoryDialog::create_empty_state()
{
    m_empty_state_panel = new wxPanel(m_scroll_window, wxID_ANY);
    m_empty_state_panel->SetBackgroundColour(HELIO_BG_BASE);

    auto* empty_sizer = new wxBoxSizer(wxVERTICAL);

    // Empty state icon (could use a search icon or similar)
    auto* empty_label = new Label(m_empty_state_panel, Label::Head_18);
    empty_label->SetLabel(_L("No Recent Runs Found"));
    empty_label->SetForegroundColour(HELIO_TEXT);

    auto* empty_desc = new Label(m_empty_state_panel, Label::Body_14);
    empty_desc->SetLabel(_L("No completed simulations or optimizations found"));
    empty_desc->SetForegroundColour(HELIO_MUTED);

    empty_sizer->AddStretchSpacer();
    empty_sizer->Add(empty_label, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(8));
    empty_sizer->Add(empty_desc, 0, wxALIGN_CENTER);
    empty_sizer->AddStretchSpacer();

    m_empty_state_panel->SetSizer(empty_sizer);
    m_content_sizer->Add(m_empty_state_panel, 1, wxEXPAND | wxALL, FromDIP(40));
    m_empty_state_panel->Hide();
}

void HelioHistoryDialog::load_recent_runs()
{
    show_loading_state();

    try {
        // Get API credentials
        std::string helio_api_url = HelioQuery::get_helio_api_url();
        std::string helio_pat = HelioQuery::get_helio_pat();

        BOOST_LOG_TRIVIAL(info) << "HelioHistoryDialog: API URL: " << helio_api_url;
        BOOST_LOG_TRIVIAL(info) << "HelioHistoryDialog: PAT available: " << (!helio_pat.empty() ? "yes" : "no");

        if (helio_pat.empty()) {
            // No PAT token - show empty state with error
            BOOST_LOG_TRIVIAL(warning) << "HelioHistoryDialog: No PAT token available";
            m_optimizations.clear();
            m_simulations.clear();
            show_empty_state();
            return;
        }

        // Query recent runs from backend (synchronous - simpler and cross-platform safe)
        BOOST_LOG_TRIVIAL(info) << "HelioHistoryDialog: Querying recent runs from backend...";
        auto result = HelioQuery::get_recent_runs(helio_api_url, helio_pat);

        if (!result.success) {
            // Error occurred - show empty state
            BOOST_LOG_TRIVIAL(error) << "HelioHistoryDialog: Query failed - " << result.error;
            m_optimizations.clear();
            m_simulations.clear();
            show_empty_state();
            return;
        }

        m_optimizations = result.optimizations;
        m_simulations = result.simulations;

        BOOST_LOG_TRIVIAL(info) << "HelioHistoryDialog loaded " << m_optimizations.size() << " optimizations and " << m_simulations.size() << " simulations";

        // Filter to last 10 of each type
        if (m_optimizations.size() > 10) {
            m_optimizations.resize(10);
        }
        if (m_simulations.size() > 10) {
            m_simulations.resize(10);
        }

        if (m_optimizations.empty() && m_simulations.empty()) {
            BOOST_LOG_TRIVIAL(info) << "HelioHistoryDialog: No runs to display, showing empty state";
            show_empty_state();
        } else {
            BOOST_LOG_TRIVIAL(info) << "HelioHistoryDialog: Showing content with " << m_optimizations.size() << " opts, " << m_simulations.size() << " sims";
            show_content();
        }
    } catch (const std::exception& e) {
        // Catch any errors and show empty state
        BOOST_LOG_TRIVIAL(error) << "HelioHistoryDialog: Exception - " << e.what();
        m_optimizations.clear();
        m_simulations.clear();
        show_empty_state();
    }
}

void HelioHistoryDialog::show_loading_state()
{
    if (m_loading_label) m_loading_label->Show();
    if (m_empty_state_panel) m_empty_state_panel->Hide();
    if (m_content_panel) m_content_panel->Hide();
    Layout();
}

void HelioHistoryDialog::show_empty_state()
{
    if (m_loading_label) m_loading_label->Hide();
    if (m_empty_state_panel) m_empty_state_panel->Show();
    if (m_content_panel) m_content_panel->Hide();
    Layout();
}

void HelioHistoryDialog::show_content()
{
    try {
        if (m_loading_label) m_loading_label->Hide();
        if (m_empty_state_panel) m_empty_state_panel->Hide();

        // Clear existing content
        if (m_content_panel && m_content_sizer) {
            m_content_sizer->Detach(m_content_panel);
            m_content_panel->Destroy();
            m_content_panel = nullptr;
        }

        if (!m_scroll_window) return;

        m_content_panel = new wxPanel(m_scroll_window, wxID_ANY);
        if (!m_content_panel) return;

        m_content_panel->SetBackgroundColour(HELIO_BG_BASE);

        auto* content_panel_sizer = new wxBoxSizer(wxVERTICAL);
        if (!content_panel_sizer) return;

        auto theme = get_theme();

        // Create sections
        create_content_sections();

        // If we have optimizations
        if (!m_optimizations.empty()) {
            auto* opt_section = create_optimization_section(theme);
            if (opt_section) {
                content_panel_sizer->Add(opt_section, 0, wxEXPAND | wxBOTTOM, FromDIP(24));
            }
        }

        // If we have simulations
        if (!m_simulations.empty()) {
            auto* sim_section = create_simulation_section(theme);
            if (sim_section) {
                content_panel_sizer->Add(sim_section, 0, wxEXPAND);
            }
        }

        m_content_panel->SetSizer(content_panel_sizer);
        if (m_content_sizer) {
            m_content_sizer->Add(m_content_panel, 1, wxEXPAND);
        }
        m_content_panel->Show();

        if (m_scroll_window) {
            m_scroll_window->FitInside();
        }
        Layout();
    } catch (const std::exception& e) {
        // If anything goes wrong, just show empty state
        show_empty_state();
    }
}

void HelioHistoryDialog::create_content_sections()
{
    // This function is called but the actual sections are created in show_content()
    // Placeholder for future enhancements
}

wxPanel* HelioHistoryDialog::create_optimization_section(const HelioInputDialogTheme& theme)
{
    auto* section_panel = new wxPanel(m_content_panel, wxID_ANY);
    section_panel->SetBackgroundColour(HELIO_BG_BASE);

    auto* section_sizer = new wxBoxSizer(wxVERTICAL);

    // Section title
    auto* title = new Label(section_panel, Label::Head_16);
    title->SetLabel(wxString::Format(_L("OPTIMIZATIONS (%d)"), (int)m_optimizations.size()));
    title->SetForegroundColour(HELIO_TEXT);
    section_sizer->Add(title, 0, wxBOTTOM, FromDIP(12));

    // Separator line
    auto* separator = new StaticLine(section_panel);
    separator->SetLineColour(HELIO_BORDER);
    section_sizer->Add(separator, 0, wxEXPAND | wxBOTTOM, FromDIP(16));

    // Add run cards
    for (const auto& run : m_optimizations) {
        auto* card = create_run_card(section_panel, run, theme);
        section_sizer->Add(card, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
    }

    section_panel->SetSizer(section_sizer);
    return section_panel;
}

wxPanel* HelioHistoryDialog::create_simulation_section(const HelioInputDialogTheme& theme)
{
    auto* section_panel = new wxPanel(m_content_panel, wxID_ANY);
    section_panel->SetBackgroundColour(HELIO_BG_BASE);

    auto* section_sizer = new wxBoxSizer(wxVERTICAL);

    // Section title
    auto* title = new Label(section_panel, Label::Head_16);
    title->SetLabel(wxString::Format(_L("SIMULATIONS (%d)"), (int)m_simulations.size()));
    title->SetForegroundColour(HELIO_TEXT);
    section_sizer->Add(title, 0, wxBOTTOM, FromDIP(12));

    // Separator line
    auto* separator = new StaticLine(section_panel);
    separator->SetLineColour(HELIO_BORDER);
    section_sizer->Add(separator, 0, wxEXPAND | wxBOTTOM, FromDIP(16));

    // Add run cards
    for (const auto& run : m_simulations) {
        auto* card = create_run_card(section_panel, run, theme);
        section_sizer->Add(card, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
    }

    section_panel->SetSizer(section_sizer);
    return section_panel;
}

wxPanel* HelioHistoryDialog::create_run_card(wxWindow* parent, const HelioQuery::OptimizationRun& run, const HelioInputDialogTheme& theme)
{
    auto* card = new wxPanel(parent, wxID_ANY);
    card->SetBackgroundColour(HELIO_CARD_BG);

    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    // Top row: Name and status
    auto* top_row = new wxBoxSizer(wxHORIZONTAL);

    auto* name_label = new Label(card, Label::Body_14);
    name_label->SetLabel(wxString(run.name));
    name_label->SetForegroundColour(HELIO_TEXT);
    top_row->Add(name_label, 1, wxALIGN_CENTER_VERTICAL);

    auto* status_label = new Label(card, Label::Body_12);
    status_label->SetLabel("[" + wxString(run.status) + "]");
    status_label->SetForegroundColour(get_status_color(run.status));
    top_row->Add(status_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    card_sizer->Add(top_row, 0, wxEXPAND | wxALL, FromDIP(12));

    // Metadata row
    auto* meta_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* printer_label = new Label(card, Label::Body_12);
    printer_label->SetLabel(wxString(run.printer_name));
    printer_label->SetForegroundColour(HELIO_MUTED);
    meta_sizer->Add(printer_label, 0, wxRIGHT, FromDIP(16));

    auto* material_label = new Label(card, Label::Body_12);
    material_label->SetLabel(wxString(run.material_name));
    material_label->SetForegroundColour(HELIO_MUTED);
    meta_sizer->Add(material_label, 0, wxRIGHT, FromDIP(16));

    auto* layers_label = new Label(card, Label::Body_12);
    layers_label->SetLabel(wxString::Format(_L("%d layers"), run.number_of_layers));
    layers_label->SetForegroundColour(HELIO_MUTED);
    meta_sizer->Add(layers_label, 0, wxRIGHT, FromDIP(16));

    auto* time_label = new Label(card, Label::Body_12);
    time_label->SetLabel(format_time_ago(run.timestamp));
    time_label->SetForegroundColour(HELIO_MUTED);
    meta_sizer->Add(time_label, 0);

    card_sizer->Add(meta_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    // Quality improvement
    if (!run.quality_mean_improvement.empty() || !run.quality_std_improvement.empty()) {
        auto* quality_label = new Label(card, Label::Body_12);
        quality_label->SetLabel(wxString::Format(_L("Quality: %s | Consistency: %s"),
            run.quality_mean_improvement, run.quality_std_improvement));
        quality_label->SetForegroundColour(HELIO_SUCCESS);
        card_sizer->Add(quality_label, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    // Buttons
    auto* button_row = new wxBoxSizer(wxHORIZONTAL);

    StateColor btn_bg(
        std::pair<wxColour, int>(HELIO_BLUE, StateColor::Hovered),
        std::pair<wxColour, int>(theme.blue, StateColor::Normal));
    StateColor btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    auto* download_btn = new Button(card, _L("Download GCode"));
    download_btn->SetBackgroundColor(btn_bg);
    download_btn->SetTextColor(btn_text);
    download_btn->SetMinSize(wxSize(FromDIP(140), FromDIP(32)));
    download_btn->SetCornerRadius(FromDIP(4));
    download_btn->Bind(wxEVT_LEFT_DOWN, [this, run](wxMouseEvent&) {
        // Use thermal index enhanced GCode URL for optimizations
        on_download_gcode(run.optimized_gcode_with_thermal_indexes_url, run.name);
    });

    StateColor view_btn_bg(
        std::pair<wxColour, int>(HELIO_CARD_HIGHLIGHT, StateColor::Hovered),
        std::pair<wxColour, int>(HELIO_CARD_BG, StateColor::Normal));
    StateColor view_btn_border(
        std::pair<wxColour, int>(HELIO_BORDER, StateColor::Normal));

    auto* view_btn = new Button(card, _L("View Details"));
    view_btn->SetBackgroundColor(view_btn_bg);
    view_btn->SetBorderColor(view_btn_border);
    view_btn->SetTextColor(StateColor(std::pair<wxColour, int>(HELIO_TEXT, StateColor::Normal)));
    view_btn->SetMinSize(wxSize(FromDIP(120), FromDIP(32)));
    view_btn->SetCornerRadius(FromDIP(4));
    view_btn->Bind(wxEVT_LEFT_DOWN, [this, run](wxMouseEvent&) {
        on_view_details_opt(run);
    });

    button_row->Add(download_btn, 0, wxRIGHT, FromDIP(8));
    button_row->Add(view_btn, 0);

    card_sizer->Add(button_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    card->SetSizer(card_sizer);
    return card;
}

wxPanel* HelioHistoryDialog::create_run_card(wxWindow* parent, const HelioQuery::SimulationRun& run, const HelioInputDialogTheme& theme)
{
    auto* card = new wxPanel(parent, wxID_ANY);
    card->SetBackgroundColour(HELIO_CARD_BG);

    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    // Top row: Name and status
    auto* top_row = new wxBoxSizer(wxHORIZONTAL);

    auto* name_label = new Label(card, Label::Body_14);
    name_label->SetLabel(wxString(run.name));
    name_label->SetForegroundColour(HELIO_TEXT);
    top_row->Add(name_label, 1, wxALIGN_CENTER_VERTICAL);

    auto* status_label = new Label(card, Label::Body_12);
    status_label->SetLabel("[" + wxString(run.status) + "]");
    status_label->SetForegroundColour(get_status_color(run.status));
    top_row->Add(status_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    card_sizer->Add(top_row, 0, wxEXPAND | wxALL, FromDIP(12));

    // Metadata row
    auto* meta_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* printer_label = new Label(card, Label::Body_12);
    printer_label->SetLabel(wxString(run.printer_name));
    printer_label->SetForegroundColour(HELIO_MUTED);
    meta_sizer->Add(printer_label, 0, wxRIGHT, FromDIP(16));

    auto* material_label = new Label(card, Label::Body_12);
    material_label->SetLabel(wxString(run.material_name));
    material_label->SetForegroundColour(HELIO_MUTED);
    meta_sizer->Add(material_label, 0, wxRIGHT, FromDIP(16));

    auto* layers_label = new Label(card, Label::Body_12);
    layers_label->SetLabel(wxString::Format(_L("%d layers"), run.number_of_layers));
    layers_label->SetForegroundColour(HELIO_MUTED);
    meta_sizer->Add(layers_label, 0, wxRIGHT, FromDIP(16));

    auto* time_label = new Label(card, Label::Body_12);
    time_label->SetLabel(format_time_ago(run.timestamp));
    time_label->SetForegroundColour(HELIO_MUTED);
    meta_sizer->Add(time_label, 0);

    card_sizer->Add(meta_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    // Print outcome
    if (!run.print_outcome.empty()) {
        auto* outcome_label = new Label(card, Label::Body_12);
        outcome_label->SetLabel(get_print_outcome_text(run.print_outcome));

        // Color based on outcome
        if (run.print_outcome == "WILL_PRINT") {
            outcome_label->SetForegroundColour(HELIO_SUCCESS);
        } else if (run.print_outcome == "MAY_PRINT") {
            outcome_label->SetForegroundColour(HELIO_WARNING);
        } else if (run.print_outcome == "LIKELY_FAIL") {
            outcome_label->SetForegroundColour(HELIO_ERROR);
        } else {
            outcome_label->SetForegroundColour(HELIO_MUTED);
        }

        card_sizer->Add(outcome_label, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    // Buttons
    auto* button_row = new wxBoxSizer(wxHORIZONTAL);

    StateColor btn_bg(
        std::pair<wxColour, int>(HELIO_PURPLE, StateColor::Hovered),
        std::pair<wxColour, int>(theme.purple, StateColor::Normal));
    StateColor btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    auto* download_btn = new Button(card, _L("Download GCode"));
    download_btn->SetBackgroundColor(btn_bg);
    download_btn->SetTextColor(btn_text);
    download_btn->SetMinSize(wxSize(FromDIP(140), FromDIP(32)));
    download_btn->SetCornerRadius(FromDIP(4));
    download_btn->Bind(wxEVT_LEFT_DOWN, [this, run](wxMouseEvent&) {
        // Use thermal index enhanced GCode URL for simulations
        on_download_gcode(run.thermal_index_gcode_url, run.name);
    });

    StateColor view_btn_bg(
        std::pair<wxColour, int>(HELIO_CARD_HIGHLIGHT, StateColor::Hovered),
        std::pair<wxColour, int>(HELIO_CARD_BG, StateColor::Normal));
    StateColor view_btn_border(
        std::pair<wxColour, int>(HELIO_BORDER, StateColor::Normal));

    auto* view_btn = new Button(card, _L("View Results"));
    view_btn->SetBackgroundColor(view_btn_bg);
    view_btn->SetBorderColor(view_btn_border);
    view_btn->SetTextColor(StateColor(std::pair<wxColour, int>(HELIO_TEXT, StateColor::Normal)));
    view_btn->SetMinSize(wxSize(FromDIP(120), FromDIP(32)));
    view_btn->SetCornerRadius(FromDIP(4));
    view_btn->Bind(wxEVT_LEFT_DOWN, [this, run](wxMouseEvent&) {
        on_view_details_sim(run);
    });

    button_row->Add(download_btn, 0, wxRIGHT, FromDIP(8));
    button_row->Add(view_btn, 0);
    card_sizer->Add(button_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    card->SetSizer(card_sizer);
    return card;
}

void HelioHistoryDialog::on_refresh(wxMouseEvent& event)
{
    BOOST_LOG_TRIVIAL(info) << "Refresh button clicked";
    load_recent_runs();
}

void HelioHistoryDialog::on_close(wxMouseEvent& event)
{
    EndModal(wxID_CLOSE);
}

void HelioHistoryDialog::on_helio_completion(wxEvent& event)
{
    BOOST_LOG_TRIVIAL(info) << "HelioHistoryDialog: Helio completion event received, closing dialog to show completion";

    // Close this dialog so the completion dialog can be shown
    EndModal(wxID_CLOSE);

    // Skip the event so it continues to propagate to the Plater
    event.Skip();
}

void HelioHistoryDialog::on_download_gcode(const std::string& gcode_url, const std::string& run_name)
{
    if (gcode_url.empty()) {
        wxMessageBox(_L("GCode URL is not available"), _L("Download Error"), wxOK | wxICON_ERROR);
        return;
    }

    // Show file save dialog
    wxFileDialog save_dialog(this,
        _L("Save GCode File"),
        wxStandardPaths::Get().GetDocumentsDir(),
        wxString(run_name) + ".gcode",
        "GCode files (*.gcode)|*.gcode",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (save_dialog.ShowModal() == wxID_CANCEL) {
        return;
    }

    std::string save_path = save_dialog.GetPath().ToStdString();

    BOOST_LOG_TRIVIAL(info) << "Downloading GCode from: " << gcode_url << " to: " << save_path;

    // Download the file using Http::get()
    auto http = Http::get(gcode_url);

    bool download_success = false;
    std::string error_msg;
    std::string downloaded_content;

    http.on_complete([&downloaded_content, &download_success](std::string body, unsigned status) {
            if (status == 200) {
                downloaded_content = body;
                download_success = true;
                BOOST_LOG_TRIVIAL(info) << "GCode download completed successfully, size: " << body.length();
            } else {
                BOOST_LOG_TRIVIAL(error) << "GCode download failed with status: " << status;
            }
        })
        .on_error([&error_msg, &download_success](std::string body, std::string error, unsigned status) {
            download_success = false;
            error_msg = error;
            BOOST_LOG_TRIVIAL(error) << "GCode download error: " << error << ", status: " << status;
        })
        .perform_sync();

    if (download_success && !downloaded_content.empty()) {
        // Save the downloaded content to file
        try {
            wxFile file(wxString::FromUTF8(save_path), wxFile::write);
            if (file.IsOpened()) {
                file.Write(downloaded_content.data(), downloaded_content.size());
                file.Close();

                wxMessageBox(
                    wxString::Format(_L("GCode file downloaded successfully!\n\nSaved to: %s"), wxString(save_path)),
                    _L("Download Complete"),
                    wxOK | wxICON_INFORMATION);

                BOOST_LOG_TRIVIAL(info) << "GCode file saved successfully: " << save_path;
            } else {
                wxMessageBox(
                    wxString::Format(_L("Failed to open file for writing:\n%s"), wxString(save_path)),
                    _L("Download Error"),
                    wxOK | wxICON_ERROR);
                BOOST_LOG_TRIVIAL(error) << "Failed to open file for writing: " << save_path;
            }
        } catch (const std::exception& e) {
            wxMessageBox(
                wxString::Format(_L("Error saving file: %s"), wxString(e.what())),
                _L("Download Error"),
                wxOK | wxICON_ERROR);
            BOOST_LOG_TRIVIAL(error) << "Error saving file: " << e.what();
        }
    } else {
        wxMessageBox(
            wxString::Format(_L("Failed to download GCode file.\n\nError: %s"),
                wxString(error_msg.empty() ? "Unknown error" : error_msg)),
            _L("Download Error"),
            wxOK | wxICON_ERROR);
    }
}

void HelioHistoryDialog::on_view_details_opt(const HelioQuery::OptimizationRun& run)
{
    // Show detail dialog
    wxString details = wxString::Format(
        "Optimization Details:\n\n"
        "Name: %s\n"
        "Status: %s\n"
        "Printer: %s\n"
        "Material: %s\n"
        "Layers: %d\n"
        "Quality Improvement: %s\n"
        "Consistency Improvement: %s",
        run.name, run.status, run.printer_name, run.material_name,
        run.number_of_layers, run.quality_mean_improvement, run.quality_std_improvement);

    wxMessageBox(details, _L("Optimization Details"), wxOK | wxICON_INFORMATION);
}

void HelioHistoryDialog::on_view_details_sim(const HelioQuery::SimulationRun& run)
{
    // TODO: Open existing HelioSimulationResultsDialog with this run's data
    // For now, show a simple detail dialog
    wxString details = wxString::Format(
        "Simulation Details:\n\n"
        "Name: %s\n"
        "Status: %s\n"
        "Printer: %s\n"
        "Material: %s\n"
        "Layers: %d\n"
        "Print Outcome: %s\n",
        run.name, run.status, run.printer_name, run.material_name,
        run.number_of_layers, run.print_outcome);

    wxMessageBox(details, _L("Simulation Details"), wxOK | wxICON_INFORMATION);
}

wxString HelioHistoryDialog::format_time_ago(const std::chrono::system_clock::time_point& timestamp)
{
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp);

    int seconds = duration.count();

    if (seconds < 60) {
        return wxString::Format(_L("%d seconds ago"), seconds);
    } else if (seconds < 3600) {
        int minutes = seconds / 60;
        return wxString::Format(_L("%d minutes ago"), minutes);
    } else if (seconds < 86400) {
        int hours = seconds / 3600;
        return wxString::Format(_L("%d hours ago"), hours);
    } else {
        int days = seconds / 86400;
        return wxString::Format(_L("%d days ago"), days);
    }
}

wxString HelioHistoryDialog::format_file_size(int layers)
{
    // Rough estimate: ~30KB per layer (this is a placeholder)
    int kb = layers * 30;
    if (kb < 1024) {
        return wxString::Format("%d KB", kb);
    } else {
        float mb = kb / 1024.0f;
        return wxString::Format("%.1f MB", mb);
    }
}

std::chrono::system_clock::time_point HelioHistoryDialog::parse_timestamp_from_name(const std::string& name)
{
    // Parse timestamp from name format: "BambuSlicer 2026-01-23T07:52:27"
    // Use regex to extract the ISO timestamp
    std::regex timestamp_regex(R"((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2}))");
    std::smatch match;

    if (std::regex_search(name, match, timestamp_regex)) {
        std::tm tm = {};
        tm.tm_year = std::stoi(match[1].str()) - 1900;
        tm.tm_mon = std::stoi(match[2].str()) - 1;
        tm.tm_mday = std::stoi(match[3].str());
        tm.tm_hour = std::stoi(match[4].str());
        tm.tm_min = std::stoi(match[5].str());
        tm.tm_sec = std::stoi(match[6].str());

        std::time_t time = std::mktime(&tm);
        return std::chrono::system_clock::from_time_t(time);
    }

    // If parsing fails, return current time
    return std::chrono::system_clock::now();
}

wxColour HelioHistoryDialog::get_status_color(const std::string& status)
{
    if (status == "FINISHED") {
        return HELIO_SUCCESS;
    } else if (status == "RUNNING") {
        return HELIO_BLUE;
    } else if (status == "FAILED" || status == "ERROR" || status == "RESTRICTED") {
        return HELIO_ERROR;
    } else {
        return HELIO_MUTED;
    }
}

wxString HelioHistoryDialog::get_print_outcome_text(const std::string& outcome)
{
    if (outcome == "WILL_PRINT") {
        return _L("Print Outcome: WILL_PRINT");
    } else if (outcome == "MAY_PRINT") {
        return _L("Print Outcome: MAY_PRINT");
    } else if (outcome == "LIKELY_FAIL") {
        return _L("Print Outcome: LIKELY_FAIL");
    } else {
        return _L("Print Outcome: ") + wxString(_L(outcome));
    }
}

void HelioHistoryDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    // Refresh DPI-dependent elements
    Refresh();
}

}} // namespace Slic3r::GUI
