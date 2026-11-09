/*****************************************************************//**
 * \file   PageFilamentSelect.hpp
 * \brief  Modal wx host of the Web Select Filament dialog.
 *********************************************************************/
#ifndef PAGEFILAMENTSELECT_HPP
#define PAGEFILAMENTSELECT_HPP

#include "slic3r/GUI/FilamentSelectTypes.hpp"

#include <functional>

class wxDialog;
class wxWindow;

namespace Slic3r { namespace GUI {

/// Shows the Web dialog modally and returns what the user confirmed (an
/// invalid result when cancelled). `place` positions the dialog before it is
/// shown; it may be empty.
FilamentSelectResult run_filament_select_web_dialog(wxWindow*                             parent,
                                                    const FilamentSelectInput&            in,
                                                    const std::function<void(wxDialog&)>& place);

}} // namespace Slic3r::GUI

#endif // PAGEFILAMENTSELECT_HPP
