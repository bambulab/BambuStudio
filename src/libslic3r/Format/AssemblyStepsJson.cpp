#include "AssemblyStepsJson.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <cmath>
#include <functional>
#include <map>

namespace Slic3r {
namespace {
static bool json_get_string(const nlohmann::json &j, const char *key, std::string &value)
{
    if (!j.contains(key) || !j[key].is_string())
        return false;
    value = j[key].get<std::string>();
    return true;
}

static bool json_get_int(const nlohmann::json &j, const char *key, int &value)
{
    if (!j.contains(key) || !j[key].is_number_integer())
        return false;
    value = j[key].get<int>();
    return true;
}

static bool json_get_size_t(const nlohmann::json &j, const char *key, size_t &value)
{
    if (!j.contains(key) || !j[key].is_number_integer())
        return false;
    const auto raw = j[key].get<long long>();
    if (raw < 0)
        return false;
    value = static_cast<size_t>(raw);
    return true;
}

static bool json_get_bool(const nlohmann::json &j, const char *key, bool &value)
{
    if (!j.contains(key) || !j[key].is_boolean())
        return false;
    value = j[key].get<bool>();
    return true;
}

static bool json_get_double(const nlohmann::json &j, const char *key, double &value)
{
    if (!j.contains(key) || !j[key].is_number())
        return false;
    value = j[key].get<double>();
    return true;
}

static bool json_get_vec2d(const nlohmann::json &j, const char *key, Vec2d &value)
{
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != 2 ||
        !j[key][0].is_number() || !j[key][1].is_number())
        return false;
    value = Vec2d(j[key][0].get<double>(), j[key][1].get<double>());
    return true;
}

static void color_to_json(nlohmann::json &j, const char *key, const std::array<int, 4> &color)
{
    j[key] = {color[0], color[1], color[2], color[3]};
}

static void bound_volumes_to_json(nlohmann::json &j, const char *key,
                                  const std::vector<std::pair<int, int>> &bound_volumes)
{
    nlohmann::json bv = nlohmann::json::array();
    for (const auto &p : bound_volumes)
        bv.push_back({p.first, p.second});
    j[key] = bv;
}

static void bound_volumes_from_json(const nlohmann::json &j, const char *key,
                                    std::vector<std::pair<int, int>> &bound_volumes)
{
    bound_volumes.clear();
    if (j.contains(key) && j[key].is_array()) {
        for (const auto &item : j[key]) {
            if (item.is_array() && item.size() == 2 && item[0].is_number_integer() && item[1].is_number_integer())
                bound_volumes.emplace_back(item[0].get<int>(), item[1].get<int>());
        }
    }
}

static void color_from_json(const nlohmann::json &j, const char *key, std::array<int, 4> &color)
{
    if (!j.contains(key) || !j[key].is_array())
        return;

    const auto &arr = j[key];
    if (arr.size() != 3 && arr.size() != 4)
        return;

    try {
        for (size_t i = 0; i < arr.size(); ++i) {
            if (!arr[i].is_number())
                return;
            int value = static_cast<int>(std::round(arr[i].get<double>()));
            value = value < 0 ? 0 : (value > 255 ? 255 : value);
            color[i] = value;
        }
        if (arr.size() == 3)
            color[3] = 255;
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(warning) << "AssemblyStepJson: ignore invalid note color: " << e.what();
    }
}
}

// ---- Transform3d helpers (file-local: only used by this translation unit) ----

static nlohmann::json transform3d_to_json(const Transform3d &t)
{
    nlohmann::json arr = nlohmann::json::array();
    const auto &m = t.matrix();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            arr.push_back(m(r, c));
    return arr;
}

static Transform3d transform3d_from_json(const nlohmann::json &j)
{
    Transform3d t = Transform3d::Identity();
    if (!j.is_array() || j.size() != 16)
        return t;
    auto &m = t.matrix();
    int idx = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c, ++idx) {
            const auto &cell = j[idx];
            if (!cell.is_number())
                return Transform3d::Identity();
            m(r, c) = cell.get<double>();
        }
    }
    return t;
}
// ---- ArrowSvgNote ----
void ArrowSvgNote::to_json(nlohmann::json &j) const
{
    j["svg_name"]           = svg_name;
    nlohmann::json bv = nlohmann::json::array();
    for (const auto &p : bound_volumes)
        bv.push_back({p.first, p.second});
    j["bound_volumes"]      = bv;
    j["arrow_start_offset"] = {arrow_start_offset.x(), arrow_start_offset.y()};
    j["arrow_end_offset"]   = {arrow_end_offset.x(), arrow_end_offset.y()};
    j["label_size"]         = {label_size.x(), label_size.y()};
    color_to_json(j, "color", color);
}

