#pragma once

#include <functional>
#include <string>
#include <vector>

#include <wx/scrolwin.h>
#include <wx/window.h>

class Label; // Widgets/Label.hpp (global namespace)

namespace Slic3r {
struct FilamentInfo;
}

namespace Slic3r::GUI {

class FilamentView;
class PillView;
class CellBox;  // rounded bordered cell (source / destination), defined in MappingRow.cpp
class BrandView; // the Bambu mark or a vendor name, defined in MappingRow.cpp

/**
 * \brief A nozzle a destination slot can feed.
 */
struct NozzleChoice
{
    double      diameter{0.0};
    std::string volume_type; ///< e.g. "Standard"; empty when unknown

    /**
     * \brief Whether this is the nozzle of \p diameter and \p volume_type. Diameter and flow together
     *        name a nozzle; two nozzles of one diameter but different flows are different choices.
     */
    bool matches(double diameter, const std::string &volume_type) const;
};

/**
 * \brief One mapping row: [ source ] ⇢ [ destination ▾ ].
 *
 * Both sides are given as \ref FilamentInfo — the same currency the matcher and the sync dialog use;
 * the row derives everything it shows (colours, name, flow, slot label, brand, mismatch) from it.
 * Source and destination are rounded cells with a dashed connector between them. Only the
 * destination is interactive: clicking it fires \ref on_dest_activate to open the slot popup, and its
 * nozzle chip can open a nozzle menu (see \ref set_dest). A
 * destination whose \c tray_id is negative shows "Unmapped"; a nozzle-diameter mismatch flags the
 * destination flow chip; a selected destination is outlined in the accent colour.
 */
class MappingRow : public wxWindow
{
public:
    explicit MappingRow(wxWindow *parent);

    void set_source(const FilamentInfo &src);
    /**
     * \brief Show the destination slot, flagging a mismatch against the source.
     *
     * With two or more nozzle choices the destination's nozzle chip acts as a combobox: it shows a
     * chevron, and a click on it opens a menu of the choices that fires \ref on_nozzle_pick. Otherwise
     * the chip only shows the destination's nozzle, and a click on it opens the slot popup.
     *
     * \param dst         the destination; tray_id < 0 shows "Unmapped"
     * \param slot_label  the slot's name on the printer (e.g. "A1", "1"), which the slot ids alone cannot tell
     * \param choices     the nozzles the user can route \p dst to, one per diameter and flow; ignored when unmapped
     */
    void set_dest(const FilamentInfo &dst, const wxString &slot_label, std::vector<NozzleChoice> choices = {});
    void set_selected(bool selected);

    /**
     * \brief The destination cell, to anchor the slot popup under.
     */
    wxWindow *dest_anchor() const;

    std::function<void()>                     on_dest_activate; ///< fired when the destination cell is clicked
    std::function<void(const NozzleChoice &)> on_nozzle_pick;   ///< fired when the user picks a nozzle from the chip menu

private:
    /**
     * \brief Whether the nozzle chip acts as a menu: only with two or more choices, since a lone
     *        choice leaves nothing to pick.
     */
    bool can_choose_nozzle() const;
    void open_nozzle_menu();

    CellBox      *m_dst_box{nullptr};
    FilamentView *m_src_spool{nullptr};
    FilamentView *m_dst_spool{nullptr};
    BrandView    *m_src_brand{nullptr};
    BrandView    *m_dst_brand{nullptr};
    ::Label      *m_src_name{nullptr};
    ::Label      *m_dst_name{nullptr};
    ::Label      *m_dst_unmapped{nullptr};
    PillView     *m_src_size{nullptr};
    PillView     *m_dst_size{nullptr};
    double        m_src_diameter{0.0}; ///< remembered to flag a destination diameter mismatch
    NozzleChoice  m_dst_nozzle;        ///< remembered to tick the current choice in the nozzle menu
    std::vector<NozzleChoice> m_nozzle_choices;
};

/**
 * \brief Vertical list of \ref MappingRow that scrolls past ~5 rows.
 */
class MappingList : public wxScrolledWindow
{
public:
    explicit MappingList(wxWindow *parent);

    MappingRow *add_row();
    void        clear();
};

} // namespace Slic3r::GUI
