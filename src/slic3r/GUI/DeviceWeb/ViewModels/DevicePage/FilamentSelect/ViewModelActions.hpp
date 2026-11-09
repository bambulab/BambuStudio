/*****************************************************************//**
 * \file   ViewModelActions.hpp
 * \brief  User actions of the Select Filament dialog (`action` submod).
 *
 * The page only sends semantic actions; every confirm is re-validated here
 * against the snapshot and the live Filament Manager store before the dialog
 * is closed.
 *********************************************************************/
#ifndef FILAMENTSELECTVIEWMODELACTIONS_HPP
#define FILAMENTSELECTVIEWMODELACTIONS_HPP

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI {

class FilamentSelectVM;

class FilamentSelectActions
{
public:
    static nlohmann::json Handle(FilamentSelectVM&      vm,
                                 const std::string&     action,
                                 const nlohmann::json&  payload);

private:
    static nlohmann::json Confirm(FilamentSelectVM& vm, const nlohmann::json& payload);
    static nlohmann::json Close(FilamentSelectVM& vm);
};

}} // namespace Slic3r::GUI

#endif // FILAMENTSELECTVIEWMODELACTIONS_HPP
