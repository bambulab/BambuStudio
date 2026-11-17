#include "GLGizmoSimplifyDialog.hpp"
#include "GLGizmoSimplify.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/CheckBox.hpp"
#include "slic3r/GUI/Widgets/ComboBox.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/TextInput.hpp"
#include "libslic3r/Utils.hpp"

#include <wx/dcclient.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/valtext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Slic3r::GUI {

namespace {

// Light-theme colors. UpdateDlgDarkUI() maps every control to its dark counterpart, and the two
// painted controls below map them with StateColor::darkModeColorFor() when they paint.
const wxColour &TITLE_COLOR = ThemeColor::TextPrimary; // labels: model name, method, options
const wxColour &LABEL_COLOR = ThemeColor::TextMuted;   // values, preview count, explanation
const wxColour &LINE_COLOR  = ThemeColor::Grey300;     // divider, #EEEEEE
const wxColour &GREEN_COLOR = ThemeColor::BrandGreen;
const wxColour &BLOCK_COLOR = ThemeColor::Grey200;
const wxColour &MARK_COLOR  = ThemeColor::Grey400;
const wxColour  WARNING_COLOR(241, 117, 78); // ColorRGB::WARNING(), as in the ImGui panel

// Design sizes, in design px (converted with FromDIP).
constexpr int DESIGN_WINDOW_WIDTH = 365; // window body below the title bar
constexpr int DESIGN_BORDER       = 24;  // padding of the window body
constexpr int DESIGN_ROW_GAP      = 12;  // gap between the rows of the window body
constexpr int DESIGN_BLOCK_PAD_X  = 8;
constexpr int DESIGN_BLOCK_PAD_Y  = 16;
constexpr int DESIGN_METHOD_ROW   = 193; // 192.84
constexpr int DESIGN_GAUGE_WIDTH  = 140;
constexpr int DESIGN_GAUGE_HEIGHT = 4;
constexpr int DESIGN_INFO_GAP     = 4;  // between the model name and triangle count rows
constexpr int DESIGN_BLOCK_RADIUS = 4;
constexpr int DESIGN_LEVEL_HEIGHT = 16; // detail level slider
constexpr int DESIGN_BUTTON_WIDTH  = 60; // as TIPS_DIALOG_BUTTON_SIZE of TipsDialog
constexpr int DESIGN_BUTTON_HEIGHT = 24;

// Captions of the detail level slider, from the highest to the lowest detail.
std::array<wxString, 5> detail_captions()
{
    return { _L("Extra high"), _L("High"), _L("Medium"), _L("Low"), _L("Extra low") };
}

wxStaticText* create_label(wxWindow *parent, const wxString &text, const wxFont &font, const wxColour &colour)
{
    auto label = new wxStaticText(parent, wxID_ANY, text);
    label->SetFont(font);
    label->SetForegroundColour(colour);
    return label;
}

// Checkbox with a text label, built the same way as the checkboxes of TipsDialog.
CheckBox* create_checkbox(wxWindow *parent, const wxString &text, wxSizer *sizer, int gap)
{
    auto box = new CheckBox(parent);
    sizer->Add(box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    sizer->Add(create_label(parent, text, Label::Body_13, TITLE_COLOR), 0, wxALIGN_CENTER_VERTICAL);
    return box;
}

} // namespace

// Multi-line text that wraps to a width it is given, and is as tall as its lines.
// (wxStaticText::Wrap() did not update the height, so the second line was cut off.)
class WrappedText : public wxPanel
{
public:
    WrappedText(wxWindow *parent, const wxString &text, const wxFont &font, const wxColour &colour)
        : wxPanel(parent), m_text(text)
    {
        SetFont(font);
        SetForegroundColour(colour);
        SetBackgroundColour(ThemeColor::White);
        Bind(wxEVT_PAINT, &WrappedText::on_paint, this);
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {}); // painted fully in on_paint
    }

