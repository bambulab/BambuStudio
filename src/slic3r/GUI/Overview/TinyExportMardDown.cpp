#include "TinyExportMardDown.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

namespace Slic3r {
namespace GUI {

namespace {

std::string escape_markdown_inline(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
        case '\\': case '`': case '*': case '_': case '{': case '}':
        case '[': case ']': case '(': case ')': case '#': case '+':
        case '-': case '.': case '!': case '|':
            out.push_back('\\');
            out.push_back(ch);
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::string md_image_line(const std::string &alt, const std::string &rel_path)
{
    return "![" + escape_markdown_inline(alt) + "](" + rel_path + ")\n\n";
}

bool copy_image_file(const std::string &src, const boost::filesystem::path &dest)
{
    namespace fs = boost::filesystem;
    boost::system::error_code ec;
    if (src.empty() || !fs::exists(src, ec))
        return false;
    fs::create_directories(dest.parent_path(), ec);
    fs::copy_file(src, dest, fs::copy_option::overwrite_if_exists, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(warning) << "assembly markdown export: failed to copy image "
                                   << src << " -> " << dest.string()
                                   << ", error=" << ec.message();
        return false;
    }
    return true;
}

std::string assets_dir_name(const boost::filesystem::path &md_path)
{
    const std::string stem = md_path.stem().string();
    return stem.empty() ? "assembly_guide_images" : (stem + "_images");
}

struct FrameInfo
{
    size_t      img_idx;
    int         step_idx;
    int         sub_idx;
    std::string step_title;
};

} // namespace

bool TinyExportMardDown::build(const AssemblyMarkdownExportParams &params)
{
    namespace fs = boost::filesystem;
    if (params.md_filename.empty() || params.frame_images.empty())
        return false;

    const fs::path md_path(params.md_filename);
    const fs::path assets_dir = md_path.parent_path() / assets_dir_name(md_path);
    const std::string assets_rel = assets_dir_name(md_path);

    boost::system::error_code ec;
    fs::create_directories(assets_dir, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "assembly markdown export: failed to create assets dir "  << assets_dir.string() << ", error=" << ec.message();
        return false;
    }

    std::vector<FrameInfo> frames;
    {
        int prev_step   = -1;
        int sub_counter = 0;
        for (size_t i = 0; i < params.frame_images.size(); ++i) {
            int si = (i < params.step_indices.size()) ? params.step_indices[i] : static_cast<int>(i + 1);
            if (si != prev_step) {
                sub_counter = 0;
                prev_step   = si;
            }
            ++sub_counter;
            std::string title = (i < params.page_titles.size() && !params.page_titles[i].empty())
                ? params.page_titles[i]
                : (params.step_label_prefix + " " + std::to_string(si));
            frames.push_back({ i, si, sub_counter, std::move(title) });
        }
    }

    auto step_frame_count = [&](int step_idx) -> int {
        int count = 0;
        for (const auto &f : frames)
            if (f.step_idx == step_idx)
                ++count;
        return count;
    };

    std::vector<std::string> copied_frame_paths;
    copied_frame_paths.reserve(params.frame_images.size());
    for (size_t i = 0; i < params.frame_images.size(); ++i) {
        const std::string filename = "frame_" + std::to_string(i + 1) + ".png";
        const fs::path dest = assets_dir / filename;
        if (!copy_image_file(params.frame_images[i], dest))
            return false;
        copied_frame_paths.push_back(assets_rel + "/" + filename);
    }

    boost::nowide::ofstream ofs(params.md_filename, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        BOOST_LOG_TRIVIAL(error) << "assembly markdown export: failed to open " << params.md_filename;
        return false;
    }

    const std::string title = params.project_title.empty() ? "Untitled" : params.project_title;
    // Centered main title with the "---- Assembly Guide" subtitle right-aligned
    // below it (mirrors the PDF / video cover layout). Use Pandoc "native div"
    // fences (::: {style=...}): Zettlr and Pandoc honor these and apply the
    // alignment on export, while the editor shows a clean real Markdown heading
    // instead of literal <h1>/<div> tags. Markdown inside the fence is parsed,
    // so the title stays a proper heading.
    ofs << "::: {style=\"text-align: center\"}\n\n# " << escape_markdown_inline(title) << "\n\n:::\n\n";
    if (!params.subtitle.empty())
        ofs << "::: {style=\"text-align: right\"}\n\n" << escape_markdown_inline(params.subtitle) << "\n\n:::\n\n";

    if (!params.cover_image_path.empty()) {
        const fs::path cover_dest = assets_dir / "cover.png";
        if (copy_image_file(params.cover_image_path, cover_dest))
            ofs << md_image_line(title, assets_rel + "/cover.png");
    }

    if (!params.second_page_image_path.empty()) {
        const fs::path second_dest = assets_dir / "second_page.png";
        if (copy_image_file(params.second_page_image_path, second_dest)) {
            ofs << "---\n\n";
            ofs << md_image_line("Second page", assets_rel + "/second_page.png");
        }
    }

    ofs << "---\n\n";

    for (size_t fi = 0; fi < frames.size(); ++fi) {
        const auto &f = frames[fi];
        const int total_in_step     = step_frame_count(f.step_idx);
        const bool multi_frame      = (total_in_step > 1);
        const bool is_first_of_step = (f.sub_idx == 1);

        if (is_first_of_step && multi_frame)
            ofs << "## " << escape_markdown_inline(f.step_title) << "\n\n";

        std::string label;
        if (multi_frame)
            label = params.step_label_prefix + " " + std::to_string(f.step_idx) + "." + std::to_string(f.sub_idx);
        else
            label = f.step_title;

        if (multi_frame)
            ofs << "### " << escape_markdown_inline(label) << "\n\n";
        else
            ofs << "## " << escape_markdown_inline(label) << "\n\n";

        ofs << md_image_line("", copied_frame_paths[f.img_idx]);
    }
    if (!ofs) {
        BOOST_LOG_TRIVIAL(error) << "assembly markdown export: failed to write " << params.md_filename;
        return false;
    }
    BOOST_LOG_TRIVIAL(info) << "assembly markdown export: saved to " << params.md_filename << ", images=" << assets_dir.string();
    return true;
}

} // namespace GUI
} // namespace Slic3r
