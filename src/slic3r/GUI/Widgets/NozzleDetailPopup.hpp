#pragma once

#include <vector>

#include <wx/gdicmn.h>

#include "slic3r/GUI/Widgets/RoundedPopupWindow.hpp"

class wxBoxSizer;

namespace Slic3r::GUI {

struct NozzleConfig;

/** \brief Read-only hover card listing every nozzle's status. */
class NozzleDetailPopup : public RoundedPopupWindow
{
public:
    explicit NozzleDetailPopup(wxWindow *parent);

    /**
     * \brief Rebuild from the current data and show beside the panel whose screen rect is `anchor`.
     *
     * \param ext     per-nozzle configs for the extruder(s) to display
     * \param anchor  screen rect of the panel the tip is shown beside
     */
    void ShowTip(const std::vector<NozzleConfig> &ext, const wxRect &anchor);
    void HideTip();

private:
    void        rebuild(const std::vector<NozzleConfig> &ext);
    wxBoxSizer *build_row(int number, const NozzleConfig &cfg);

    bool m_dark{false};
};

} // namespace Slic3r::GUI
