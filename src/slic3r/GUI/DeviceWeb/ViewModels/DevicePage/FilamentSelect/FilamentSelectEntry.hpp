/*****************************************************************//**
 * \file   FilamentSelectEntry.hpp
 * \brief  Single entry point of the Select Filament dialog.
 *
 * Picks the Web dialog or the native FilamentSelectDialog according to
 * filament_select_use_web(), cascades it from `parent`, runs it modally and
 * returns the confirmed result (invalid when cancelled).
 *********************************************************************/
#ifndef FILAMENTSELECTENTRY_HPP
#define FILAMENTSELECTENTRY_HPP

#include "slic3r/GUI/FilamentSelectTypes.hpp"

class wxWindow;

namespace Slic3r { namespace GUI {

FilamentSelectResult show_filament_select_dialog(wxWindow* parent, const FilamentSelectInput& in);

}} // namespace Slic3r::GUI

#endif // FILAMENTSELECTENTRY_HPP
