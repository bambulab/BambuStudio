#pragma once

#include <wx/panel.h>
#include "libslic3r/PrintConfig.hpp"
#include <cmath>
#include <vector>

namespace Slic3r::GUI {

class NozzleVariantButton;

class NozzleVariantSwitcher : public wxPanel
{
public:
    struct Option
    {
        int                      extruder_id = 0;
        float                    diameter_mm = 0.f;
        NozzleVolumeType         volume_type = NozzleVolumeType::nvtStandard;
        std::vector<std::string> nozzles;
        void                    *client_data = nullptr;

        bool operator==(const Option &o) const
        {
            constexpr float kDiameterEps = 1e-4f;
            return extruder_id == o.extruder_id && std::fabs(diameter_mm - o.diameter_mm) < kDiameterEps && volume_type == o.volume_type && nozzles == o.nozzles;
        }
        bool operator!=(const Option &o) const { return !(*this == o); }
    };

    NozzleVariantSwitcher(wxWindow *parent = nullptr, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = 0);
    ~NozzleVariantSwitcher();
    void SetOptions(const std::vector<Option> &options, bool show_diameter = true);

    unsigned int GetCount() const { return btns.size(); }
    int          GetSelection() const { return sel; }
    void         SetSelection(int index);

    Option GetOption(unsigned int index) const;

    // Mark a button as having modified parameters
    void SetButtonModified(unsigned int index, bool modified);

    void Rescale();

private:
    void button_clicked(wxCommandEvent &event);
    void update_button_styles();
    bool send_selection_event();
    void inherit_parent_background();
    void clear();

    std::vector<NozzleVariantButton *> btns;
    std::vector<Option>                m_options;
    int                                sel             = -1;
    bool                               m_show_diameter = true;
};

} // namespace Slic3r::GUI
