#include "AlertDialog.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <wx/image.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>

#include <boost/format.hpp>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r::GUI {

namespace {

// Layout in DIP
constexpr int DLG_WIDTH       = 520;
constexpr int PAD_LEFT        = 32;
constexpr int PAD_RIGHT       = 24;
constexpr int PAD_TOP         = 24;
constexpr int BUTTON_ROW_PAD  = 24; // around the button row
constexpr int SECTION_GAP     = 20; // between image, message, content, notes and checkbox
constexpr int NOTE_GAP        = 12; // between consecutive tips / warnings
constexpr int BUTTON_GAP      = 12;
constexpr int BUTTON_HEIGHT   = 24;
constexpr int BUTTON_PAD_X    = 12;
constexpr int BUTTON_PAD_Y    = 6;
constexpr int IMAGE_WIDTH     = 360;
constexpr int IMAGE_HEIGHT    = 240;
constexpr int WARNING_ICON_SIZE       = 16;
constexpr int ICON_TEXT_GAP   = 4;
constexpr int CHECKBOX_GAP    = 9;
constexpr int TEXT_MAX_HEIGHT = 320; // the message scrolls beyond this

StateColor filled(const wxColour &normal, const wxColour &hovered, const wxColour &pressed)
{
    return StateColor(std::pair<wxColour, int>(pressed, StateColor::Pressed),
                      std::pair<wxColour, int>(hovered, StateColor::Hovered),
                      std::pair<wxColour, int>(normal, StateColor::Normal));
}

void style_button(Button *btn, AlertButtonRole role)
{
    switch (role) {
    case AlertButtonRole::Primary: {
        const StateColor bg = filled(ThemeColor::BrandGreen, ThemeColor::BrandGreenHovered, ThemeColor::BrandGreenPressed);
        btn->SetBackgroundColor(bg);
        btn->SetBorderColor(bg);
        btn->SetTextColor(StateColor(ThemeColor::WhiteFixed));
        break;
    }
    case AlertButtonRole::Danger: {
        // No hover / pressed shades: the theme has no tokens for them
        const StateColor bg(ThemeColor::Danger);
        btn->SetBackgroundColor(bg);
        btn->SetBorderColor(bg);
        btn->SetTextColor(StateColor(ThemeColor::WhiteFixed));
        break;
    }
    case AlertButtonRole::Secondary:
        btn->SetBackgroundColor(filled(ThemeColor::White, ThemeColor::Grey300, ThemeColor::Grey400));
        btn->SetBorderColor(StateColor(ThemeColor::TextMuted));
        btn->SetTextColor(StateColor(ThemeColor::TextMuted));
        break;
    }
}

// Spread \p gap between the visible items of \p sizer; hidden and empty items take no space
void apply_gaps(wxSizer *sizer, int gap)
{
    bool first = true;
    for (wxSizerItem *item : sizer->GetChildren()) {
        if (!item->IsShown())
            continue;
        item->SetFlag((item->GetFlag() & ~wxALL) | wxTOP);
        item->SetBorder(first ? 0 : gap);
        first = false;
    }
}

} // namespace

struct AlertDialog::Impl
{
    struct WrappedText
    {
        Label *label;
        int    indent; // DIP taken from the content width, e.g. by a leading icon
    };
    struct ButtonEntry
    {
        Button          *button;
        AlertButtonRole role;
    };
    struct WarningRow
    {
        wxBoxSizer     *row;
        wxStaticBitmap *icon;
        Label          *label;
    };

    AlertDialog *dlg;

    wxBoxSizer       *column{nullptr};
    wxSizerItem      *pad_left{nullptr};
    wxSizerItem      *pad_right{nullptr};
    wxSizerItem      *pad_top{nullptr};
    wxSizerItem      *button_row{nullptr};
    wxStaticBitmap   *image{nullptr};
    wxBitmap          image_source;
    wxScrolledWindow *message_area{nullptr};
    Label            *message{nullptr};
    wxBoxSizer       *content{nullptr};
    wxBoxSizer       *notes{nullptr};
    size_t            tip_count{0};
    wxBoxSizer       *dsa{nullptr};
    CheckBox         *dsa_box{nullptr};
    wxBoxSizer       *buttons{nullptr};
    std::vector<ButtonEntry> button_list; // in display order
    std::vector<WrappedText> wrapped;     // tips and warnings
    std::vector<WarningRow>  warning_rows;
    bool              dark_ui_applied{false};