void ArrowSvgNote::from_json(const nlohmann::json &j)
{
    json_get_string(j, "svg_name", svg_name);
    bound_volumes.clear();
    if (j.contains("bound_volumes") && j["bound_volumes"].is_array()) {
        for (const auto &item : j["bound_volumes"]) {
            if (item.is_array() && item.size() == 2 && item[0].is_number_integer() && item[1].is_number_integer())
                bound_volumes.emplace_back(item[0].get<int>(), item[1].get<int>());
        }
    }
    json_get_vec2d(j, "arrow_start_offset", arrow_start_offset);
    json_get_vec2d(j, "arrow_end_offset", arrow_end_offset);
    json_get_vec2d(j, "label_size", label_size);
    color_from_json(j, "color", color);
}

// ---- TextLabelNote ----
void TextLabelNote::to_json(nlohmann::json &j) const
{
    j["text"]       = text;
    bound_volumes_to_json(j, "bound_volumes", bound_volumes);
    j["pos_offset"] = {pos_offset.x(), pos_offset.y()};
    j["size"]       = {size.x(), size.y()};
    color_to_json(j, "color", color);
    color_to_json(j, "background_color", background_color);
}

void TextLabelNote::from_json(const nlohmann::json &j)
{
    json_get_string(j, "text", text);
    bound_volumes_from_json(j, "bound_volumes", bound_volumes);
    json_get_vec2d(j, "pos_offset", pos_offset);
    json_get_vec2d(j, "size", size);
    color_from_json(j, "color", color);
    color_from_json(j, "background_color", background_color);
}

// ---- CircleNote ----
void CircleNote::to_json(nlohmann::json &j) const
{
    bound_volumes_to_json(j, "bound_volumes", bound_volumes);
    j["pos_offset"] = {pos_offset.x(), pos_offset.y()};
    j["size"]       = {size.x(), size.y()};
    color_to_json(j, "color", color);
}

void CircleNote::from_json(const nlohmann::json &j)
{
    bound_volumes_from_json(j, "bound_volumes", bound_volumes);
    json_get_vec2d(j, "pos_offset", pos_offset);
    json_get_vec2d(j, "size", size);
    color_from_json(j, "color", color);
}

// ---- RectangleNote ----
void RectangleNote::to_json(nlohmann::json &j) const
{
    bound_volumes_to_json(j, "bound_volumes", bound_volumes);
    j["pos_offset"] = {pos_offset.x(), pos_offset.y()};
    j["size"]       = {size.x(), size.y()};
    color_to_json(j, "color", color);
}

void RectangleNote::from_json(const nlohmann::json &j)
{
    bound_volumes_from_json(j, "bound_volumes", bound_volumes);
    json_get_vec2d(j, "pos_offset", pos_offset);
    json_get_vec2d(j, "size", size);
    color_from_json(j, "color", color);
}

// ---- PlainArrowNote ----
void PlainArrowNote::to_json(nlohmann::json &j) const
{
    bound_volumes_to_json(j, "bound_volumes", bound_volumes);
    j["arrow_start_offset"] = {arrow_start_offset.x(), arrow_start_offset.y()};
    j["arrow_end_offset"]   = {arrow_end_offset.x(), arrow_end_offset.y()};
    color_to_json(j, "color", color);
}

void PlainArrowNote::from_json(const nlohmann::json &j)
{
    bound_volumes_from_json(j, "bound_volumes", bound_volumes);
    json_get_vec2d(j, "arrow_start_offset", arrow_start_offset);
    json_get_vec2d(j, "arrow_end_offset", arrow_end_offset);
    color_from_json(j, "color", color);
}

// ---- PartNumberLabel ----
void PartNumberLabel::to_json(nlohmann::json &j) const
{
    j["object_idx"]         = object_idx;
    j["volume_idx"]         = volume_idx;
    j["part_name"]          = part_name;
    j["arrow_start_offset"] = {arrow_start_offset.x(), arrow_start_offset.y()};
    j["arrow_end_offset"]   = {arrow_end_offset.x(), arrow_end_offset.y()};
}

void PartNumberLabel::from_json(const nlohmann::json &j)
{
    json_get_int(j, "object_idx", object_idx);
    json_get_int(j, "volume_idx", volume_idx);
    json_get_string(j, "part_name", part_name);
    json_get_vec2d(j, "arrow_start_offset", arrow_start_offset);
    json_get_vec2d(j, "arrow_end_offset", arrow_end_offset);
}

