#include <algorithm>
#include <wx/display.h>
#include <wx/sizer.h>
#include "libslic3r/FlushVolCalc.hpp"
#include "WipeTowerDialog.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Config.hpp"


using namespace Slic3r;
using namespace Slic3r::GUI;

static const float g_min_flush_multiplier = 0.f;
static const float g_max_flush_multiplier = 3.f;

static std::vector<float> MatrixFlatten(const WipingDialog::VolumeMatrix& matrix) {
    std::vector<float> vec;
    for (auto row_elems : matrix) {
        for (auto elem : row_elems)
            vec.emplace_back(elem);
    }
    return vec;
}

wxString WipingDialog::BuildTableObjStr()
{
    auto full_config = wxGetApp().preset_bundle->full_config();
    auto filament_colors = full_config.option<ConfigOptionStrings>("filament_colour")->values;
    auto flush_multiplier = full_config.option<ConfigOptionFloats>("flush_multiplier")->values;
    int nozzle_num = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values.size();
    auto raw_matrix_data = full_config.option<ConfigOptionFloats>("flush_volumes_matrix")->values;
    std::vector<std::vector<double>> flush_matrixs;
    for (int idx = 0; idx < nozzle_num; ++idx) {
        flush_matrixs.emplace_back(get_flush_volumes_matrix(raw_matrix_data, idx, nozzle_num));
    }
    flush_multiplier.resize(nozzle_num, 1);

    m_raw_matrixs = flush_matrixs;
    m_flush_multipliers = flush_multiplier;

    json obj;
    obj["flush_multiplier"] = flush_multiplier;
    obj["extruder_num"] = nozzle_num;
    obj["filament_colors"] = filament_colors;
    obj["flush_volume_matrixs"] = json::array();
    obj["min_flush_volumes"] = json::array();
    obj["max_flush_volumes"] = json::array();
    obj["min_flush_multiplier"] = g_min_flush_multiplier;
    obj["max_flush_multiplier"] = g_max_flush_multiplier;
    obj["is_dark_mode"] = wxGetApp().dark_mode();

    for (const auto& vec : flush_matrixs) {
        obj["flush_volume_matrixs"].push_back(vec);
    }

    for (int idx = 0; idx < nozzle_num; ++idx) {
        obj["min_flush_volumes"].push_back(0);
        obj["max_flush_volumes"].push_back(m_max_flush_volume);
    }

    auto obj_str = obj.dump();
    return obj_str;
}

wxString WipingDialog::BuildTextObjStr(bool multi_language)
{
    wxString auto_flush_tip;
    wxString volume_desp_panel;
    wxString volume_range_panel;
    wxString multiplier_range_panel;
    wxString calc_btn_panel;
    wxString extruder_label_0;
    wxString extruder_label_1;
    wxString multiplier_label;
    wxString ok_btn_label;
    wxString cancel_btn_label;

    if (multi_language) {
        auto_flush_tip = _L("Studio would re-calculate your flushing volumes everytime the filaments color changed or filaments changed. You could disable the auto-calculate in Bambu Studio > Preferences");
        volume_desp_panel = _L("Flushing volume (mm³) for each filament pair.");
        volume_range_panel = wxString::Format(_L("Suggestion: Flushing Volume in range [%d, %d]"), 0, 700);
        multiplier_range_panel = wxString::Format(_L("The multiplier should be in range [%.2f, %.2f]."), 0, 3);
        calc_btn_panel = _L("Re-calculate");
        extruder_label_0 = _L("Left extruder");
        extruder_label_1 = _L("Right extruder");
        multiplier_label = _L("Multiplier");
        ok_btn_label = _L("OK");
        cancel_btn_label = _L("Cancel");
    } else {
        auto_flush_tip = "Studio would re-calculate your flushing volumes everytime the filaments color changed or filaments changed. You could disable the auto-calculate in Bambu Studio > Preferences";
        volume_desp_panel = wxString::FromUTF8("Flushing volume (mm³) for each filament pair.");
        volume_range_panel = wxString::Format("Suggestion: Flushing Volume in range [%d, %d]", 0, 700);
        multiplier_range_panel = wxString::Format("The multiplier should be in range [%.2f, %.2f].", 0, 3);
        calc_btn_panel = "Re-calculate";
        extruder_label_0 = "Left extruder";
        extruder_label_1 = "Right extruder";
        multiplier_label = "Multiplier";
        ok_btn_label = "OK";
        cancel_btn_label = "Cancel";
    }

    wxString text_obj = "{";
    text_obj += wxString::Format("\"volume_desp_panel\":\"%s\",", volume_desp_panel);
    text_obj += wxString::Format("\"volume_range_panel\":\"%s\",", volume_range_panel);
    text_obj += wxString::Format("\"multiplier_range_panel\":\"%s\",", multiplier_range_panel);
    text_obj += wxString::Format("\"calc_btn_panel\":\"%s\",", calc_btn_panel);
    text_obj += wxString::Format("\"extruder_label_0\":\"%s\",", extruder_label_0);
    text_obj += wxString::Format("\"extruder_label_1\":\"%s\",", extruder_label_1);
    text_obj += wxString::Format("\"multiplier_label\":\"%s\",", multiplier_label);
    text_obj += wxString::Format("\"ok_btn_label\":\"%s\",", ok_btn_label);
    text_obj += wxString::Format("\"cancel_btn_label\":\"%s\",", cancel_btn_label);
    text_obj += wxString::Format("\"auto_flush_tip\":\"%s\"", auto_flush_tip);
    text_obj += "}";
    return text_obj;
}