    // Wraps the text to this width (in pixels) and sets the height of the lines.
    void SetWrapWidth(int width)
    {
        m_lines.clear();
        wxString line;
        for (size_t i = 0; i < m_text.length(); ++i) {
            const wxString ch(m_text[i]);
            const wxString candidate = line + ch;
            if (! line.empty() && GetTextExtent(candidate).GetWidth() > width && ! no_line_start(m_text[i])) {
                m_lines.push_back(line);
                line = ch;
            } else {
                line = candidate;
            }
        }
        if (! line.empty())
            m_lines.push_back(line);
        SetMinSize(wxSize(width, static_cast<int>(m_lines.size()) * GetCharHeight()));
        InvalidateBestSize();
        Refresh();
    }

private:
    // Punctuation that must not start a line; it is kept at the end of the previous line instead.
    static bool no_line_start(wxUniChar ch)
    {
        static const wxString chars = wxString::FromUTF8("，。、；：！？）》」』】,.;:!?)");
        return chars.Find(ch) != wxNOT_FOUND;
    }

    void on_paint(wxPaintEvent &)
    {
        wxPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();
        dc.SetFont(GetFont());
        dc.SetTextForeground(GetForegroundColour());
        int y = 0;
        for (const wxString &line : m_lines) {
            dc.DrawText(line, 0, y);
            y += GetCharHeight();
        }
    }

    wxString m_text;
    std::vector<wxString> m_lines;
};

// Slider with five marks on a line, as in the design: the selected mark is a green ring. Click or drag
// snaps to the nearest mark and raises wxEVT_SLIDER with the new value, like wxSlider does.
class LevelSlider : public wxPanel
{
public:
    LevelSlider(wxWindow *parent, int levels, int value)
        : wxPanel(parent), m_levels(levels), m_value(std::clamp(value, 0, levels - 1))
    {
        SetBackgroundColour(BLOCK_COLOR);
        Bind(wxEVT_PAINT, &LevelSlider::on_paint, this);
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {}); // painted fully in on_paint
        Bind(wxEVT_LEFT_DOWN, &LevelSlider::on_mouse, this);
        Bind(wxEVT_MOTION, &LevelSlider::on_mouse, this);
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) { if (HasCapture()) ReleaseMouse(); });
        // The capture can be taken away (e.g. by a popup); wx asserts if this event is not handled.
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [](wxMouseCaptureLostEvent &) {});
    }

    int GetValue() const { return m_value; }

    void SetValue(int value)
    {
        m_value = std::clamp(value, 0, m_levels - 1);
        Refresh();
    }

private:
    // The ring of the selected mark is kept inside the panel, so the marks start one radius in.
    int inset() const { return FromDIP(12); }

    int x_of(int index) const
    {
        const int width = GetClientSize().GetWidth() - 2 * inset();
        return inset() + width * index / (m_levels - 1);
    }

    void on_paint(wxPaintEvent &)
    {
        wxPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();
        const int y = GetClientSize().GetHeight() / 2;
        const wxColour mark_color = StateColor::darkModeColorFor(MARK_COLOR);

        dc.SetPen(wxPen(mark_color, FromDIP(1)));
        dc.DrawLine(x_of(0), y, x_of(m_levels - 1), y);

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(mark_color));
        for (int i = 0; i < m_levels; ++i)
            if (i != m_value)
                dc.DrawCircle(x_of(i), y, FromDIP(2));

        const int x = x_of(m_value);
        dc.SetBrush(wxBrush(StateColor::darkModeColorFor(GREEN_COLOR)));
        dc.DrawCircle(x, y, FromDIP(7));
        dc.SetBrush(wxBrush(StateColor::darkModeColorFor(ThemeColor::White)));
        dc.DrawCircle(x, y, FromDIP(3));
    }

    void on_mouse(wxMouseEvent &evt)
    {
        if (evt.LeftDown()) {
            if (! HasCapture())
                CaptureMouse();
        } else if (! (evt.Dragging() && evt.LeftIsDown())) {
            evt.Skip();
            return;
        }

        const int width = GetClientSize().GetWidth() - 2 * inset();
        const int index = width > 0 ? std::clamp(static_cast<int>(std::lround(static_cast<double>(evt.GetX() - inset()) * (m_levels - 1) / width)), 0, m_levels - 1) : m_value;
        if (index == m_value)
            return;
        m_value = index;
        Refresh();
        wxCommandEvent slider_evt(wxEVT_SLIDER, GetId());
        slider_evt.SetEventObject(this);
        slider_evt.SetInt(m_value);
        ProcessWindowEvent(slider_evt);
    }

    int m_levels;
    int m_value;
};