// ---- AssemblyNote ----
void AssemblyNote::to_json(nlohmann::json &j) const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &a : arrow_svgs) {
        nlohmann::json aj;
        a.to_json(aj);
        arr.push_back(aj);
    }
    j["arrow_svgs"] = arr;

    nlohmann::json text_arr = nlohmann::json::array();
    for (const auto &text_label : text_labels) {
        nlohmann::json tj;
        text_label.to_json(tj);
        text_arr.push_back(tj);
    }
    j["text_labels"] = text_arr;

    nlohmann::json circle_arr = nlohmann::json::array();
    for (const auto &circle : circle_notes) {
        nlohmann::json cj;
        circle.to_json(cj);
        circle_arr.push_back(cj);
    }
    j["circle_notes"] = circle_arr;

    nlohmann::json rect_arr = nlohmann::json::array();
    for (const auto &rect : rectangle_notes) {
        nlohmann::json rj;
        rect.to_json(rj);
        rect_arr.push_back(rj);
    }
    j["rectangle_notes"] = rect_arr;

    nlohmann::json arrow_arr = nlohmann::json::array();
    for (const auto &arrow : plain_arrows) {
        nlohmann::json aj;
        arrow.to_json(aj);
        arrow_arr.push_back(aj);
    }
    j["plain_arrows"] = arrow_arr;

    nlohmann::json pn_arr = nlohmann::json::array();
    for (const auto &pn : part_number_labels) {
        nlohmann::json pj;
        pn.to_json(pj);
        pn_arr.push_back(pj);
    }
    j["part_number_labels"] = pn_arr;
    j["show_part_labels"] = show_part_labels;
}

void AssemblyNote::from_json(const nlohmann::json &j)
{
    arrow_svgs.clear();
    if (j.contains("arrow_svgs") && j["arrow_svgs"].is_array()) {
        for (const auto &aj : j["arrow_svgs"]) {
            ArrowSvgNote a;
            a.from_json(aj);
            arrow_svgs.push_back(std::move(a));
        }
    }

    text_labels.clear();
    if (j.contains("text_labels") && j["text_labels"].is_array()) {
        for (const auto &tj : j["text_labels"]) {
            TextLabelNote text_label;
            text_label.from_json(tj);
            text_labels.push_back(std::move(text_label));
        }
    }

    circle_notes.clear();
    if (j.contains("circle_notes") && j["circle_notes"].is_array()) {
        for (const auto &cj : j["circle_notes"]) {
            CircleNote circle;
            circle.from_json(cj);
            circle_notes.push_back(std::move(circle));
        }
    }

    rectangle_notes.clear();
    if (j.contains("rectangle_notes") && j["rectangle_notes"].is_array()) {
        for (const auto &rj : j["rectangle_notes"]) {
            RectangleNote rect;
            rect.from_json(rj);
            rectangle_notes.push_back(std::move(rect));
        }
    }

    plain_arrows.clear();
    if (j.contains("plain_arrows") && j["plain_arrows"].is_array()) {
        for (const auto &aj : j["plain_arrows"]) {
            PlainArrowNote arrow;
            arrow.from_json(aj);
            plain_arrows.push_back(std::move(arrow));
        }
    }

    part_number_labels.clear();
    bool has_legacy_label_show = false;
    bool legacy_label_show = false;
    if (j.contains("part_number_labels") && j["part_number_labels"].is_array()) {
        for (const auto &pj : j["part_number_labels"]) {
            bool label_show = false;
            if (json_get_bool(pj, "is_show", label_show)) {
                has_legacy_label_show = true;
                legacy_label_show = legacy_label_show || label_show;
            }
            PartNumberLabel pn;
            pn.from_json(pj);
            part_number_labels.push_back(std::move(pn));
        }
    }

    if (!json_get_bool(j, "show_part_labels", show_part_labels) &&
        !json_get_bool(j, "is_show", show_part_labels) && has_legacy_label_show)
        show_part_labels = legacy_label_show;
}

