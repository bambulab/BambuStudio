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

namespace Slic3r { namespace GUI {

struct NozzleConfig;

enum class NozzleStatus : int
{
    nsNormal = 0,
    nsError,
    nsUnknown
};

class CustomSeparator : public wxPanel
{
public:
    enum State { Normal, Error, Hidden };

    CustomSeparator(wxWindow *parent, bool is_small);

    void  SetState(State state);
    State GetState() const { return m_state; }
    void  UpdatePosition();
    void  SetIconYOffset(int offset)
    {
        m_icon_y_offset = offset;
        UpdatePosition();
    }

private:
    wxPanel        *m_separator_line{nullptr};
    wxStaticBitmap *m_icon{nullptr};

    ScalableBitmap m_normal_bitmap;
    ScalableBitmap m_error_bitmap;

    State m_state{Hidden};
    int   m_icon_y_offset{-1};

    void CreateLayout();
};

class DiameterButtonPanel : public wxPanel
{
public:
    using NozzleQueryCallback = std::function<bool(const wxString &diameter)>;

    enum ButtonState { Selected, HasNozzle, NoNozzle };

    DiameterButtonPanel(wxWindow *parent, const std::vector<wxString> &diameter_choices = std::vector<wxString>(), bool show_title = true);
    wxString    GetSelectedDiameter() const { return m_selected_diameter; }
    void        SetSelectedDiameter(const wxString &diameter);
    void        SetNozzleQueryCallback(NozzleQueryCallback callback) { m_nozzle_query_callback = callback; }
    void        UpdateSingleButtonState(Button *btn, const wxString &diameter);
    void        UpdateButtonStates();
    ButtonState GetButtonState(const wxString &diameter) const;
    void        RefreshLayout(const std::vector<wxString> &choices);
    std::vector<wxString> GetChoices() const { return m_choices; }
    void        Rescale();

private:
    void OnButtonClicked(wxCommandEvent &event);
    void CreateLayout();

    std::vector<Button *> m_buttons;
    std::vector<wxString> m_choices;
    int                   m_selected_index{-1};
    wxString              m_selected_diameter;
    bool                  m_show_title{true};
    NozzleQueryCallback   m_nozzle_query_callback{nullptr};
};

class HorizontalScrollablePanel : public wxScrolledWindow
{
public:
    HorizontalScrollablePanel(wxWindow* parent,
                             wxWindowID id = wxID_ANY,
                             const wxPoint& pos = wxDefaultPosition,
                             const wxSize& size = wxDefaultSize);

    void Clear();

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

    ExtruderPanel(wxWindow *parent, GroupType type);

    GroupType GetType() const { return m_type; }
    NozzleVolumeType GetVolumeType(const wxString& diameter) const;

    void SetDiameter(const wxString& diameter);
    void SetFlow(const wxString& diameter);
    void SetNozzleNumber(int num);
    void SetNozzleIndex(std::vector<int> indices);
    void Rescale();
    void ShowNozzleNumber(bool show) { m_nozzle_number_label->Show(show); }
    void ShowEnable(bool enable);
    void UpdateScrollButtons();

    virtual void set_ams_count(int n4, int n1);
    virtual void update_ams();
    void sync_ams(MachineObject const *obj, std::vector<DevAms *> const &ams4, std::vector<DevAms *> const &ams1, std::vector<DevAmsTray> const &ext);

protected:
    void CreateLayout();
    void OnLeftScrollClick(wxCommandEvent &evt);
    void OnRightScrollClick(wxCommandEvent &evt);
    void OnScrollSizeChanged(wxSizeEvent &evt);

    void OnScrollTimer(wxTimerEvent &evt);
    void OnLeftScrollDown(wxMouseEvent &evt);
    void OnLeftScrollUp(wxMouseEvent &evt);
    void OnRightScrollDown(wxMouseEvent &evt);
    void OnRightScrollUp(wxMouseEvent &evt);

