#ifndef slic3r_GUI_CalibrationWizardPage_hpp_
#define slic3r_GUI_CalibrationWizardPage_hpp_

#include <set>
#include <map>
#include <utility>

#include "wx/event.h"
#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/AMSControl.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Widgets/RadioBox.hpp"
#include "wxExtensions.hpp"
#include "PresetComboBoxes.hpp"

#include "../slic3r/Utils/CalibUtils.hpp"
#include "../../libslic3r/Calib.hpp"

class wxBitmapButton;

namespace Slic3r { namespace GUI {


#define MIN_CALIBRATION_PAGE_WIDTH         FromDIP(1100)
#define PRESET_GAP                         FromDIP(25)
#define CALIBRATION_COMBOX_SIZE            wxSize(FromDIP(500), FromDIP(24))
#define CALIBRATION_FILAMENT_COMBOX_SIZE   wxSize(FromDIP(250), FromDIP(24))
#define CALIBRATION_OPTIMAL_INPUT_SIZE     wxSize(FromDIP(300), FromDIP(24))
#define CALIBRATION_FROM_TO_INPUT_SIZE     wxSize(FromDIP(160), FromDIP(24))
#define CALIBRATION_NOZZLE_COMBO_SIZE      wxSize(FromDIP(160), FromDIP(24))

#define CALIBRATION_NOZZLE_ITEM_GAP        FromDIP(8)
#define CALIBRATION_NOZZLE_SECTION_GAP     FromDIP(60)
#define CALIBRATION_FGSIZER_HGAP           FromDIP(50)
#define CALIBRATION_TEXT_MAX_LENGTH        FromDIP(90) + CALIBRATION_FGSIZER_HGAP + 2 * CALIBRATION_FILAMENT_COMBOX_SIZE.x
#define CALIBRATION_PROGRESSBAR_LENGTH     FromDIP(690)

class CalibrationWizard;
class SubAmsPopup;

/* note: it's not extruder id !!! */
enum ExtruderRole {
    SINGLE_EXTRUDER = 0,
    MAIN_EXTRUDER,
    DEPUTY_EXTRUDER,
};

enum class CalibrationStyle : int
{
    CALI_STYLE_DEFAULT = 0,
    CALI_STYLE_X1,
    CALI_STYLE_P1P,
};

CalibrationStyle get_cali_style(MachineObject* obj);

wxString get_cali_mode_caption_string(CalibMode mode);

enum CalibrationFilamentMode {
    /* calibration single filament at once */
    CALI_MODEL_SINGLE = 0,
    /* calibration multi filament at once */
    CALI_MODEL_MULITI,
};

enum CalibrationMethod {
    CALI_METHOD_MANUAL = 0,
    CALI_METHOD_AUTO,
    CALI_METHOD_NEW_AUTO,
    CALI_METHOD_NONE,
};

wxString get_calibration_wiki_page(CalibMode cali_mode);

CalibrationFilamentMode get_cali_filament_mode(MachineObject* obj, CalibMode mode);

CalibMode get_obj_calibration_mode(const MachineObject* obj);

CalibMode get_obj_calibration_mode(const MachineObject* obj, int& cali_stage);

CalibMode get_obj_calibration_mode(const MachineObject* obj, CalibrationMethod& method, int& cali_stage);


enum class CaliPageType {
    CALI_PAGE_START = 0,
    CALI_PAGE_PRESET,
    CALI_PAGE_CALI,
    CALI_PAGE_COARSE_SAVE,
    CALI_PAGE_FINE_CALI,
    CALI_PAGE_FINE_SAVE,
    CALI_PAGE_PA_SAVE,
    CALI_PAGE_FLOW_SAVE,
    CALI_PAGE_COMMON_SAVE,
};

struct SelectedFilamentInfo
{
    int          tray_id{-1};
    int          extruder_id{-1};
    int          nozzle_pos_id{-1};    // matches CaliFilamentInfo::nozzle_pos_id
    Preset*      preset{nullptr};
    std::string  tray_name;
    bool         is_bbl_filament{false};
    ExtruderRole extruder_role{ExtruderRole::SINGLE_EXTRUDER};
    int          model_index{0};       // index of model objects in 3mf
};

class FilamentComboBox : public wxPanel
{
public:
    FilamentComboBox(wxWindow* parent, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    ~FilamentComboBox() {};

    SelectedFilamentInfo get_selected_filament_info() const;

    void set_select_mode(CalibrationFilamentMode mode);
    CalibrationFilamentMode get_select_mode() { return m_mode; }

    void        load_tray_from_ams(int id, const DynamicPrintConfig& tray);
    void        update_from_preset();
    void        set_index(int index) { m_index = index; }
    int         get_index() { return m_index; }
    int         get_tray_id() { return m_tray_id; }
    bool        is_bbl_filament() { return m_is_bbl_filamnet; }
    std::string get_tray_name() { return m_tray_name; }
    CalibrateFilamentComboBox* GetComboBox() { return m_comboBox; }
    CheckBox* GetCheckBox() { return m_checkBox; }
    void SetCheckBox(CheckBox* cb) { m_checkBox = cb; }
    RadioBox* GetRadioBox() { return m_radioBox; }
    void SetRadioBox(RadioBox* btn) { m_radioBox = btn; }
    void SetChipSize(const wxSize& size);

    virtual bool Show(bool show = true);
    virtual bool Enable(bool enable);
    virtual void SetValue(bool value, bool send_event = true);
    virtual void msw_rescale();

    void ShowPanel();
    void HidePanel();

    void UpdateNozzleCombo(const std::vector<std::pair<wxString, int>>& nozzle_list);
    virtual int GetNozzlePosId() const;
    void ShowNozzleCombo();
    void HideNozzleCombo();

    void SetExtuderRole(const ExtruderRole& role){ m_extuder_role = role; }
    ExtruderRole GetExtuderRole() const { return m_extuder_role; }

protected:
    int         m_index{0};         // index of model objects in 3mf
    int         m_tray_id { -1 };
    std::string m_tray_name;
    bool        m_is_bbl_filamnet{ false };

    CheckBox* m_checkBox{ nullptr };
    RadioBox* m_radioBox{ nullptr };
    CalibrateFilamentComboBox* m_comboBox{ nullptr };
    wxStaticBitmap* m_nozzle_bmp{ nullptr };
    ComboBox* m_nozzle_combo{ nullptr };

    CalibrationFilamentMode     m_mode { CalibrationFilamentMode::CALI_MODEL_SINGLE };
    ExtruderRole                m_extuder_role{ExtruderRole::SINGLE_EXTRUDER};
};

class FilamentExtendComboBox : public FilamentComboBox
{
public:
    FilamentExtendComboBox(wxWindow* parent, int nozzle_id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);

