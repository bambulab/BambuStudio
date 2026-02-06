#ifndef slic3r_GUI_HelioHistoryDialog_hpp_
#define slic3r_GUI_HelioHistoryDialog_hpp_

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <vector>
#include <chrono>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "HelioDragon.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ScrolledWindow.hpp"

namespace Slic3r { namespace GUI {

// Forward declaration
struct HelioInputDialogTheme;

class HelioHistoryDialog : public DPIDialog
{
public:
    HelioHistoryDialog(wxWindow* parent = nullptr);
    ~HelioHistoryDialog();

    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    // UI Components
    wxScrolledWindow* m_scroll_window{nullptr};
    wxBoxSizer* m_main_sizer{nullptr};
    wxBoxSizer* m_content_sizer{nullptr};
    Button* m_button_refresh{nullptr};
    Button* m_button_close{nullptr};

    // Loading/empty state
    Label* m_loading_label{nullptr};
    wxPanel* m_empty_state_panel{nullptr};
    wxPanel* m_content_panel{nullptr};

    // Data
    std::vector<HelioQuery::OptimizationRun> m_optimizations;
    std::vector<HelioQuery::SimulationRun> m_simulations;

    // Shared state
    std::shared_ptr<int> shared_ptr{nullptr};

    // Theme helper
    HelioInputDialogTheme get_theme() const;

    // UI Creation
    void create_ui();
    void create_header(wxBoxSizer* parent_sizer);
    void create_loading_state();
    void create_empty_state();
    void create_content_sections();

    // Section creators
    wxPanel* create_optimization_section(const HelioInputDialogTheme& theme);
    wxPanel* create_simulation_section(const HelioInputDialogTheme& theme);
    wxPanel* create_run_card(wxWindow* parent, const HelioQuery::OptimizationRun& run, const HelioInputDialogTheme& theme);
    wxPanel* create_run_card(wxWindow* parent, const HelioQuery::SimulationRun& run, const HelioInputDialogTheme& theme);

    // Actions
    void on_refresh(wxMouseEvent& event);
    void on_close(wxMouseEvent& event);
    void on_download_gcode(const std::string& gcode_url, const std::string& run_name);
    void on_view_details_opt(const HelioQuery::OptimizationRun& run);
    void on_view_details_sim(const HelioQuery::SimulationRun& run);
    void on_helio_completion(wxEvent& event);

    // Data loading
    void load_recent_runs();
    void show_loading_state();
    void show_empty_state();
    void show_content();

    // Helper functions
    wxString format_time_ago(const std::chrono::system_clock::time_point& timestamp);
    wxString format_file_size(int layers);
    std::chrono::system_clock::time_point parse_timestamp_from_name(const std::string& name);
    wxColour get_status_color(const std::string& status);
    wxString get_print_outcome_text(const std::string& outcome);
};

}} // namespace Slic3r::GUI

#endif
