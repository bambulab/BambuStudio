#ifndef slic3r_wgtFilaManagerCsvUi_h_
#define slic3r_wgtFilaManagerCsvUi_h_

// UI glue for Filament Manager CSV import/export (bambulab/BambuStudio#10453):
// the thin wxWidgets layer the File > Import / Export menu items call. The pure
// parse/export/reconcile logic lives in wgtFilaManagerCsv.{h,cpp}.

class wxWindow;

namespace Slic3r { namespace GUI {

void export_filament_library_csv(wxWindow* parent);
void import_filament_library_csv(wxWindow* parent);

// Enable conditions: export needs the inventory loaded; import additionally
// needs the user signed in and connected (the cloud write path is reachable).
bool can_import_filament_library_csv();
bool can_export_filament_library_csv();

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerCsvUi_h_