    void SetNozzleInfo(const DevNozzle& nozzle, MachineObject* obj);

    NozzleDiameterType GetSelectedDiameterType() const;
    NozzleFlowType     GetSelectedFlowType() const;

    int GetNozzlePosId() const override { return m_nozzle_pos_id; }  // based on 0x10

    void SetSubAmsMode(bool enable, const wxString& text = wxEmptyString);
    bool IsSubAmsMode() const { return m_subams_mode; }
    wxWindow* GetSubAmsPanel() const { return m_subams_panel; }

    /* SubAms selection state — owned per ext_fcb so popup can restore last picks and the page can collect them at calibration start.*/
    bool GetSubAmsSelected(int tray_id) const;
    /* chip_src: the popup row's combo whose colour chip is mirrored on the panel (nullptr keeps the old chip). */
    void SetSubAmsSelected(int tray_id, const SelectedFilamentInfo& info, const CalibrateFilamentComboBox* chip_src = nullptr);
    void ClearSubAmsSelected(int tray_id);
    void ClearSubAmsSelection(bool refresh = true);
    const std::map<int, SelectedFilamentInfo>& GetSubAmsSelection() const { return m_subams_selection; }

    /* Cached slot set for this nozzle (union of bound/preload/external AMS slots), computed in
       update_rack_filament_combobox() and consumed lazily by on_subams_click() to feed the shared
       popup. Uses std::pair<int,int> == DevAmsSlotId (ams_id, slot_id) to avoid a header dependency. */
    void SetSubAmsSlotSet(const std::set<std::pair<int, int>>& slots) { m_subams_slot_set = slots; }
    const std::set<std::pair<int, int>>& GetSubAmsSlotSet() const { return m_subams_slot_set; }

    /* Per-tray model index (index of model objects in 3mf), assigned by
       CalibrationPresetPage::assign_rack_model_indices() purely from data so it no longer depends on
       any live popup row. SetData() reads it back onto the row before the user picks. */
    void SetSubAmsModelIndex(int tray_id, int index) { m_subams_model_index[tray_id] = index; }
    int  GetSubAmsModelIndex(int tray_id) const {
        auto it = m_subams_model_index.find(tray_id);
        return it != m_subams_model_index.end() ? it->second : -1;
    }
    void ClearSubAmsModelIndex() { m_subams_model_index.clear(); }

