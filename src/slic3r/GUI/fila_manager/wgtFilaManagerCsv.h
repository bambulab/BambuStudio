#ifndef slic3r_wgtFilaManagerCsv_h_
#define slic3r_wgtFilaManagerCsv_h_

// Filament Manager CSV import/export (bambulab/BambuStudio#10453). Pure,
// UI-free logic, unit-testable without wxWidgets or the cloud.
//
// Identity is the `id` column (== FilamentSpool::spool_id):
//   - no id -> create;  id matches a spool -> update;  id matches nothing -> error
//   - `deleted` truthy -> delete (requires a valid id)
// A spool absent from the file is left alone; deletion is explicit only. Bad
// rows are reported by 1-based source line and skipped; good rows still apply.
// The CSV `setting_id` column maps to FilamentSpool::filament_id.

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "wgtFilaManagerStore.h"   // FilamentSpool

namespace Slic3r { namespace GUI {

// Importable columns, in order. These are the only headers the importer reads.
const std::vector<std::string>& filament_csv_columns();

// Reference-only fields the backend does not persist; appended only in an
// "all fields" export, each with a caller-supplied suffix.
const std::vector<std::string>& filament_csv_readonly_columns();

// Serialize an inventory to CSV. With `include_readonly`, the read-only columns
// are appended, each header given `readonly_suffix` so a re-import skips them.
std::string export_spools_to_csv(const std::vector<FilamentSpool>& spools,
                                  bool               include_readonly = false,
                                  const std::string& readonly_suffix  = std::string());

struct CsvRow {
    int                                line = 0;   // 1-based source line
    std::map<std::string, std::string> cells;      // column -> trimmed value
    std::string get(const std::string& col) const;
};

struct ParsedCsv {
    std::vector<std::string> header;
    std::vector<CsvRow>      rows;
};

// Quote-, comma- and newline-aware reader. Blank lines are dropped; the first
// record is the header.
ParsedCsv parse_csv(const std::string& text);

struct RowError {
    int         row;        // 1-based source line
    std::string reason;
};

struct PendingDelete {
    std::string spool_id;
    std::string label;      // human label for the confirm UI
};

// Reconciled plan: `creates` / `updates` are JSON patches for the cloud
// dispatcher; each update carries its "spool_id".
struct ImportPlan {
    std::vector<nlohmann::json> creates;
    std::vector<nlohmann::json> updates;
    std::vector<PendingDelete>  deletes;
    std::vector<RowError>       errors;

    bool empty_plan() const
    { return creates.empty() && updates.empty() && deletes.empty(); }
};

ImportPlan reconcile_import(const ParsedCsv&                   parsed,
                            const std::vector<FilamentSpool>&  existing);

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerCsv_h_