SimplifyDialog::SimplifyDialog(GLGizmoSimplify &owner)
    : DPIDialog(wxGetApp().mainframe, wxID_ANY, _L("Simplify model"), wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
    , m_owner(&owner)
{
    // Same window icon and background as the other dialogs (see TipsDialog).
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetBackgroundColour(ThemeColor::White);

    create_controls();
    apply_sizes();
    Bind(wxEVT_CLOSE_WINDOW, &SimplifyDialog::on_close, this);
    Bind(wxEVT_CHAR_HOOK, &SimplifyDialog::on_char_hook, this);
    // Dark mode: maps the background and text colors of every control.
    wxGetApp().UpdateDlgDarkUI(this);
}

void SimplifyDialog::create_controls()
{
    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    const int border = FromDIP(DESIGN_BORDER);
    const int row_gap = FromDIP(DESIGN_ROW_GAP);

    // Model name and triangle count: each label followed directly by its value, the rows 4 apart.
    auto info_sizer = new wxBoxSizer(wxVERTICAL);
    auto add_info_row = [this, info_sizer](const wxString &label, int top) {
        auto row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(create_label(this, label, Label::Body_13, TITLE_COLOR), 0, wxALIGN_CENTER_VERTICAL);
        auto value = create_label(this, wxEmptyString, Label::Body_13, LABEL_COLOR);
        row->Add(value, 0, wxALIGN_CENTER_VERTICAL);
        info_sizer->Add(row, 0, wxTOP, top);
        return value;
    };
    m_mesh_name_value = add_info_row(_L("Mesh name:"), 0);
    m_triangle_value  = add_info_row(_L("Triangle count:"), FromDIP(DESIGN_INFO_GAP));
    main_sizer->Add(info_sizer, 0, wxLEFT | wxRIGHT | wxTOP, border);

    // Divider: a 1 px line in the design's color (wxStaticLine draws the system's color).
    auto divider = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(1)));
    divider->SetBackgroundColour(LINE_COLOR);
    divider->SetMinSize(wxSize(-1, FromDIP(1)));
    main_sizer->AddSpacer(row_gap);
    main_sizer->Add(divider, 0, wxEXPAND | wxLEFT | wxRIGHT, border);

    // Simplification method: label and combo share a fixed row (see apply_sizes()).
    auto method_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_method_label = create_label(this, _L("Simplification method"), Label::Body_13, TITLE_COLOR);
    method_sizer->Add(m_method_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_method_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
    // A read-only ComboBox and its list use Body_14; the design's text is 13 px, like the rest of the
    // dialog. The list has its own font, so it is set separately.
    m_method_combo->SetFont(Label::Body_13);
    m_method_combo->GetDropDown().SetFont(Label::Body_13);
    m_method_combo->Append(_L("Detail level"));
    m_method_combo->Append(_L("Decimate ratio"));
    method_sizer->Add(m_method_combo, 0, wxALIGN_CENTER_VERTICAL);
    m_method_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) {
        if (m_updating || m_owner == nullptr) return;
        m_owner->on_dialog_method_changed(m_method_combo->GetSelection() == 1);
    });
    main_sizer->AddSpacer(row_gap);
    main_sizer->Add(method_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, border);

    // Light block with the slider of the selected method.
    m_block = new wxPanel(this);
    // The block's color is its background (the controls on it take it over); the panel paints it as a
    // rounded rectangle, with the dialog's color in the corners.
    m_block->SetBackgroundColour(BLOCK_COLOR);
    m_block->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {}); // painted fully in the paint handler
    m_block->Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
        wxPaintDC dc(m_block);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(m_block->GetBackgroundColour()));
        dc.DrawRoundedRectangle(wxPoint(0, 0), m_block->GetClientSize(), FromDIP(DESIGN_BLOCK_RADIUS));
    });
    auto block_sizer = new wxBoxSizer(wxVERTICAL);

    // Detail level: five level marks on a line, the selected one as a green ring, as in the design.
    // The level caption is centred in the space left after the slider.
    auto detail_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_detail_slider = new LevelSlider(m_block, 5, 2);
    detail_sizer->Add(m_detail_slider, 0, wxALIGN_CENTER_VERTICAL);
    detail_sizer->AddStretchSpacer(1);
    m_detail_caption = create_label(m_block, wxEmptyString, Label::Body_13, GREEN_COLOR);
    detail_sizer->Add(m_detail_caption, 0, wxALIGN_CENTER_VERTICAL);
    detail_sizer->AddStretchSpacer(1);
    m_detail_slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent &) {
        if (m_updating || m_owner == nullptr) return;
        m_owner->on_dialog_detail_changed(m_detail_slider->GetValue());
    });

    // Decimate ratio: slider, then the value box and unit centred in the space left after it.
    auto ratio_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_ratio_slider = new wxSlider(m_block, wxID_ANY, 50, 0, 100, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
    ratio_sizer->Add(m_ratio_slider, 0, wxALIGN_CENTER_VERTICAL);
    ratio_sizer->AddStretchSpacer(1);
    m_ratio_input = new TextInput(m_block, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_CENTER);
    m_ratio_input->GetTextCtrl()->SetFont(Label::Body_13);
    m_ratio_input->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    ratio_sizer->Add(m_ratio_input, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
    m_ratio_unit = create_label(m_block, "%", Label::Body_13, LABEL_COLOR);
    ratio_sizer->Add(m_ratio_unit, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
    ratio_sizer->AddStretchSpacer(1);

    auto apply_ratio = [this]() {
        if (m_updating || m_owner == nullptr) return;
        double value = 0.;
        if (! m_ratio_input->GetTextCtrl()->GetValue().ToDouble(&value)) return;
        m_owner->on_dialog_ratio_changed(std::clamp(static_cast<float>(value), 0.f, 100.f));
    };
    m_ratio_slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent &) {
        if (m_updating || m_owner == nullptr) return;
        m_owner->on_dialog_ratio_changed(static_cast<float>(m_ratio_slider->GetValue()));
    });
    m_ratio_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [apply_ratio](wxCommandEvent &) { apply_ratio(); });
    m_ratio_input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [apply_ratio](wxFocusEvent &e) { apply_ratio(); e.Skip(); });

    // Design padding of the block: 16 px top and bottom, 8 px left and right. Each method has its
    // own padded wrapper, so hiding the other method also hides its padding.
    auto padded_row = [this](wxSizer *row) {
        auto wrapper = new wxBoxSizer(wxVERTICAL);
        wrapper->AddSpacer(FromDIP(DESIGN_BLOCK_PAD_Y));
        wrapper->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(DESIGN_BLOCK_PAD_X));
        wrapper->AddSpacer(FromDIP(DESIGN_BLOCK_PAD_Y));
        return wrapper;
    };
    m_detail_row = padded_row(detail_sizer);
    m_ratio_row  = padded_row(ratio_sizer);
    block_sizer->Add(m_detail_row, 0, wxEXPAND);
    block_sizer->Add(m_ratio_row, 0, wxEXPAND);
    m_block->SetSizer(block_sizer);
    main_sizer->AddSpacer(row_gap);
    main_sizer->Add(m_block, 0, wxEXPAND | wxLEFT | wxRIGHT, border);

    // Triangle count of the preview.
    m_result_label = create_label(this, wxEmptyString, Label::Body_13, LABEL_COLOR);
    main_sizer->AddSpacer(row_gap);
    main_sizer->Add(m_result_label, 0, wxLEFT | wxRIGHT, border);

    // Options.
    auto options_sizer = new wxBoxSizer(wxVERTICAL);
    auto wireframe_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_wireframe_box = create_checkbox(this, _L("Show wireframe"), wireframe_sizer, FromDIP(8));
    options_sizer->Add(wireframe_sizer, 0);
    // The checkbox handlers skip the event: CheckBox swaps its checked / unchecked image in its own
    // handler, which runs after these (wx calls the handler bound last first) only when they skip.
    m_wireframe_box->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
        e.Skip();
        if (m_updating || m_owner == nullptr) return;
        m_owner->on_dialog_wireframe_changed(m_wireframe_box->GetValue());
    });

    auto preserve_sizer = new wxBoxSizer(wxHORIZONTAL);
    // No tooltip: the text below the box already says what checking or unchecking it does (the ImGui
    // panel's tooltip was the one of Mesh Boolean, worded for its result).
    m_preserve_box = create_checkbox(this, _L("Keep all painting"), preserve_sizer, FromDIP(8));
    options_sizer->Add(preserve_sizer, 0, wxTOP, row_gap);
    m_options = options_sizer;
    m_preserve_row = preserve_sizer;
    m_preserve_box->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
        e.Skip();
        if (m_updating || m_owner == nullptr) return;
        m_owner->on_dialog_preserve_changed(m_preserve_box->GetValue());
    });

    // What keeping the painting does; shown while the box is checked, from the left edge of the content.
    m_preserve_hint = new WrappedText(this, _L("Colors and painting will be remapped to the new mesh, which may cause deviations or loss."), Label::Body_13, LABEL_COLOR);
    options_sizer->Add(m_preserve_hint, 0, wxTOP, row_gap);

    m_warning = new WrappedText(this,
        _L("Warning") + ": " + _L("All painting (color, seam, support and fuzzy skin) will be removed from the simplified model. "
                                  "Check \"Keep all painting\" to keep it."),
        Label::Body_13, WARNING_COLOR);
    options_sizer->Add(m_warning, 0, wxTOP, row_gap);
    main_sizer->AddSpacer(row_gap);
    main_sizer->Add(options_sizer, 0, wxLEFT | wxRIGHT, border);

    // Preview progress on the left, Apply and Cancel on the right: one row, as in the design.
    auto button_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL | wxGA_SMOOTH);
    button_sizer->Add(m_gauge, 0, wxALIGN_CENTER_VERTICAL);
    m_gauge_text = create_label(this, wxEmptyString, Label::Body_13, LABEL_COLOR);
    button_sizer->Add(m_gauge_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, row_gap);
    button_sizer->AddStretchSpacer(1);

    // Same colors as the OK and Cancel buttons of TipsDialog, plus a disabled state: Apply is disabled
    // while the preview is computed. Text size per the design (12 px), as most dialogs' buttons (Body_12).
    StateColor btn_bg_green(std::pair<wxColour, int>(ThemeColor::TextDisabled, StateColor::Disabled),
                            std::pair<wxColour, int>(ThemeColor::BrandGreenPressed, StateColor::Pressed),
                            std::pair<wxColour, int>(ThemeColor::BrandGreenHovered, StateColor::Hovered),
                            std::pair<wxColour, int>(ThemeColor::BrandGreen, StateColor::Normal));
    StateColor btn_bd_green(std::pair<wxColour, int>(ThemeColor::TextDisabled, StateColor::Disabled),
                            std::pair<wxColour, int>(ThemeColor::BrandGreen, StateColor::Normal));
    StateColor btn_text_green(std::pair<wxColour, int>(ThemeColor::WhiteFixed, StateColor::Normal));
    m_apply_button = new Button(this, _L("Apply"));
    m_apply_button->SetBackgroundColor(btn_bg_green);
    m_apply_button->SetBorderColor(btn_bd_green);
    m_apply_button->SetTextColor(btn_text_green);
    m_apply_button->SetFont(Label::Body_12);
    m_apply_button->SetCornerRadius(FromDIP(12));
    m_apply_button->EnableTooltipEvenDisabled();
    m_apply_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { if (m_owner) m_owner->on_dialog_apply(); });
    button_sizer->Add(m_apply_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, row_gap);

    StateColor btn_bg_white(std::pair<wxColour, int>(ThemeColor::Grey400, StateColor::Pressed),
                            std::pair<wxColour, int>(ThemeColor::Grey300, StateColor::Hovered),
                            std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
    StateColor btn_bd_white(std::pair<wxColour, int>(ThemeColor::TextPrimary, StateColor::Normal));
    StateColor btn_text_white(std::pair<wxColour, int>(ThemeColor::TextPrimary, StateColor::Normal));
    m_cancel_button = new Button(this, _L("Cancel"));
    m_cancel_button->SetBackgroundColor(btn_bg_white);
    m_cancel_button->SetBorderColor(btn_bd_white);
    m_cancel_button->SetTextColor(btn_text_white);
    m_cancel_button->SetFont(Label::Body_12);
    m_cancel_button->SetCornerRadius(FromDIP(12));
    m_cancel_button->EnableTooltipEvenDisabled();
    m_cancel_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { if (m_owner) m_owner->on_dialog_cancel(); });
    button_sizer->Add(m_cancel_button, 0, wxALIGN_CENTER_VERTICAL);

    main_sizer->AddSpacer(row_gap);
    main_sizer->Add(button_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);

    // SetSizer, not SetSizerAndFit: the latter fixes the minimal window size to the first layout, and
    // the window could then never shrink when rows are hidden. The size is set in relayout().
    SetSizer(main_sizer);
}