    GroupType m_type;
    bool      m_disabled{false};
    bool      m_show_title{false};
    int       m_nozzle_count{0};
    wxStaticBitmap *m_nozzle_icon{nullptr};
    Label          *m_diameter_label{nullptr};
    Label          *m_flow_label{nullptr};
    wxBitmap        icon;
    wxStaticBitmap *m_disabled_icon{nullptr};
    Label          *m_nozzle_number_label{nullptr};
    Label          *m_nozzle_index_label{nullptr};
    ScalableButton *m_left_scroll{nullptr};
    ScalableButton *m_right_scroll{nullptr};

    wxTimer *m_scroll_timer{nullptr};
    bool m_scroll_left{false};
    bool m_scroll_right{false};
    HorizontalScrollablePanel *m_scroll{nullptr};
    wxBoxSizer *m_main_sizer{nullptr};
    wxSizer *m_ams_sizer{nullptr};

    std::vector<AMSPreview *> m_ams_previews;
    std::vector<AMSPreview *> m_ext_previews;

public:
    size_t m_ams_n4 {0};
    size_t m_ams_n1 {0};
    std::vector<AMSinfo> m_ams_4;
    std::vector<AMSinfo> m_ams_1;
    std::vector<AMSinfo> m_ext;
};

class ExtruderDialogPanel : public ExtruderPanel
{
public:
    struct NozzleInternal
    {
        wxStaticBitmap *warning_icon{nullptr};
        ComboBox       *type_combo{nullptr};
        Label *diameter_flow_label {nullptr};
        ComboBox *diameter_combo{nullptr};
        ComboBox *flow_combo{nullptr};
        Label *nozzle_label {nullptr};
        wxPanel *material_color_panel {nullptr};
        StaticBox *material_color_box {nullptr};
        Label *material_label {nullptr};
        Label *error_label {nullptr};
        Label *unknown_label {nullptr};
        bool is_serious_warning{false};
    };
    ExtruderDialogPanel(wxWindow *parent, GroupType type, std::vector<NozzleConfig> cfg);
    std::vector<NozzleInternal> GetNozzleConfigs() const { return m_nozzles; }
    Label* GetFilamentLabel() const { return m_filament_label; }
    void UpdateWarningStates(const wxString &selected_diameter);
    void ShowAMSCountPopup();
    void ShowAMSDeletePopup(AMSPreview *ams_preview);
    void ShowBadge(bool show);
    void Rescale();
    std::vector<NozzleConfig> ExtractConfig();
    ExtruderNozzleStat BuildExtruderNozzleStat(wxString diameter, const std::vector<NozzleConfig> &configs);

    void sync_ams_from_panel(const ExtruderPanel *source_panel);
    void set_ams_count(int n4, int n1) override
    {
        if (n4 == m_ams_n4 && n1 == m_ams_n1)
            return;

        m_ams_n4 = n4;
        m_ams_n1 = n1;
        update_ams();
    }
    void update_ams() override;
    bool is_extruder_synced(wxString diameter);

private:
    void OnNozzleErrorClick(wxMouseEvent &evt);

#ifdef __WXMSW__
    ScalableBitmap m_badge;
    void           OnPaint(wxPaintEvent &event);
#endif
#ifdef __WXOSX__
    ScalableButton *m_badge{nullptr};
    void            LayoutBadge();
    void            OnSize(wxSizeEvent &event);
#endif

    wxBitmap m_warning_bitmap;
    wxBitmap m_warning_serious_bitmap;
    Label *m_title_label {nullptr};
    Label *m_filament_label{nullptr};
    ScalableButton *m_add_btn{nullptr};
    PopupWindow *m_ams_popup{nullptr};
    std::vector<NozzleInternal> m_nozzles;
    std::vector<NozzleConfig>   m_nozzle_configs;
};

class ExtruderSettingsDialog : public DPIDialog
{
public:
    ExtruderSettingsDialog(wxWindow                        *parent,
                           bool                             is_dual_extruder,
                           wxString                        &diameter,
                           const std::vector<NozzleConfig> &left_configs,
                           const std::vector<NozzleConfig> &right_configs    = {},
                           ExtruderPanel                   *left_source = nullptr,
                           ExtruderPanel                   *right_source = nullptr,
                           CustomSeparator::State           state = CustomSeparator::State::Hidden);

