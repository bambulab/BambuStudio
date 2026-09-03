#include "CreateFilamentWebDialog.hpp"

#include <wx/webview.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <unordered_set>
#include <openssl/md5.h>
#include <openssl/evp.h>

#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "DeviceManager.hpp"
#include "DeviceCore/DevManager.h"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "Widgets/WebView.hpp"
#include "CreatePresetsDialog.hpp"  // for CreatePresetSuccessfulDialog

using namespace nlohmann;

namespace Slic3r { namespace GUI {

// ── helpers ────────────────────────────────────────────────────────────────

// MD5 helper: identical to calculate_md5() in CreatePresetsDialog.cpp
static std::string calc_md5(const std::string &input)
{
    unsigned char digest[MD5_DIGEST_LENGTH];
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit(ctx, EVP_md5());
    EVP_DigestUpdate(ctx, input.c_str(), input.length());
    EVP_DigestFinal(ctx, digest, nullptr);
    EVP_MD_CTX_free(ctx);
    char hex[MD5_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i)
        sprintf(hex + i * 2, "%02x", digest[i]);
    hex[MD5_DIGEST_LENGTH * 2] = '\0';
    return std::string(hex);
}

// Generate a filament ID using the same algorithm as get_filament_id() in CreatePresetsDialog.cpp:
//   "P" + first 7 chars of MD5(vendor_type_serial [@user_id])
// Collisions are resolved by re-hashing with a timestamp suffix, same as the original.
static std::string make_filament_id(const std::string &vendor_type_serial)
{
    // Build the same id->name map as the original for collision detection
    std::unordered_map<std::string, std::set<std::string>> id_to_names;

    // 1. temp preset bundle (same as original get_filament_id)
    PresetBundle temp_pb;
    temp_pb.load_system_filaments_json(Slic3r::ForwardCompatibilitySubstitutionRule::EnableSilent);
    std::string dir_user = wxGetApp().app_config->get("preset_folder");
    if (dir_user.empty())
        temp_pb.load_user_presets(DEFAULT_USER_FOLDER_NAME, ForwardCompatibilitySubstitutionRule::EnableSilent);
    else
        temp_pb.load_user_presets(dir_user, ForwardCompatibilitySubstitutionRule::EnableSilent);
    for (const Preset &p : temp_pb.filaments.get_presets()) {
        size_t at = p.name.find_first_of('@');
        if (at == std::string::npos) continue;
        std::string fname = p.name.substr(0, at - 1);
        if (fname == vendor_type_serial && p.filament_id != "null")
            return p.filament_id; // exact match: reuse existing id
        id_to_names[p.filament_id].insert(fname);
    }

    // 2. live preset bundle
    PresetBundle *pb = wxGetApp().preset_bundle;
    auto filament_map = pb->filaments.get_filament_presets();
    for (const auto &kv : filament_map) {
        if (kv.first.empty()) continue;
        for (const Preset *p : kv.second) {
            size_t at = p->name.find_first_of('@');
            if (at == std::string::npos) continue;
            std::string fname = p->name.substr(0, at - 1);
            if (fname == vendor_type_serial && p->filament_id != "null")
                return p->filament_id;
            id_to_names[p->filament_id].insert(fname);
        }
    }

    // 3. Compute hash the same way: include user_id to avoid cross-user collision
    std::string hash_input = vendor_type_serial;
    NetworkAgent *agent = wxGetApp().getAgent();
    if (agent && agent->is_user_login() && !agent->get_user_id().empty())
        hash_input += "@" + agent->get_user_id();

    std::string candidate = "P" + calc_md5(hash_input).substr(0, 7);

    while (id_to_names.count(candidate)) {
        // Check if the collision is actually the same filament name
        bool same_name = false;
        for (const auto &n : id_to_names.at(candidate)) {
            if (n == vendor_type_serial) { same_name = true; break; }
        }
        if (same_name) break;
        // Different name with same id: re-hash with timestamp
        auto now = std::chrono::system_clock::now();
        auto ts  = std::to_string(std::chrono::system_clock::to_time_t(now));
        candidate = "P" + calc_md5(vendor_type_serial + ts).substr(0, 7);
    }

    BOOST_LOG_TRIVIAL(info) << "make_filament_id: " << vendor_type_serial << " -> " << candidate;
    return candidate;
}

// Collect the user's own saved filament presets, reloaded fresh from disk — the live
// preset_bundle doesn't reliably hold every preset the user has ever saved (see
// make_filament_id() above for the same need). Matches legacy
// CreateFilamentPresetDialog::get_all_filament_presets() loop1 (CreatePresetsDialog.cpp:1364-1379),
// which pulls user presets unconditionally except for a valid filament_id.
//
// filament_type: pass empty to skip the type filter entirely (used when the caller only knows
// a preset's public name, e.g. send_compatible_printers).
//
// exclude_derived additionally skips presets that were customized FROM an existing preset
// (non-empty "inherits" config option), keeping only fully-custom-vendor ones — matching
// legacy's get_filament_preset_choices() (CreatePresetsDialog.cpp:1180-1183), used by the
// "pick a base type/preset" dropdown flows. Pass false for the "copy from printer" flows,
// which don't apply that extra restriction (get_filament_presets_by_machine(),
// CreatePresetsDialog.cpp:1292-1360).
static std::vector<Preset> collect_user_filament_presets(const std::string &filament_type, bool exclude_derived)
{
    std::vector<Preset> result;

    PresetBundle temp_pb;
    std::string dir_user = wxGetApp().app_config->get("preset_folder");
    if (dir_user.empty())
        temp_pb.load_user_presets(DEFAULT_USER_FOLDER_NAME, ForwardCompatibilitySubstitutionRule::EnableSilent);
    else
        temp_pb.load_user_presets(dir_user, ForwardCompatibilitySubstitutionRule::EnableSilent);

    for (const Preset &p : temp_pb.filaments.get_presets()) {
        if (p.filament_id.empty() || p.filament_id == "null") continue;
        if (!filament_type.empty()) {
            auto *ft = dynamic_cast<ConfigOptionStrings *>(const_cast<Preset &>(p).config.option("filament_type", false));
            if (!ft || ft->values.empty() || ft->values[0] != filament_type) continue;
        }
        if (exclude_derived) {
            auto *inh = dynamic_cast<ConfigOptionString *>(const_cast<Preset &>(p).config.option(BBL_JSON_KEY_INHERITS, false));
            if (inh && !inh->value.empty()) continue;
        }
        // Skip user presets that carry an official Bambu vendor (or "Generic"). Same
        // rationale as WebGuideDialog::update_custom_filaments's vendor filter: these
        // are almost always polluted clones from cloud sync or an old wizard that lacked
        // the reserved-vendor check, not truly user-authored materials. Letting them
        // through here would let a hidden system preset (e.g. "Bambu ABS-GF") still
        // appear as a base-preset option in the wizard through user-preset shadowing,
        // and would give it no viable adapter printer downstream.
        auto *fv = dynamic_cast<ConfigOptionStrings *>(const_cast<Preset &>(p).config.option("filament_vendor", false));
        if (fv && !fv->values.empty()) {
            const std::string &v = fv->values[0];
            if (v == "Generic" || v == "Bambu" || v == "Bambu Lab" || v == "BBL") continue;
        }
        result.push_back(p);
    }
    return result;
}

// ── Constructor / Destructor ───────────────────────────────────────────────

CreateFilamentWebDialog::CreateFilamentWebDialog(wxWindow *parent,
                                                 const std::string &vendor,
                                                 const std::string &type,
                                                 const std::string &serial)
    : DPIDialog(parent ? parent : nullptr,
                wxID_ANY,
                _L("Create Custom Filament"),
                wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX | wxCENTRE)
    , m_prefill_vendor(vendor)
    , m_prefill_type(type)
    , m_prefill_serial(serial)
{
    SetBackgroundColour(*wxWHITE);

    // Set window icon (same as other preset dialogs)
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    // Fixed dialog size to match our HTML canvas
    wxSize dlg_size = FromDIP(wxSize(800, 680));
    SetSize(dlg_size);
    SetMinSize(dlg_size);

    // Build the URL for step1
    wxString url = wxString::Format(
        "file://%s/web/filament_create/index.html",
        from_u8(resources_dir()));
    url.Replace("\\", "/");
    wxString strlang = wxGetApp().current_language_code_safe();
    if (!strlang.IsEmpty()) url = wxString::Format("%s?lang=%s", url, strlang);

    m_browser = WebView::CreateWebView(this, url);
    if (!m_browser) {
        wxLogError("CreateFilamentWebDialog: failed to create WebView");
        return;
    }

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_browser, 1, wxEXPAND);
    SetSizer(sizer);

