#include "EncodedFilament.hpp"

#include "GUI_App.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r
{

// Reconciles filament_colour / filament_multi_colour in preset_bundle->project_config
// against the color order authored in filaments_color_codes.json, for multi-color /
// gradient filaments.
//
// Needed because project_config predates JSON-defined color ordering: filament_colour/
// filament_multi_colour saved before that requirement existed (old 3mf projects,
// AppConfig-restored sessions) may carry colors in HSV-sort order instead of JSON order.
// Current-session writes (color picker dialogs, etc.) already resolve the order via
// GetFilaInfo, so this is purely legacy-data reconciliation on load, not an ongoing
// source of truth.
void align_project_filament_primary_colors_with_json(PresetBundle* preset_bundle)
{
    if (preset_bundle == nullptr)
        return;

    auto* color_query = Slic3r::GUI::wxGetApp().get_filament_color_code_query();
    if (color_query == nullptr)
        return;

    DynamicPrintConfig&   proj_cfg             = preset_bundle->project_config;
    ConfigOptionStrings*  filament_color       = proj_cfg.option<ConfigOptionStrings>("filament_colour");
    ConfigOptionStrings*  filament_multi_color = proj_cfg.option<ConfigOptionStrings>("filament_multi_colour");
    ConfigOptionStrings*  filament_color_type  = proj_cfg.option<ConfigOptionStrings>("filament_colour_type");
    if (filament_color == nullptr || filament_multi_color == nullptr || filament_color_type == nullptr)
        return;

    const size_t filament_count = filament_color->values.size();
    for (size_t i = 0; i < filament_count; ++i) {
        if (i >= filament_multi_color->values.size() ||
            i >= filament_color_type->values.size() ||
            i >= preset_bundle->filament_presets.size())
            continue;

        // Only multi / gradient filaments need an order fix.
        std::vector<std::string> stored_colors = Slic3r::split_string(filament_multi_color->values[i], ' ');
        if (stored_colors.size() < 2)
            continue;

        // Resolve filament_id for JSON lookup.
        const Preset* preset = preset_bundle->filaments.find_preset(preset_bundle->filament_presets[i]);
        if (preset == nullptr || preset->filament_id.empty())
            continue;

        // Match against filaments_color_codes.json by color set + type.
        std::vector<wxString> hex_colors;
        hex_colors.reserve(stored_colors.size());
        for (const auto& hex : stored_colors)
            hex_colors.emplace_back(wxString::FromUTF8(hex));

        const int color_type = (filament_color_type->values[i] == "0") ? 0 : 1;
        FilamentColorCode* color_code = color_query->GetFilaInfo(wxString::FromUTF8(preset->filament_id), hex_colors, color_type);
        if (color_code == nullptr)
            continue;

        // Rewrite the full list in the JSON-authored order (not just the primary).
        const FilamentColor& matched_color = color_code->GetFilaColor();
        const std::vector<wxColour>& ordered_colors = matched_color.GetColors();
        if (ordered_colors.empty())
            continue;

        std::string multi_pack;
        for (const wxColour& color : ordered_colors) {
            if (!multi_pack.empty())
                multi_pack += ' ';
            multi_pack += color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        }
        filament_color->values[i]       = ordered_colors.front().GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        filament_multi_color->values[i] = multi_pack;
    }
}

static wxString _ColourToString(const wxColour& color)
{
    return wxString::Format("#%02X%02X%02X%02X", color.Red(), color.Green(), color.Blue(), color.Alpha());
}

FilamentColorCodeQuery::FilamentColorCodeQuery()
{
    m_fila_id2colors_map = new std::unordered_map<wxString, FilamentColorCodes*>;
    m_fila_path = data_dir() + "/system/BBL/filament/filaments_color_codes.json";
    LoadFromLocal();
}


FilamentColorCodeQuery::~FilamentColorCodeQuery()
{
    for (auto& pair : *m_fila_id2colors_map) { delete pair.second; }

    delete m_fila_id2colors_map;
    m_fila_id2colors_map = nullptr;
}

FilamentColorCodes* FilamentColorCodeQuery::GetFilaInfoMap(const wxString& fila_id) const
{
    const auto& iter = m_fila_id2colors_map->find(fila_id);
    return (iter != m_fila_id2colors_map->end()) ? iter->second : nullptr;
}

Slic3r::FilamentColorCode* FilamentColorCodeQuery::GetFilaInfo(const wxString& fila_id, const FilamentColor& colors) const
{
    FilamentColorCodes* color_info_map = GetFilaInfoMap(fila_id);

#if 0
    if (color_info_map && !color_info_map->GetColorCode(colors))
    {
        wxString clr_strs;
        for (const auto& clr : colors.GetColors())
        {
            clr_strs += " ";
            clr_strs += _ColourToString(clr);
        }

        BOOST_LOG_TRIVIAL(warning) << "FilamentColorCodeQuery::GetFilaInfo: No color code found for " << fila_id << " with color type " << (int)colors.m_color_type << "colors" << clr_strs;
        color_info_map->Debug(" ");
    }
#endif

    return color_info_map ? color_info_map->GetColorCode(colors) : nullptr;
}

Slic3r::FilamentColorCode* FilamentColorCodeQuery::GetFilaInfo(const wxString& fila_id,
                                                               const std::vector<wxString>& hex_colors,
                                                               int color_type) const
{
    FilamentColor colors;
    for (const auto& hex : hex_colors) {
        if (!hex.empty())
            colors.AddColor(wxColour(hex));
    }
    colors.EndSet(color_type);
    return GetFilaInfo(fila_id, colors);
}

wxString FilamentColorCodeQuery::GetFilaColorName(const wxString& fila_id, const FilamentColor& colors) const
{
    FilamentColorCode* color_info = GetFilaInfo(fila_id, colors);
    return (color_info) ? color_info->GetFilaColorName() : wxString();
}

wxString FilamentColorCodeQuery::GetFilaColorName(const wxString& fila_id,
                                                  const std::vector<wxString>& hex_colors,
                                                  int color_type) const
{
    FilamentColorCode* color_info = GetFilaInfo(fila_id, hex_colors, color_type);
    return (color_info) ? color_info->GetFilaColorName() : wxString();
}

void FilamentColorCodeQuery::LoadFromLocal()
{
    std::ifstream json_file(encode_path(m_fila_path.c_str()));
    try
    {
        if (json_file.is_open())
        {
            const json& json_content = json::parse(json_file);
            if (!json_content.contains("data")) { return; }

            const json& json_data = json_content["data"];
            for (const auto& json_data_item : json_data)
            {
                const wxString& fila_id = json_data_item.contains("fila_id") ? json_data_item["fila_id"].get<wxString>() : wxString();
                const wxString& fila_type = json_data_item.contains("fila_type") ? json_data_item["fila_type"].get<wxString>() : wxString();
                const wxString& fila_color_code = json_data_item.contains("fila_color_code") ? json_data_item["fila_color_code"].get<wxString>() : wxString();
                const wxString& color_code = json_data_item.contains("color_code") ? json_data_item["color_code"].get<wxString>() : wxString();

                FilamentColor fila_color;
                if (json_data_item.contains("fila_color"))
                {
                    const auto& fila_color_strs = json_data_item["fila_color"].get<std::vector<wxString>>();
                    for (const auto& color_str : fila_color_strs) {
                        if (color_str.size() > 3) /* Skip the value like "#0"*/{
                            fila_color.AddColor(wxColour(color_str));
                        }
                    }
                }

                if (fila_color.GetColors().empty()) {
                    BOOST_LOG_TRIVIAL(warning) << "FilamentColorCodeQuery::LoadFromLocal: No colors found for fila_color_code: " << fila_color_code;
                    continue; // Skip if no colors are defined
                };

                const wxString& fila_color_type = json_data_item.contains("fila_color_type") ? wxString::FromUTF8(json_data_item["fila_color_type"].get<std::string>()) : wxString();
                if (fila_color_type == wxString::FromUTF8("单色")) {
                    fila_color.m_color_type = FilamentColor::ColorType::SINGLE_CLR;
                } else if (fila_color_type == wxString::FromUTF8("多拼色")) {
                    fila_color.m_color_type = FilamentColor::ColorType::MULTI_CLR;
                } else if (fila_color_type == wxString::FromUTF8("渐变色"))
                {
                    fila_color.m_color_type = FilamentColor::ColorType::GRADIENT_CLR;
                };

                std::unordered_map<wxString, wxString> fila_color_names;
                if (json_data_item.contains("fila_color_name"))
                {
                    const json& color_names_json = json_data_item["fila_color_name"];
                    for (const auto& color_name_item : color_names_json.items())
                    {
                        const wxString& lang_code = wxString::FromUTF8(color_name_item.key());
                        const wxString& color_name = wxString::FromUTF8(color_name_item.value().get<std::string>());
                        fila_color_names[lang_code] = color_name;
                    }
                }

                CreateFilaCode(fila_id, fila_type, fila_color_code, color_code, std::move(fila_color), std::move(fila_color_names));
            }
        }
    }
    catch (...)
    {
        assert(0 && "FilamentColorCodeQuery::LoadFromLocal failed");
        BOOST_LOG_TRIVIAL(error) << "FilamentColorCodeQuery::LoadFromLocal failed";
    }
}

void FilamentColorCodeQuery::CreateFilaCode(const wxString& fila_id,
                                            const wxString& fila_type,
                                            const wxString& fila_color_code,
                                            const wxString& color_code,
                                            FilamentColor&& fila_color,
                                            std::unordered_map<wxString, wxString>&& fila_color_names)
{
    FilamentColorCodes* color_codes = GetFilaInfoMap(fila_id);
    if (!color_codes)
    {
        color_codes = new FilamentColorCodes(fila_id, fila_type);
        (*m_fila_id2colors_map)[fila_id] = color_codes;
    }

    FilamentColorCode* fila_code = new FilamentColorCode(fila_color_code, color_code, color_codes, std::move(fila_color), std::move(fila_color_names));
    color_codes->AddColorCode(fila_code);
}
// End of class EncodedFilamentQuery

wxString FilamentColorCode::GetFilaColorName() const
{
    const wxString& strLanguage = Slic3r::GUI::wxGetApp().app_config->get("language");
    const wxString& lang_code = strLanguage.BeforeFirst('_');
    auto it = m_fila_color_names.find(lang_code);
    if (it != m_fila_color_names.end() && !it->second.empty()) {  return it->second; }

    it = m_fila_color_names.find("en");// retry with English as fallback
    return (it != m_fila_color_names.end()) ? it->second : "Unknown";
}

FilamentColorCode::FilamentColorCode(const wxString& fila_color_code,
                                     const wxString& color_code,
                                     FilamentColorCodes* owner,
                                     FilamentColor&& color,
                                     std::unordered_map<wxString, wxString>&& name_map)
    : m_fila_color_code(fila_color_code),
      m_color_code(color_code),
      m_owner(owner),
      m_fila_color(std::move(color)),
      m_fila_color_names(std::move(name_map))
{
}

void FilamentColorCode::Debug(const char* prefix)
{
    BOOST_LOG_TRIVIAL(debug) << prefix << "Fila Color Code: " << m_fila_color_code
                             << ", Color Code: " << m_color_code
                             << ", Colors: " << m_fila_color.ColorCount()
                             << ", Type: " << static_cast<int>(m_fila_color.m_color_type);
    for (const auto& color : m_fila_color.GetColors()) { BOOST_LOG_TRIVIAL(debug) << prefix << "  Color: " << _ColourToString(color); }
    //for (const auto& name_pair : m_fila_color_names) { BOOST_LOG_TRIVIAL(debug) << prefix << "  Color Name [" << name_pair.first << "]: " << name_pair.second;}
}

FilamentColorCodes::FilamentColorCodes(const wxString& fila_id, const wxString& fila_type)
    : m_fila_id(fila_id), m_fila_type(fila_type)
{
    m_fila_colors_map = new FilamentColor2CodeMap;
}

FilamentColorCodes::~FilamentColorCodes()
{
    for (auto iter : *m_fila_colors_map) { delete iter.second; }

    m_fila_colors_map->clear();
    delete m_fila_colors_map;
}

Slic3r::FilamentColorCode* FilamentColorCodes::GetColorCode(const FilamentColor& colors) const
{
    const auto& it = m_fila_colors_map->find(colors);
    if (it != m_fila_colors_map->end()) {
        return it->second;
    }

    // Fallback: ignore insertion order and match the same color set.
    for (const auto& pair : *m_fila_colors_map) {
        if (pair.first.MatchesColorSet(colors))
            return pair.second;
    }

    return nullptr;
}

void FilamentColorCodes::AddColorCode(FilamentColorCode* code)
{
    m_fila_colors_map->emplace(code->GetFilaColor(), code);
}

void FilamentColorCodes::Debug(const char* prefix)
{
    BOOST_LOG_TRIVIAL(debug) << prefix << "Fila ID: " << m_fila_id << ", Type: " << m_fila_type;

    auto iter = m_fila_colors_map->begin();
    while (iter != m_fila_colors_map->end())
    {
        iter->second->Debug(prefix);
        iter++;
    }

    BOOST_LOG_TRIVIAL(debug) << prefix << "End";
}



} // namespace Slic3r