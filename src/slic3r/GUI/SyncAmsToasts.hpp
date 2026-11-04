#pragma once

/**
 * \file
 * \brief Transient toasts shown beside the sidebar around a printer / filament sync.
 */

#include <wx/gdicmn.h>

#include "BaseTransparentDPIFrame.hpp"

namespace Slic3r::GUI {

/**
 * \brief Toast after a printer-info sync, offering to go on and sync the filaments too.
 */
class SyncNozzleAndAmsDialog : public BaseTransparentDPIFrame
{
public:
    struct InputInfo
    {
        wxPoint dialog_pos{wxPoint(400, 200)};
        wxPoint ams_btn_pos{wxPoint(400, 200)};
    };
    SyncNozzleAndAmsDialog(InputInfo &input_info);
    ~SyncNozzleAndAmsDialog() override;
    void deal_ok() override;
    void deal_cancel() override;
    void update_info(InputInfo &info);

private:
    InputInfo m_input_info;
};

/**
 * \brief Toast confirming that the filaments were synced from the printer.
 */
class FinishSyncAmsDialog : public BaseTransparentDPIFrame
{
public:
    struct InputInfo
    {
        wxPoint dialog_pos{wxPoint(400, 200)};
        wxPoint ams_btn_pos{wxPoint(400, 200)};
    };
    FinishSyncAmsDialog(InputInfo &input_info);
    ~FinishSyncAmsDialog() override;
    void deal_ok();
    void update_info(InputInfo &info);

    /**
     * \brief Replace the toast's message and re-fit it.
     */
    void set_custom_message(const wxString &message);

    void set_disappearance_mode(DisappearanceMode mode) { m_timed_disappearance_mode = mode; }

private:
    InputInfo m_input_info;
};

} // namespace Slic3r::GUI