    // Event bindings
    Bind(wxEVT_WEBVIEW_LOADED,
         &CreateFilamentWebDialog::OnDocumentLoaded, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
         &CreateFilamentWebDialog::OnScriptMessage, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR,
         &CreateFilamentWebDialog::OnError, this, m_browser->GetId());

    // Center on parent
    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);

    BOOST_LOG_TRIVIAL(info) << "CreateFilamentWebDialog created";
}

CreateFilamentWebDialog::~CreateFilamentWebDialog()
{
    if (m_browser) {
        delete m_browser;
        m_browser = nullptr;
    }
}

void CreateFilamentWebDialog::on_dpi_changed(const wxRect &)
{
    // The WebView content scales itself; nothing extra needed.
    Layout();
}

// ── Private helpers ────────────────────────────────────────────────────────

void CreateFilamentWebDialog::run_script(const wxString &js)
{
    if (m_browser)
        WebView::RunScript(m_browser, js);
}

void CreateFilamentWebDialog::send_init_data(const std::string &filament_type)
{
    PresetBundle *pb = wxGetApp().preset_bundle;

    // 1. Vendor list: same as original - hardcoded list (filament_vendors in CreatePresetsDialog.cpp)
    static const std::vector<std::string> s_filament_vendors = {
        "Polymaker", "OVERTURE", "Kexcelled", "HATCHBOX", "eSUN", "SUNLU", "Prusament",
        "Creality", "Protopasta", "Anycubic", "Basf", "ELEGOO", "INLAND", "FLASHFORGE",
        "FusRock", "AMOLEN", "MIKA3D", "3DXTECH", "Duramic", "Priline", "Eryone",
        "3Dgenius", "Novamaker", "Justmaker", "Giantarm", "iProspect", "LDO"
    };
    json vendors = json::array();
    for (const auto &v : s_filament_vendors) vendors.push_back(v);

    // 2. Filament type list: collect from ALL presets (including non-visible),
    // same as m_system_filament_types_set in get_all_filament_presets()
    std::set<std::string> type_set;
    for (const Preset &p : pb->filaments.get_presets()) {
        if (p.filament_id.empty() || p.filament_id == "null") continue;
        auto *opt = dynamic_cast<ConfigOptionStrings *>(
            const_cast<Preset &>(p).config.option("filament_type", false));
        if (opt && !opt->values.empty())
            type_set.insert(opt->values[0]);
    }
    json types = json::array();
    for (const auto &t : type_set) types.push_back(t);

    // 3. Visible printer list grouped by model name
    std::map<std::string, std::set<std::string>> printer_nozzles; // model -> {nozzles}
    for (const Preset &p : pb->printers.get_presets()) {
        if (!p.is_visible) continue;
        auto *opt = dynamic_cast<ConfigOptionFloats *>(
            const_cast<Preset &>(p).config.option("nozzle_diameter", false));
        if (opt && !opt->values.empty()) {
            std::string nozzle = std::to_string(opt->values[0]);
            // trim trailing zeros: "0.400000" → "0.4"
            nozzle.erase(nozzle.find_last_not_of('0') + 1);
            if (nozzle.back() == '.') nozzle += '0';
            nozzle += "mm";
            // model name = printer preset name without the "<size> nozzle" suffix,
            // e.g. "Bambu Lab P1S 0.4 nozzle" -> "Bambu Lab P1S"
            std::string model = p.name;
            size_t sp2 = model.rfind(' ');
            if (sp2 != std::string::npos) {
                size_t sp1 = model.rfind(' ', sp2 - 1);
                if (sp1 != std::string::npos) model = model.substr(0, sp1);
            }
            printer_nozzles[model].insert(nozzle);
        }
    }
    json printers = json::array();
    for (const auto &kv : printer_nozzles) {
        json entry;
        entry["name"] = kv.first;
        json nz = json::array();
        for (const auto &n : kv.second) nz.push_back(n);
        entry["nozzles"] = nz;
        printers.push_back(entry);
    }

    // 4. Available system filament presets (for base-preset dropdowns)
    // Same logic as get_filament_preset_choices(): collect system presets filtered by
    // filament_type, strip the ' @machine' suffix, deduplicate the public names.
    // Exact same logic as get_filament_preset_choices():
    // 1. iterate m_all_presets_map equivalent (system visible presets)
    // 2. skip presets with 'inherits' set
    // 3. filter by filament_type
    // 4. group by filament_id
    // 5. for each group, only add presets whose name contains ' @' (strip suffix as public name)
    // 6. one public name per filament_id
    // Use live bundle, system+visible presets only.
    // is_visible reflects update_compatible (printer compatibility), matching
    // what the original dialog shows. is_system excludes user-imported copies.
    // @base templates are internal and excluded by the ' @' name check below.
    json system_presets = json::array();
    if (!filament_type.empty()) {
        std::map<std::string, std::vector<const Preset *>> choice_map;
        for (const Preset &p : pb->filaments.get_presets()) {
            // Matches legacy CreateFilamentPresetDialog::get_all_filament_presets(), which
            // requires is_visible for system presets (CreatePresetsDialog.cpp:1388) — a
            // type with nothing installed (e.g. ABS-GF) should stay unavailable here.
            if (!p.is_system || p.is_project_embedded || !p.is_visible) continue;
            if (p.filament_id.empty() || p.filament_id == "null") continue;
            auto *ft = dynamic_cast<ConfigOptionStrings *>(
                const_cast<Preset &>(p).config.option("filament_type", false));
            if (!ft || ft->values.empty() || ft->values[0] != filament_type) continue;
            BOOST_LOG_TRIVIAL(info) << "send_init_data candidate: " << p.name
                << " is_system=" << p.is_system << " is_visible=" << p.is_visible;
            choice_map[p.filament_id].push_back(&p);
        }
        // Also offer the user's own from-scratch custom presets (no base preset) as a base —
        // matches legacy's get_filament_preset_choices() (CreatePresetsDialog.cpp:1180-1183),
        // which only skips presets DERIVED from an existing one (non-empty "inherits").
        std::vector<Preset> user_presets = collect_user_filament_presets(filament_type, /*exclude_derived=*/true);
        for (const Preset &p : user_presets)
            choice_map[p.filament_id].push_back(&p);
        std::set<std::string> seen_names;
        for (const auto &kv : choice_map) {
            std::set<std::string> name_set;
            for (const Preset *fp : kv.second) {
                size_t at = fp->name.find(" @");
                if (at != std::string::npos)
                    name_set.insert(fp->name.substr(0, at));
            }
            for (const auto &pub : name_set) {
                if (seen_names.insert(pub).second)
                    system_presets.push_back(pub);
            }
        }
    }

    // Build the JS call
    json msg;
    msg["command"]        = "init_data";
    msg["vendors"]        = vendors;
    msg["types"]          = types;
    msg["printers"]       = printers;
    msg["system_presets"] = system_presets;
    if (!m_prefill_vendor.empty()) msg["selected_vendor"] = m_prefill_vendor;
    if (!m_prefill_type.empty())   msg["selected_type"]   = m_prefill_type;
    if (!m_prefill_serial.empty()) msg["selected_serial"] = m_prefill_serial;

    wxString js = wxString::Format("HandleStudio(%s)",
        wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
    run_script(js);

    BOOST_LOG_TRIVIAL(info) << "CreateFilamentWebDialog::send_init_data sent";
}

void CreateFilamentWebDialog::send_compatible_printers(const std::string &public_name)
{
    PresetBundle *pb = wxGetApp().preset_bundle;

    // Build printer_name -> filament_preset_name mapping so the web side can
    // send back the exact preset name and we can find it without guessing.
    std::map<std::string, std::string> printer_to_filament_preset;
    for (const Preset &p : pb->filaments.get_presets()) {
        // See the comment in send_init_data(): matches legacy's is_visible/filament_id gating.
        if (!p.is_system || p.is_project_embedded || !p.is_visible) continue;
        if (p.filament_id.empty() || p.filament_id == "null") continue;
        std::string pub = p.name;
        size_t at = pub.find(" @");
        if (at != std::string::npos) pub = pub.substr(0, at);
        if (pub != public_name) continue;
        auto *opt = dynamic_cast<ConfigOptionStrings *>(
            const_cast<Preset &>(p).config.option("compatible_printers", false));
        if (opt)
            for (const auto &cp : opt->values)
                printer_to_filament_preset[cp] = p.name;
    }
    // Also match against the user's own from-scratch custom presets (no base preset) —
    // see the comment in send_init_data(). filament_type isn't known here, so query across
    // all types and filter by public name below.
    std::vector<Preset> user_presets = collect_user_filament_presets("", /*exclude_derived=*/true);
    for (const Preset &p : user_presets) {
        std::string pub = p.name;
        size_t at = pub.find(" @");
        if (at != std::string::npos) pub = pub.substr(0, at);
        if (pub != public_name) continue;
        auto *opt = dynamic_cast<ConfigOptionStrings *>(
            const_cast<Preset &>(p).config.option("compatible_printers", false));
        if (opt)
            for (const auto &cp : opt->values)
                printer_to_filament_preset[cp] = p.name;
    }

    // Group by model name for the UI, carrying the exact filament preset name per printer.
    std::map<std::string, std::vector<std::pair<std::string,std::string>>> model_to_presets;
    std::vector<std::string> model_order;
    for (const Preset &p : pb->printers.get_presets()) {
        if (!p.is_visible) continue;
        if (printer_to_filament_preset.find(p.name) == printer_to_filament_preset.end()) continue;
        std::string model = p.name;
        size_t sp2 = model.rfind(' ');
        if (sp2 != std::string::npos) {
            size_t sp1 = model.rfind(' ', sp2 - 1);
            if (sp1 != std::string::npos)
                model = model.substr(0, sp1);
        }
        if (model_to_presets.find(model) == model_to_presets.end())
            model_order.push_back(model);
        model_to_presets[model].push_back({p.name, printer_to_filament_preset[p.name]});
    }

    json printers = json::array();
    for (const auto &model : model_order) {
        json entry;
        entry["name"] = model;
        json presets = json::array();
        for (const auto &pf : model_to_presets[model]) {
            json item;
            item["printer"]          = pf.first;
            item["filament_preset"]  = pf.second;
            presets.push_back(item);
        }
        entry["presets"] = presets;
        printers.push_back(entry);
    }

    json msg;
    msg["command"]  = "compatible_printers";
    msg["printers"] = printers;

    wxString js = wxString::Format("HandleStudio(%s)",
        wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
    run_script(js);

    BOOST_LOG_TRIVIAL(info) << "send_compatible_printers for public_name: " << public_name
                            << ", models=" << printers.size();
}

void CreateFilamentWebDialog::send_device_info(const std::string &filament_type)
{
    // Get connected machine
    auto *dev = wxGetApp().getDeviceManager();
    MachineObject *obj = dev ? dev->get_selected_machine() : nullptr;

    json msg;
    msg["command"] = "device_info";

    if (!obj || !obj->is_online()) {
        msg["connected"] = false;
        wxString js = wxString::Format("HandleStudio(%s)",
            wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
        run_script(js);
        return;
    }

    msg["connected"]    = true;
    msg["device_name"]  = obj->get_dev_name();

    std::string model_id = obj->get_show_printer_type();
    PresetBundle *pb = wxGetApp().preset_bundle;

    // Build model_id -> model_name map from vendor profiles
    // so we can match printer presets by their printer_model config field.
    std::string model_name; // e.g. "Bambu Lab P1S"
    for (const auto &vp : pb->vendors) {
        for (const auto &vm : vp.second.models) {
            if (vm.model_id == model_id) {
                model_name = vm.name;
                break;
            }
        }
        if (!model_name.empty()) break;
    }

    // Collect nozzle sizes from visible printer presets whose printer_model matches,
    // together with the exact printer preset name for each nozzle so the web side
    // doesn't have to guess it by regex-substituting the nozzle into a template name.
    // This is every nozzle the printer MODEL has a preset for, not yet filtered by
    // whether the selected filament type actually supports that nozzle (see below).
    std::map<std::string, std::string> nozzle_to_printer_all; // nozzle -> exact printer preset name
    for (const Preset &p : pb->printers.get_presets()) {
        if (!p.is_visible) continue;
        auto *opt = dynamic_cast<ConfigOptionString *>(
            const_cast<Preset &>(p).config.option("printer_model", false));
        if (!opt || opt->value != model_name) continue;
        // extract nozzle from name suffix: "Bambu Lab P1S 0.4 nozzle" -> "0.4"
        const std::string &name = p.name;
        size_t nozzle_pos = name.rfind(' '); // points to "nozzle"
        if (nozzle_pos != std::string::npos) {
            size_t prev = name.rfind(' ', nozzle_pos - 1); // points to "0.4"
            if (prev != std::string::npos) {
                std::string nozzle_str = name.substr(prev + 1, nozzle_pos - prev - 1);
                nozzle_to_printer_all[nozzle_str] = name;
            }
        }
    }

    // Available base filament presets for this machine + type (reuse send_init_data logic).
    // Computed before the nozzle checkbox list below so that list can be restricted to
    // nozzles that actually have a compatible preset for this type — a printer model can
    // have preset entries for nozzle sizes it doesn't support this filament on (e.g. an
    // H2S offering a 0.2 nozzle checkbox with no matching preset), which previously let
    // the user pick a nozzle that silently failed at creation time (see step2.js #btn-next).
    json system_presets = json::array();
    std::map<std::string, std::map<std::string, std::string>> nozzle_presets; // nozzle -> pub_name -> exact_filament_preset
    if (!filament_type.empty()) {
        std::map<std::string, std::vector<const Preset *>> choice_map;
        // For each type-matching preset, remember which nozzle sizes (of this model) it is
        // actually compatible with — derived from the matched entries in its own
        // compatible_printers list, not from parsing the preset's own name suffix. Some
        // presets are nozzle-agnostic in their name (e.g. "Foo @BBL P1S", no nozzle suffix)
        // even though compatible_printers spans several nozzle-specific printer presets, so
        // name-suffix parsing would silently drop those nozzles (see send_compatible_printers,
        // which reads compatible_printers directly and doesn't have this problem).
        std::map<const Preset *, std::set<std::string>> preset_nozzles;
        for (const Preset &p : pb->filaments.get_presets()) {
            // See the comment in send_init_data(): matches legacy's is_visible gating.
            if (!p.is_system || p.is_project_embedded || !p.is_visible) continue;
            if (p.filament_id.empty() || p.filament_id == "null") continue;
            auto *ft = dynamic_cast<ConfigOptionStrings *>(
                const_cast<Preset &>(p).config.option("filament_type", false));
            if (!ft || ft->values.empty() || ft->values[0] != filament_type) continue;
            // must be compatible with this machine — match by printer_model
            auto *opt = dynamic_cast<ConfigOptionStrings *>(
                const_cast<Preset &>(p).config.option("compatible_printers", false));
            if (!opt) continue;
            std::set<std::string> matched_nozzles;
            for (const auto &cp : opt->values) {
                Preset *pp = pb->printers.find_preset(cp, false);
                if (!pp) continue;
                auto *pm = dynamic_cast<ConfigOptionString *>(
                    const_cast<Preset &>(*pp).config.option("printer_model", false));
                if (!pm || pm->value != model_name) continue;
                // extract nozzle from the printer preset name, e.g. "Bambu Lab P1S 0.4 nozzle"
                const std::string &pname = pp->name;
                size_t nozzle_pos = pname.rfind(' ');
                if (nozzle_pos == std::string::npos) continue;
                size_t prev = pname.rfind(' ', nozzle_pos - 1);
                if (prev == std::string::npos) continue;
                matched_nozzles.insert(pname.substr(prev + 1, nozzle_pos - prev - 1));
            }
            if (matched_nozzles.empty()) continue;
            choice_map[p.filament_id].push_back(&p);
            preset_nozzles[&p] = std::move(matched_nozzles);
        }
        // Also offer the user's own from-scratch custom presets (no base preset) for this
        // machine + type — see the comment in send_init_data(). Same compatible_printers /
        // printer_model / nozzle-extraction matching as the system-preset loop above.
        std::vector<Preset> user_presets = collect_user_filament_presets(filament_type, /*exclude_derived=*/true);
        for (const Preset &p : user_presets) {
            auto *opt = dynamic_cast<ConfigOptionStrings *>(
                const_cast<Preset &>(p).config.option("compatible_printers", false));
            if (!opt) continue;
            std::set<std::string> matched_nozzles;
            for (const auto &cp : opt->values) {
                Preset *pp = pb->printers.find_preset(cp, false);
                if (!pp) continue;
                auto *pm = dynamic_cast<ConfigOptionString *>(
                    const_cast<Preset &>(*pp).config.option("printer_model", false));
                if (!pm || pm->value != model_name) continue;
                const std::string &pname = pp->name;
                size_t nozzle_pos = pname.rfind(' ');
                if (nozzle_pos == std::string::npos) continue;
                size_t prev = pname.rfind(' ', nozzle_pos - 1);
                if (prev == std::string::npos) continue;
                matched_nozzles.insert(pname.substr(prev + 1, nozzle_pos - prev - 1));
            }
            if (matched_nozzles.empty()) continue;
            choice_map[p.filament_id].push_back(&p);
            preset_nozzles[&p] = std::move(matched_nozzles);
        }
        // Build nozzle -> (public_name -> exact_filament_preset) map
        // so Web can look up the right preset per nozzle tab.
        // nozzle_presets: { "0.4": { "Bambu ABS": "Bambu ABS @BBL P1S 0.4 nozzle", ... }, ... }
        for (const auto &kv : choice_map) {
            for (const Preset *fp : kv.second) {
                size_t at = fp->name.find(" @");
                if (at == std::string::npos) continue;
                std::string pub = fp->name.substr(0, at);
                for (const auto &nozzle : preset_nozzles[fp])
                    nozzle_presets[nozzle][pub] = fp->name;
            }
        }

        std::set<std::string> seen_names;
        for (const auto &kv : choice_map) {
            for (const Preset *fp : kv.second) {
                size_t at = fp->name.find(" @");
                if (at == std::string::npos) continue;
                std::string pub = fp->name.substr(0, at);
                if (!seen_names.insert(pub).second) continue;

                json item;
                item["name"] = pub;
                // per-nozzle exact preset names
                json nozzle_map = json::object();
                for (const auto &nkv : nozzle_presets) {
                    auto it = nkv.second.find(pub);
                    if (it != nkv.second.end())
                        nozzle_map[nkv.first] = it->second;
                }
                item["nozzle_presets"] = nozzle_map;
                // default filament_preset = first available nozzle
                item["filament_preset"] = nozzle_map.empty() ? "" : nozzle_map.begin().value();
                system_presets.push_back(item);
            }
        }
    }
    msg["system_presets"] = system_presets;

    // Nozzle checkbox list: send every nozzle size the printer MODEL has a preset for
    // (nozzle_to_printer_all), plus which of those are actually usable for the current
    // filament type (have at least one type-compatible filament preset, nozzle_presets
    // computed above). The web side keeps the unsupported ones visible but disabled/greyed
    // out instead of removing them, so the user can see a nozzle exists but isn't offered
    // for this filament type rather than having it silently vanish.
    std::set<std::string> supported_nozzle_set;
    std::map<std::string, std::string> nozzle_to_printer = nozzle_to_printer_all; // nozzle -> exact printer preset name
    std::string printer_preset_base;
    for (const auto &kv : nozzle_to_printer_all) {
        bool supported = filament_type.empty() || nozzle_presets.count(kv.first);
        if (supported) {
            supported_nozzle_set.insert(kv.first);
            if (printer_preset_base.empty()) printer_preset_base = kv.second;
        }
    }
    if (printer_preset_base.empty() && !nozzle_to_printer_all.empty())
        printer_preset_base = nozzle_to_printer_all.begin()->second;

    json nozzles = json::array();
    for (const auto &kv : nozzle_to_printer_all) nozzles.push_back(kv.first);
    msg["nozzles"] = nozzles;
    json supported_nozzles = json::array();
    for (const auto &n : supported_nozzle_set) supported_nozzles.push_back(n);
    msg["supported_nozzles"] = supported_nozzles;
    msg["printer_model_id"] = model_id;
    msg["printer_preset_base"] = printer_preset_base;
    json nozzle_printers = json::object();
    for (const auto &kv : nozzle_to_printer) nozzle_printers[kv.first] = kv.second;
    msg["nozzle_printers"] = nozzle_printers;

    wxString js = wxString::Format("HandleStudio(%s)",
        wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
    run_script(js);
    BOOST_LOG_TRIVIAL(info) << "send_device_info: model_id=" << model_id
                            << " model_name=" << model_name
                            << " nozzles=" << nozzle_to_printer_all.size()
                            << " supported_nozzles=" << supported_nozzle_set.size()
                            << " presets=" << system_presets.size();
}

void CreateFilamentWebDialog::send_supported_types()
{
    // For the "current_printer" creation mode: which filament types can actually be
    // created for the connected printer model. Drives step1.js's Type dropdown so the
    // user gets fast feedback ("this printer can't do carbon fiber") without having to
    // walk through to step2 and land on an empty base-preset dropdown.
    //
    // IMPORTANT: DO NOT add `!p.is_visible` here. The system-material panel's "hide"
    // action flips is_visible, which is a personal panel preference — coupling the Type
    // dropdown to it means hiding materials silently shrinks the wizard's Type list,
    // which is surprising and was the exact bug that caused this filter to be reverted
    // once already. Filter by compatible_printers/printer_model only.
    auto *dev = wxGetApp().getDeviceManager();
    MachineObject *obj = dev ? dev->get_selected_machine() : nullptr;

    json msg;
    msg["command"] = "supported_types";

    if (!obj || !obj->is_online()) {
        msg["connected"] = false;
        msg["types"] = json::array();
        wxString js = wxString::Format("HandleStudio(%s)",
            wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
        run_script(js);
        return;
    }

    std::string model_id = obj->get_show_printer_type();
    PresetBundle *pb = wxGetApp().preset_bundle;

    std::string model_name;
    for (const auto &vp : pb->vendors) {
        for (const auto &vm : vp.second.models) {
            if (vm.model_id == model_id) {
                model_name = vm.name;
                break;
            }
        }
        if (!model_name.empty()) break;
    }

    std::set<std::string> type_set;
    for (const Preset &p : pb->filaments.get_presets()) {
        if (!p.is_system || p.is_project_embedded) continue;
        if (p.filament_id.empty() || p.filament_id == "null") continue;
        auto *opt = dynamic_cast<ConfigOptionStrings *>(
            const_cast<Preset &>(p).config.option("compatible_printers", false));
        if (!opt) continue;
        bool compat = false;
        for (const auto &cp : opt->values) {
            Preset *pp = pb->printers.find_preset(cp, false);
            if (!pp) continue;
            auto *pm = dynamic_cast<ConfigOptionString *>(
                const_cast<Preset &>(*pp).config.option("printer_model", false));
            if (pm && pm->value == model_name) { compat = true; break; }
        }
        if (!compat) continue;
        auto *ft = dynamic_cast<ConfigOptionStrings *>(
            const_cast<Preset &>(p).config.option("filament_type", false));
        if (ft && !ft->values.empty())
            type_set.insert(ft->values[0]);
    }
    // Also count types covered only by the user's own from-scratch custom presets (no base
    // preset). collect_user_filament_presets doesn't consult is_visible, so the visibility
    // decoupling above already extends here.
    std::vector<Preset> user_presets = collect_user_filament_presets("", /*exclude_derived=*/true);
    for (const Preset &p : user_presets) {
        auto *opt = dynamic_cast<ConfigOptionStrings *>(
            const_cast<Preset &>(p).config.option("compatible_printers", false));
        if (!opt) continue;
        bool compat = false;
        for (const auto &cp : opt->values) {
            Preset *pp = pb->printers.find_preset(cp, false);
            if (!pp) continue;
            auto *pm = dynamic_cast<ConfigOptionString *>(
                const_cast<Preset &>(*pp).config.option("printer_model", false));
            if (pm && pm->value == model_name) { compat = true; break; }
        }
        if (!compat) continue;
        auto *ft = dynamic_cast<ConfigOptionStrings *>(
            const_cast<Preset &>(p).config.option("filament_type", false));
        if (ft && !ft->values.empty())
            type_set.insert(ft->values[0]);
    }

    json types = json::array();
    for (const auto &t : type_set) types.push_back(t);
    msg["connected"] = true;
    msg["types"] = types;

    wxString js = wxString::Format("HandleStudio(%s)",
        wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
    run_script(js);
    BOOST_LOG_TRIVIAL(info) << "send_supported_types: model_id=" << model_id
                            << " model_name=" << model_name
                            << " types=" << type_set.size();
}

void CreateFilamentWebDialog::send_filament_params(const std::string &preset_name,
                                                   const std::string &printer_preset)
{
    PresetBundle *pb = wxGetApp().preset_bundle;

    // Try exact match first, then find by printer-specific variant
    Preset *p = pb->filaments.find_preset(preset_name, false);
    if (!p && !printer_preset.empty())
        p = pb->filaments.find_preset(preset_name + " @" + printer_preset, false);
    if (!p) {
        json msg;
        msg["command"] = "filament_params";
        msg["error"]   = "preset not found: " + preset_name;
        wxString js = wxString::Format("HandleStudio(%s)",
            wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
        run_script(js);
        return;
    }

    // Helper lambdas
    auto get_str = [&](const char *key) -> std::string {
        auto *opt = dynamic_cast<ConfigOptionStrings *>(const_cast<Preset &>(*p).config.option(key, false));
        return (opt && !opt->values.empty()) ? opt->values[0] : "";
    };
    // filament_flow_ratio/filament_max_volumetric_speed/nozzle_temperature/... are all
    // declared nullable, so at runtime they are ConfigOptionFloatsNullable/IntsNullable —
    // a sibling template instantiation of ConfigOptionFloats/Ints, not a subclass. Cast to
    // the common ConfigOptionVector<T> base so both the nullable and non-nullable variants
    // are handled.
    auto get_float = [&](const char *key) -> double {
        auto *opt = dynamic_cast<ConfigOptionVector<double> *>(const_cast<Preset &>(*p).config.option(key, false));
        return (opt && !opt->values.empty()) ? opt->values[0] : 0.0;
    };
    auto get_int = [&](const char *key) -> int {
        auto *opt = dynamic_cast<ConfigOptionVector<int> *>(const_cast<Preset &>(*p).config.option(key, false));
        return (opt && !opt->values.empty()) ? opt->values[0] : 0;
    };
    auto get_single_str = [&](const char *key) -> std::string {
        auto *opt = dynamic_cast<ConfigOptionString *>(const_cast<Preset &>(*p).config.option(key, false));
        return opt ? opt->value : "";
    };
    // filament_shrink is coPercents, not coStrings — serialize the option directly
    // (ConfigOptionPercents::serialize() already appends '%') instead of down-casting
    // to the wrong concrete type.
    auto get_percent_str = [&](const char *key) -> std::string {
        auto *opt = const_cast<Preset &>(*p).config.option(key, false);
        return opt ? opt->serialize() : "";
    };

    json params;
    params["filament_type"]                  = get_str("filament_type");
    params["filament_vendor"]                = get_str("filament_vendor");
    params["filament_diameter"]              = get_float("filament_diameter");
    params["filament_density"]               = get_float("filament_density");
    params["filament_flow_ratio"]            = get_float("filament_flow_ratio");
    params["filament_max_volumetric_speed"]  = get_float("filament_max_volumetric_speed");
    params["nozzle_temperature"]             = get_int("nozzle_temperature");
    params["nozzle_temperature_initial_layer"] = get_int("nozzle_temperature_initial_layer");
    // Mirrors the native "Print temperature" group (TabFilament, Tab.cpp) field-for-field:
    // there is no single generic bed temperature, only a per-plate-type pair.
    params["supertack_plate_temp"]             = get_int("supertack_plate_temp");
    params["supertack_plate_temp_initial_layer"] = get_int("supertack_plate_temp_initial_layer");
    params["cool_plate_temp"]                  = get_int("cool_plate_temp");
    params["cool_plate_temp_initial_layer"]    = get_int("cool_plate_temp_initial_layer");
    params["eng_plate_temp"]                   = get_int("eng_plate_temp");
    params["eng_plate_temp_initial_layer"]     = get_int("eng_plate_temp_initial_layer");
    params["hot_plate_temp"]                   = get_int("hot_plate_temp");
    params["hot_plate_temp_initial_layer"]     = get_int("hot_plate_temp_initial_layer");
    params["textured_plate_temp"]              = get_int("textured_plate_temp");
    params["textured_plate_temp_initial_layer"] = get_int("textured_plate_temp_initial_layer");
    params["filament_shrink"]                = get_percent_str("filament_shrink");
    params["default_filament_colour"]        = get_str("default_filament_colour");

    json msg;
    msg["command"] = "filament_params";
    msg["preset"]  = preset_name;
    msg["params"]  = params;

    wxString js = wxString::Format("HandleStudio(%s)",
        wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
    run_script(js);
    BOOST_LOG_TRIVIAL(info) << "send_filament_params: " << preset_name;
}

void CreateFilamentWebDialog::send_all_printers(const std::string &filament_type)
{
    PresetBundle *pb = wxGetApp().preset_bundle;

    // Collect printer names that have at least one system filament preset of the given type.
    std::set<std::string> printers_with_preset;
    if (!filament_type.empty()) {
        for (const Preset &p : pb->filaments.get_presets()) {
            // See the comment in send_init_data(): matches legacy's is_visible/filament_id gating.
            if (!p.is_system || p.is_project_embedded || !p.is_visible) continue;
            if (p.filament_id.empty() || p.filament_id == "null") continue;
            auto *ft = dynamic_cast<ConfigOptionStrings *>(
                const_cast<Preset &>(p).config.option("filament_type", false));
            if (!ft || ft->values.empty() || ft->values[0] != filament_type) continue;
            auto *opt = dynamic_cast<ConfigOptionStrings *>(
                const_cast<Preset &>(p).config.option("compatible_printers", false));
            if (opt)
                for (const auto &cp : opt->values)
                    printers_with_preset.insert(cp);
        }
        // Also count the user's own saved presets of this type — matches legacy's
        // get_filament_presets_by_machine() (CreatePresetsDialog.cpp:1292-1360), which doesn't
        // exclude presets derived from an existing one (unlike the dropdown flows).
        for (const Preset &p : collect_user_filament_presets(filament_type, /*exclude_derived=*/false)) {
            auto *opt = dynamic_cast<ConfigOptionStrings *>(
                const_cast<Preset &>(p).config.option("compatible_printers", false));
            if (opt)
                for (const auto &cp : opt->values)
                    printers_with_preset.insert(cp);
        }
    }

    std::map<std::string, std::vector<std::string>> model_to_presets;
    std::vector<std::string> model_order;
    for (const Preset &p : pb->printers.get_presets()) {
        if (!p.is_visible) continue;
        if (!filament_type.empty() && printers_with_preset.find(p.name) == printers_with_preset.end()) continue;
        std::string model = p.name;
        size_t sp2 = model.rfind(' ');
        if (sp2 != std::string::npos) {
            size_t sp1 = model.rfind(' ', sp2 - 1);
            if (sp1 != std::string::npos)
                model = model.substr(0, sp1);
        }
        if (model_to_presets.find(model) == model_to_presets.end())
            model_order.push_back(model);
        model_to_presets[model].push_back(p.name);
    }

    json printers = json::array();
    for (const auto &model : model_order) {
        json entry;
        entry["name"] = model;
        json presets = json::array();
        for (const auto &pn : model_to_presets[model])
            presets.push_back(pn);
        entry["presets"] = presets;
        printers.push_back(entry);
    }

    json msg;
    msg["command"]  = "all_printers";
    msg["printers"] = printers;

    wxString js = wxString::Format("HandleStudio(%s)",
        wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
    run_script(js);
}

void CreateFilamentWebDialog::send_presets_by_machine(const std::vector<std::string> &printer_names,
                                                      const std::string              &filament_type)
{
    // For each selected printer, collect all system filament presets of the given type
    // that are compatible with that printer, so the user can pick one as the copy template.
    PresetBundle *pb = wxGetApp().preset_bundle;

    // Build printer_name -> [{ public_name, exact_preset_name }] map
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> printer_to_presets;
    for (const std::string &pn : printer_names)
        printer_to_presets[pn]; // ensure entry exists

    for (const Preset &p : pb->filaments.get_presets()) {
        // See the comment in send_init_data(): matches legacy's is_visible gating.
        if (!p.is_system || p.is_project_embedded || !p.is_visible) continue;
        if (p.filament_id.empty() || p.filament_id == "null") continue;
        // filter by type
        auto *ft = dynamic_cast<ConfigOptionStrings *>(
            const_cast<Preset &>(p).config.option("filament_type", false));
        if (!ft || ft->values.empty() || ft->values[0] != filament_type) continue;
        // get public name (strip " @machine" suffix)
        std::string pub = p.name;
        size_t at = pub.find(" @");
        if (at != std::string::npos) pub = pub.substr(0, at);
        // check compatible_printers
        auto *opt = dynamic_cast<ConfigOptionStrings *>(
            const_cast<Preset &>(p).config.option("compatible_printers", false));
        if (!opt) continue;
        for (const std::string &cp : opt->values) {
            auto it = printer_to_presets.find(cp);
            if (it == printer_to_presets.end()) continue;
            // deduplicate by public name per printer
            auto &vec = it->second;
            bool already = false;
            for (const auto &entry : vec)
                if (entry.first == pub) { already = true; break; }
            if (!already)
                vec.push_back({pub, p.name});
        }
    }
    // Also offer the user's own saved presets of this type — see the comment in
    // send_all_printers() above.
    for (const Preset &p : collect_user_filament_presets(filament_type, /*exclude_derived=*/false)) {
        std::string pub = p.name;
        size_t at = pub.find(" @");
        if (at != std::string::npos) pub = pub.substr(0, at);
        auto *opt = dynamic_cast<ConfigOptionStrings *>(
            const_cast<Preset &>(p).config.option("compatible_printers", false));
        if (!opt) continue;
        for (const std::string &cp : opt->values) {
            auto it = printer_to_presets.find(cp);
            if (it == printer_to_presets.end()) continue;
            auto &vec = it->second;
            bool already = false;
            for (const auto &entry : vec)
                if (entry.first == pub) { already = true; break; }
            if (!already)
                vec.push_back({pub, p.name});
        }
    }

    json result = json::array();
    for (const std::string &pn : printer_names) {
        json entry;
        entry["printer"] = pn;
        json presets = json::array();
        for (const auto &kv : printer_to_presets[pn]) {
            json item;
            item["name"]           = kv.first;   // public name for display
            item["filament_preset"] = kv.second;  // exact preset name for cloning
            presets.push_back(item);
        }
        entry["filament_presets"] = presets;
        result.push_back(entry);
    }

    json msg;
    msg["command"] = "presets_by_machine";
    msg["data"]    = result;

    wxString js = wxString::Format("HandleStudio(%s)",
        wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
    run_script(js);

    BOOST_LOG_TRIVIAL(info) << "send_presets_by_machine: " << printer_names.size() << " printers";
}

// ── Event handlers ─────────────────────────────────────────────────────────

void CreateFilamentWebDialog::OnDocumentLoaded(wxWebViewEvent &evt)
{
    if (evt.GetURL() == m_browser->GetCurrentURL())
        send_init_data();
}

void CreateFilamentWebDialog::OnError(wxWebViewEvent &evt)
{
    BOOST_LOG_TRIVIAL(error) << "CreateFilamentWebDialog WebView error: "
                             << evt.GetString().ToUTF8().data();
}

void CreateFilamentWebDialog::OnScriptMessage(wxWebViewEvent &evt)
{
    try {
        json j = json::parse(evt.GetString().ToUTF8().data());
        std::string cmd = j.value("command", "");

        BOOST_LOG_TRIVIAL(info) << "CreateFilamentWebDialog command: " << cmd;
        if (cmd == "create_filament_confirm")
            BOOST_LOG_TRIVIAL(info) << "CreateFilamentWebDialog payload: " << j.dump();

        if (cmd == "close_page") {
            EndModal(wxID_CANCEL);

        } else if (cmd == "sync_and_close") {
            // User clicked "Sync user presets" on the success screen
            wxGetApp().app_config->set("sync_user_preset", "true");
            wxGetApp().start_sync_user_preset();
            EndModal(wxID_OK);

        } else if (cmd == "success_close") {
            EndModal(wxID_OK);

        } else if (cmd == "create_filament_confirm") {
            handle_create_filament(j);

        } else if (cmd == "request_init_data") {
            std::string filament_type = j.value("type", "");
            send_init_data(filament_type);

        } else if (cmd == "get_compatible_printers") {
            std::string preset_name = j.value("preset", "");
            send_compatible_printers(preset_name);

        } else if (cmd == "get_device_status") {
            auto *dev = wxGetApp().getDeviceManager();
            MachineObject *obj = dev ? dev->get_selected_machine() : nullptr;
            json smsg;
            smsg["command"]   = "device_status";
            smsg["connected"] = obj && obj->is_online();
            if (obj && obj->is_online()) smsg["device_name"] = obj->get_dev_name();
            wxString sjs = wxString::Format("HandleStudio(%s)",
                wxString::FromUTF8(smsg.dump(-1, ' ', false, json::error_handler_t::ignore)));
            run_script(sjs);

        } else if (cmd == "get_device_info") {
            send_device_info(j.value("type", ""));

        } else if (cmd == "get_supported_types") {
            send_supported_types();

        } else if (cmd == "get_filament_params") {
            send_filament_params(j.value("preset", ""), j.value("printer_preset", ""));

        } else if (cmd == "get_all_printers") {
            send_all_printers(j.value("type", ""));

        } else if (cmd == "get_presets_by_machine") {
            // copy mode: Web sends selected printer list + filament type,
            // C++ returns each printer's available filament presets for user to pick from.
            std::vector<std::string> printers;
            for (const auto &p : j.value("printers", json::array()))
                printers.push_back(p.get<std::string>());
            std::string filament_type = j.value("type", "");
            send_presets_by_machine(printers, filament_type);
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "CreateFilamentWebDialog::OnScriptMessage exception: " << e.what();
    }
}

// ── Creation logic ─────────────────────────────────────────────────────────

// Same excluded-character set as remove_special_key() in CreatePresetsDialog.cpp:
// these would otherwise break preset name parsing (e.g. '@' is the machine-suffix separator).
static const std::set<char> s_filament_name_special_key = {'\n', '\t', '\r', '\v', '@', ';'};

static std::string remove_special_key(const std::string &str)
{
    std::string res;
    for (char c : str)
        if (s_filament_name_special_key.find(c) == s_filament_name_special_key.end())
            res.push_back(c);
    return res;
}

// Build filament_preset_name and user_filament_id from vendor/type/serial fields in j,
// mirroring the validation in CreatePresetsDialog.cpp's create-filament flow: strip
// characters that would break preset name parsing, reject the reserved "Bambu"/"Generic"
// vendor names, and warn (with a Yes/No confirm) when the name collides with an existing
// preset alias. Returns false if creation should not proceed (a dialog explaining why has
// already been shown to the user).
static bool build_and_validate_filament_name(wxWindow *parent, const json &j,
                                              std::string &out_vendor,
                                              std::string &out_name,
                                              std::string &out_id)
{
    std::string vendor = j.value("vendor", "");
    std::string type   = j.value("type",   "");
    std::string serial = j.value("serial", "");

    vendor = remove_special_key(vendor);
    serial = remove_special_key(serial);
    boost::algorithm::trim(vendor);
    boost::algorithm::trim(serial);

    if (vendor.empty() || serial.empty()) {
        MessageDialog(parent,
            _L("There may be escape characters in the vendor or serial input of filament. Please delete and re-enter."),
            wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE).ShowModal();
        return false;
    }
    if (vendor == "Bambu" || vendor == "Generic") {
        MessageDialog(parent,
            _L("\"Bambu\" or \"Generic\" can not be used as a Vendor for custom filaments."),
            wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE).ShowModal();
        return false;
    }

    std::string name = vendor;
    if (!type.empty()) name += " " + type;
    name += " " + serial;

    PresetBundle *pb = wxGetApp().preset_bundle;
    if (pb->filaments.is_alias_exist(name)) {
        wxString msg = wxString::Format(
            _L("The Filament name %s you created already exists. \nIf you continue, the new preset will be saved alongside the existing custom filaments and shown with its full name to distinguish it. Do you want to continue?"),
            wxString::FromUTF8(name));
        MessageDialog dlg(parent, msg, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES_NO | wxYES_DEFAULT | wxCENTRE);
        if (dlg.ShowModal() != wxID_YES)
            return false;
    }

    out_vendor = vendor;
    out_name   = name;
    out_id     = make_filament_id(name);
    return true;
}

// Shared implementation used by both "based_on_type" and "copy_presets" modes.
// The two modes differ only in semantics conveyed to the user; the preset-cloning
// logic is identical, so a single implementation avoids divergence.
static bool do_clone_filament_presets(wxWindow *parent, const json &j, const std::string &log_mode)
{
    std::string vendor, filament_name, filament_id;
    if (!build_and_validate_filament_name(parent, j, vendor, filament_name, filament_id))
        return false;
    std::string type = j.value("type", "");

    PresetBundle *pb = wxGetApp().preset_bundle;
    bool any_success = false;

    for (const auto &item : j.value("printer_nozzles", json::array())) {
        std::string printer_name = item.value("printer", "");
        std::string base_preset  = item.value("base_preset", "");

        // Web sends back the exact filament preset name from send_compatible_printers.
        Preset *tmpl = pb->filaments.find_preset(base_preset, false);
        if (!tmpl) {
            BOOST_LOG_TRIVIAL(warning) << "base preset not found: " << base_preset;
            continue;
        }

        DynamicConfig dc;
        dc.set_key_value("filament_vendor",     new ConfigOptionStrings({vendor}));
        dc.set_key_value("compatible_printers", new ConfigOptionStrings({printer_name}));
        dc.set_key_value("filament_type",       new ConfigOptionStrings({type}));

        std::vector<std::string> failures;
        bool ok = pb->filaments.clone_presets_for_filament(
            tmpl, failures, filament_name, filament_id, dc, printer_name);

        if (!ok && !failures.empty()) {
            wxString msg = wxString::Format(
                _L("Preset \"%s\" already exists. Overwrite?"),
                wxString::FromUTF8(failures[0]));
            MessageDialog dlg(parent, msg,
                wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                wxYES_NO | wxYES_DEFAULT | wxCENTRE);
            if (dlg.ShowModal() == wxID_YES) {
                ok = pb->filaments.clone_presets_for_filament(
                    tmpl, failures, filament_name, filament_id, dc, printer_name, true);
            }
        }

        if (ok)
            any_success = true;
    }

    if (any_success)
        BOOST_LOG_TRIVIAL(info) << "CreateFilamentWebDialog [" << log_mode << "]: " << filament_name;
    return any_success;
}

void CreateFilamentWebDialog::handle_create_filament(const json &j)
{
    std::string mode = j.value("mode", "");
    if (mode == "based_on_type" || mode == "copy_presets" || mode == "current_printer") {
        bool ok = do_clone_filament_presets(this, j, mode);
        if (ok) {
            wxGetApp().preset_bundle->update_compatible(PresetSelectCompatibleType::Always);
            // Send success state to Web so step3 can show result inline.
            bool need_sync = wxGetApp().getAgent() &&
                             wxGetApp().app_config->get("sync_user_preset") == "false";
            json msg;
            msg["command"]   = "create_success";
            msg["need_sync"] = need_sync;
            wxString js = wxString::Format("HandleStudio(%s)",
                wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
            run_script(js);
            // Dialog stays open; Web will send close_page or sync_and_close to dismiss it.
        } else {
            // Creation failed (validation rejected, or every clone attempt failed/was
            // declined). Keep the dialog open and let Web re-enable its buttons instead
            // of silently closing, so the user can correct the input and retry.
            json msg;
            msg["command"] = "create_fail";
            wxString js = wxString::Format("HandleStudio(%s)",
                wxString::FromUTF8(msg.dump(-1, ' ', false, json::error_handler_t::ignore)));
            run_script(js);
        }
    } else {
        BOOST_LOG_TRIVIAL(warning) << "CreateFilamentWebDialog: unknown mode " << mode;
        EndModal(wxID_CANCEL);
    }
}

void CreateFilamentWebDialog::handle_create_filament_based_on_type(const json &j)
{
    EndModal(do_clone_filament_presets(this, j, "based_on_type") ? wxID_OK : wxID_CANCEL);
}

void CreateFilamentWebDialog::handle_create_filament_copy_presets(const json &j)
{
    EndModal(do_clone_filament_presets(this, j, "copy_presets") ? wxID_OK : wxID_CANCEL);
}

}} // namespace Slic3r::GUI