    // Refresh the SubAms summary from current selection; empty -> fallback label.
    void RefreshSubAmsLabel(const wxString& empty_fallback = wxEmptyString);

    bool Enable(bool enable) override;
    void msw_rescale() override;

private:
    void init_diameter_combo(MachineObject* obj, const NozzleVolumeType& volume_type);
    void init_flow_type_combo(MachineObject* obj);

    // Post EVT_SUBAMS_CLICK to open the popup (no-op when the sub-AMS area is disabled).
    void NotifySubAmsClick();
    // Update the hover highlight of the sub-AMS panel border (forced off when disabled).
    void SetSubAmsHover(bool hover);
    /* Re-apply the radio/checkbox enabled state; call after m_nozzle_selectable or the selection changes. */
    void SyncSelectionEnableState();

    int       m_nozzle_id{-1};
    int       m_nozzle_pos_id{-1};
    Label*    m_id_label{nullptr};
    Label*    m_nozzle_info_label{nullptr};
    ComboBox* m_diameter_combo{nullptr};
    ComboBox* m_flow_type_combo{nullptr};
    wxPanel*  m_subams_panel{nullptr};
    Button*   m_subams_label{nullptr};
    wxBoxSizer* m_subams_picker_sizer{nullptr};
    std::vector<wxBitmapButton*> m_subams_picker_buttons;
    wxButton* m_subams_ellipsis_picker{nullptr};
    bool      m_subams_mode{false};
    bool      m_subams_enabled{true};
    bool      m_subams_hover{false};
    bool      m_nozzle_selectable{true};
    std::map<int, SelectedFilamentInfo> m_subams_selection;
    /* tray_id -> (chip colour, chip label); bitmaps are rebuilt at the current DPI on every refresh. */
    std::map<int, std::pair<std::string, std::string>> m_subams_picker_chips;
    std::set<DevAmsSlotId> m_subams_slot_set;  // cached (ams_id, slot_id) union for the shared popup
    std::map<int, int> m_subams_model_index;          // tray_id -> model index (data-side, popup-independent)
};

typedef std::vector<FilamentComboBox*> FilamentComboBoxList;

class CaliPageCaption : public wxPanel
{
public:
    CaliPageCaption(wxWindow* parent,
        CalibMode cali_mode,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void show_prev_btn(bool show = true);
    void show_help_icon(bool show = true);
    void on_sys_color_changed();
    void msw_rescale();

protected:
    ScalableButton* m_prev_btn;
    ScalableButton* m_help_btn;

private:
    void init_bitmaps();
    void create_wiki(wxWindow* parent);

    Label* m_wiki_text;
    wxString  m_wiki_url;
    ScalableBitmap m_prev_bmp_normal;
    ScalableBitmap m_prev_bmp_hover;
    ScalableBitmap m_help_bmp_normal;
    ScalableBitmap m_help_bmp_hover;
};

class CaliPageStepGuide : public wxPanel
{
public:
    CaliPageStepGuide(wxWindow* parent,
        wxArrayString steps,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void set_steps(int index);
    void set_steps_string(wxArrayString steps);
protected:
    wxArrayString m_steps;
    wxBoxSizer* m_step_sizer;
    std::vector<Label*> m_text_steps;
};

class CaliPagePicture : public wxPanel
{
public:
    CaliPagePicture(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void set_bmp(const ScalableBitmap& bmp);
    void paint_on_img();
    void msw_rescale();

protected:
    ScalableBitmap m_bmp;
    wxStaticBitmap* m_img;
};

class PAPageHelpPanel : public wxPanel
{
public:
    PAPageHelpPanel(wxWindow* parent,
        bool ground_panel = true,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);
    void msw_rescale();

protected:
    void create_pop_window();

