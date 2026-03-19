#pragma once

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/html/htmlwin.h>
#include <tuple>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

class Button;
class Label;
class CheckBox;
namespace Slic3r { 
namespace GUI {

class SupportComboCard : public wxPanel
{
public:
    SupportComboCard(wxWindow* parent,
                     const std::vector<wxString>& objects,
                     const std::vector<std::tuple<int, wxColour, wxString>>& mainMat,
                     const std::tuple<int, wxColour, wxString>& supportMat,
                     const wxArrayString& params);
    void restore_color();

private:
    void create_ui(const std::vector<wxString> &objects,
                   const std::vector<std::tuple<int, wxColour, wxString>> &mainMat,
                   const std::tuple<int, wxColour, wxString>& supportMat,
                   const wxArrayString& params);

    std::vector<std::pair<wxPanel*, wxColour>> m_color_boxes;
    std::vector<std::pair<wxStaticText*, wxColour>> m_color_texts;
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
    void AddSupportComboCard(const std::vector<wxString>                            &objects,
                             const std::vector<std::tuple<int, wxColour, wxString>> &mainMat,
                             const std::tuple<int, wxColour, wxString>              &supportMat,
                             const wxArrayString                                    &params);

protected:
    virtual void on_dpi_changed(const wxRect& suggested_rect) override;
    void save_dont_show_again();
    void on_apply_click(wxCommandEvent &event)
    {
        save_dont_show_again();
        EndModal(wxID_APPLY);
    };
    void on_cancel_click(wxCommandEvent &event)
    {
        save_dont_show_again();
        EndModal(wxID_CANCEL);
    };
    void on_close(wxCloseEvent& event)
    {
        save_dont_show_again();
        event.Skip();
    };

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
    CheckBox *m_dont_show_again;
};

} // namespace GUI
} // namespace Slic3r