// ---- KeyFrame ----
void KeyFrame::to_json(nlohmann::json &j) const
{
    j["id"]                = id;
    j["name"]              = name;
    j["is_camera_define"]  = is_camera_define;
    j["view_matrix"]       = transform3d_to_json(view_matrix);
    j["projection_matrix"] = transform3d_to_json(projection_matrix);
    j["camera_target"]     = {camera_target.x(), camera_target.y(), camera_target.z()};
    j["camera_zoom"]       = camera_zoom;
    j["camera_margin_factor"] = camera_margin_factor;
    j["labels_show_type"]  = (int) labels_show_type;
    // Per-object instance assemble matrices. Captured separately so replay can
    nlohmann::json ot_arr = nlohmann::json::array();
    for (const auto &p : object_transformations) {
        nlohmann::json item;
        item["object_id"]      = p.first;
        item["transformation"] = transform3d_to_json(p.second.get_matrix());
        ot_arr.push_back(item);
    }
    j["object_transformations"] = ot_arr;

    nlohmann::json vt_arr = nlohmann::json::array();
    for (const auto &p : volume_transformations) {
        nlohmann::json item;
        item["object_id"]      = p.first.first;
        item["volume_idx"]     = p.first.second;
        auto it = volume_names.find(p.first);
        if (it != volume_names.end())
            item["volume_name"] = it->second;
        item["transformation"] = transform3d_to_json(p.second.get_matrix());
        vt_arr.push_back(item);
    }
    j["volume_transformations"] = vt_arr;

    if (!assembly_note.arrow_svgs.empty() ||
        !assembly_note.text_labels.empty() || !assembly_note.circle_notes.empty() || !assembly_note.rectangle_notes.empty() ||
        !assembly_note.plain_arrows.empty() || !assembly_note.part_number_labels.empty()) {
        nlohmann::json nj;
        assembly_note.to_json(nj);
        j["assembly_note"] = nj;
    }
}

void KeyFrame::from_json(const nlohmann::json &j)
{
    json_get_int(j, "id", id);
    json_get_string(j, "name", name);
    json_get_bool(j, "is_camera_define", is_camera_define);
    if (j.contains("view_matrix"))
        view_matrix = transform3d_from_json(j["view_matrix"]);
    if (j.contains("projection_matrix"))
        projection_matrix = transform3d_from_json(j["projection_matrix"]);
    if (j.contains("camera_target") && j["camera_target"].is_array() && j["camera_target"].size() == 3 &&
        j["camera_target"][0].is_number() && j["camera_target"][1].is_number() && j["camera_target"][2].is_number())
        camera_target = Vec3d(j["camera_target"][0].get<double>(), j["camera_target"][1].get<double>(), j["camera_target"][2].get<double>());
    json_get_double(j, "camera_zoom", camera_zoom);
    json_get_double(j, "camera_margin_factor", camera_margin_factor);
    int labels_show_type_value = (int) LabelsShowType::AutoRecommend;
    if (json_get_int(j, "labels_show_type", labels_show_type_value))
        labels_show_type = (LabelsShowType) labels_show_type_value;

    object_transformations.clear();
    if (j.contains("object_transformations") && j["object_transformations"].is_array()) {
        for (const auto &item : j["object_transformations"]) {
            int obj_id = -1;
            if (!json_get_int(item, "object_id", obj_id) || !item.contains("transformation"))
                continue;
            const Transform3d mat = transform3d_from_json(item["transformation"]);
            object_transformations[obj_id] = Geometry::Transformation(mat);
        }
    }

    volume_transformations.clear();
    volume_names.clear();
    if (j.contains("volume_transformations") && j["volume_transformations"].is_array()) {
        for (const auto &item : j["volume_transformations"]) {
            int obj_id = -1;
            if (!json_get_int(item, "object_id", obj_id) || !item.contains("transformation"))
                continue;
            // Older test fixtures might omit volume_idx; default to 0 so a
            // single-volume object behaves the same as before the rename.
            int volume_idx = 0;
            json_get_int(item, "volume_idx", volume_idx);
            const Transform3d mat = transform3d_from_json(item["transformation"]);
            const std::pair<int, int> key{obj_id, volume_idx};
            volume_transformations[key] = Geometry::Transformation(mat);
            std::string volume_name;
            if (json_get_string(item, "volume_name", volume_name))
                volume_names[key] = volume_name;
        }
    }

    assembly_note = AssemblyNote{};
    if (j.contains("assembly_note") && j["assembly_note"].is_object()) {
        assembly_note.from_json(j["assembly_note"]);
    }
}
// ---- AssembleBaseInfo ----
void AssembleBaseInfo::to_json(nlohmann::json &j) const
{
    j["type"] = get_type();
    j["name"] = name;

    nlohmann::json kf_arr = nlohmann::json::array();
    for (const auto &kf : keyframes) {
        nlohmann::json kf_j;
        kf.to_json(kf_j);
        kf_arr.push_back(kf_j);
    }
    j["keyframes"] = kf_arr;
}