    ScalableButton* m_help_btn;
    PopupWindow* m_pop_win;
    ScalableBitmap m_bmp;
    wxStaticBitmap* m_img;
};

enum class CaliPageActionType : int
{
    CALI_ACTION_MANAGE_RESULT = 0,
    CALI_ACTION_MANUAL_CALI,
    CALI_ACTION_AUTO_CALI,
    CALI_ACTION_START,
    CALI_ACTION_CALI,
    CALI_ACTION_FLOW_CALI_STAGE_2,
    CALI_ACTION_RECALI,
    CALI_ACTION_PREV,
    CALI_ACTION_NEXT,
    CALI_ACTION_CALI_NEXT,
    CALI_ACTION_PA_SAVE,
    CALI_ACTION_FLOW_SAVE,
    CALI_ACTION_FLOW_COARSE_SAVE,
    CALI_ACTION_FLOW_FINE_SAVE,
    CALI_ACTION_COMMON_SAVE,
    CALI_ACTION_GO_HOME,
    CALI_ACTION_COUNT
};

class CaliPageButton : public Button
{
public:
    CaliPageButton(wxWindow* parent, CaliPageActionType type, wxString text = wxEmptyString);

    CaliPageActionType get_action_type() { return m_action_type; }

    void msw_rescale();

private:
    CaliPageActionType m_action_type;
};

class CaliPageSendingPanel : public wxPanel
{
public:
    CaliPageSendingPanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);
    void create(wxWindow* parent);
    void update_print_error_info(int code, const std::string& msg, const std::string& extra);
    void show_send_failed_info(bool show, int code = 0, wxString description = wxEmptyString, wxString extra = wxEmptyString);
    std::shared_ptr<BBLStatusBarSend> get_sending_progress_bar();
    void reset();

private:
    std::shared_ptr<BBLStatusBarSend> m_send_progress_bar;
    wxScrolledWindow* m_sw_print_failed_info{ nullptr };
    Label* m_st_txt_error_code{ nullptr };
    Label* m_st_txt_error_desc{ nullptr };
    Label* m_st_txt_extra_info{ nullptr };
    int                               m_print_error_code;
    std::string                       m_print_error_msg;
    std::string                       m_print_error_extra;
};

class CaliPageActionPanel : public wxPanel
{
public:
    CaliPageActionPanel(wxWindow* parent,
        CalibMode cali_mode,
        CaliPageType page_type,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void bind_button(CaliPageActionType action_type, bool is_block);
    void show_button(CaliPageActionType action_type, bool show = true);
    void enable_button(CaliPageActionType action_type, bool enable = true);
    void msw_rescale();

protected:
    std::vector<CaliPageButton*> m_action_btns;
};

class CalibrationWizardPage : public wxPanel
{
public:
    CalibrationWizardPage(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~CalibrationWizardPage() {};

    CaliPageType get_page_type() { return m_page_type; }

    CalibrationWizardPage* get_prev_page() { return m_prev_page; }
    CalibrationWizardPage* get_next_page() { return m_next_page; }
    void set_prev_page(CalibrationWizardPage* prev) { m_prev_page = prev; }
    void set_next_page(CalibrationWizardPage* next) { m_next_page = next; }
    CalibrationWizardPage* chain(CalibrationWizardPage* next)
    {
        set_next_page(next);
        next->set_prev_page(this);
        return next;
    }

    virtual void update(MachineObject* obj) { curr_obj = obj; }
    /* device changed and connected */
    virtual void on_device_connected(MachineObject* obj) { curr_obj = obj; }

    virtual void on_reset_page() {}

    virtual void set_cali_filament_mode(CalibrationFilamentMode mode) {
        m_cali_filament_mode = mode;
    }

    virtual void set_cali_method(CalibrationMethod method) {
        m_cali_method = method;
    }

    virtual void msw_rescale();
    virtual void on_sys_color_changed();

protected:
    CalibMode             m_cali_mode;
    CaliPageType          m_page_type;
    CalibrationFilamentMode  m_cali_filament_mode;
    CalibrationMethod     m_cali_method{ CalibrationMethod::CALI_METHOD_MANUAL };
    MachineObject*        curr_obj { nullptr };

    wxWindow*             m_parent { nullptr };
    CaliPageCaption*      m_page_caption { nullptr };
    CaliPageActionPanel*  m_action_panel { nullptr };
    Label*         m_statictext_printer_msg{ nullptr };

private:
    CalibrationWizardPage* m_prev_page {nullptr};
    CalibrationWizardPage* m_next_page {nullptr};
};

wxDECLARE_EVENT(EVT_CALI_ACTION, wxCommandEvent);
wxDECLARE_EVENT(EVT_CALI_TRAY_CHANGED, wxCommandEvent);
wxDECLARE_EVENT(EVT_SUBAMS_CLICK, wxCommandEvent);


}} // namespace Slic3r::GUI

#endif