#include "Model.hpp"

#include <boost/filesystem.hpp>

namespace Slic3r {

std::string Model::propose_export_file_name_and_path() const
{
    std::string input_file;
    for (const ModelObject *model_object : this->objects)
        for (ModelInstance *model_instance : model_object->instances)
            if (model_instance->is_printable()) {
                input_file = model_object->get_export_filename();
                if (!input_file.empty())
                    goto end;
                break;
            }
end:
    return input_file;
}

std::string Model::propose_export_file_name_and_path(const std::string &new_extension) const
{
    return boost::filesystem::path(this->propose_export_file_name_and_path()).replace_extension(new_extension).string();
}

std::string ModelObject::get_export_filename() const
{
    std::string ret = input_file;

    if (!name.empty()) {
        if (ret.empty())
            ret = name;
        else
            ret = (boost::filesystem::path(name).parent_path().empty()) ? (boost::filesystem::path(ret).parent_path() / name).make_preferred().string() : name;
    }

    return ret;
}

} // namespace Slic3r