    wxString GetSelectedDiameter() const;
    std::vector<NozzleConfig> GetExtruderConfigs(int index) const;
    void UpdateDialogState();
    void OnAMSCountChanged();

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void CreateLayout();
    std::vector<NozzleConfig> ExtractConfigsFromPanel(ExtruderDialogPanel *panel) const;
    void SetupCallbacks();
    void SaveAMSInitialState(ExtruderPanel *left_source, ExtruderPanel *right_source);
    void RestoreAMSInitialState();
    void AlignSeparatorIcon();

    bool m_is_dual_extruder{false};
    int  m_max_nozzle_count{6};

    DiameterButtonPanel *m_diameter_panel{nullptr};
    wxPanel             *m_extruder_container{nullptr};
    ExtruderDialogPanel *m_left_extruder_panel{nullptr};
    ExtruderDialogPanel *m_right_extruder_panel{nullptr};
    wxPanel             *m_warning_panel{nullptr};
    wxPanel             *m_warning_serious_panel{nullptr};
    Button              *m_confirm_btn{nullptr};
    Button              *m_cancel_btn{nullptr};
    Button              *m_refresh_btn{nullptr};
    CustomSeparator     *m_separator{nullptr};

    wxString m_diameter;

    std::vector<NozzleConfig> m_left_configs;
    std::vector<NozzleConfig> m_right_configs;

    struct InitialAMSState
    {
        size_t               ams_n4{0};
        size_t               ams_n1{0};
        std::vector<AMSinfo> ams_4;
        std::vector<AMSinfo> ams_1;
        std::vector<AMSinfo> ext;
    };
    InitialAMSState m_initial_left_ams;
    InitialAMSState m_initial_right_ams;
};

class ExtruderPanelGroup : public wxPanel
{
public:
    enum PanelType {
        Single,
        Dual
    };

    ExtruderPanelGroup(wxWindow                        *parent,
                       PanelType                        type,
                       const std::vector<NozzleConfig> &left_configs   = {},
                       const std::vector<NozzleConfig> &right_configs  = {},
                       const std::vector<NozzleConfig> &single_configs = {});

    // Getters
    PanelType      GetPanelType() const { return m_panel_type; }
    ExtruderPanel *GetLeftPanel() const { return m_left_panel; }
    ExtruderPanel *GetRightPanel() const { return m_right_panel; }
    ExtruderPanel *GetSinglePanel() const { return m_single_panel; }

    ExtruderPanel *GetPanel(int index) const;

    // Update methods
    void SetAMSCount(int n4, int n1, int extruder_index = 0);
    void ShowBadge(bool show);

    void SetOnClick(std::function<void(bool)> callback) { m_on_click = callback; }

    // Layout
    void Rescale();

    void SetSeparatorState(CustomSeparator::State state);
    CustomSeparator::State GetSeparatorState() const { return m_separator->GetState(); }
    void ShowSeparatorIcon(bool show);

private:
    void CreateLayout();
    void BindClickEvents();
    void OnPanelClick(wxMouseEvent &evt);
    void OnPaint(wxPaintEvent &evt);

#ifdef __WXMSW__
    ScalableBitmap m_badge;
#endif
#ifdef __WXOSX__
    ScalableButton *m_badge{nullptr};
    void            LayoutBadge();
    void            OnSize(wxSizeEvent &event);
#endif

    PanelType m_panel_type;

    ExtruderPanel *m_left_panel{nullptr};
    ExtruderPanel *m_right_panel{nullptr};
    ExtruderPanel *m_single_panel{nullptr};

    // Configs
    std::vector<NozzleConfig> m_left_configs;
    std::vector<NozzleConfig> m_right_configs;
    std::vector<NozzleConfig> m_single_configs;

    // Layout components
    wxBoxSizer *m_main_sizer{nullptr};
    std::function<void(bool)> m_on_click{};
    CustomSeparator *m_separator{nullptr};
};

}}

#endif // slic3r_GUI_ExtruderPanel_hpp_