void AssembleBaseInfo::from_json(const nlohmann::json &j)
{
    json_get_string(j, "name", name);

    keyframes.clear();
    if (j.contains("keyframes") && j["keyframes"].is_array()) {
        keyframes.reserve(j["keyframes"].size());
        for (const auto &kf_j : j["keyframes"]) {
            try {
                keyframes.emplace_back();
                keyframes.back().from_json(kf_j);
            } catch (const std::exception &e) {
                if (!keyframes.empty())
                    keyframes.pop_back();
                BOOST_LOG_TRIVIAL(warning) << "AssemblyStepJson: skip invalid keyframe: " << e.what();
            }
        }
    }
}

std::shared_ptr<AssembleBaseInfo> AssembleBaseInfo::create_from_json(const nlohmann::json &j)
{
    std::string type_str;
    json_get_string(j, "type", type_str);

    if (type_str == "sub") {
        auto ptr = std::make_shared<AssembleSub>();
        ptr->from_json(j);
        return ptr;
    } else if (type_str == "single") {
        auto ptr = std::make_shared<AssembleSingleInfo>();
        ptr->from_json(j);
        return ptr;
    }
    return nullptr;
}

// ---- AssembleSingleInfo ----
void AssembleSingleInfo::to_json(nlohmann::json &j) const
{
    AssembleBaseInfo::to_json(j);
    if (object_idx >= 0)
        j["object_idx"] = object_idx;
    if (object_id != 0)
        j["object_id"] = object_id;
}

void AssembleSingleInfo::from_json(const nlohmann::json &j)
{
    AssembleBaseInfo::from_json(j);
    json_get_int(j, "object_idx", object_idx);
    json_get_size_t(j, "object_id", object_id);
}

// ---- AssembleSub ----
void AssembleSub::to_json(nlohmann::json &j) const
{
    AssembleBaseInfo::to_json(j);
    if (id >= 0)
        j["id"] = id;
    j["step"] = step;
    j["is_final_assembly"] = is_final_assembly;
    if (assembly_tree_checked) {
        nlohmann::json checked = nlohmann::json::object();
        for (const auto& item : *assembly_tree_checked)
            checked[item.first] = item.second;
        j["assembly_tree_checked"] = std::move(checked);
    }

    nlohmann::json ch_arr = nlohmann::json::array();
    for (const auto &child : children) {
        nlohmann::json ch_j;
        child->to_json(ch_j);
        ch_arr.push_back(ch_j);
    }
    j["children"] = ch_arr;
}

void AssembleSub::from_json(const nlohmann::json &j)
{
    AssembleBaseInfo::from_json(j);
    json_get_int(j, "id", id);
    json_get_int(j, "step", step);
    json_get_bool(j, "is_final_assembly", is_final_assembly);
    assembly_tree_checked.reset();
    if (j.contains("assembly_tree_checked") && j["assembly_tree_checked"].is_object()) {
        assembly_tree_checked.emplace();
        for (auto it = j["assembly_tree_checked"].begin(); it != j["assembly_tree_checked"].end(); ++it) {
            if (it.value().is_boolean())
                (*assembly_tree_checked)[it.key()] = it.value().get<bool>();
        }
    }

    children.clear();
    if (j.contains("children") && j["children"].is_array()) {
        for (const auto &ch_j : j["children"]) {
            auto child = AssembleBaseInfo::create_from_json(ch_j);
            if (child)
                children.push_back(std::move(child));
        }
    }
}

// ---- AssemblyStepJson ----
std::string AssemblyStepJson::get_debug_file_path() {
    return (boost::filesystem::path(Slic3r::data_dir()) / "cache" / "assembly_step.json").string();
}

void AssemblyStepJson::load_pdf_export_params(const nlohmann::json &root)
{
    m_pdf_export_params = PdfExportParams();
    if (!root.contains("pdf_export") || !root["pdf_export"].is_object())
        return;

    const auto &pdf_export = root["pdf_export"];
    if (pdf_export.contains("title") && pdf_export["title"].is_string())
        m_pdf_export_params.title = pdf_export["title"].get<std::string>();
}

void AssemblyStepJson::load_assembly_part_number_label_font_size(const nlohmann::json &root)
{
    m_assembly_part_number_label_font_size = 0.0f;
    double font_size = 0.0;
    if (json_get_double(root, "assembly_part_number_label_font_size", font_size) && std::isfinite(font_size) && font_size > 0.0)
        m_assembly_part_number_label_font_size = static_cast<float>(font_size);
}