WipingDialog::WipingDialog(wxWindow* parent, const std::vector<std::vector<int>>& extra_flush_volume,const int max_flush_volume) :
    wxDialog(parent, wxID_ANY, _(L("Flushing volumes for filament change")),
    wxDefaultPosition, wxDefaultSize,
    wxDEFAULT_DIALOG_STYLE ),
    m_extra_flush_volume(extra_flush_volume),
    m_max_flush_volume(max_flush_volume)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % Slic3r::resources_dir()).str();
    SetIcon(wxIcon(Slic3r::encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(main_sizer);
    this->SetBackgroundColour(*wxWHITE);
    auto filament_count= wxGetApp().preset_bundle->project_config.option<ConfigOptionStrings>("filament_colour")->values.size();
    wxSize extra_size = { FromDIP(100),FromDIP(235) };
    if (filament_count <= 2)
        extra_size.y += FromDIP(16) * 3 + FromDIP(32);
    else if (filament_count == 3)
        extra_size.y += FromDIP(16) * 3;
    else if (4 <= filament_count && filament_count <= 8)
        extra_size.y += FromDIP(16) * 2;
    else
        extra_size.y += FromDIP(16);

    wxSize max_scroll_size = { FromDIP(1000),FromDIP(500) };
    wxSize estimate_size = { (int)(filament_count + 1) * FromDIP(60),(int)(filament_count + 1) * FromDIP(30)+FromDIP(2)};
    wxSize scroll_size ={ std::min(max_scroll_size.x,estimate_size.x),std::min(max_scroll_size.y,estimate_size.y) };
    wxSize applied_size = scroll_size + extra_size;

    wxSize scaled_screen_size = wxGetDisplaySize();
    double scale_factor = wxDisplay().GetScaleFactor();
    scaled_screen_size = { (int)(scaled_screen_size.x / scale_factor),(int)(scaled_screen_size.y / scale_factor) };

    applied_size = { std::min(applied_size.x,scaled_screen_size.x),std::min(applied_size.y,scaled_screen_size.y) };
    m_webview = wxWebView::New(this, wxID_ANY,
        wxEmptyString,
        wxDefaultPosition,
        applied_size,
        wxWebViewBackendDefault,
        wxNO_BORDER);

    m_webview->AddScriptMessageHandler("wipingDialog");
    main_sizer->Add(m_webview, 1, wxEXPAND);

    fs::path filepath = fs::path(resources_dir()) / "web/flush/WipingDialog.html";
    wxString filepath_str = wxString::FromUTF8(filepath.string());
    wxFileName fn(filepath_str);
    if(fn.FileExists()) {
        wxString url = wxFileSystem::FileNameToURL(fn);
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__<< "File exists and load url " << url.ToStdString();
        m_webview->LoadURL(url);
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__<< "Successfully loaded url: " << url.ToStdString();
    } else {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< "WipingDialog.html not exist at path: " << filepath.string();
    }

    main_sizer->SetSizeHints(this);
    main_sizer->Fit(this);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);

    //m_webview->Bind(wxEVT_WEBVIEW_NAVIGATED, [this](auto& evt) {
    //    auto table_obj_str = BuildTableObjStr();
    //    auto text_obj_str = BuildTextObjStr();
    //    CallAfter([table_obj_str, text_obj_str, this] {
    //        wxString script = wxString::Format("buildTable(%s);buildText(%s)", table_obj_str, text_obj_str);
    //        m_webview->RunScript(script);
    //        });
    //    });

    m_webview->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, [this](wxWebViewEvent& evt) {
        std::string message = evt.GetString().ToStdString();
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__<< "Received message: " << message;
        try {
            json j = json::parse(message);
            if (j["msg"].get<std::string>() == "init") {
                auto table_obj_str = BuildTableObjStr();
                auto text_obj_str = BuildTextObjStr(true);
                CallAfter([table_obj_str, text_obj_str, this] {
                    wxString script1 = wxString::Format("buildTable(%s)", table_obj_str);
                    m_webview->RunScript(script1);
                    wxString script2 = wxString::Format("buildText(%s)", text_obj_str);
                    bool result = m_webview->RunScript(script2);
                    if (!result) {
                        BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< "Failed to run buildText, retry without multi language";
                        wxString script3 = wxString::Format("buildText(%s)", BuildTextObjStr(false));
                        m_webview->RunScript(script3);
                    }
                    });
            }
            else if (j["msg"].get<std::string>() == "updateMatrix") {
                int extruder_id = j["extruder_id"].get<int>();
                auto new_matrix = CalcFlushingVolumes(extruder_id);
                json obj;
                obj["flush_volume_matrix"] = MatrixFlatten(new_matrix);
                obj["extruder_id"] = extruder_id;
                CallAfter([obj, this] {
                    std::string flush_volume_matrix_str = obj["flush_volume_matrix"].dump();
                    std::string extruder_id_str = std::to_string(obj["extruder_id"].get<int>());
                    wxString script = wxString::Format("updateTable(%s,%s)", flush_volume_matrix_str, extruder_id_str);
                    m_webview->RunScript(script);
                    });
            }
            else if (j["msg"].get<std::string>() == "storeData") {
                int extruder_num = j["number_of_extruders"].get<int>();
                std::vector<std::vector<double>> store_matrixs;
                for (auto iter = j["raw_matrix"].begin(); iter != j["raw_matrix"].end(); ++iter) {
                    store_matrixs.emplace_back((*iter).get<std::vector<double>>());
                }
                std::vector<double>store_multipliers = j["flush_multiplier"].get<std::vector<double>>();
                this->StoreFlushData(extruder_num, store_matrixs, store_multipliers);
                m_submit_flag = true;
                this->Close();
            }
            else if (j["msg"].get<std::string>() == "quit") {
                this->Close();
            }
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< "Failed to parse json message: " << message;
        }
        });
}


