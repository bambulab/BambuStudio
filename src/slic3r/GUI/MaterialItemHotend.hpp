/*****************************************************************//**
 * \file   MaterialItemHotend.hpp
 * \brief  SelectMachine in-row filament button for hotend-rack layout.
 *
 * Hosts the rounded "color bar + spool + slot label + nozzle info"
 * widget rendered per filament inside the send-print dialog.
 *
 * Split out of AmsMappingHotend.hpp so the hotend mapping popup can be
 * web-ified independently without dragging this widget along.
 *
 * \author xin.zhang
 * \date   May 2026
 *********************************************************************/
#ifndef slic3r_GUI_MaterialItemHotend_hpp_
#define slic3r_GUI_MaterialItemHotend_hpp_

#include "AmsMappingPopup.hpp"

namespace Slic3r { namespace GUI {

class MaterialItemHotend : public MaterialItem
{
public:
    MaterialItemHotend(wxWindow *parent, wxColour mcolour, wxString mname, std::string filament_id);
    ~MaterialItemHotend() override;

    void msw_rescale() override;
    void set_nozzle_dia_and_type(const wxString& dia_type_info) override;

private:
    void render(wxDC& dc) override;
    void doRender(wxDC& dc) override;
    void messure_size() override;

private:
    wxString m_rack_nozzle_type;
    wxString m_nozzle_dia_and_type;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_MaterialItemHotend_hpp_
