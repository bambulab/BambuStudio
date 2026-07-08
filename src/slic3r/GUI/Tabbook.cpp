#include "Tabbook.hpp"

//#ifdef _WIN32

#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "TabButton.hpp"

//BBS set font size
#include "Widgets/Label.hpp"

#include <wx/button.h>
#include <wx/sizer.h>

wxDEFINE_EVENT(wxCUSTOMEVT_TABBOOK_SEL_CHANGED, wxCommandEvent);

const static wxColour TAB_BUTTON_BG  = wxColour("#FEFFFF");
const static wxColour TAB_BUTTON_SEL = wxColour(219, 253, 213, 255);

static const wxFont& TAB_BUTTON_FONT     = Label::Body_14;
static const wxFont& TAB_BUTTON_FONT_SEL = Label::Head_14;


static const int BUTTON_DEF_HEIGHT = 46;
static const int BUTTON_DEF_WIDTH  = 220;


TabButtonsListCtrl::TabButtonsListCtrl(wxWindow *parent, wxBoxSizer *side_tools) :
    wxScrolled<wxControl>(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    SetBackgroundColour(TAB_BUTTON_BG);
    SetScrollRate(0, 0);  // no scrolling by default

    m_btn_size.Set(BUTTON_DEF_WIDTH, BUTTON_DEF_HEIGHT);
    m_btn_bg_colors = StateColor(
        std::make_pair(TAB_BUTTON_SEL, (int) StateColor::Checked),
        std::make_pair(TAB_BUTTON_BG, (int)StateColor::Normal)
    );

    m_arrow_img = ScalableBitmap(this, "monitor_arrow", 14);

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(sizer);

    sizer->AddSpacer(0);  // spacer for optional top padding

    if (side_tools != NULL) {
        const size_t count = side_tools->GetItemCount();
        for (size_t idx = 0; idx < count; ++idx) {
            if (wxWindow *win = side_tools->GetItem(idx)->GetWindow()) {
                win->Reparent(this);
            }
        }
        sizer->Add(side_tools, 0, wxEXPAND);
    }

    m_buttons_sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_buttons_sizer);
    sizer->AddStretchSpacer(1);

    update_client_size(true);
}

void TabButtonsListCtrl::Rescale()
{
    m_arrow_img.msw_rescale();

    const wxSize sz = m_btn_size * (em_unit(this) / 10);
    for (TabButton *btn : std::as_const(m_pageButtons)) {
        btn->SetMinSize(sz);
        btn->Rescale();
    }

    update_client_size(true);
}

void TabButtonsListCtrl::SetSelection(int sel)
{
    if (m_selection == sel)
        return;
    // TODO: Move font selection into button code based on state, like colors are handled.
    if (m_selection >= 0 && m_selection < Count()) {
        m_pageButtons[m_selection]->SetSelected(false);
        m_pageButtons[m_selection]->SetFont(TAB_BUTTON_FONT);
    }
    m_selection = sel;

    if (sel >= 0 && sel < Count()) {
        m_pageButtons[sel]->SetSelected(true);
        m_pageButtons[sel]->SetFont(TAB_BUTTON_FONT_SEL);
    }
    Refresh();
}

void TabButtonsListCtrl::showNewTag(int sel, bool tag)
{
    if (sel < 0 || sel >= Count() || m_pageButtons[sel]->GetShowNewTag() == tag)
        return;

    m_pageButtons[sel]->ShowNewTag(tag);
    Refresh();
}

bool TabButtonsListCtrl::InsertPage(size_t n, const wxString &text, const std::string &bmp_name, const wxString &tooltip)
{
    if (bmp_name.empty())
        return InsertPage(n, text, m_arrow_img, tooltip);
    return InsertPage(n, text, ScalableBitmap(this, bmp_name, 14), tooltip);
}

bool TabButtonsListCtrl::InsertPage(size_t n, const wxString& text, const ScalableBitmap &bmp, const wxString &tooltip)
{
     wxCHECK_MSG(n <= m_pageButtons.size(), false, wxS("Invalid page"));

    TabButton *btn = new TabButton(this, text, bmp);
    btn->SetCornerRadius(0);
    btn->SetMinSize(m_btn_size * (em_unit(this) / 10));
    btn->SetBackgroundColor(m_btn_bg_colors);
    btn->SetTextColor(*wxBLACK);
    btn->SetBorderWidth(m_btn_border_width);
    btn->SetPaddingSize(m_btn_padding);
    if (!tooltip.IsEmpty())
        btn->SetToolTip(tooltip);
    if (m_btn_border_colors.count())
        btn->SetBorderColor(m_btn_border_colors);

    btn->Bind(wxEVT_BUTTON, [this, btn](wxCommandEvent& event) {
        if (auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn); it != m_pageButtons.end()) {
            auto sel = it - m_pageButtons.begin();
            SetSelection(sel);
            wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_TABBOOK_SEL_CHANGED);
            evt.SetId(sel);
            wxPostEvent(this->GetParent(), evt);
        }
    });
    Slic3r::GUI::wxGetApp().UpdateDarkUI(btn);

    m_pageButtons.insert(m_pageButtons.begin() + n, btn);
    m_buttons_sizer->Insert(n, new wxSizerItem(btn, 0, wxEXPAND));

    Layout();
    FitInside();
    return true;
}