bool AssemblyStepJson::load(const std::string &path)
{
    boost::nowide::ifstream ifs(path);
    if (!ifs.is_open()) {
        BOOST_LOG_TRIVIAL(warning) << "AssemblyStepJson: cannot open " << path;
        return false;
    }

    try {
        nlohmann::json root;
        ifs >> root;

        m_items.clear();
        load_pdf_export_params(root);
        load_assembly_part_number_label_font_size(root);
        if (root.contains("items") && root["items"].is_array()) {
            for (const auto &item_j : root["items"]) {
                auto ptr = AssembleBaseInfo::create_from_json(item_j);
                if (ptr)
                    m_items.push_back(std::move(ptr));
            }
        }
        return true;
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "AssemblyStepJson: parse error: " << e.what();
        return false;
    }
}

bool AssemblyStepJson::load_from_string(const std::string &json_str)
{
    try {
        nlohmann::json root = nlohmann::json::parse(json_str);
        m_items.clear();
        load_pdf_export_params(root);
        load_assembly_part_number_label_font_size(root);
        if (root.contains("items") && root["items"].is_array()) {
            for (const auto &item_j : root["items"]) {
                auto ptr = AssembleBaseInfo::create_from_json(item_j);
                if (ptr)
                    m_items.push_back(std::move(ptr));
            }
        }
        return true;
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "AssemblyStepJson: parse from string error: " << e.what();
        return false;
    }
}

std::string AssemblyStepJson::to_json_string() const
{
    try {
        nlohmann::json root;
        root["version"] = 1;//V1.0 2020610
        root["assembly_part_number_label_font_size"] = m_assembly_part_number_label_font_size;
        root["pdf_export"] = {
            {"title", m_pdf_export_params.title},
        };

        nlohmann::json items_arr = nlohmann::json::array();
        for (const auto &item : m_items) {
            nlohmann::json item_j;
            item->to_json(item_j);
            items_arr.push_back(item_j);
        }
        root["items"] = items_arr;
        return root.dump(2);
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "AssemblyStepJson: to_json_string error: " << e.what();
        return {};
    }
}

bool AssemblyStepJson::save(const std::string &path) const
{
    boost::filesystem::path dir = boost::filesystem::path(path).parent_path();
    if (!boost::filesystem::exists(dir))
        boost::filesystem::create_directories(dir);

    boost::nowide::ofstream ofs(path);
    if (!ofs.is_open()) {
        BOOST_LOG_TRIVIAL(error) << "AssemblyStepJson: cannot write " << path;
        return false;
    }

    try {
        ofs << to_json_string();
        return true;
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "AssemblyStepJson: write error: " << e.what();
        return false;
    }
}

std::string AssemblyStepsTreeData::to_json_string() const
{
    std::function<std::shared_ptr<AssembleBaseInfo>(int)> build_item;
    build_item = [&](int node_idx) -> std::shared_ptr<AssembleBaseInfo> {
        if (node_idx < 0 || node_idx >= (int)nodes.size())
            return nullptr;
        const AssemblyStepsTreeNode& node = nodes[node_idx];

        if (node.type == AssemblyStepsTreeNode::Type::Folder) {
            auto sub = std::make_shared<AssembleSub>();
            sub->name              = node.name;
            sub->is_final_assembly = node.is_final_assembly;
            sub->id                = node.id;
            sub->step              = node.step;
            sub->assembly_tree_checked = node.assembly_tree_checked;
            // Mirror legacy AssemblyStepsUtils::build_steps_json_string — all entries
            for (const auto& kf : node.kf_data.entries)
                sub->keyframes.push_back(kf.data);
            for (int child_idx : node.children) {
                if (auto child_item = build_item(child_idx))
                    sub->children.push_back(std::move(child_item));
            }
            return sub;
        }

        auto single = std::make_shared<AssembleSingleInfo>();
        single->name            = node.name;
        single->object_idx      = node.object_idx;
        single->object_id       = node.object_id;
        for (const auto& kf : node.kf_data.entries)
            single->keyframes.push_back(kf.data);
        return single;
    };

    AssemblyStepJson doc;
    std::vector<std::shared_ptr<AssembleBaseInfo>> items;
    items.reserve(roots.size());
    for (int root_idx : roots) {
        if (auto item = build_item(root_idx))
            items.push_back(std::move(item));
    }
    doc.set_items(std::move(items));
    return doc.to_json_string();
}

