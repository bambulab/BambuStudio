#include "AssemblyPdfExportDialog.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/Label.hpp"
#include "../Widgets/StateColor.hpp"

#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace Slic3r {
namespace GUI {
namespace {
static constexpr unsigned long kPdfCoverTitleMaxLength = 80;

static wxString single_line_value(wxString value)
{
    value.Replace("\r", " ");
    value.Replace("\n", " ");
    return value;
}

static wxColour dlg_bg()  { return StateColor::darkModeColorFor(*wxWHITE); }
static wxColour label_fg() { return StateColor::darkModeColorFor(wxColour("#262E30")); }
}

AssemblyPdfExportDialog::AssemblyPdfExportDialog(wxWindow *parent, const AssemblyPdfExportParams &params)
    : DPIDialog(parent, wxID_ANY, _L("Export file Settings"), wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(*wxWHITE);

    auto *top_sizer = new wxBoxSizer(wxVERTICAL);

    auto add_text_row = [this, top_sizer](const wxString &label, const wxString &value) {
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        auto *label_ctrl = new wxStaticText(this, wxID_ANY, label);
        label_ctrl->SetForegroundColour(label_fg());
        m_title_ctrl = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, FromDIP(wxSize(360, -1)));
        m_title_ctrl->SetMaxLength(kPdfCoverTitleMaxLength);
        row->Add(label_ctrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        row->Add(m_title_ctrl, 1, wxEXPAND);
        top_sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    };

    add_text_row(_L("Homepage  title"), params.title);
    const wxString path_tooltip = _L("For privacy reasons, image paths are not saved or restored.Just for pdf/md file.");
    m_cover_image_ctrl = create_path_row(this, top_sizer, _L("Homepage image"), params.cover_image_path, path_tooltip);
    m_second_page_image_ctrl = create_path_row(this, top_sizer, _L("Second page image"), params.second_page_image_path, path_tooltip);

    auto *button_sizer = new wxBoxSizer(wxHORIZONTAL);
    button_sizer->AddStretchSpacer();

    // Disabled palette mirrors the AMS_CONTROL_DISABLE_* tokens used elsewhere
    StateColor ok_btn_bg(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Disabled),
                         std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
                         std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                         std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor ok_btn_bd(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Disabled),
                         std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor ok_btn_text(std::pair<wxColour, int>(wxColour(128, 128, 128), StateColor::Disabled),
                           std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));
    StateColor cancel_btn_bg(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                             std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                             std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    StateColor cancel_btn_bd(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal));
    StateColor cancel_btn_text(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal));

    const wxSize btn_size = FromDIP(wxSize(58, 24));
    m_ok_btn = new Button(this, _L("OK"));
    m_ok_btn->SetMinSize(btn_size);
    m_ok_btn->SetCornerRadius(FromDIP(12));
    m_ok_btn->SetBackgroundColor(ok_btn_bg);
    m_ok_btn->SetBorderColor(ok_btn_bd);
    m_ok_btn->SetTextColor(ok_btn_text);
    m_ok_btn->SetFont(Label::Body_12);
    // Belt-and-braces: even though Enable(false) makes wxWidgets stop
    m_ok_btn->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &evt) {
        if (m_ok_btn && !m_ok_btn->IsEnabled())
            return; // swallow the click while disabled
        if (!has_any_input())
            return;
        EndModal(wxID_OK);
    });
    // Watch every input field; toggle OK as the user types. wxEVT_TEXT also
    auto bind_text_watcher = [this](wxTextCtrl *ctrl) {
        if (!ctrl) return;
        ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent &evt) {
            evt.Skip();
            update_ok_button_state();
        });
    };
    bind_text_watcher(m_title_ctrl);
    bind_text_watcher(m_cover_image_ctrl);
    bind_text_watcher(m_second_page_image_ctrl);

    auto *cancel_btn = new Button(this, _L("Cancel"));
    cancel_btn->SetMinSize(btn_size);
    cancel_btn->SetCornerRadius(FromDIP(12));
    cancel_btn->SetBackgroundColor(cancel_btn_bg);
    cancel_btn->SetBorderColor(cancel_btn_bd);
    cancel_btn->SetTextColor(cancel_btn_text);
    cancel_btn->SetFont(Label::Body_12);
    cancel_btn->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { EndModal(wxID_CANCEL); });

    button_sizer->Add(m_ok_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(8));
    button_sizer->Add(cancel_btn, 0, wxALIGN_CENTER_VERTICAL);
    top_sizer->Add(button_sizer, 0, wxEXPAND | wxALL, FromDIP(16));

    SetSizer(top_sizer);
    top_sizer->SetSizeHints(this);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);

    // Initial state: when the caller passes empty params (no remembered
    update_ok_button_state();
}

