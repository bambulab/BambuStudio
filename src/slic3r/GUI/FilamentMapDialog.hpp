#pragma once

#include <vector>
#include "CapsuleButton.hpp"
#include "libslic3r/PrintConfig.hpp"

class Button;
class wxScrolledWindow;

namespace Slic3r {
class Print;
}

namespace Slic3r::GUI {
class Plater;
class PartPlate;
class FilamentMapPanel;
class SmartFilamentPanel;

/**
 * @brief Try to pop up the filament map dialog before slicing.
 * 
 * Only pop up in multi extruder machines. If user don't want the pop up, we
 * pop up if the applied filament map mode in manual
 * 
 * @param is_slice_all  In slice all
 * @param plater_ref Plater to get/set global filament map
 * @param partplate_ref Partplate to get/set plate filament map mode
 * @return whether continue slicing
*/
bool try_pop_up_before_slice(bool is_slice_all, Plater* plater_ref, PartPlate* partplate_ref, bool force_pop_up = false);
std::vector<FilamentMapMode> resolve_available_auto_modes(Print *print_obj, const std::vector<FilamentMapMode> &requested_modes, bool machine_synced);
/**
 * @brief Mode to use when the stored one cannot be used right now.
 *
 * An auto mode falls back to the first available mode when it is not available, or when it is
 * the convenience mode and the printer is not synced. Other modes are returned unchanged.
 *
 * @param mode            Stored mode, fmmNozzleManual already mapped to fmmManual
 * @param available_modes Modes that can be chosen now; the first one is the fallback
 * @param machine_synced  Whether the selected printer matches the printer preset
 */
FilamentMapMode resolve_fallback_filament_map_mode(FilamentMapMode mode, const std::vector<FilamentMapMode> &available_modes, bool machine_synced);
/**
 * @brief Translate a custom-panel result into the maps the backend expects.
 *
 * On single-extruder multi-nozzle machines filament_map stays all-1 (there is only one real extruder)
 * and the panel's AMS/fixed choice (1 = AMS nozzle, 2 = fixed nozzle) moves into filament_nozzle_map.
 *
 * @param dialog_maps       What the custom panel returned
 * @param out_filament_maps Receives the all-1 filament map
 * @param out_nozzle_maps   Receives the per filament nozzle index
 * @return false on any other machine, where dialog_maps is already the extruder id and the outputs are
 *         left untouched
 */
bool build_multi_nozzle_manual_maps(const ConfigBase       &config,
                                    const std::vector<int> &dialog_maps,
                                    std::vector<int>       &out_filament_maps,
                                    std::vector<int>       &out_nozzle_maps);

class FilamentMapDialog : public wxDialog
{
    enum PageType { ptAuto, ptManual };

public:
    FilamentMapDialog(wxWindow *parent,
        const std::vector<std::string>& filament_type,
        const std::vector<int> &filament_map,
        const std::vector<int> &filament_volume_map,
        const std::vector<int> &filaments,
        const FilamentMapMode mode,
        bool machine_synced,
        bool show_default=true,
        bool with_checkbox = false,
        const std::vector<FilamentMapMode>& available_modes = {}
    );

    FilamentMapMode get_mode();
    std::vector<int> get_filament_maps() const {
        if (m_page_type == PageType::ptManual)
            return m_filament_map;
        return {};
    }

    std::vector<int> get_filament_volume_maps() const {
        if (m_page_type == PageType::ptManual)
            return m_filament_volume_map;
        return {};
    }

    int ShowModal() override;

private:
    void make_header(wxBoxSizer *sizer, bool only_saving_mode, bool single_default_mode, bool show_manual_mode);
    void make_body(wxBoxSizer                         *sizer,
                   bool                                only_saving_mode,
                   bool                                single_default_mode,
                   bool                                is_multi_nozzle_mode,
                   bool                                show_manual_mode,
                   const std::vector<FilamentMapMode> &modes_to_use,
                   const std::vector<int>             &filaments,
                   const FilamentMapMode               mode,
                   bool                                machine_synced);
    void make_footer(wxBoxSizer *sizer, const FilamentMapMode mode);

    void on_ok(wxCommandEvent &event);
    void on_cancle(wxCommandEvent &event);
    void on_switch_mode(wxCommandEvent &event);
    void on_smart_filament_checkbox(wxCommandEvent &event);

    void update_panel_status(PageType page);
    void constrain_dialog_height();

 private:
     FilamentMapPanel   *m_auto_panel{};
     FilamentMapPanel   *m_manual_panel{};
     wxScrolledWindow   *m_manual_scroll{};
     SmartFilamentPanel *smart_filament{};

     CapsuleButton *m_auto_btn{};
     CapsuleButton *m_manual_btn{};

     Button *m_ok_btn{};
     Button *m_cancel_btn{};

     PageType m_page_type;
     bool     m_fila_switch_ready{false};
     bool     m_auto_mode_available{true};

     std::vector<int>         m_filament_map;
     std::vector<int>         m_filament_volume_map;
     std::vector<std::string> m_filament_type;
};

} // namespace Slic3r::GUI