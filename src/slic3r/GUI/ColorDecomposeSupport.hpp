#ifndef slic3r_GUI_ColorDecomposeSupport_hpp_
#define slic3r_GUI_ColorDecomposeSupport_hpp_

#include <set>
#include <string>
#include <vector>
#include <wx/string.h>
#include <wx/colour.h>

class wxWindow;

namespace Slic3r {
class Preset;
namespace GUI {

// ---- Constants ----

inline constexpr const char* kDecomposePlaBasicType     = "PLA Basic";
inline constexpr const char* kDecomposePlaPaletteType   = "PLA Palette";
inline constexpr const char* kDecomposePetgBasicType    = "PETG Basic";
inline constexpr const char* kDecomposePlaShortType     = "PLA";
inline constexpr const char* kDecomposePetgShortType    = "PETG";
inline constexpr const char* kDecomposePlaFilamentId    = "GFA00";
inline constexpr const char* kDecomposePlaPaletteFilamentId = "GFA20";
inline constexpr const char* kDecomposePetgFilamentId   = "GFG00";
inline constexpr const char* kDecomposeBambuPresetPrefix = "Bambu ";

// ---- Types ----

enum class DecomposeBaseColor {
    None,
    Cyan,
    Magenta,
    Yellow,
    White,
    Red,
    Green,
    Blue,
    Black
};

struct DecomposeOfficialComponent {
    DecomposeBaseColor base_color{DecomposeBaseColor::None};
    std::string        color_hex;
    std::string        filament_id;
};

struct OfficialMixingKitGroup {
    std::string        diameter;
    // 0-based project config indices, ordered like pla_palette_cmykw_official_components().
    std::vector<int>   config_indices;

    bool complete() const;
    size_t present_count() const;
};

struct OfficialMixingKitAddPlan {
    size_t              missing_count{0};
    size_t              required_delete_count{0};
    // Unused physical filaments removed before adding the kit, 0-based config indices in descending order.
    std::vector<size_t> delete_indices;
    std::vector<std::string> delete_colors;
    bool                kit_already_present{false};
    bool                can_add{true};

    bool needs_delete() const { return required_delete_count > 0; }
};

enum class DecomposeColorBlockReason {
    None,
    Gradient,
    Transparent,
    MultiColor
};

DecomposeColorBlockReason decompose_color_block_reason(int filament_idx);

// ---- Functions ----

std::string decompose_normalize_color_hex(std::string color);

const char* decompose_base_color_en(DecomposeBaseColor color);

std::string decompose_basic_filament_id(const std::string& basic_type);

void set_created_standard_component_metadata(size_t config_idx, const DecomposeOfficialComponent& component);

DecomposeOfficialComponent lookup_decompose_official_component(
    const std::string& basic_type,
    DecomposeBaseColor base_color,
    const wxColour& fallback);

std::string find_decompose_standard_preset_name(size_t source_config_idx, const std::string& basic_type);

// Make every instantiated system preset in the official series visible and persist
// hidden ones to SECTION_FILAMENTS, matching the system-materials wizard (grouped
// by shortName). Returns whether the active printer has a compatible series preset.
bool ensure_official_series_installed(const std::string& basic_type);

// Returns "PLA Basic" / "PLA Palette" / "PETG Basic" when preset_name names an
// official Bambu standard-mode filament series, else an empty string.
std::string official_basic_type_from_preset_name(const std::string& preset_name);

// Resolve display type for color-decompose: an official Bambu series overrides
// get_filament_type when the preset name matches; empty/missing -> "PLA".
// This override matters for PLA Palette, whose filament_type is plain "PLA"
// (inherited from fdm_filament_common) and would otherwise be indistinguishable
// from PLA Basic.
std::string filament_type_for_color_decompose(Preset* preset);

// The source slot is eligible: a 100% official base (or a mix that uses that
// base) should reuse the existing filament instead of duplicating it.
int find_existing_decompose_component(
    const DecomposeOfficialComponent& component,
    const std::vector<std::string>& physical_colors,
    const std::vector<size_t>& physical_config_indices);

// Official PLA Palette CMYKW mixing kit, one entry per base (white / cyan / magenta / yellow / black).
std::vector<DecomposeOfficialComponent> pla_palette_cmykw_official_components();

// Groups project PLA Palette CMYKW filaments by effective nozzle diameter.
std::vector<OfficialMixingKitGroup> collect_official_mixing_kit_groups(const std::set<size_t>& excluded_indices = {});

// Returns the preferred complete group, or nullptr when no same-diameter kit is complete.
const OfficialMixingKitGroup* find_complete_official_mixing_kit(const std::vector<OfficialMixingKitGroup>& groups);

// Computes the space needed for the missing kit filaments. Unused filaments are selected for
// deletion when the project is full; protected_indices are 0-based config indices to keep.
OfficialMixingKitAddPlan plan_official_mixing_kit_addition(size_t reserve_slots = 0,
                                                           const std::set<size_t>& protected_indices = {});

// Deletes the unused filaments selected by plan, then appends every missing kit filament.
// Returns false, after telling the user, when the active printer has no compatible PLA Palette
// preset or the plan cannot fit the kit.
bool add_official_mixing_kit_to_project(wxWindow* parent, const OfficialMixingKitAddPlan& plan,
                                        size_t reserve_slots = 0);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_ColorDecomposeSupport_hpp_
