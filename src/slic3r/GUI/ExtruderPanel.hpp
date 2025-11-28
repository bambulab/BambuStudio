#ifndef slic3r_GUI_ExtruderPanel_hpp_
#define slic3r_GUI_ExtruderPanel_hpp_

#include <wx/dialog.h>
#include <wx/panel.h>

#include "GUI_Utils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/AMSItem.hpp"

class wxStaticText;
class wxBoxSizer;

namespace Slic3r {

namespace GUI {

wxDECLARE_EVENT(EVT_NOZZLE_DIAMETER_CHANGED, wxCommandEvent);
wxDECLARE_EVENT(EVT_NOZZLE_DIAMETER_SELECTED, wxCommandEvent);
wxDECLARE_EVENT(EVT_NOZZLE_PANELS_UPDATED, wxCommandEvent);
wxDECLARE_EVENT(EVT_NOZZLE_FLOW_UPDATED, wxCommandEvent);

class ExtruderNozzlePanel : public wxPanel
{
public:
    ExtruderNozzlePanel(wxWindow              *parent,
                        const wxString        &diameter  = "0.4",
                        NozzleVolumeType       flow      = nvtStandard,
                        const std::vector<wxString> &diameters = std::vector<wxString>{"0.2", "0.4", "0.6", "0.8"},
                        const std::vector<wxString> &flows     = std::vector<wxString>{"Standard"});
    void Select(bool selected);

    enum Status {
        Normal,
        Edited,
        Disabled,
        EditedDisabled,
        Error,
        Empty,
        Unknown
    };

    void SetDiameter(const wxString &diameter, bool user_edit = false);
    void SetFlow(NozzleVolumeType flow, bool user_edit = false);
    void SetNozzleConfig(const wxString &diameter, NozzleVolumeType flow, bool user_edit = false);
    wxString GetDiameter() const { return m_diameter; }
    NozzleVolumeType GetFlow() const { return m_flow; }
    bool IsDefined() const { return m_defined; }
    void SetDefined(bool defined) { m_defined = defined; }
    bool IsUserEdited() const { return m_user_edited; }
    void SetUserEdited(bool edited) { m_user_edited = edited; }

    void SetStatus(Status status);
    Status GetStatus() const { return m_status; }
    void UpdateStatus(Status status = Normal);

protected:
    void OnPaint(wxPaintEvent &event);

private:
    void UpdateContent();
    void MarkAsModified();
    void NotifyContentChanged(bool diameter_changed, bool flow_changed, bool user_edit);
    void UpdateVisualState();

    wxBitmap icon;
    wxStaticBitmap *m_icon;
    Label          *label_diameter{nullptr};
    Label          *label_volume{nullptr};

    wxString         m_diameter;
    NozzleVolumeType m_flow;
    bool             m_defined{false};
    bool             m_user_edited{false};

    Status           m_status{Normal};
    bool             m_hover{false};
};

class HorizontalScrollablePanel : public wxScrolledWindow
{
public:
    HorizontalScrollablePanel(wxWindow* parent,
                             wxWindowID id = wxID_ANY,
                             const wxPoint& pos = wxDefaultPosition,
                             const wxSize& size = wxDefaultSize);

    void Clear();
    void UpdateScrollbars();

private:
    wxBoxSizer *m_content_sizer{nullptr};
};

class ExtruderPanel : public wxPanel
{
public:
    enum GroupType {
        LeftExtruder,
        RightExtruder,
        SingleExtruder
    };

    ExtruderPanel(wxWindow *parent, GroupType type, bool wide = false);
    void SetNozzlePanelCount(int count);
    int  GetNozzlePanelCount() const { return m_nozzle_count; }
    void SetWideLayout(bool wide);

    void SetDiameters(const std::vector<wxString> &diameters) { m_nozzle_diameter_choices = diameters; }
    void SetFlows(const std::vector<wxString> &flows) { m_nozzle_flow_choices = flows; }
    void SetDiameter(const wxString diameter)
    {
        for (auto panel : m_nozzle_panels) {
            panel->SetNozzleConfig(diameter, panel->GetFlow());
        }
    }
    void SetFlow(NozzleVolumeType flow)
    {
        if (flow == nvtHybrid) {
            return;
        }
        for (auto panel : m_nozzle_panels) {
            panel->SetNozzleConfig(panel->GetDiameter(), flow);
        }
    }
    void SetConfigs(const std::vector<std::pair<std::string, NozzleVolumeType>> &configs)
    {
        assert(static_cast<int>(configs.size()) <= m_nozzle_count);
        for (int i = 0; i < m_nozzle_count; ++i) {
            if (i < configs.size()) {
                m_nozzle_panels[i]->SetNozzleConfig(configs[i].first, configs[i].second);
            }
        }
    }
    void SetPanelStatus(const std::vector<ExtruderNozzlePanel::Status> &statuses)
    {
        assert(static_cast<int>(statuses.size()) == m_nozzle_count);
        for (int i = 0; i < m_nozzle_count; ++i) {
            if (i < statuses.size()) {
                m_nozzle_panels[i]->SetStatus(statuses[i]);
            }
        }
    }

    void SetDefined(bool defined)
    {
        for (auto panel : m_nozzle_panels) {
            panel->SetDefined(defined);
        }
    }