AssemblyPdfExportParams AssemblyPdfExportDialog::get_params() const
{
    AssemblyPdfExportParams params;
    if (m_title_ctrl)
        params.title = single_line_value(m_title_ctrl->GetValue());
    if (m_cover_image_ctrl)
        params.cover_image_path = m_cover_image_ctrl->GetValue();
    if (m_second_page_image_ctrl)
        params.second_page_image_path = m_second_page_image_ctrl->GetValue();
    return params;
}

void AssemblyPdfExportDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    if (GetSizer())
        GetSizer()->SetSizeHints(this);
    Fit();
    Refresh();
}

wxTextCtrl *AssemblyPdfExportDialog::create_path_row(wxWindow *parent, wxBoxSizer *sizer,
                                                     const wxString &label, const wxString &value,
                                                     const wxString &tooltip)
{
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    auto *label_ctrl = new wxStaticText(parent, wxID_ANY, label);
    label_ctrl->SetForegroundColour(label_fg());
    auto *text_ctrl = new wxTextCtrl(parent, wxID_ANY, value, wxDefaultPosition, FromDIP(wxSize(360, -1)));

    StateColor browse_bg(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                         std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                         std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    StateColor browse_fg(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal));
    auto *browse_btn = new Button(parent, _L("Browse"));
    browse_btn->SetBackgroundColor(browse_bg);
    browse_btn->SetTextColor(browse_fg);
    browse_btn->SetMinSize(wxSize(FromDIP(48), FromDIP(20)));
    browse_btn->SetCornerRadius(FromDIP(4));

    browse_btn->Bind(wxEVT_LEFT_DOWN, [this, text_ctrl](wxMouseEvent &) { browse_image(text_ctrl); });

    if (!tooltip.IsEmpty()) {
        label_ctrl->SetToolTip(tooltip);
        text_ctrl->SetToolTip(tooltip);
        browse_btn->SetToolTip(tooltip);
    }

    row->Add(label_ctrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    row->Add(text_ctrl, 1, wxEXPAND | wxRIGHT, FromDIP(8));
    row->Add(browse_btn, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    return text_ctrl;
}

void AssemblyPdfExportDialog::browse_image(wxTextCtrl *ctrl)
{
    if (!ctrl)
        return;

    const wxString wildcard = _L("Image files") + " (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg|"
                            + _L("All files") + " (*.*)|*.*";
    wxFileDialog dlg(this, _L("Open image file"), wxEmptyString, ctrl->GetValue(),
                     wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        ctrl->SetValue(dlg.GetPath());
        // SetValue() already fires wxEVT_TEXT on every supported wx port, so
        update_ok_button_state();
    }
}

bool AssemblyPdfExportDialog::has_any_input() const
{
    auto non_empty = [](const wxTextCtrl *ctrl) {
        return ctrl && !ctrl->GetValue().Trim(true).Trim(false).IsEmpty();
    };
    return non_empty(m_title_ctrl) || non_empty(m_cover_image_ctrl) ||
           non_empty(m_second_page_image_ctrl);
}

void AssemblyPdfExportDialog::update_ok_button_state()
{
    if (!m_ok_btn)
        return;
    const bool enable = has_any_input();
    if (m_ok_btn->IsEnabled() == enable)
        return; // no-op: avoid redundant repaints / EVT_ENABLE_CHANGED churn
    m_ok_btn->Enable(enable);
}

} // namespace GUI
} // namespace Slic3r
