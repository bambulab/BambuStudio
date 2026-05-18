#include "wgtDeviceFilamentHotendBatchFlow.h"

namespace Slic3r::GUI
{

void wgtDeviceFilamentHotendBatchFlow::SetAction(BatchAction action)
{
    m_action = action;
}

wgtDeviceFilamentHotendBatchFlow::BatchAction wgtDeviceFilamentHotendBatchFlow::GetAction() const
{
    return m_action;
}

void wgtDeviceFilamentHotendBatchFlow::UpdateSelectedSlots(const std::vector<BatchSlotTask>& slots)
{
    m_selected_slots = slots;
}

const std::vector<wgtDeviceFilamentHotendBatchFlow::BatchSlotTask>& wgtDeviceFilamentHotendBatchFlow::GetSelectedSlots() const
{
    return m_selected_slots;
}

void wgtDeviceFilamentHotendBatchFlow::SetBatchSlots(const std::vector<BatchSlotTask>& slots)
{
    m_batch_slots = slots;
}

const std::vector<wgtDeviceFilamentHotendBatchFlow::BatchSlotTask>& wgtDeviceFilamentHotendBatchFlow::GetBatchSlots() const
{
    return m_batch_slots;
}

std::vector<wgtDeviceFilamentHotendBatchFlow::BatchSlotTask>& wgtDeviceFilamentHotendBatchFlow::MutableBatchSlots()
{
    return m_batch_slots;
}

void wgtDeviceFilamentHotendBatchFlow::SetCompletedSlots(const std::vector<BatchSlotTask>& slots)
{
    m_batch_completed_slots = slots;
}

const std::vector<wgtDeviceFilamentHotendBatchFlow::BatchSlotTask>& wgtDeviceFilamentHotendBatchFlow::GetCompletedSlots() const
{
    return m_batch_completed_slots;
}

std::vector<wgtDeviceFilamentHotendBatchFlow::BatchSlotTask>& wgtDeviceFilamentHotendBatchFlow::MutableCompletedSlots()
{
    return m_batch_completed_slots;
}

void wgtDeviceFilamentHotendBatchFlow::SetCurrentIndex(size_t index)
{
    m_batch_current_index = index;
}

size_t wgtDeviceFilamentHotendBatchFlow::GetCurrentIndex() const
{
    return m_batch_current_index;
}

void wgtDeviceFilamentHotendBatchFlow::IncrementCurrentIndex()
{
    ++m_batch_current_index;
}

void wgtDeviceFilamentHotendBatchFlow::SetWaitingStateChange(bool waiting)
{
    m_batch_waiting_state_change = waiting;
}

bool wgtDeviceFilamentHotendBatchFlow::IsWaitingStateChange() const
{
    return m_batch_waiting_state_change;
}

void wgtDeviceFilamentHotendBatchFlow::SetSeenBusyState(bool seen_busy)
{
    m_batch_seen_busy_state = seen_busy;
}

bool wgtDeviceFilamentHotendBatchFlow::HasSeenBusyState() const
{
    return m_batch_seen_busy_state;
}

void wgtDeviceFilamentHotendBatchFlow::SetStepStartedAt(std::chrono::steady_clock::time_point started_at)
{
    m_batch_step_started_at = started_at;
}

std::chrono::steady_clock::time_point wgtDeviceFilamentHotendBatchFlow::GetStepStartedAt() const
{
    return m_batch_step_started_at;
}

void wgtDeviceFilamentHotendBatchFlow::SetLastStatus(const wxString& status_text, const wxColour& color)
{
    m_last_batch_status = status_text;
    m_last_batch_status_color = color;
}

const wxString& wgtDeviceFilamentHotendBatchFlow::GetLastStatus() const
{
    return m_last_batch_status;
}

const wxColour& wgtDeviceFilamentHotendBatchFlow::GetLastStatusColor() const
{
    return m_last_batch_status_color;
}

void wgtDeviceFilamentHotendBatchFlow::ClearLastStatus()
{
    m_last_batch_status.clear();
    m_last_batch_status_color = wxColour("#00AE42");
}

void wgtDeviceFilamentHotendBatchFlow::ResetRuntimeData()
{
    m_batch_slots.clear();
    m_batch_completed_slots.clear();
    m_batch_current_index = 0;
    m_batch_waiting_state_change = false;
    m_batch_seen_busy_state = false;
}

} // namespace Slic3r::GUI
