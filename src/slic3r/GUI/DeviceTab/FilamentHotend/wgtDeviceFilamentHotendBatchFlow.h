#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <wx/colour.h>
#include <wx/string.h>

namespace Slic3r::GUI
{

enum class wgtDeviceFilamentHotendBatchAction
{
    None = 0,
    Load,
    Unload
};

struct wgtDeviceFilamentHotendBatchSlotTask
{
    int nozzle_pos{ -1 };
    std::string ams_id;
    std::string slot_id;
};

class wgtDeviceFilamentHotendBatchFlow
{
public:
    using BatchAction = wgtDeviceFilamentHotendBatchAction;
    using BatchSlotTask = wgtDeviceFilamentHotendBatchSlotTask;

    void SetAction(BatchAction action);
    BatchAction GetAction() const;

    void UpdateSelectedSlots(const std::vector<BatchSlotTask>& slots);
    const std::vector<BatchSlotTask>& GetSelectedSlots() const;

    void SetBatchSlots(const std::vector<BatchSlotTask>& slots);
    const std::vector<BatchSlotTask>& GetBatchSlots() const;
    std::vector<BatchSlotTask>& MutableBatchSlots();

    void SetCompletedSlots(const std::vector<BatchSlotTask>& slots);
    const std::vector<BatchSlotTask>& GetCompletedSlots() const;
    std::vector<BatchSlotTask>& MutableCompletedSlots();

    void SetCurrentIndex(size_t index);
    size_t GetCurrentIndex() const;
    void IncrementCurrentIndex();

    void SetWaitingStateChange(bool waiting);
    bool IsWaitingStateChange() const;

    void SetSeenBusyState(bool seen_busy);
    bool HasSeenBusyState() const;

    void SetStepStartedAt(std::chrono::steady_clock::time_point started_at);
    std::chrono::steady_clock::time_point GetStepStartedAt() const;

    void SetLastStatus(const wxString& status_text, const wxColour& color = wxColour("#00AE42"));
    const wxString& GetLastStatus() const;
    const wxColour& GetLastStatusColor() const;
    void ClearLastStatus();

    void ResetRuntimeData();

private:
    BatchAction m_action{ BatchAction::None };
    std::vector<BatchSlotTask> m_selected_slots;
    std::vector<BatchSlotTask> m_batch_slots;
    std::vector<BatchSlotTask> m_batch_completed_slots;
    size_t m_batch_current_index{ 0 };
    bool m_batch_waiting_state_change{ false };
    bool m_batch_seen_busy_state{ false };
    wxString m_last_batch_status;
    wxColour m_last_batch_status_color{ wxColour("#00AE42") };
    std::chrono::steady_clock::time_point m_batch_step_started_at{};
};

} // namespace Slic3r::GUI
