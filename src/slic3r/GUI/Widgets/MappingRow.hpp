#pragma once

#include <functional>

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
 * \brief One mapping row: [ source ] ⇢ [ destination ▾ ].
 *
 * Both sides are given as \ref FilamentInfo — the same currency the matcher and the sync dialog use;
 * the row derives everything it shows (colours, name, flow, slot label, brand, mismatch) from it.
 * Source and destination are rounded cells with a dashed connector between them. Only the
 * destination is interactive: clicking it fires \ref on_dest_activate to open the slot popup. A
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
     * \param dst         the destination; tray_id < 0 shows "Unmapped"
     * \param slot_label  the slot's name on the printer (e.g. "A1", "1"), which the slot ids alone cannot tell
     */
    void set_dest(const FilamentInfo &dst, const wxString &slot_label);
    void set_selected(bool selected);

    /**
     * \brief The destination cell, to anchor the slot popup under.
     */
    wxWindow *dest_anchor() const;

    std::function<void()> on_dest_activate; ///< fired when the destination cell is clicked

private:
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