    explicit Impl(AlertDialog *owner) : dlg(owner) {}

    int content_width() const { return dlg->FromDIP(DLG_WIDTH - PAD_LEFT - PAD_RIGHT); }

    Label *create_label(wxWindow *parent, const wxString &text, const wxColour &colour)
    {
        auto *label = new Label(parent, Label::Body_14, text, LB_AUTO_WRAP);
        label->SetForegroundColour(StateColor::darkModeColorFor(colour));
        label->SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::White));
        return label;
    }

    static void wrap(Label *label, int width)
    {
        label->SetMinSize(wxSize(width, -1));
        label->SetMaxSize(wxSize(width, -1));
        label->Wrap(width);
    }

    void build()
    {
        image = new wxStaticBitmap(dlg, wxID_ANY, wxNullBitmap);
        image->Hide();

        message_area = new wxScrolledWindow(dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        message_area->SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::White));
        message_area->SetScrollRate(0, 20);
        message_area->EnableScrolling(false, true);
        message = create_label(message_area, {}, ThemeColor::TextPrimary);
        auto *message_sizer = new wxBoxSizer(wxVERTICAL);
        message_sizer->Add(message, 0);
        message_area->SetSizer(message_sizer);
        message_area->Hide();

        content = new wxBoxSizer(wxVERTICAL);
        notes   = new wxBoxSizer(wxVERTICAL);
        dsa     = new wxBoxSizer(wxHORIZONTAL);

        column = new wxBoxSizer(wxVERTICAL);
        column->Add(image, 0, wxALIGN_CENTER_HORIZONTAL);
        column->Add(message_area, 0, wxEXPAND);
        column->Add(content, 0, wxEXPAND);
        column->Add(notes, 0, wxEXPAND);
        column->Add(dsa, 0);

        auto *body_row = new wxBoxSizer(wxHORIZONTAL);
        pad_left = body_row->AddSpacer(0);
        body_row->Add(column, 1, wxEXPAND);
        pad_right = body_row->AddSpacer(0);

        buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->AddStretchSpacer();

        auto *main_sizer = new wxBoxSizer(wxVERTICAL);
        pad_top = main_sizer->AddSpacer(0);
        main_sizer->Add(body_row, 1, wxEXPAND);
        button_row = main_sizer->Add(buttons, 0, wxEXPAND | wxALL, 0);
        dlg->SetSizer(main_sizer);
    }

    void set_message(const wxString &text)
    {
        message->SetLabel(text);
        message_area->Show(!text.IsEmpty());
    }

    void update_image()
    {
        if (!image_source.IsOk() || image_source.GetWidth() <= 0 || image_source.GetHeight() <= 0) {
            image->SetBitmap(wxNullBitmap);
            image->Hide();
            return;
        }
        const double scale = std::min(double(dlg->FromDIP(IMAGE_WIDTH)) / image_source.GetWidth(),
                                      double(dlg->FromDIP(IMAGE_HEIGHT)) / image_source.GetHeight());
        wxImage img = image_source.ConvertToImage();
        img.Rescale(std::max(1, int(std::lround(image_source.GetWidth() * scale))),
                    std::max(1, int(std::lround(image_source.GetHeight() * scale))), wxIMAGE_QUALITY_HIGH);
        image->SetBitmap(wxBitmap(img));
        image->Show();
    }

    void update_message_area()
    {
        if (!message_area->IsShown())
            return;
        int width = content_width();
        wrap(message, width);
        const int text_height = message_area->GetSizer()->GetMinSize().GetHeight();
        const int max_height  = dlg->FromDIP(TEXT_MAX_HEIGHT);
        if (text_height > max_height) {
            // Keep the wrapped text clear of the scrollbar
            width += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, dlg);
        }
        const wxSize size(width, std::min(text_height, max_height));
        message_area->SetMinSize(size);
        message_area->SetMaxSize(size);
        message_area->FitInside();
    }

    void style_button_geometry(Button *btn)
    {
        btn->SetPaddingSize(wxSize(dlg->FromDIP(BUTTON_PAD_X), dlg->FromDIP(BUTTON_PAD_Y)));
        btn->SetMinSize(wxSize(-1, dlg->FromDIP(BUTTON_HEIGHT)));
        btn->SetCornerRadius(dlg->FromDIP(BUTTON_HEIGHT / 2));
    }

    void add_button(wxWindowID id, const wxString &label, AlertButtonRole role)
    {
        auto *btn = new Button(dlg, label, "", 0, 0, id);
        btn->SetFont(Label::Body_12);
        style_button(btn, role);
        style_button_geometry(btn);
        btn->Bind(wxEVT_BUTTON, [this, id](wxCommandEvent &) {
            if (dlg->IsModal()) {
                dlg->EndModal(id);
            } else {
                dlg->SetReturnCode(id);
                dlg->Close();
            }
        });

        // Recommended actions stay rightmost: a secondary button goes before the first non-secondary one
        auto pos = role == AlertButtonRole::Secondary ?
                       std::find_if(button_list.begin(), button_list.end(),
                                    [](const ButtonEntry &b) { return b.role != AlertButtonRole::Secondary; }) :
                       button_list.end();
        const size_t sizer_index = 1 /* leading stretch spacer */ + size_t(pos - button_list.begin());
        buttons->Insert(sizer_index, btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, dlg->FromDIP(BUTTON_GAP));
        button_list.insert(pos, {btn, role});
    }

    void focus_default_button()
    {
        // The rightmost button is the recommended one
        if (!button_list.empty())
            button_list.back().button->SetFocus();
    }

    // Icon size and spacing of a warning row follow the current DPI
    void update_warning_row(const WarningRow &row)
    {
        row.icon->SetBitmap(create_scaled_bitmap("warning", dlg, WARNING_ICON_SIZE));
        // Centre the icon on the first text line
        const int icon_offset = std::max(0, (Label::Body_14.GetPixelSize().y - dlg->FromDIP(WARNING_ICON_SIZE)) / 2);
        row.row->GetItem(row.icon)->SetBorder(icon_offset);
        row.row->GetItem(row.label)->SetBorder(dlg->FromDIP(ICON_TEXT_GAP));
    }

    // Recompute every DIP-dependent size; the only place that sizes the dialog
    void layout()
    {
        pad_left->AssignSpacer(dlg->FromDIP(PAD_LEFT), 0);
        pad_right->AssignSpacer(dlg->FromDIP(PAD_RIGHT), 0);
        pad_top->AssignSpacer(0, dlg->FromDIP(PAD_TOP));
        button_row->SetBorder(dlg->FromDIP(BUTTON_ROW_PAD));
        column->SetMinSize(wxSize(content_width(), -1));

        update_image();
        update_message_area();
        for (const WrappedText &text : wrapped)
            wrap(text.label, content_width() - dlg->FromDIP(text.indent));
        for (const WarningRow &row : warning_rows)
            update_warning_row(row);
        for (const ButtonEntry &entry : button_list) {
            style_button_geometry(entry.button);
            buttons->GetItem(entry.button)->SetBorder(dlg->FromDIP(BUTTON_GAP));
        }
        if (dsa_box)
            dsa->GetItem(size_t(1))->SetBorder(dlg->FromDIP(CHECKBOX_GAP));

        apply_gaps(column, dlg->FromDIP(SECTION_GAP));
        apply_gaps(notes, dlg->FromDIP(NOTE_GAP));

        dlg->Layout();
        dlg->Fit();
    }

    // Setters called while the dialog is visible take effect immediately
    void relayout_if_shown()
    {
        if (dlg->IsShown()) {
            layout();
            dlg->Refresh();
        }
    }

    void prepare_to_show()
    {
        layout();
        dlg->CenterOnParent();
        if (!dark_ui_applied) {
            wxGetApp().UpdateDlgDarkUI(dlg);
            dark_ui_applied = true;
        }
        focus_default_button();
    }
};

