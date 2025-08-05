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
    ManualNozzleCountDialog(wxWindow *parent, int default_selection);
    ~ManualNozzleCountDialog() {};
    virtual void on_dpi_changed(const wxRect& suggested_rect) {};
    int GetNozzleCount() const;
private:
    wxChoice *m_choice;
    Button* m_confirm_btn;
};

void manuallySetNozzleCount(int extruder_id);

void setNozzleCountToAppConf(int extruder_id,const std::vector<MultiNozzleUtils::NozzleGroupInfo> &nozzle_info);

int getNozzleCountFromAppConf(const DynamicConfig &config, int extruder_id, double nozzle_diameter, NozzleVolumeType type);

} // namespace Slic3r

#endif