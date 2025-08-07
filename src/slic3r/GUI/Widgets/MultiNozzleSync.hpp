#ifndef MULTI_NOZZLE_SYNC_HPP
#define MULTI_NOZZLE_SYNC_HPP

#include "../wxExtensions.hpp"
#include "../MsgDialog.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/MultiNozzleUtils.hpp"

namespace Slic3r {

class ManualNozzleCountDialog : public GUI::DPIDialog
{
public:
    ManualNozzleCountDialog(wxWindow *parent, int default_selection,int max_nozzle_count);
    ~ManualNozzleCountDialog() {};
    virtual void on_dpi_changed(const wxRect& suggested_rect) {};
    int GetNozzleCount() const;
private:
    wxChoice *m_choice;
    Button* m_confirm_btn;
};

void manuallySetNozzleCount(int extruder_id);

} // namespace Slic3r

#endif