AlertDialog::AlertDialog(wxWindow *parent, const wxString &title, const wxString &message)
    : DPIDialog(parent ? parent : static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE)
    , m_impl(std::make_unique<Impl>(this))
{
    SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::White));
    SetFont(Label::Body_14);
    const std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    // wx maps Esc to a click on a wxButton with the escape id; our buttons are not wxButtons
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &event) {
        if (event.GetKeyCode() != WXK_ESCAPE) {
            event.Skip();
            return;
        }
        if (IsModal()) {
            EndModal(wxID_CANCEL);
        } else {
            SetReturnCode(wxID_CANCEL);
            Close();
        }
    });

    m_impl->build();
    m_impl->set_message(message);
}

AlertDialog::~AlertDialog() = default;

void AlertDialog::SetMessage(const wxString &message)
{
    m_impl->set_message(message);
    m_impl->relayout_if_shown();
}

void AlertDialog::SetImage(const wxBitmap &image)
{
    m_impl->image_source = image;
    m_impl->relayout_if_shown();
}

void AlertDialog::AddTip(const wxString &tip)
{
    Label *label = m_impl->create_label(this, tip, ThemeColor::TextMuted);
    m_impl->wrapped.push_back({label, 0});
    // Tips stay above warnings whatever the call order
    m_impl->notes->Insert(m_impl->tip_count++, label, 0, wxEXPAND);
    m_impl->relayout_if_shown();
}