void SimplifyDialog::apply_sizes()
{
    // The combo fills the rest of the design's method row, but is never narrower than its longest
    // item: a long label (e.g. English) leaves almost nothing of that row, and the item was cut off.
    // It still stays within the content width; a longer item is then ellipsized by the combo.
    const int label_width = m_method_label->GetBestSize().GetWidth() + FromDIP(8);
    int item_width = 0;
    for (unsigned int i = 0; i < m_method_combo->GetCount(); ++i)
        item_width = std::max(item_width, m_method_combo->GetTextExtent(m_method_combo->GetString(i)).GetWidth());
    // Room for the drop-down arrow on the left and the padding on both sides (see TextInput::render()).
    const int combo_width = std::max(FromDIP(DESIGN_METHOD_ROW) - label_width, item_width + FromDIP(36));
    const int combo_max   = FromDIP(DESIGN_WINDOW_WIDTH - 2 * DESIGN_BORDER) - label_width;
    m_method_combo->SetMinSize(wxSize(std::min(combo_width, combo_max), -1));

    // Wide enough for the longest value, so the number is visible.
    const int ratio_width = m_ratio_input->GetTextCtrl()->GetTextExtent("100.00%").GetWidth() + FromDIP(12);
    m_ratio_input->SetMinSize(wxSize(ratio_width, -1));

    // Both sliders have the same length: the block's inner width minus the text after the slider (the
    // longest level caption, or the value box with its unit) and a margin on each side of that text.
    int text_width = ratio_width + FromDIP(4) + m_ratio_unit->GetBestSize().GetWidth();
    for (const wxString &caption : detail_captions())
        text_width = std::max(text_width, m_detail_caption->GetTextExtent(caption).GetWidth());
    const int block_inner  = FromDIP(DESIGN_WINDOW_WIDTH - 2 * DESIGN_BORDER - 2 * DESIGN_BLOCK_PAD_X);
    const int slider_width = block_inner - text_width - 2 * FromDIP(12);
    m_detail_slider->SetMinSize(wxSize(slider_width, FromDIP(DESIGN_LEVEL_HEIGHT)));
    const wxSize button_size(FromDIP(DESIGN_BUTTON_WIDTH), FromDIP(DESIGN_BUTTON_HEIGHT));
    m_apply_button->SetMinSize(button_size);
    m_cancel_button->SetMinSize(button_size);
    m_ratio_slider->SetMinSize(wxSize(slider_width, -1));

    m_gauge->SetMinSize(wxSize(FromDIP(DESIGN_GAUGE_WIDTH), FromDIP(DESIGN_GAUGE_HEIGHT)));
    // Room for "100%", so the percentage can change without a new layout.
    m_gauge_text->SetMinSize(wxSize(m_gauge_text->GetTextExtent("100%").GetWidth(), -1));
}

