#ifndef slic3r_AMSNewFilamentDialogs_hpp_
#define slic3r_AMSNewFilamentDialogs_hpp_

// New-filament recording dialogs, split out of AMSMaterialsSetting.
// Shown by StatusPanel when the printer detects a newly-inserted filament and
// offers to record it into / link it against the Filament Library.
#include "AMSMaterialsSetting.hpp"

namespace Slic3r { namespace GUI {

// AMSNewOfficialFilamentDlg
class AMSNewOfficialFilamentDlg : public DPIDialog
{
public:
    enum class Choice { RecordNew, LinkExisting, Skip };

    AMSNewOfficialFilamentDlg(wxWindow* parent);
    Choice GetChoice() const { return m_choice; }

    void SetTrayContext(MachineObject* obj,
                        const std::string& ams_id,
                        const std::string& slot_id);
    void SetSoftMatchData(const SoftMatchPendingResponse& data);
    std::string GetSelectedLinkSpoolId() const { return m_selected_link_spool_id; }
    int GetHitSpoolId() const          { return m_hit_spool_id; }
    int GetSelectedCandidateId() const { return m_selected_candidate_id; }

    void on_dpi_changed(const wxRect&) override {
        m_btn_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
        m_btn_confirm->SetCornerRadius(FromDIP(12));
        if (m_btn_record_new) {
            m_btn_record_new->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
            m_btn_record_new->SetCornerRadius(FromDIP(12));
        }
        if (m_combo_link)
            m_combo_link->SetMinSize(wxSize(FromDIP(360), FromDIP(56)));
        // Raw-bitmap cards don't auto-rescale; re-render them at the new DPI.
        if (m_obj) populate_link_combo();
        Fit();
    }

private:
    void create();
    void populate_link_combo();
    void on_combo_selected(wxCommandEvent&);
    void on_confirm(wxCommandEvent&);
    void on_record_new(wxCommandEvent&);

    Choice    m_choice{ Choice::RecordNew };
    wxStaticText*   m_title{ nullptr };
    wxStaticBitmap* m_hit_card{ nullptr };
    wxStaticText*   m_match_label{ nullptr };
    Button*   m_btn_record_new{ nullptr };
    Button*   m_btn_confirm{ nullptr };

    ::ComboBox*                m_combo_link{ nullptr };
    wxSizer*                   m_combo_row{ nullptr };
    std::map<int, std::string>   m_combo_idx_to_spool_id;
    std::map<int, FilamentSpool> m_combo_idx_to_spool;
    std::string                m_selected_link_spool_id;

    MachineObject* m_obj    { nullptr };
    std::string    m_ams_id;
    std::string    m_slot_id;

    SoftMatchPendingResponse m_soft_match_data;
    FilamentSpool m_hit_spool;              // hit shown in the collapsed box by default
    int  m_prev_combo_sel{ -1 };            // last picked candidate index (-1 = none); for re-click toggle
    int  m_hit_spool_id{ 0 };
    int  m_selected_candidate_id{ 0 };
    bool m_only_hit{ false };
};

// AMSNewFilamentRecordedDlg
class AMSNewFilamentRecordedDlg : public DPIDialog
{
public:
    AMSNewFilamentRecordedDlg(wxWindow* parent, const FilamentSpool& sp);

    void on_dpi_changed(const wxRect&) override { Fit(); }

private:
    void create(const FilamentSpool& sp);
};

}} // namespace Slic3r::GUI

#endif // slic3r_AMSNewFilamentDialogs_hpp_