    std::vector<ExtruderNozzlePanel *> GetNozzlePanels() const { return m_nozzle_panels; }
    GroupType GetType() const { return m_type; }
    NozzleVolumeType GetVolumeType(const wxString& diameter) const;

    std::set<wxString> GetExistingDiameters() const;
    std::unordered_map<NozzleVolumeType, int> GetNozzleFlowCounts(const wxString& diameter) const;
    void SetNozzleBySelection(const wxString &diameter);
    void NotifyNozzlePanelsUpdated();
    void UpdateLayout();
    void ShowBadge(bool show);
    void Rescale();

    void set_ams_count(int n4, int n1);
    void update_ams();
    void sync_ams(MachineObject const *obj, std::vector<DevAms *> const &ams4, std::vector<DevAms *> const &ams1, std::vector<DevAmsTray> const &ext);

private:
    void CreateLayout();
    void SortNozzlePanels();
    void AddPanels();

    void CreateAMSLayout();
    void UpdateAMSLayout();
    void ShowAMSCountPopup();
    void ShowAMSDeletePopup(AMSPreview *ams_preview);

#ifdef __WXMSW__
    ScalableBitmap m_badge;
    void OnPaint(wxPaintEvent &event);
#endif
#ifdef __WXOSX__
    ScalableButton *m_badge{nullptr};
    void LayoutBadge();
    void OnSize(wxSizeEvent &event);
#endif

    GroupType m_type;
    bool      m_wide_layout{false};
    bool      m_show_title{false};
    int       m_nozzle_count{0};
    Label    *m_title_label{nullptr};
    ScalableButton *m_add_btn{nullptr};
    PopupWindow *m_ams_popup{nullptr};
    HorizontalScrollablePanel *m_scroll{nullptr};
    wxBoxSizer *m_main_sizer{nullptr};
    wxBoxSizer *m_ams_sizer{nullptr};

    std::vector<ExtruderNozzlePanel *> m_nozzle_panels;
    std::vector<AMSPreview *> m_ams_previews;
    std::vector<AMSPreview *> m_ext_previews;
    size_t m_ams_n4 {0};
    size_t m_ams_n1 {0};
    std::vector<AMSinfo> m_ams_4;
    std::vector<AMSinfo> m_ams_1;
    std::vector<AMSinfo> m_ext;


    std::vector<wxString> m_nozzle_diameter_choices;
    std::vector<wxString> m_nozzle_flow_choices;
};

class NozzleConfigDialog : public DPIDialog
{
public:
    NozzleConfigDialog(wxWindow *parent, ExtruderNozzlePanel *nozzle_panel, const std::vector<wxString> &diameter_choices, const std::vector<wxString> &flow_choices);

    void on_dpi_changed(const wxRect &suggested_rect) override;
    wxString GetSelectedDiameter() const { return m_diameter_combo->GetValue(); }
    NozzleVolumeType GetSelectedFlow() const
    {
        if (m_flow_combo->GetValue() == _L("Standard")) {
            return nvtStandard;
        } else if (m_flow_combo->GetValue() == _L("High Flow")) {
            return nvtHighFlow;
        } else if (m_flow_combo->GetValue() == _L("TPU High Flow")) {
            return nvtTPUHighFlow;
        } else {
            return nvtStandard;
        }
    }

private:
    void CreateLayout();

    void OnDiameterChanged(wxCommandEvent &event);
    void OnFlowChanged(wxCommandEvent &event);
    void OnWikiClicked(wxMouseEvent &event);
    void OnOKClicked(wxCommandEvent &event);

private:
    ExtruderNozzlePanel *m_nozzle_panel{nullptr};

    Label          *m_diameter_label{nullptr};
    ComboBox       *m_diameter_combo{nullptr};

    Label          *m_flow_label{nullptr};
    ComboBox       *m_flow_combo{nullptr};

    Button  *m_ok_btn{nullptr};

    std::vector<wxString> m_diameter_choices;
    std::vector<wxString> m_flow_choices;

    wxString         m_original_diameter;
    NozzleVolumeType m_original_flow;
    bool             m_original_defined;

    bool m_diameter_changed{false};
    bool m_flow_changed{false};
};

class DiameterButtonPanel : public wxPanel
{
public:
    using NozzleQueryCallback = std::function<bool(const wxString &diameter)>;

    enum ButtonState { Selected, HasNozzle, NoNozzle };

    DiameterButtonPanel(wxWindow *parent, const std::vector<wxString> &diameter_choices = std::vector<wxString>());
    wxString GetSelectedDiameter() const { return m_selected_diameter; }
    void     SetSelectedDiameter(const wxString &diameter);
    void     SetNozzleQueryCallback(NozzleQueryCallback callback) { m_nozzle_query_callback = callback; }
    void     UpdateSingleButtonState(Button *btn, const wxString &diameter);
    void     UpdateButtonStates();
    ButtonState GetButtonState(const wxString &diameter) const;
    void     RefreshLayout(const std::vector<wxString> &choices);

private:
    void OnButtonClicked(wxCommandEvent &event);
    void CreateLayout();

    std::vector<Button *> m_buttons;
    std::vector<wxString> m_choices;
    int                   m_selected_index{-1};
    wxString              m_selected_diameter;
    NozzleQueryCallback   m_nozzle_query_callback{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ExtruderPanel_hpp_