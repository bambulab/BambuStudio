#include <iostream>
#include <vector>
#include <string>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/algorithm/string/predicate.hpp>

bool write_to_pot(boost::filesystem::path path, const std::vector<std::pair<std::string, std::string>>& data, const std::string& ini_filename)
{
    boost::nowide::ofstream file(path.string(), std::ios_base::app);
    if (!file.is_open()) {
        std::cout << "HINTS_TO_POT FAILED: CANNOT OPEN POT FILE" << std::endl;
        return false;
    }

    for (const auto& element : data)
    {
        file << "\n#: resources/data/" << ini_filename << ": [" << element.first << "]\n"
            << "msgid \"" << element.second << "\"\n"
            << "msgstr \"\"\n";
    }
    file.close();
    return true;
}

bool read_hints_ini(boost::filesystem::path path, std::vector<std::pair<std::string, std::string>>& pot_elements)
{
    namespace pt = boost::property_tree;
    pt::ptree tree;
    boost::nowide::ifstream ifs(path.string());
    if (!ifs.is_open()) {
        std::cout << "HINTS_TO_POT FAILED: CANNOT OPEN INI FILE - " << path.string() << std::endl;
        return false;
    }

    try {
        pt::read_ini(ifs, tree);
    }
    catch (const boost::property_tree::ini_parser::ini_parser_error& err) {
        std::cout << err.what() << std::endl;
        return false;
    }

    for (const auto& section : tree) {
        if (boost::starts_with(section.first, "hint:")) {
            for (const auto& data : section.second) {
                if (data.first == "text")
                {
                    pot_elements.emplace_back(section.first, data.second.data());
                    break;
                }
            }
        }
    }
    return true;
}

int main(int argc, char* argv[])
{
    const std::vector<std::string> ini_filenames = { "hints.ini", "helio_hints.ini" };
    std::vector<std::pair<std::string, std::string>> data;
    boost::filesystem::path path_to_ini;
    boost::filesystem::path path_to_pot;

    if (argc != 3)
    {
        std::cout << "HINTS_TO_POT FAILED: WRONG NUM OF ARGS" << std::endl;
        return -1;
    }

    try {
        path_to_pot = boost::filesystem::canonical(boost::filesystem::path(argv[2])).parent_path() / "i18n" / "BambuStudio.pot";

        for (const auto& ini_name : ini_filenames) {
            path_to_ini = boost::filesystem::canonical(boost::filesystem::path(argv[1])).parent_path()
                / "resources" / "data" / ini_name;

            if (!boost::filesystem::exists(path_to_ini)) {
                std::cout << "HINTS_TO_POT FAILED: PATH TO INI DOES NOT EXISTS - " << path_to_ini.string() << std::endl;
                return -1;
            }

            data.clear();
            if (!read_hints_ini(path_to_ini, data)) {
                std::cout << "HINTS_TO_POT FAILED TO READ - " << ini_name << std::endl;
                return -1;
            }

            if (!write_to_pot(path_to_pot, data, ini_name)) {
                std::cout << "HINTS_TO_POT FAILED TO WRITE - " << ini_name << std::endl;
                return -1;
            }

            std::cout << "HINTS_TO_POT PROCESSED: " << ini_name << std::endl;
        }
    }
    catch (std::exception& e) {
        std::cout << "HINTS_TO_POT FAILED: BOOST CANNONICAL - " << e.what() << std::endl;
        return -1;
    }

    std::cout << "HINTS_TO_POT SUCCESS: ALL FILES PROCESSED" << std::endl;
    return 0;
}
