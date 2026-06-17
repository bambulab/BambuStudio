#include "Exception.hpp"
#include "PrintBase.hpp"

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#include "I18N.hpp"

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

void PrintBase::update_object_placeholders(DynamicConfig &config, const std::string &default_ext) const
{
    std::string              input_file;
    std::vector<std::string> v_scale;
    int                      num_objects   = 0;
    int                      num_instances = 0;
    for (const ModelObject *model_object : m_model.objects) {
        ModelInstance *printable = nullptr;
        for (ModelInstance *model_instance : model_object->instances)
            if (model_instance->is_printable()) {
                printable = model_instance;
                ++num_instances;
            }
        if (printable) {
            ++num_objects;
            v_scale.push_back("x:" + boost::lexical_cast<std::string>(printable->get_scaling_factor(X) * 100) + "% y:" +
                              boost::lexical_cast<std::string>(printable->get_scaling_factor(Y) * 100) + "% z:" +
                              boost::lexical_cast<std::string>(printable->get_scaling_factor(Z) * 100) + "%");
            if (input_file.empty())
                input_file = model_object->name.empty() ? model_object->input_file : model_object->name;
        }
    }

    config.set_key_value("num_objects", new ConfigOptionInt(num_objects));
    config.set_key_value("num_instances", new ConfigOptionInt(num_instances));

    config.set_key_value("scale", new ConfigOptionStrings(v_scale));
    if (! input_file.empty()) {
        const std::string input_filename      = boost::filesystem::path(input_file).filename().string();
        const std::string input_filename_base = input_filename.substr(0, input_filename.find_last_of("."));
        config.set_key_value("input_filename", new ConfigOptionString(input_filename_base + default_ext));
        config.set_key_value("input_filename_base", new ConfigOptionString(input_filename_base));
    }
}

std::string PrintBase::output_filename(const std::string &format, const std::string &default_ext, const std::string &filename_base,
                                       const DynamicConfig *config_override) const
{
    DynamicConfig cfg;
    if (config_override != nullptr)
        cfg = *config_override;
    cfg.set_key_value("version", new ConfigOptionString(std::string(SLIC3R_VERSION)));
    PlaceholderParser::update_timestamp(cfg);
    this->update_object_placeholders(cfg, default_ext);
    if (! filename_base.empty()) {
        cfg.set_key_value("input_filename", new ConfigOptionString(filename_base + default_ext));
        cfg.set_key_value("input_filename_base", new ConfigOptionString(filename_base));
    }
    try {
        boost::filesystem::path filename =
            format.empty() ? cfg.opt_string("input_filename_base") + default_ext : this->placeholder_parser().process(format, 0, &cfg);
        if (filename.extension().empty())
            filename = boost::filesystem::change_extension(filename, default_ext);
        return filename.string();
    } catch (std::runtime_error &err) {
        throw Slic3r::PlaceholderParserError(L("Failed processing of the filename_format template.") + "\n" + err.what());
    }
}

std::string PrintBase::output_filepath(const std::string &path, const std::string &filename_base) const
{
    if (path.empty())
        return (boost::filesystem::path(m_model.propose_export_file_name_and_path()).parent_path() / this->output_filename(filename_base))
            .make_preferred()
            .string();

    boost::filesystem::path p(path);
    if (boost::filesystem::is_directory(p))
        return (p / this->output_filename(filename_base)).make_preferred().string();

    return path;
}

} // namespace Slic3r
