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
class SwitchButton;

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
    using NoNozzleClickCallback = std::function<void(const wxString &diameter)>;
    using MultiNozzleModeCallback = std::function<void(bool enabled)>;

    enum ButtonState { Selected, HasNozzle, NoNozzle };
    enum DisabledTooltipContext { PrinterContext, FilamentContext };

    DiameterButtonPanel(wxWindow *parent, const std::vector<wxString> &diameter_choices = std::vector<wxString>(),
                        bool show_title = true, DisabledTooltipContext tooltip_context = PrinterContext,
                        bool show_multi_nozzle_mode = false);
    wxString    GetSelectedDiameter() const { return m_selected_diameter; }
    void        SetSelectedDiameter(const wxString &diameter);
    void        SetNozzleQueryCallback(NozzleQueryCallback callback) { m_nozzle_query_callback = callback; }
    void        SetNoNozzleClickCallback(NoNozzleClickCallback callback) { m_no_nozzle_click_callback = callback; }
    void        SetMultiNozzleMode(bool enabled);
    void        SetMultiNozzleModeCallback(MultiNozzleModeCallback callback) { m_multi_nozzle_mode_callback = callback; }
    void        UpdateSingleButtonState(Button *btn, const wxString &diameter);
    void        UpdateButtonStates();
    void        RefreshLayout(const std::vector<wxString> &choices);
    std::vector<wxString> GetChoices() const { return m_choices; }
    void        Rescale();

private:
    void OnButtonClicked(wxCommandEvent &event);
    void CreateLayout();

    std::vector<Button *> m_buttons;
    std::vector<wxString> m_choices;
    wxString              m_selected_diameter;
    bool                  m_show_title{true};
    bool                  m_show_multi_nozzle_mode{false};
    bool                  m_multi_nozzle_mode_enabled{false};
    DisabledTooltipContext m_tooltip_context{PrinterContext};
    NozzleQueryCallback   m_nozzle_query_callback{nullptr};
    NoNozzleClickCallback m_no_nozzle_click_callback{nullptr};
    MultiNozzleModeCallback m_multi_nozzle_mode_callback{nullptr};
    SwitchButton          *m_multi_nozzle_mode_switch{nullptr};
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
    void sync_ams(MachineObject const *obj, std::vector<DevAms *> const &ams_internal, std::vector<DevAms *> const &ams4, std::vector<DevAms *> const &ams1, std::vector<DevAmsTray> const &ext);

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
    std::vector<AMSinfo> m_ams_internal;
    std::vector<AMSinfo> m_ext;
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