void AlertDialog::AddWarning(const wxString &warning)
{
    auto  *row   = new wxBoxSizer(wxHORIZONTAL);
    auto  *icon  = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap);
    Label *label = m_impl->create_label(this, warning, ThemeColor::Warning);
    row->Add(icon, 0, wxTOP);
    row->Add(label, 1, wxLEFT);
    m_impl->warning_rows.push_back({row, icon, label});
    m_impl->update_warning_row(m_impl->warning_rows.back());
    m_impl->wrapped.push_back({label, WARNING_ICON_SIZE + ICON_TEXT_GAP});
    m_impl->notes->Add(row, 0, wxEXPAND);
    m_impl->relayout_if_shown();
}

void AlertDialog::AddContent(wxWindow *window)
{
    wxCHECK_RET(window && window->GetParent() == this, "custom content must be a child of the dialog");
    m_impl->content->Add(window, 0, wxEXPAND);
    m_impl->relayout_if_shown();
}

void AlertDialog::ShowDontShowAgain(const wxString &label)
{
    if (m_impl->dsa_box)
        return;

    auto *box  = new CheckBox(this);
    auto *text = new Label(this, Label::Body_14, label.IsEmpty() ? _L("Don't show again") : label);
    text->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::Grey500));
    text->SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::White));
    text->SetCursor(wxCURSOR_HAND);
    text->Bind(wxEVT_LEFT_UP, [box](wxMouseEvent &) { box->SetValue(!box->GetValue()); });

    m_impl->dsa_box = box;
    m_impl->dsa->Add(box, 0, wxALIGN_CENTER_VERTICAL);
    m_impl->dsa->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(CHECKBOX_GAP));
    m_impl->relayout_if_shown();
}

bool AlertDialog::IsDontShowAgainChecked() const
{
    return m_impl->dsa_box && m_impl->dsa_box->GetValue();
}

void AlertDialog::AddButton(wxWindowID id, const wxString &label, AlertButtonRole role)
{
    m_impl->add_button(id, label, role);
    m_impl->relayout_if_shown();
}

int AlertDialog::ShowModal()
{
    m_impl->prepare_to_show();
    return DPIDialog::ShowModal();
}

bool AlertDialog::Show(bool show)
{
    if (show && !IsShown())
        m_impl->prepare_to_show();
    return DPIDialog::Show(show);
}

void AlertDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_impl->layout();
    Refresh();
}

} // namespace Slic3r::GUI