void TabButtonsListCtrl::RemovePage(size_t n)
{
    if (n >= m_pageButtons.size()) return;
    TabButton *btn = m_pageButtons[n];
    m_pageButtons.erase(m_pageButtons.begin() + n);
    m_buttons_sizer->Remove(n);
    btn->Reparent(nullptr);
    btn->Destroy();
    Layout();
    FitInside();
}

bool TabButtonsListCtrl::SetPageImage(size_t n, const std::string &bmp_name)
{
     wxCHECK_MSG(n < m_pageButtons.size(), false, wxS("Invalid page"));

    ScalableBitmap bitmap;
    if (!bmp_name.empty())
        bitmap = ScalableBitmap(this, bmp_name, 14);
    m_pageButtons[n]->SetBitmap(bitmap);

    return true;
}

bool TabButtonsListCtrl::SetPageText(size_t n, const wxString &strText)
{
    wxCHECK_MSG(n < m_pageButtons.size(), false, wxS("Invalid page"));

    m_pageButtons[n]->SetLabel(strText);
    return true;
}

wxString TabButtonsListCtrl::GetPageText(size_t n) const
{
    wxCHECK_MSG(n < m_pageButtons.size(), wxEmptyString, wxS("Invalid page"));

    return m_pageButtons[n]->GetLabel();
}

bool TabButtonsListCtrl::SetPageToolTip(size_t n, const wxString &tooltip)
{
    wxCHECK_MSG(n < m_pageButtons.size(), false, wxS("Invalid page"));

    m_pageButtons[n]->SetToolTip(tooltip);
    return true;
}

wxString TabButtonsListCtrl::GetPageToolTip(size_t n) const
{
    wxCHECK_MSG(n < m_pageButtons.size(), wxEmptyString, wxS("Invalid page"));

    return m_pageButtons[n]->GetToolTipText();
}

void TabButtonsListCtrl::SetPaddingSize(const wxSize& size)
{
    if (size == GetPaddingSize())
        return;

    m_btn_padding = size;
    for (auto *btn : std::as_const(m_pageButtons)) {
        btn->SetPaddingSize(size);
    }
}

const wxSize& TabButtonsListCtrl::GetPaddingSize(size_t n) const
{
    if (n < m_pageButtons.size())
        return m_pageButtons[n]->GetPaddingSize();
    return m_btn_padding;
}

void TabButtonsListCtrl::SetButtonBGColors(const StateColor &stateColor)
{
    if (m_btn_bg_colors == stateColor)
        return;

    m_btn_bg_colors = stateColor;

    for (auto *btn : std::as_const(m_pageButtons))
        btn->SetBackgroundColor(m_btn_bg_colors);
}

void TabButtonsListCtrl::SetButtonBorderWidth(int width)
{
    if (width == m_btn_border_width || width < 0)
        return;

    m_btn_border_width = width;

    for (auto *btn : std::as_const(m_pageButtons))
        btn->SetBorderWidth(width);
}

void TabButtonsListCtrl::SetButtonBorderColors(const StateColor &stateColor)
{
    if (m_btn_border_colors == stateColor)
        return;

    m_btn_border_colors = stateColor;

    for (auto *btn : std::as_const(m_pageButtons))
        btn->SetBorderColor(m_btn_border_colors);
}

void TabButtonsListCtrl::SetButtonSize(const wxSize &size)
{
    if (m_btn_size == size)
        return;

    m_btn_size = size;

    const wxSize sz = size * (em_unit(this) / 10);
    for (auto *btn : std::as_const(m_pageButtons))
        btn->SetMinSize(sz);

    update_client_size(true);
}

void TabButtonsListCtrl::SetListTopPadding(int padding)
{
    if (wxSizerItem *item = GetSizer()->GetItem(size_t(0)); item && item->IsSpacer())
        item->AssignSpacer(0, padding);
}

void TabButtonsListCtrl::SetUseClientAreaForScrollbar(bool enable)
{
    if (m_scrollbar_in_client_area == enable)
        return;

    m_scrollbar_in_client_area = enable;
    update_client_size(true);
}

void TabButtonsListCtrl::update_client_size(bool layout)
{
    if (m_scrollbar_in_client_area && m_btn_size.x > 0)
        SetMaxClientSize({ m_btn_size.x * em_unit(this) / 10, -1 });
    else
        SetMaxClientSize(wxDefaultSize);

    if (layout) {
        Layout();
        FitInside();
    }
}
