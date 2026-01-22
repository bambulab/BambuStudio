#pragma once

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/html/htmlwin.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

class Button;
class Label;
namespace Slic3r { 
namespace GUI {

class SupportComboCard : public wxPanel
{
public:
    SupportComboCard(wxWindow* parent,
                     const wxString& mainMat,
                     const wxString& supportMat,
                     const wxArrayString& params);

private:
    void create_ui(const wxString& mainMat,
                   const wxString& supportMat,
                   const wxArrayString& params);
};

class SupportRecommendDialog : public DPIDialog
{
public:
    SupportRecommendDialog(wxWindow* parent, const wxString& title);
    SupportRecommendDialog(SupportRecommendDialog&&) = delete;
    SupportRecommendDialog(const SupportRecommendDialog&) = delete;
    SupportRecommendDialog& operator=(SupportRecommendDialog&&) = delete;
    SupportRecommendDialog& operator=(const SupportRecommendDialog&) = delete;
    virtual ~SupportRecommendDialog() = default;

public:
    void SetTipText(const wxString& text);;
    void SetComboTitle(const wxString& title);;
    void AddSupportComboCard(const wxString& mainMat,
                             const wxString& supportMat,
                             const wxArrayString& params);

protected:
    virtual void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_apply_click(wxCommandEvent& event) { EndModal(wxID_APPLY); };
    void on_cancel_click(wxCommandEvent& event) { EndModal(wxID_CANCEL); };

private:
    void create_ui();

private:
    wxSizer* m_main_sizer;
    Label* m_tip_text;
    Label* m_combo_title;
    wxScrolledWindow* m_scroll_panel;
    wxBoxSizer* m_scroll_sizer;

    Button* m_cancel_btn;
    Button* m_apply_btn;
};

} // namespace GUI
} // namespace Slic3r