void SimplifyDialog::relayout()
{
    const int content_width = FromDIP(DESIGN_WINDOW_WIDTH - 2 * DESIGN_BORDER);

    // Both texts take the full width of the content.
    m_preserve_hint->SetWrapWidth(content_width);
    m_warning->SetWrapWidth(content_width);
    InvalidateBestSize();
    Layout();
    SetClientSize(wxSize(FromDIP(DESIGN_WINDOW_WIDTH), GetSizer()->GetMinSize().GetHeight()));
}

void SimplifyDialog::update(const SimplifyDialogView &view)
{
    m_updating = true;

    m_mesh_name_value->SetLabel(view.mesh_name);
    m_triangle_value->SetLabel(wxString(std::to_string(view.triangle_count)));
    // _u8L keeps the translation UTF-8; ToStdString() would convert it to the system code page.
    const std::string result_count = view.result_count.ToUTF8().data();
    m_result_label->SetLabel(wxString::FromUTF8(GUI::format(_u8L("Triangles after simplification: %1%"), result_count)));

    m_method_combo->SetSelection(view.use_count ? 1 : 0);
    m_detail_slider->SetValue(view.detail_level);
    m_detail_caption->SetLabel(detail_captions()[std::clamp(view.detail_level, 0, 4)]);
    m_ratio_slider->SetValue(static_cast<int>(std::round(view.decimate_ratio)));
    // Do not overwrite a value the user is typing.
    if (wxWindow::FindFocus() != m_ratio_input->GetTextCtrl())
        m_ratio_input->GetTextCtrl()->SetValue(wxString::Format("%.2f", view.decimate_ratio));
    // Only the slider row of the selected method is shown.
    m_detail_row->ShowItems(! view.use_count);
    m_ratio_row->ShowItems(view.use_count);

    m_wireframe_box->SetValue(view.show_wireframe);
    m_preserve_box->SetValue(view.preserve_color);
    // Hide whole rows: the labels are separate controls and would stay visible otherwise.
    m_options->Show(m_preserve_row, view.painted);
    m_preserve_hint->Show(view.painted && view.preserve_color);
    m_warning->Show(view.painted && ! view.preserve_color);

    m_gauge->Show(view.worker_running);
    m_gauge_text->Show(view.worker_running);
    update_progress(view.progress);

    // Same tooltips as the ImGui panel had, shown while the button is disabled.
    m_apply_button->Enable(view.apply_enabled);
    if (view.worker_running)
        m_apply_button->SetToolTip(_L("Can't apply when process preview."));
    else
        m_apply_button->UnsetToolTip();
    m_cancel_button->Enable(! view.cancelling);
    if (view.cancelling)
        m_cancel_button->SetToolTip(_L("Operation already cancelling. Please wait few seconds."));
    else
        m_cancel_button->UnsetToolTip();

    relayout();
    m_updating = false;
}

