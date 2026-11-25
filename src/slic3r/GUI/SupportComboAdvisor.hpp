#ifndef slic3r_GUI_SupportComboAdvisor_hpp_
#define slic3r_GUI_SupportComboAdvisor_hpp_

// Support-material combination advisor.
//
// A "support-material combination" is an interface filament that pairs with the model material in
// a way support_recommended_params.json has recommended parameters for (printer entries first,
// then the default entries). Two directions are built on that predicate:
//
//   forward  -- the user picks such an interface filament: offer the recommended parameters.
//   backward -- the combination stops holding: offer to put those parameters back to default.
//
// Everything here is free-standing: the predicate, the recommendation, and the dialog texts only
// need a config plus the global preset bundle / plater. The parts that actually write the
// parameters back stay on Tab, where the per-scope state (presets, object overrides) lives.

#include <optional>
#include <string>
#include <vector>

#include <wx/string.h>

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

class ConfigOption;
class ModelObject;

namespace GUI {

class ConfigManipulation;

// Everything compute_support_interface_recommendation() reports about one interface filament.
struct SupportComboRecommendation
{
    DynamicPrintConfig conf;                          // the recommended parameter values
    std::string        support_material_display_name; // its alias, or the type when it has none
    std::string        model_material_display_name;   // empty when the entry matches any model material
    bool               use_as_support_base             = false; // fit for the support base as well
    bool               thick_bridges_in_recommendation = false;
};

// Baseline for the "undo support material setup" detection: the config is already mutated to the
// new value by the time the change handler fires, so the *previous* combo state has to be cached.
// Each tab keeps its own, because each edits a different scope.
struct SupportComboBaseline
{
    int                interface_filament_id = -1; // -1 = never observed / default
    bool               was_combo             = false;
    DynamicPrintConfig applied_conf;
    // The combo predicate scans the current plate, so the baseline only means anything on the
    // plate it was taken on. Never prompt across a plate switch.
    int                plate_idx             = -1; // -1 = never observed

    void seed(int filament_id, int plate, bool found_recommendation, const DynamicPrintConfig &recommended_conf);

    // Keys still sitting at the value the recommendation set. The user may already have
    // hand-reverted some. This also drops support_filament once a delete has shifted indices --
    // the recorded value no longer matches, and we cannot tell if the new index means the same
    // physical filament.
    DynamicPrintConfig still_applied(const DynamicPrintConfig &current) const;
};

// Per-object combo state, captured right before a filament delete. A delete erases the object's
// support_interface_filament override when it named the deleted slot, so afterwards "had an
// override that was wiped" and "never had one" are indistinguishable. The snapshot is the only
// way to tell, and unlike the per-tab baseline it does not depend on anything being selected.
// Covers every plate, since a delete rewrites overrides project-wide.
void capture_object_support_combo_snapshot();

// Objects whose captured combination no longer holds and that still carry the recommended values
// as object-level overrides. keys_out collects the union of those keys. Consuming clears the
// snapshot, so a delete that raises no dialog cannot leak state into the next one.
std::vector<ModelObject *> take_objects_losing_support_combo(DynamicPrintConfig &keys_out);

// Looks up in the JSON table whether interface_filament_id constitutes a support-material combination with
// every model material in scope, and if so assembles the full set of support-specific parameter
// recommendations (including the support_filament / thick_bridges follow-on tweaks). config supplies the
// scope's current values (thick_bridges, support_top_z_distance). The scope is objects when given, otherwise
// every object on one plate: plate_idx selects it, -1 = the current one. Returns false if no recommendation applies.
// require_interface: no support interface layers means no recommendation; a support base check passes false,
// since whether a filament suits the support base does not depend on the interface.
bool compute_support_interface_recommendation(const DynamicPrintConfig &config, int interface_filament_id,
                                              SupportComboRecommendation &out, int plate_idx = -1,
                                              const std::vector<ModelObject *> &objects = {},
                                              bool require_interface = true);

// Index of the plate compute_support_interface_recommendation() scans when passed -1.
int current_support_combo_plate_index();

// "<label>: <value><unit>", with filament slots rendered as "<slot index> <filament type>" the way
// the support filament combo box does. Empty when the key has no config definition.
wxString generate_support_param_description(const std::string &key, const ConfigOption *opt);

// Header plus one line per key of conf, closed by the apply question. Shared by the support base
// and support interface suggestions, so that both dialogs read alike.
wxString support_recommendation_dialog_text(const SupportComboRecommendation &rec, const DynamicPrintConfig &conf);

// Object name for dialog text, truncated so a long one cannot blow out the dialog width.
wxString shorten_object_name(const std::string &name);

// Whose settings the "exiting support material usage" dialog is about. The wording has to name
// it: a global reset and releasing an object's locks look the same in the dialog otherwise.
enum class SupportRevertLead {
    Global,    // the global print settings
    Plate,     // the current plate's
    Selection, // whatever the user has selected
    Objects,   // the objects listed in the body, with nothing selected
};

// Body of the "you are exiting support material usage" dialog. listed_objects is named in the
// body when non-empty. offer_choice then makes the closing question a "the selection only, or all
// of them?" choice, which the caller has to back with the matching buttons; false asks about the
// whole list at once. also_global adds a line saying the global copies of the same parameters go
// too, for the filament-delete dialog that covers both scopes in one answer. Pass lead =
// Global/Objects instead whenever only that one scope is actually restored, so the dialog never
// promises a change it will not make.
wxString build_support_revert_dialog_text(SupportRevertLead lead, const DynamicPrintConfig &still_applied_conf,
                                          const std::vector<ModelObject *> &listed_objects,
                                          bool offer_choice = false, bool also_global = false);

// Show Suggestion dialog recommending thick_bridges = Off. Returns true if the dialog was shown.
// top_z_distance replaces the config's support_top_z_distance when the gap in force lives elsewhere.
bool suggest_disable_thick_bridges_if_needed(DynamicPrintConfig *config, ConfigManipulation &config_manipulation,
                                             std::optional<double> top_z_distance = std::nullopt);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_SupportComboAdvisor_hpp_
