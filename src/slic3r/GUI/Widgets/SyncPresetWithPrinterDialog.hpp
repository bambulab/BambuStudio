#ifndef slic3r_GUI_SyncPresetWithPrinterDialog_hpp_
#define slic3r_GUI_SyncPresetWithPrinterDialog_hpp_

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <wx/string.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"

class ComboBox;
class CheckBox;
class Button;

namespace Slic3r {
class MachineObject;

namespace GUI {

// One side of the comparison. `volume` is unread unless compare_flow_type is set.
struct NozzleSpec
{
    float diameter{0.f};
    NozzleVolumeType volume{nvtStandard};

    bool same_diameter(const NozzleSpec &other) const { return std::abs(diameter - other.diameter) < EPSILON; }

    bool operator==(const NozzleSpec &other) const { return same_diameter(other) && volume == other.volume; }
};

// "0.4 Standard" when the flow type is part of the comparison, plain "0.4" when it is not.
wxString format_nozzle_spec_label(const NozzleSpec &spec, bool with_flow_type);

// What the nozzle section of the dialog shows. Confirm stays enabled in all four.
enum class NozzleSectionKind
{
    None,        // printer already carries everything; section hidden
    Notice,      // one candidate only; a sentence states the forced outcome
    Unavailable, // machine reported no usable nozzle; a sentence says so
    Table,       // one row per project nozzle, each with a dropdown
};

// Everything the dialog renders. The caller assembles it up front; the dialog itself neither reads
// the machine nor touches presets.
struct PrinterMigrationInfo
{
    // Model table: project's printer preset on the left, connected printer on the right, read-only.
    std::string source_model_id;
    wxString source_model_name;
    std::string target_model_id;
    wxString target_model_name;
    wxString target_dev_name;

    // True only when the project runs multi-diameter AND the target has a nozzle rack; otherwise the
    // whole comparison is on diameters alone. Safe to drop the flow type there because it never
    // picks the filament preset - that is material plus model plus diameter - it only selects one of
    // the preset's own per-variant entries.
    bool compare_flow_type{false};

    // Per filament slot; a zero diameter marks a slot with no usable spec. A fully covered migration
    // replays this verbatim.
    std::vector<NozzleSpec> slot_nozzles;

    // Table rows, deduplicated in whatever unit compare_flow_type selects. Collapses to a single row
    // covering every slot when the flow type is out, since only one diameter can then be picked.
    std::vector<NozzleSpec> project_nozzles;
    // Slots behind each row, and each row's left-cell label. Both parallel to project_nozzles.
    std::vector<std::vector<size_t>> row_slots;
    std::vector<wxString> row_labels;

    // Dropdown candidates: every nozzle the printer carries, both extruders merged (the project side
    // does not distinguish them). Empty when the machine reported nothing usable.
    std::vector<NozzleSpec> printer_nozzles;

    NozzleSectionKind section_kind{NozzleSectionKind::None};

    // Lets the caller skip the dialog entirely when the model matches too - nothing would change.
    bool nozzles_fully_covered{false};

    // Only the 3MF import path offers to stop checking on future loads.
    bool offer_skip_future_checks{false};
};

// The confirmed migration, handed from whichever path showed the dialog to the single place that
// applies it. Per filament slot rather than per row, because one row can stand for several slots and
// a fully covered printer replays each slot's own spec instead of collapsing them. A zero diameter
// means "leave this slot alone".
struct PendingNozzleMigration
{
    std::vector<NozzleSpec> per_slot;
    // False when the dialog never asked about flow types, so the migration must leave them to the
    // values sync_extruder_volume_type() derives from the machine.
    bool apply_flow_type{false};
};

// Best stand-in for `want` among `candidates`, or -1 when there is nothing to choose from. Diameter
// dominates (distance first, coarser nozzle on a tie); the flow type only breaks ties between equal
// diameters, and is skipped when `with_flow_type` is false.
//
// Deliberately not get_best_diameter() from ExtruderHelpers: that answers "which diameter does this
// machine have most of" and ignores the wanted value.
int find_closest_nozzle(const NozzleSpec &want, const std::vector<NozzleSpec> &candidates, bool with_flow_type);

// Assemble what the dialog needs from the current project config plus a connected machine. Nullopt
// when either side cannot be described, so the caller can fall back instead of showing an empty
// comparison.
std::optional<PrinterMigrationInfo> collect_printer_migration_info(MachineObject *obj, bool offer_skip_future_checks);

// Lets the user confirm how to migrate the project onto the connected printer. Shared by the manual
// Sync button and the post-3MF-import check; they differ only in the "don't check again" checkbox.
class SyncPresetWithPrinterDialog : public DPIDialog
{
public:
    SyncPresetWithPrinterDialog(wxWindow *parent, PrinterMigrationInfo info);

    void on_dpi_changed(const wxRect &) override {}

    // The printer-side nozzle picked for each row, parallel to PrinterMigrationInfo::project_nozzles.
    std::vector<NozzleSpec> GetChosenNozzles() const;

    // The same choices spread onto the slots they apply to. Only meaningful after wxID_OK.
    PendingNozzleMigration GetConfirmedMigration() const;

    // Always false on the manual sync path, which does not show the checkbox.
    bool SkipFutureChecks() const;

private:
    using TableFactory = std::function<wxWindow *(wxWindow *)>;

    void build_layout();
    wxWindow *build_section(const wxString &label, const TableFactory &make_table);
    wxWindow *build_model_table(wxWindow *parent);
    wxWindow *build_nozzle_table(wxWindow *parent);
    wxWindow *build_nozzle_notice(wxWindow *parent);
    wxWindow *build_footer(wxWindow *parent);

    wxWindow *make_model_cell(wxWindow *parent, const std::string &model_id, const wxString &model_name, const wxString &dev_name);

    PrinterMigrationInfo m_info;
    // one per project nozzle row
    std::vector<ComboBox *> m_nozzle_combos;
    CheckBox *m_skip_check{nullptr};
    Button *m_confirm_btn{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_SyncPresetWithPrinterDialog_hpp_