void SimplifyDialog::update_progress(int progress)
{
    progress = std::clamp(progress, 0, 100);
    m_gauge->SetValue(progress);
    m_gauge_text->SetLabel(wxString::Format("%d%%", progress));
}

void SimplifyDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_method_combo->Rescale();
    m_method_combo->SetFont(Label::Body_13);
    m_ratio_input->Rescale();
    m_wireframe_box->Rescale();
    m_preserve_box->Rescale();
    m_apply_button->Rescale();
    m_cancel_button->Rescale();
    apply_sizes();
    relayout();
    Refresh();
}

void SimplifyDialog::on_char_hook(wxKeyEvent &evt)
{
    // Esc closes the dialog the same as Cancel.
    if (evt.GetKeyCode() == WXK_ESCAPE && m_owner != nullptr) {
        m_owner->on_dialog_cancel();
        return;
    }
    evt.Skip();
}

void SimplifyDialog::on_close(wxCloseEvent &evt)
{
    // Closing the dialog closes the gizmo, the same as Cancel; the gizmo then destroys the dialog.
    // The event is not skipped: the default handler would act on a dialog that is being destroyed.
    if (m_owner != nullptr)
        m_owner->on_dialog_closed();
    else
        Destroy();
}

} // namespace Slic3r::GUI