int WipingDialog::CalcFlushingVolume(const wxColour& from, const wxColour& to, int min_flush_volume ,bool is_multi_extruder, NozzleVolumeType volume_type)
{
    Slic3r::FlushVolCalculator calculator(min_flush_volume, Slic3r::g_max_flush_volume, is_multi_extruder, volume_type);
    return calculator.calc_flush_vol(from.Alpha(), from.Red(), from.Green(), from.Blue(), to.Alpha(), to.Red(), to.Green(), to.Blue());
}

WipingDialog::VolumeMatrix WipingDialog::CalcFlushingVolumes(int extruder_id)
{
    auto& preset_bundle = wxGetApp().preset_bundle;
    auto full_config = preset_bundle->full_config();
    auto& ams_multi_color_filament = preset_bundle->ams_multi_color_filment;

    std::vector<std::string> filament_color_strs = full_config.option<ConfigOptionStrings>("filament_colour")->values;
    std::vector<std::vector<wxColour>> multi_colors;
    std::vector<wxColour> filament_colors;
    for (auto color_str : filament_color_strs)
        filament_colors.emplace_back(color_str);

    NozzleVolumeType volume_type = NozzleVolumeType(full_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values[extruder_id]);
    bool is_multi_extruder = preset_bundle->get_printer_extruder_count() > 1;
    // Support for multi-color filament
    for (int i = 0; i < filament_colors.size(); ++i) {
        std::vector<wxColour> single_filament;
        if (i < ams_multi_color_filament.size()) {
            if (!ams_multi_color_filament[i].empty()) {
                std::vector<std::string> colors = ams_multi_color_filament[i];
                for (int j = 0; j < colors.size(); ++j) {
                    single_filament.push_back(wxColour(colors[j]));
                }
                multi_colors.push_back(single_filament);
                continue;
            }
        }
        single_filament.push_back(wxColour(filament_colors[i]));
        multi_colors.push_back(single_filament);
    }

    VolumeMatrix matrix;

    for (int from_idx = 0; from_idx < multi_colors.size(); ++from_idx) {
        bool is_from_support = is_support_filament(from_idx);
        matrix.emplace_back();
        for (int to_idx = 0; to_idx < multi_colors.size(); ++to_idx) {
            if (from_idx == to_idx) {
                matrix.back().emplace_back(0);
                continue;
            }

            bool is_to_support = is_support_filament(to_idx);

            int flushing_volume = 0;
            if (is_to_support) {
                flushing_volume = Slic3r::g_flush_volume_to_support;
            }
            else {
                for (int i = 0; i < multi_colors[from_idx].size(); ++i) {
                    const wxColour& from = multi_colors[from_idx][i];
                    for (int j = 0; j < multi_colors[to_idx].size(); ++j) {
                        const wxColour& to = multi_colors[to_idx][j];
                        int volume = CalcFlushingVolume(from, to, m_extra_flush_volume[extruder_id][from_idx], is_multi_extruder, volume_type);
                        flushing_volume = std::max(flushing_volume, volume);
                    }
                }

                if (is_from_support) {
                    flushing_volume = std::max(Slic3r::g_min_flush_volume_from_support, flushing_volume);
                }
            }
            matrix.back().emplace_back(flushing_volume);
        }
    }
    return matrix;
}

void WipingDialog::StoreFlushData(int extruder_num, const std::vector<std::vector<double>>& flush_volume_vecs, const std::vector<double>&flush_multipliers)
{
    m_flush_multipliers = flush_multipliers;
    m_raw_matrixs = flush_volume_vecs;
}

std::vector<double> WipingDialog::GetFlattenMatrix()const
{
    std::vector<double> ret;
    for (auto& matrix : m_raw_matrixs) {
        ret.insert(ret.end(), matrix.begin(), matrix.end());
    }
    return ret;
}


std::vector<double> WipingDialog::GetMultipliers()const
{
    return m_flush_multipliers;
}