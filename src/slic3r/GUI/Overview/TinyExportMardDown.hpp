#ifndef slic3r_TinyExportMardDown_hpp_
#define slic3r_TinyExportMardDown_hpp_

#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

struct AssemblyMarkdownExportParams
{
    std::string              md_filename;
    std::string              project_title;
    std::string              subtitle;
    std::string              cover_image_path;
    std::string              second_page_image_path;
    std::vector<std::string> frame_images;
    std::vector<std::string> page_titles;
    std::vector<int>         step_indices;
    std::string              step_label_prefix;
};

class TinyExportMardDown
{
public:
    static bool build(const AssemblyMarkdownExportParams &params);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_TinyExportMardDown_hpp_