// ---- AssemblyStepsTreeData::from_json_string ----
bool AssemblyStepsTreeData::from_json_string(
    const std::string&     json_str,
    AssemblyStepsTreeData& tree,
    const Model&           model,
    std::string*           error,
    float*                 assembly_part_number_label_font_size)
{
    try {
        if (json_str.empty()) {
            if (error != nullptr)
                *error = "empty JSON string";
            return false;
        }

        AssemblyStepJson json_doc;
        if (!json_doc.load_from_string(json_str)) {
            if (error != nullptr)
                *error = "AssemblyStepJson::load_from_string failed";
            return false;
        }
        if (assembly_part_number_label_font_size != nullptr)
            *assembly_part_number_label_font_size = json_doc.get_assembly_part_number_label_font_size();

        AssemblyStepsTreeData parsed;
        const int             object_count = static_cast<int>(model.objects.size());
        std::function<size_t(const std::shared_ptr<AssembleBaseInfo>&)> count_item_nodes;
        count_item_nodes = [&](const std::shared_ptr<AssembleBaseInfo>& item) -> size_t {
            if (!item)
                return 0;
            size_t count = 1;
            if (const auto *sub = dynamic_cast<const AssembleSub*>(item.get())) {
                for (const auto &child : sub->children)
                    count += count_item_nodes(child);
            }
            return count;
        };
        size_t expected_nodes = 0;
        for (const auto &item : json_doc.get_items())
            expected_nodes += count_item_nodes(item);
        parsed.nodes.reserve(expected_nodes);
        parsed.roots.reserve(json_doc.get_items().size());

        // Resolve stable object_id -> current object_idx. The fallback chain mirrors
        auto resolve_object_idx = [&](int hint_idx, size_t object_id, const std::string& name) -> int {
            if (object_id != 0) {
                for (int i = 0; i < object_count; ++i) {
                    const ModelObject* obj = model.objects[i];
                    if (obj != nullptr && obj->id().id == object_id)
                        return i;
                }
                // Intentional fall-through to hint_idx / name match below.
            }
            if (hint_idx >= 0 && hint_idx < object_count) {
                const ModelObject* obj = model.objects[hint_idx];
                if (obj != nullptr && obj->name == name)
                    return hint_idx;
            }
            for (int i = 0; i < object_count; ++i) {
                const ModelObject* obj = model.objects[i];
                if (obj != nullptr && obj->name == name)
                    return i;
            }
            return -1;
        };

        auto next_folder_id = [&]() {
            int max_id = 0;
            for (const auto& n : parsed.nodes)
                if (n.type == AssemblyStepsTreeNode::Type::Folder)
                    max_id = std::max(max_id, n.id);
            return max_id + 1;
        };

        auto create_object_node_for_item = [&](int hint_idx, size_t object_id, const std::string& name) -> int {
            const int resolved = resolve_object_idx(hint_idx, object_id, name);
            if (resolved < 0)
                return -1;
            // Always create a new node — the same ModelObject may appear in
            const int idx = static_cast<int>(parsed.nodes.size());
            parsed.nodes.emplace_back();
            AssemblyStepsTreeNode &obj_node = parsed.nodes.back();
            obj_node.type       = AssemblyStepsTreeNode::Type::Object;
            obj_node.object_idx = resolved;
            const ModelObject* mo = model.objects[resolved];
            obj_node.object_id  = (mo != nullptr ? mo->id().id : 0);
            obj_node.name       = (!name.empty() || mo == nullptr) ? name : mo->name;
            obj_node.visible    = true;
            obj_node.kf_data.node_idx   = idx;
            obj_node.kf_data.object_idx = obj_node.object_idx;
            obj_node.kf_data.is_folder  = false;

            return idx;
        };
        // Copy JSON-side AssembleBaseInfo keyframes into a node's kf_data. For Object
        auto load_node_keyframes = [&](int node_idx, const AssembleBaseInfo& info, bool is_folder) {
            if (node_idx < 0 || node_idx >= (int)parsed.nodes.size())
                return;
            AssemblyStepsTreeNode& node = parsed.nodes[node_idx];
            if (info.keyframes.empty())
                return;

            const AssembleSingleInfo* single =
                is_folder ? nullptr : dynamic_cast<const AssembleSingleInfo*>(&info);
            const bool need_remap =
                single != nullptr && single->object_idx >= 0 && single->object_idx != node.object_idx;

            const int from_obj = single ? single->object_idx : -1;
            const int to_obj   = node.object_idx;

            auto remap_object_idx_in_pair_map = [&](auto &pair_map) {
                if (!need_remap)
                    return;
                using MapType = std::remove_reference_t<decltype(pair_map)>;
                MapType remapped;
                for (auto it = pair_map.begin(); it != pair_map.end(); ++it) {
                    const std::pair<int, int> key = it->first;
                    if (key.first == from_obj)
                        remapped.emplace(std::make_pair(to_obj, key.second), std::move(it->second));
                    else
                        remapped.emplace(key, std::move(it->second));
                }
                pair_map = std::move(remapped);
            };
            // object_transformations is keyed by a single int (object_idx) so the pair-map helper above does not apply; do the equivalent remap here.
            auto remap_object_idx_in_int_map = [&](auto &int_map) {
                if (!need_remap)
                    return;
                using MapType = std::remove_reference_t<decltype(int_map)>;
                MapType remapped;
                for (auto it = int_map.begin(); it != int_map.end(); ++it) {
                    const int key = it->first;
                    if (key == from_obj)
                        remapped.emplace(to_obj, std::move(it->second));
                    else
                        remapped.emplace(key, std::move(it->second));
                }
                int_map = std::move(remapped);
            };

            node.kf_data.entries.reserve(node.kf_data.entries.size() + info.keyframes.size());
            for (const auto& kf : info.keyframes) {
                node.kf_data.entries.emplace_back();
                KeyFrameEntry &entry = node.kf_data.entries.back();
                entry.need_save = true;
                entry.data      = kf;
                remap_object_idx_in_int_map(entry.data.object_transformations);
                remap_object_idx_in_pair_map(entry.data.volume_transformations);
                remap_object_idx_in_pair_map(entry.data.volume_names);
            }
        };

        std::function<int(const std::shared_ptr<AssembleBaseInfo>&)> restore_item;
        restore_item = [&](const std::shared_ptr<AssembleBaseInfo>& item) -> int {
            if (!item)
                return -1;

            if (item->get_type() == "sub") {
                const auto* sub = dynamic_cast<const AssembleSub*>(item.get());
                if (sub == nullptr)
                    return -1;

                const int folder_idx = static_cast<int>(parsed.nodes.size());
                parsed.nodes.emplace_back();
                AssemblyStepsTreeNode &folder = parsed.nodes.back();
                folder.type            = AssemblyStepsTreeNode::Type::Folder;
                folder.id              = (sub->id >= 0) ? sub->id : next_folder_id();
                folder.step            = sub->step;
                folder.name            = sub->name;
                folder.is_final_assembly = sub->is_final_assembly;
                folder.assembly_tree_checked = sub->assembly_tree_checked;
                folder.kf_data.node_idx   = folder_idx;
                folder.kf_data.object_idx = folder.object_idx;
                folder.kf_data.is_folder  = true;
                folder.children.reserve(sub->children.size());

                load_node_keyframes(folder_idx, *sub, /*is_folder=*/true);

                for (const auto& child : sub->children) {
                    const int child_idx = restore_item(child);
                    if (child_idx >= 0)
                        parsed.nodes[folder_idx].children.push_back(child_idx);
                }
                return folder_idx;
            }

            if (item->get_type() == "single") {
                const auto* single = dynamic_cast<const AssembleSingleInfo*>(item.get());
                if (single == nullptr)
                    return -1;
                const int node_idx = create_object_node_for_item(single->object_idx, single->object_id, item->name);
                if (node_idx >= 0) {
                    load_node_keyframes(node_idx, *single, /*is_folder=*/false);
                }
                return node_idx;
            }

            return -1;
        };

        for (const auto& item : json_doc.get_items()) {
            const int idx = restore_item(item);
            if (idx >= 0)
                parsed.roots.push_back(idx);
        }

        // One-line summary so 3MF reopen empty-folder regressions show up in the
        size_t total_children = 0;
        for (int root_idx : parsed.roots) {
            if (root_idx >= 0 && root_idx < (int) parsed.nodes.size())
                total_children += parsed.nodes[root_idx].children.size();
        }
        BOOST_LOG_TRIVIAL(info) << "AssemblyStepsTreeData::from_json_string loaded nodes="
                                << parsed.nodes.size() << " roots=" << parsed.roots.size()
                                << " total_root_children=" << total_children;

        tree = std::move(parsed);
        return true;
    } catch (const std::exception& e) {
        if (error != nullptr)
            *error = e.what();
        return false;
    }
}

} // namespace Slic3r
