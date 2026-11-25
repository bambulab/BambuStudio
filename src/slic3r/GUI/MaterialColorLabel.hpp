#pragma once

#include "EncodedFilament.hpp"

namespace Slic3r { namespace GUI {

// Keep the declared color order: a gradient describes a transition, not a set.
inline wxString format_material_color_label(const wxString& name, const FilamentColor& filament_color,
                                           const wxString& unknown)
{
    wxString values;
    const wxString separator = filament_color.m_color_type == FilamentColor::ColorType::GRADIENT_CLR
        ? " -> " : ", ";
    for (const auto& color : filament_color.GetColors()) {
        if (!values.IsEmpty()) values += separator;
        values += color.IsOk()
            ? wxString::Format("#%02X%02X%02X%02X", unsigned(color.Red()), unsigned(color.Green()),
                               unsigned(color.Blue()), unsigned(color.Alpha()))
            : unknown;
    }
    if (values.IsEmpty()) return name.IsEmpty() ? unknown : name;
    return name.IsEmpty() ? "(" + values + ")" : name + " (" + values + ")";
}

}} // namespace Slic3r::GUI
