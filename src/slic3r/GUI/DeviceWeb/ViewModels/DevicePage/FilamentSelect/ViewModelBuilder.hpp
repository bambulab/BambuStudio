/*****************************************************************//**
 * \file   ViewModelBuilder.hpp
 * \brief  Builds the Select Filament snapshot and its display schema.
 *
 * The snapshot is taken once when the dialog opens (the dialog does not follow
 * later Filament Manager changes, same as the native dialog). BuildState() only
 * fills Schema structs; it never assembles JSON by hand.
 *********************************************************************/
#ifndef FILAMENTSELECTVIEWMODELBUILDER_HPP
#define FILAMENTSELECTVIEWMODELBUILDER_HPP

#include "Schema.hpp"

#include "slic3r/GUI/FilamentSelectCommon.hpp"
#include "slic3r/GUI/FilamentSelectTypes.hpp"

#include <cstddef>
#include <string>

namespace Slic3r { namespace GUI {

struct FilamentSelectSnapshot
{
    /// Spool rows in display order; the row_key of a row is its index here.
    FilamentSelectCommon::SpoolRowList rows;
    FilamentSelectCommon::PresetGroups presets;
    /// System preset aliases the caller offered; confirm only accepts these.
    wxArrayString                      filament_items;
    /// Preselected alias; empty unless it is one of filament_items.
    wxString                           checked_alias;
};

class FilamentSelectBuilder
{
public:
    static FilamentSelectSnapshot BuildSnapshot(const FilamentSelectInput& in);
    static FilamentSelectSchema::format::State BuildState(const FilamentSelectSnapshot& snapshot, bool dark);

    static std::string RowKey(std::size_t index);
    /// Returns false when `key` is not a row key of the form produced by RowKey().
    static bool ParseRowKey(const std::string& key, std::size_t& index);
};

}} // namespace Slic3r::GUI

#endif // FILAMENTSELECTVIEWMODELBUILDER_HPP
