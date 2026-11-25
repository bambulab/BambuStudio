#include "SupportComboAdvisor.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/SupportRecommendedParams.hpp"

#include "ConfigManipulation.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"

namespace Slic3r {
namespace GUI {

// 生成参数描述文案
wxString generate_support_param_description(const std::string& key, const ConfigOption* opt)
{
    const ConfigOptionDef* def = print_config_def.get(key);
    if (!def) return wxString();

    wxString label = def->label.empty() ? wxString::FromUTF8(key) : _L(def->label);
    if (key == "support_interface_speed") {
        label = _L("Support interface speed");
    }
    // Filament slots are shown as "<slot index> <filament type>", the same way as in the support filament combo box
    if (key == "support_filament" || key == "support_interface_filament") {
        auto *int_opt = dynamic_cast<const ConfigOptionInt *>(opt);
        if (!int_opt) return wxString();
        if (int_opt->value <= 0) return wxString::Format("%s: %s", label, _L("Default"));

        wxString slot_str         = wxString::Format("%d", int_opt->value);
        auto    &filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
        auto    &filaments        = Slic3r::GUI::wxGetApp().preset_bundle->filaments;
        if (int_opt->value <= static_cast<int>(filament_presets.size())) {
            Slic3r::Preset *filament = filaments.find_preset(filament_presets[int_opt->value - 1]);
            if (filament)
                slot_str += " " + wxString::FromUTF8(filament->config.option<ConfigOptionStrings>("filament_type")->values[0]);
        }
        return wxString::Format("%s: %s", label, slot_str);
    }
    wxString value_str;

    switch (def->type) {
        case coBool: {
            auto* bool_opt = dynamic_cast<const ConfigOptionBool*>(opt);
            value_str = (bool_opt && bool_opt->value) ? _L("On") : _L("Off");
            break;
        }
        case coEnum: {
            std::string serialized = opt->serialize();
            value_str = wxString::FromUTF8(serialized);
            if (!def->enum_values.empty() && !def->enum_labels.empty()) {
                for (size_t i = 0; i < def->enum_values.size(); ++i) {
                    if (def->enum_values[i] == serialized && i < def->enum_labels.size()) {
                        value_str = _L(def->enum_labels[i]);
                        break;
                    }
                }
            }
            break;
        }
        case coInt: {
            value_str = wxString::FromUTF8(opt->serialize());
            break;
        }
        default: {
            value_str = wxString::FromUTF8(opt->serialize());
            break;
        }
    }

    wxString unit_str;
    if (!def->sidetext.empty()) {
        unit_str = " " + _L(def->sidetext);
    }

    return wxString::Format("%s: %s%s", label, value_str, unit_str);
}

static bool is_zero_gap_distance(double distance)
{
    return std::abs(distance) <= EPSILON;
}

// See the declaration in SupportComboAdvisor.hpp for the contract.
wxString support_recommendation_dialog_text(const SupportComboRecommendation &rec, const DynamicPrintConfig &conf)
{
    wxString msg_text = rec.model_material_display_name.empty() ?
        _L("When using support material for the support interface, We recommend the following settings:") :
        wxString::Format(_L("When using %s to support %s, We recommend the following settings:"),
                         wxString::FromUTF8(rec.support_material_display_name), wxString::FromUTF8(rec.model_material_display_name));
    msg_text += "\n\n";
    for (const auto &key : conf.keys()) {
        const ConfigOption *opt = conf.option(key);
        if (!opt)
            continue;
        wxString desc = generate_support_param_description(key, opt);
        // Wide literal: a narrow U+2022 escape would be UTF-8 bytes under
        // /utf-8, then re-decoded by wxConvLibc with the system ANSI code page.
        if (!desc.empty())
            msg_text += L"  \u2022 " + desc + "\n";
    }
    msg_text += "\n" + _L("Do you want to apply these settings?");
    return msg_text;
}

static std::string filament_type_of_index(int filament_idx_0based)
{
    auto &filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    auto &filaments        = Slic3r::GUI::wxGetApp().preset_bundle->filaments;
    if (filament_idx_0based < 0 || filament_idx_0based >= static_cast<int>(filament_presets.size()))
        return {};
    Slic3r::Preset *filament = filaments.find_preset(filament_presets[filament_idx_0based]);
    if (!filament)
        return {};
    auto *type_opt = filament->config.option<ConfigOptionStrings>("filament_type");
    if (!type_opt || type_opt->values.empty())
        return {};
    return type_opt->values[0];
}

// True when support interface material differs from the model body, or is a dedicated support/soluble filament.
// Note: do not rely on has_filaments() alone - volumes with default extruder (0) are often omitted there,
// which would miss common cases like PETG model + PLA interface.
static bool is_dissimilar_or_support_interface_filament(int interface_filament_id)
{
    if (interface_filament_id < 0)
        return false;
    if (is_support_filament(interface_filament_id, false) || is_soluble_filament(interface_filament_id))
        return true;

    const std::string interface_type = filament_type_of_index(interface_filament_id);
    if (interface_type.empty())
        return false;

    if (!Slic3r::GUI::wxGetApp().plater())
        return false;

    for (const ModelObject *mo : Slic3r::GUI::wxGetApp().plater()->model().objects) {
        if (!mo)
            continue;
        for (const ModelVolume *vol : mo->volumes) {
            if (!vol || !vol->is_model_part())
                continue;
            // extruder_id() already falls back to object/default filament (1-based).
            const int eid = vol->extruder_id();
            if (eid <= 0)
                continue;
            const std::string model_type = filament_type_of_index(eid - 1);
            if (!model_type.empty() && model_type != interface_type)
                return true;
        }
    }
    return false;
}

// Zero-gap support interface that uses a dissimilar / support / soluble material as the contact surface.
static bool is_zero_gap_dissimilar_support_interface(const DynamicPrintConfig &config, std::optional<double> top_z_distance)
{
    if (!config.has("enable_support") || !config.has("support_top_z_distance") ||
        !config.has("support_interface_filament") || !config.has("enforce_support_layers"))
        return false;

    const bool support_enabled =
        config.opt_bool("enable_support") || config.opt_int("enforce_support_layers") > 0;
    if (!support_enabled)
        return false;
    if (!is_zero_gap_distance(top_z_distance ? *top_z_distance : config.opt_float("support_top_z_distance")))
        return false;

    const int interface_filament_id = config.opt_int("support_interface_filament") - 1;
    return is_dissimilar_or_support_interface_filament(interface_filament_id);
}

static bool should_suggest_disable_thick_bridges(const DynamicPrintConfig &config, std::optional<double> top_z_distance)
{
    return config.has("thick_bridges") && config.opt_bool("thick_bridges") &&
           is_zero_gap_dissimilar_support_interface(config, top_z_distance);
}

// Show Suggestion dialog recommending thick_bridges = Off. Returns true if the dialog was shown.
bool suggest_disable_thick_bridges_if_needed(DynamicPrintConfig *config, ConfigManipulation &config_manipulation,
                                             std::optional<double> top_z_distance)
{
    if (!config || !should_suggest_disable_thick_bridges(*config, top_z_distance))
        return false;

    DynamicPrintConfig recommended_conf;
    recommended_conf.set_key_value("thick_bridges", new ConfigOptionBool(false));

    DynamicPrintConfig filtered_conf;
    for (const auto &key : recommended_conf.keys()) {
        const ConfigOption *current_opt = config->option(key);
        const ConfigOption *new_opt     = recommended_conf.option(key);
        if (current_opt && new_opt && current_opt->serialize() != new_opt->serialize())
            filtered_conf.set_key_value(key, new_opt->clone());
    }
    if (filtered_conf.empty())
        return false;

    wxString msg_text = _L("When using support material for the support interface, We recommend the following settings:");
    msg_text += "\n\n";
    // The bullet must be a wide literal: under /utf-8 a narrow U+2022 escape is UTF-8 bytes,
    // which wxString then re-decodes with wxConvLibc (system ANSI code page) and garbles.
    for (const auto &key : filtered_conf.keys()) {
        const ConfigOption *opt = filtered_conf.option(key);
        if (!opt)
            continue;
        wxString desc = generate_support_param_description(key, opt);
        if (!desc.empty())
            msg_text += L"  \u2022 " + desc + "\n";
    }
    msg_text += "\n" + _L("Do you want to apply these settings?");

    MessageDialog dialog(wxGetApp().plater(), msg_text, "Suggestion", wxICON_WARNING | wxYES | wxNO);
    if (dialog.ShowModal() == wxID_YES)
        config_manipulation.apply(config, &filtered_conf);
    wxGetApp().plater()->update();
    return true;
}

// See the declaration in SupportComboAdvisor.hpp for the contract.
bool compute_support_interface_recommendation(const DynamicPrintConfig &config, int interface_filament_id,
                                              SupportComboRecommendation &out, int plate_idx,
                                              const std::vector<ModelObject *> &objects, bool require_interface)
{
    out = SupportComboRecommendation();
    if (require_interface && config.has("support_interface_top_layers") && config.opt_int("support_interface_top_layers") <= 0)
        return false;

    DynamicPrintConfig &recommended_conf = out.conf;

    // 获取支撑接触面耗材信息
    auto &filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    auto &filaments        = Slic3r::GUI::wxGetApp().preset_bundle->filaments;
    const int slot_count   = static_cast<int>(filament_presets.size());

    // One preset lookup per filament slot: the plate scan below hits the same few slots once per
    // volume-extruder pair, which adds up on a crowded plate.
    std::vector<Slic3r::Preset *> preset_by_slot(slot_count, nullptr);
    std::vector<bool>             slot_resolved(slot_count, false);
    auto preset_for_slot = [&](int idx) -> Slic3r::Preset * {
        if (idx < 0 || idx >= slot_count)
            return nullptr;
        if (!slot_resolved[idx]) {
            preset_by_slot[idx] = filaments.find_preset(filament_presets[idx]);
            slot_resolved[idx]  = true;
        }
        return preset_by_slot[idx];
    };

    if (Slic3r::Preset *filament = preset_for_slot(interface_filament_id)) {
        const std::string alias = filaments.get_preset_alias(*filament, true);
        out.support_material_display_name = alias.empty() ? filament->config.option<ConfigOptionStrings>("filament_type")->values[0] : alias;
    }

    SupportMaterialCombination combination;

    // 收集模型耗材：指定了对象时只看这些对象，否则看目标盘上的所有对象
    std::vector<ModelObject *> model_objects = objects;
    if (model_objects.empty()) {
        auto &plate_list = Slic3r::GUI::wxGetApp().plater()->get_partplate_list();
        // The baseline callers can reach this while the plate list is being rebuilt.
        PartPlate *plate = nullptr;
        if (plate_idx < 0)
            plate = plate_list.get_plate_count() > 0 ? plate_list.get_curr_plate() : nullptr;
        else if (plate_idx < plate_list.get_plate_count())
            plate = plate_list.get_plate(plate_idx);
        if (plate == nullptr)
            return false;
        model_objects = plate->get_objects_on_this_plate();
    }

    // Every distinct model material in scope, in first-seen order.
    std::vector<SupportMaterialProperties> model_materials;
    std::set<std::pair<std::string, std::string>> seen_materials;
    const GlobalFeatureFilaments global_features = GlobalFeatureFilaments::from_config(config, slot_count);
    auto collect_explicit_filaments = [](const auto &source, std::set<int> &filament_ids) {
        for (const char *key : {"extruder", "wall_filament", "sparse_infill_filament", "solid_infill_filament"}) {
            if (source.has(key)) {
                const int filament_id = source.option(key)->getInt();
                if (filament_id > 0)
                    filament_ids.insert(filament_id);
            }
        }
    };
    for (ModelObject *obj : model_objects) {
        if (!obj) continue;
        std::set<int> filament_ids;
        for (const auto *vol : obj->volumes) {
            if (!vol) continue;
            if (vol->is_model_part()) {
                for (int filament_id : vol->get_extruders(global_features))
                    if (filament_id > 0)
                        filament_ids.insert(filament_id);
            } else if (vol->is_modifier())
                collect_explicit_filaments(vol->config, filament_ids);
        }
        for (const auto &layer_range : obj->layer_config_ranges)
            collect_explicit_filaments(layer_range.second, filament_ids);
        for (int filament_id : filament_ids) {
            Slic3r::Preset *fp = preset_for_slot(filament_id - 1);
            if (!fp) continue;
            const std::string &ft = fp->config.option<ConfigOptionStrings>("filament_type")->values[0];
            const std::string material_name = support_recommended_material_name(
                filaments.get_preset_alias(*fp, true));
            if (seen_materials.emplace(material_name, ft).second)
                model_materials.push_back({material_name, ft});
        }
    }

    // 与多口径切片同一查法：每种主体料先查机型条目再查默认条目，须全部命中
    size_t matched = 0;
    if (!query_support_recommended_params_for_combination(interface_filament_id, model_materials,
            recommended_conf, combination, matched))
        return false;
    // 弹窗显示匹配上的主体料：按名称匹配用名称，按类型匹配用类型，任意主体料不显示
    const std::string &how = combination.recommended_params->model_material_type;
    out.model_material_display_name = how == "name" ? model_materials[matched].name :
                                      how == "type" ? model_materials[matched].type : std::string();

    out.use_as_support_base = combination.use_as_support_base;
    // A filament fit for the support base is recommended for it as well
    if (combination.use_as_support_base)
        recommended_conf.set_key_value("support_filament", new ConfigOptionInt(interface_filament_id + 1));

    // Zero-gap support interface + thick bridges: also recommend turning thick bridges off
    // so it appears in the same Suggestion dialog instead of a second popup.
    if (config.has("thick_bridges") && config.opt_bool("thick_bridges")) {
        bool will_be_zero_gap = false;
        if (const ConfigOptionFloat *top_z_opt =
                recommended_conf.option<ConfigOptionFloat>("support_top_z_distance"))
            will_be_zero_gap = is_zero_gap_distance(top_z_opt->value);
        else if (config.has("support_top_z_distance"))
            will_be_zero_gap = is_zero_gap_distance(config.opt_float("support_top_z_distance"));
        if (will_be_zero_gap) {
            recommended_conf.set_key_value("thick_bridges", new ConfigOptionBool(false));
            out.thick_bridges_in_recommendation = true;
        }
    }

    return true;
}

// See the declaration in SupportComboAdvisor.hpp for the contract.
int current_support_combo_plate_index()
{
    if (auto *plater = Slic3r::GUI::wxGetApp().plater())
        return plater->get_partplate_list().get_curr_plate_index();
    return -1;
}

// -----------------------------------------------------------------------------------------------
// SupportComboBaseline
// -----------------------------------------------------------------------------------------------

void SupportComboBaseline::seed(int filament_id, int plate, bool found_recommendation,
                                const DynamicPrintConfig &recommended_conf)
{
    interface_filament_id = filament_id;
    was_combo             = found_recommendation;
    applied_conf          = found_recommendation ? recommended_conf : DynamicPrintConfig();
    plate_idx             = plate;
}

DynamicPrintConfig SupportComboBaseline::still_applied(const DynamicPrintConfig &current) const
{
    DynamicPrintConfig out;
    for (const auto &key : applied_conf.keys()) {
        const ConfigOption *cur_opt = current.option(key);
        const ConfigOption *was_opt = applied_conf.option(key);
        if (cur_opt && was_opt && cur_opt->serialize() == was_opt->serialize())
            out.set_key_value(key, was_opt->clone());
    }
    return out;
}

// -----------------------------------------------------------------------------------------------
// Pre-delete object snapshot
// -----------------------------------------------------------------------------------------------

namespace {

struct ObjectComboSnapshot
{
    ObjectID           object_id;
    DynamicPrintConfig applied_conf;
};

std::vector<ObjectComboSnapshot> s_object_combo_snapshot;

// The global values an object's own overrides sit on top of. Taken from the edited print preset
// rather than the Print tab, so this file stays free of Tab.
const DynamicPrintConfig &global_print_config()
{
    return wxGetApp().preset_bundle->prints.get_edited_preset().config;
}

// Effective interface filament of one object, 0-based, falling back to the global value.
int object_interface_filament_id(const ModelObject &obj, int global_interface_filament)
{
    static const std::string key = "support_interface_filament";
    return (obj.config.has(key) ? obj.config.opt_int(key) : global_interface_filament) - 1;
}

// The recommendation for one object, judged on its own model materials with its effective config.
// Empty result = not a support-material combination.
DynamicPrintConfig object_recommendation(ModelObject &obj, const DynamicPrintConfig &global_conf, int global_interface)
{
    DynamicPrintConfig conf = global_conf;
    conf.apply(obj.config.get(), true);
    SupportComboRecommendation rec;
    if (!compute_support_interface_recommendation(conf, object_interface_filament_id(obj, global_interface), rec, -1, {&obj}))
        return {};
    return rec.conf;
}

} // namespace

// See the declaration in SupportComboAdvisor.hpp for the contract.
void capture_object_support_combo_snapshot()
{
    s_object_combo_snapshot.clear();

    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;

    const DynamicPrintConfig &global_conf = global_print_config();
    if (!global_conf.has("support_interface_filament"))
        return;
    const int global_interface = global_conf.opt_int("support_interface_filament");

    // Every object, not just the current plate's: a filament delete rewrites overrides project-wide.
    for (ModelObject *obj : plater->model().objects) {
        if (obj == nullptr)
            continue;
        DynamicPrintConfig rec_conf = object_recommendation(*obj, global_conf, global_interface);
        // Not a combination now, so there is nothing this delete could take away.
        if (!rec_conf.empty())
            s_object_combo_snapshot.push_back({obj->id(), std::move(rec_conf)});
    }
}

// See the declaration in SupportComboAdvisor.hpp for the contract.
std::vector<ModelObject *> take_objects_losing_support_combo(DynamicPrintConfig &keys_out)
{
    std::vector<ObjectComboSnapshot> snapshot;
    snapshot.swap(s_object_combo_snapshot);

    keys_out = DynamicPrintConfig();
    std::vector<ModelObject *> objects;

    Plater *plater = wxGetApp().plater();
    if (plater == nullptr || snapshot.empty())
        return objects;

    const DynamicPrintConfig &global_conf = global_print_config();
    const int global_interface = global_conf.has("support_interface_filament") ?
                                     global_conf.opt_int("support_interface_filament") : 0;

    // Model order, which is the order the object list shows, so the first hit is a name the user
    // can actually find. Objects absent from the snapshot were never a combination, and are
    // skipped.
    for (ModelObject *obj : plater->model().objects) {
        if (obj == nullptr)
            continue;
        auto snap = std::find_if(snapshot.begin(), snapshot.end(),
                                 [obj](const ObjectComboSnapshot &s) { return s.object_id == obj->id(); });
        if (snap == snapshot.end())
            continue;

        // Still a combination -- the delete moved something else, and this object is untouched.
        if (!object_recommendation(*obj, global_conf, global_interface).empty())
            continue;

        // Keys the object still holds at the value the recommendation set. One the user already
        // reverted by hand drops out here, and so does support_filament once the delete shifted
        // indices -- the recorded value no longer matches, and we cannot tell whether the new
        // index means the same physical filament.
        bool any = false;
        for (const auto &key : snap->applied_conf.keys()) {
            if (!obj->config.has(key))
                continue;
            const ConfigOption *cur_opt = obj->config.option(key);
            const ConfigOption *was_opt = snap->applied_conf.option(key);
            if (cur_opt && was_opt && cur_opt->serialize() == was_opt->serialize()) {
                keys_out.set_key_value(key, was_opt->clone());
                any = true;
            }
        }
        if (any)
            objects.push_back(obj);
    }
    return objects;
}

// See the declaration in SupportComboAdvisor.hpp for the contract.
wxString shorten_object_name(const std::string &name)
{
    static const size_t max_len = 24;
    wxString out = from_u8(name);
    return out.length() > max_len ? out.Left(max_len - 3) + "..." : out;
}

// See the declaration in SupportComboAdvisor.hpp for the contract.
wxString build_support_revert_dialog_text(SupportRevertLead lead, const DynamicPrintConfig &still_applied_conf,
                                          const std::vector<ModelObject *> &listed_objects, bool offer_choice,
                                          bool also_global)
{
    wxString scope_line, confirm_line;
    switch (lead) {
    case SupportRevertLead::Plate:
        scope_line   = _L("The following settings of the current plate will be restored to default:");
        confirm_line = _L("Restore these settings of the current plate to default?");
        break;
    case SupportRevertLead::Selection:
        // The selection can hold several objects, so the wording stays count-neutral.
        scope_line   = _L("The following settings locked on the current selection will be restored to default:");
        confirm_line = _L("Restore these settings of the current selection to default?");
        break;
    case SupportRevertLead::Objects:
        scope_line   = _L("The following settings locked on the objects below will be restored to default:");
        confirm_line = _L("Restore these settings to default?");
        break;
    case SupportRevertLead::Global:
        scope_line   = _L("The following global settings will be restored to default:");
        confirm_line = _L("Restore these global settings to default?");
        break;
    }

    wxString msg_text = _L("It looks like you are no longer using a support material "
                           "combination for the support interface. Are you exiting support "
                           "material usage?") + " " + scope_line + "\n\n";
    for (const auto &key : still_applied_conf.keys()) {
        const ConfigOption *opt = still_applied_conf.option(key);
        if (!opt)
            continue;
        wxString desc = generate_support_param_description(key, opt);
        if (!desc.empty())
            msg_text += L"  \u2022 " + desc + "\n";
    }

    // The delete dialog covers both scopes at once, so say so rather than let the global half
    // happen silently behind an object-worded question.
    if (also_global)
        msg_text += "\n" + _L("The same settings applied globally will be restored as well.") + "\n";

    if (listed_objects.empty()) {
        msg_text += "\n" + confirm_line;
        return msg_text;
    }

    // "also" only reads right when the body already named a scope the objects come on top of.
    wxString list_header = offer_choice                       ? _L("Other objects also have these settings locked:") :
                           lead == SupportRevertLead::Objects ? _L("Affected objects:") :
                                                                _L("These objects also have these settings locked:");
    msg_text += "\n" + list_header + "\n";
    size_t shown = 0;
    for (const ModelObject *obj : listed_objects) {
        if (shown++ >= 8) {
            msg_text += L"  \u2022 ...\n";
            break;
        }
        msg_text += L"  \u2022 " + shorten_object_name(obj->name) + "\n";
    }
    // "all of them" needs more than one object to be about.
    msg_text += "\n" + (offer_choice               ? _L("Restore the current selection only, or all of them?") :
                        listed_objects.size() == 1 ? confirm_line :
                                                     _L("Restore all of them to default?"));
    return msg_text;
}

} // namespace GUI
} // namespace Slic3r
