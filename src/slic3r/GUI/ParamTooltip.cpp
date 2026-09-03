#include "ParamTooltip.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp" // complete type for the MainFrame*->wxWindow* upcast in the ctor; clangd wrongly flags this as unused
#include "I18N.hpp"
#include "OptionsGroup.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "wxExtensions.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"

#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <wx/region.h>
#include <wx/display.h>
#include <wx/settings.h>
#include <wx/utils.h>
#include <wx/clipbrd.h>
#include <wx/panel.h>
#ifdef __WIN32__
#include <wx/msw/wrapwin.h>
#endif

#include <unordered_map>

namespace Slic3r::GUI {

namespace {
struct Palette
{
    wxColour card_bg, border, title, description, details, note, divider, link, optkey_fg, optkey_bg;
};

// Light palette expressed with the shared semantic tokens (Widgets/StateColor.hpp).
const Palette &light_palette()
{
    static const Palette p{
        ThemeColor::White,       // card_bg
        ThemeColor::Grey350,     // border (card outline; visible against a light panel)
        ThemeColor::TextPrimary, // title
        ThemeColor::TextPrimary, // description
        ThemeColor::TextMuted,   // details (== Grey700)
        ThemeColor::Warning,     // note (remind)
        ThemeColor::Grey350,     // divider
        ThemeColor::BrandGreen,  // link
        ThemeColor::Grey500,     // optkey_fg
        ThemeColor::Grey300,     // optkey_bg
    };
    return p;
}

// Dark palette
const Palette &dark_palette()
{
    auto                 d = [](const wxColour &c) { return StateColor::darkModeColorFor(c); };
    const Palette       &l = light_palette();
    static const Palette p{
        d(l.card_bg),
        d(l.border),
        d(l.title),
        d(l.description),
        d(l.details),
        d(l.note),
        d(l.divider),
        d(l.link),
        wxColour(0xB3, 0xB3, 0xB4), // optkey_fg
        wxColour(0x3F, 0x3F, 0x46), // optkey_bg
    };
    return p;
}

// Spacing/size tokens
constexpr int CARD_WIDTH       = 300; // total card width incl. padding
constexpr int IMAGE_W          = 256; // uniform display width for every image (DIP), centered
constexpr int PAD              = 12;  // inner padding
constexpr int GAP              = 8;   // vertical gap between rows
constexpr int CARD_RADIUS      = 4;
constexpr int ANCHOR_GAP       = 8; // horizontal gap between the option row and the card's left edge

constexpr int SHOW_DELAY_MS = 200;
constexpr int HIDE_DELAY_MS = 100;

// Bullet marker prefixed to every line of the details block. The trailing space is part of the
// marker, not a sizer spacer, so the text column sits exactly one space after the bullet the way a
// rendered markdown list does, measured in the details font at whatever the current DPI is.
const wxString BULLET_MARKER = "* ";

// Copy-icon "copied!" feedback: crossfade to a check mark, hold, crossfade back.
constexpr int COPY_ICON_PX    = 14;  // icon side length (DIP)
constexpr int COPY_FRAME_MS   = 20;  // one crossfade frame
constexpr int COPY_FADE_STEPS = 6;   // frames per crossfade (~120 ms each way)
constexpr int COPY_HOLD_MS    = 900; // how long the check mark stays before fading back
} // namespace

// ----------------------------------------------------------------------------
// ParamTipStore — opt_key -> per-parameter content not derivable from ConfigOptionDef.
// image (basename under resources/tooltip/images, no _dark/.png)
// description / details / note are localized at store-build time via _L(), so
// ParamTooltip::Shutdown() drops the store and the next show rebuilds it after a language switch.
// The wiki link comes from the caller's live label_path, not from the store.
// Any text field may be empty — the card then falls back to ConfigOptionDef.
// ----------------------------------------------------------------------------
namespace {
struct ParamTipEntry
{
    wxString    description;
    std::string image;
    wxString    details;
    wxString    note;
    // Per-combobox-value tips
    std::vector<std::pair<std::string, wxString>> value_tips;
};

using TipMap = std::unordered_map<std::string, ParamTipEntry>;

// Build one entry. image (basename under resources/tooltip/images) is a stable identifier;
// Any empty text field falls back to ConfigOptionDef when the card renders.
ParamTipEntry make_entry(std::string image, wxString description = {}, wxString details = {}, wxString note = {})
{
    ParamTipEntry e;
    e.image       = std::move(image);
    e.description = std::move(description);
    e.details     = std::move(details);
    e.note        = note.IsEmpty() ? note : _L("Note: ") + note;
    return e;
}

class ParamTipStore
{
public:
    static const ParamTipStore &get();
    static void                 reset(); // drop the singleton so the next get() rebuilds
    const ParamTipEntry        *find(const std::string &opt_key) const;

private:
    ParamTipStore();
    TipMap m_map;
};

ParamTipStore::ParamTipStore()
{
    m_map = {
        // --- Quality ---
        {"layer_height",
         make_entry("layer_height", _L("Controls the printed height of each layer."),
                    _L("A larger layer height prints faster but shows more obvious layer lines.\n"
                       "A smaller layer height gives finer curved surfaces but takes longer to print.\n"
                       "If the layer height is too small while the speed is too high, extrusion can't keep up and the surface is left starved and fuzzy."),
                    _L("Recommended layer height is 20%-70% of the nozzle diameter."))},
        {"initial_layer_print_height",
         make_entry("initial_layer_print_height",
                    _L("Controls the thickness of the model's first layer extruded onto the heat bed, affecting how well the first layer bonds to the plate."),
                    _L("Too small, and the first layer is sensitive to bed flatness and prone to print defects.\n"
                       "Too large, and the first layer's width-to-height ratio becomes too small, which hurts material adhesion."),
                    _L("Usually set to half the nozzle diameter."))},
        {"enable_mixed_color_sublayer", make_entry("enable_mixed_color_sublayer",
                                                   _L("For the mixed-color filament feature. When enabled, layers that use mixed-color filament are split proportionally into "
                                                      "thinner sub-layers for a more even color blend."),
                                                   _L("When off, colors change per full layer, so the blend is coarser and less even.\n"
                                                      "When on, the sub-layers interleave more densely for a more even blend, but printing takes longer."),
                                                   _L("Recommended only when using mixed-color filament, to get a better layered-color effect."))},
        {"seam_position", make_entry("seam_position", _L("Controls where each layer's outer wall starts and ends."),
                                     _L("That point can leave a small blob or seam line, so choosing a suitable position reduces the seam's impact on the model's appearance."))},
        {"top_one_wall_type",
         make_entry("top_one_wall_type",
                    _L("Prints only a single wall loop on top surfaces so the top is filled as much as possible by the infill pattern, for a more consistent look."),
                    _L("On sloped or domed surfaces this option worsens the stair-stepping, so it is not recommended there."), _L("Recommended only for flat top surfaces."))},
        {"only_one_wall_first_layer", make_entry("",
                                                 _L("Uses only a single wall on the model's first layer (the one touching the bed) so the largest possible area is filled by the "
                                                    "first-layer infill pattern, for a more consistent look."),
                                                 _L("When off, the first layer uses multiple wall loops.\n"
                                                    "When on, the infill is more continuous and the bottom is flatter, but the first-layer edge outline is slightly weaker."))},
        // --- Strength ---
        {"wall_loops", make_entry("wall_loops", _L("Controls the number of wall loops in each layer's shell, a key factor in a model's strength."),
                                  _L("More walls give higher strength but use more material and time;\n"
                                     "fewer walls print faster and save material but reduce strength and may let the infill pattern show through the surface."),
                                  _L("2-3 loops is recommended for typical models; increase it for functional parts."))},
        {"embedding_wall_into_infill",
         make_entry("embedding_wall_into_infill",
                    _L("At the boundary between walled and wall-less areas, this embeds the wall into the infill region that has no outer wall on alternating layers, so the "
                       "wall and infill connect more firmly."),
                    _L("It only handles boundaries where one side has a wall and the other has zero walls; ordinary wall-to-infill boundaries inside a part are left alone.\n"
                       "Embedding every other layer creates a staggered interlock that is stronger than a flush joint."),
                    _L("Use it when a modifier turns a region into wall-less infill that still needs to bond firmly to the surrounding shell. Not needed when both sides print "
                       "normal walls."))},
        {"alternate_extra_wall", make_entry("alternate_extra_wall", _L("Adds an extra wall loop on odd layers while leaving even layers unchanged."),
                                            _L("Adding a wall every other layer strengthens the bond between layers without adding one throughout, improving layer bonding and "
                                               "part strength while avoiding the material and time cost of a full extra wall."),
                                            _L("Requires sparse infill; not applicable in vase mode."))},
        {"top_surface_pattern",
         make_entry("top_surface_pattern", _L("The toolpath pattern for the topmost solid infill; it mainly affects the flatness and look of the top surface."))},
        {"bottom_surface_pattern", make_entry("bottom_surface_pattern", _L("Controls the infill toolpath of the model's bottom surface, affecting bottom texture and flatness."),
                                              {}, _L("Bridging areas are not affected by this parameter."))},
        {"top_shell_layers", make_entry("top_shell_layers", _L("Controls the number of solid top shell layers, including the top surface."),
                                        _L("More layers make the top shell thicker and less likely to reveal the infill beneath.\n"
                                           "If layers x layer height is less than the top shell thickness, the slicer adds layers automatically."))},
        {"top_shell_thickness",
         make_entry("top_shell_layers", _L("Sets a minimum thickness for the solid top shell, preventing it from becoming too thin at small layer heights."),
                    _L("Set to 0 to disable this and let the layer count alone decide.\n"
                       "If the thickness from layers x layer height is not enough, the slicer adds top shell layers automatically."))},
        {"bottom_shell_layers", make_entry("bottom_shell_layers", _L("Controls the number of solid bottom shell layers, including the bottom surface."),
                                           _L("More layers make the bottom more solid but take longer.\n"
                                              "If layer height x layers is less than the bottom shell thickness, the slicer adds layers automatically."))},
        {"bottom_shell_thickness",
         make_entry("bottom_shell_layers", _L("Sets a minimum thickness for the solid bottom shell, preventing it from becoming too thin at small layer heights."),
                    _L("Set to 0 to disable this and let the layer count alone decide.\n"
                       "If the thickness from the layer count is not enough, the slicer adds bottom shell layers automatically."))},
        {"top_color_penetration_layers",
         make_entry("top_color_penetration_layers",
                    _L("Controls how many layers the top-surface color penetrates downward, keeping other filament colors inside from showing through."),
                    _L("Fewer layers make color bleed-through more likely.\n"
                       "More layers reduce bleed-through but increase the number of filament changes.\n"
                       "To keep the downward penetration from affecting the model's side colors, the penetrated area shrinks layer by layer."))},
        {"bottom_color_penetration_layers",
         make_entry("bottom_color_penetration_layers",
                    _L("Controls how many layers the bottom-surface color penetrates upward, keeping other filament colors inside from showing through."),
                    _L("Fewer layers make color bleed-through more likely.\n"
                       "More layers reduce bleed-through but increase the number of filament changes.\n"
                       "To keep the upward penetration from affecting the model's side colors, the penetrated area shrinks layer by layer."))},
        {"internal_solid_infill_pattern",
         make_entry("internal_solid_infill_pattern",
                    _L("Controls the toolpath pattern of internal solid infill, applied to the solid layers between the top and bottom shells and to internal solid regions."),
                    {}, _L("If detection of narrow internal solid infill is enabled, small regions use the concentric pattern automatically."))},
        {"sparse_infill_density",
         make_entry("sparse_infill_density", _L("Controls how dense the model's internal sparse infill is."),
                    // xgettext:no-c-format, no-boost-format
                    _L("10-15% is recommended for typical models; raise it for functional parts.\n"
                       "100% means fully solid, where only the Concentric, Rectilinear, Monotonic, Monotonic Line, Aligned Rectilinear, Hilbert Curve, Archimedean Chords, and Octagram Spiral patterns are available."))},
        {"fill_multiline", make_entry("fill_multiline", _L("Enables multi-line infill, printing each infill path as several parallel lines side by side."),
                                      _L("Higher values make the infill beams thicker and stronger but use more material and time;\n"
                                         "1 is ordinary single-line infill."),
                                      _L("Supported by patterns such as Line, Grid, Gyroid, Honeycomb, Cubic, and Lightning."))},
        {"sparse_infill_pattern", make_entry("sparse_infill_pattern", _L("Controls the toolpath structure of the internal sparse infill."))},
        // --- Support ---
        {"enable_support",
         make_entry("enable_support", _L("Supports are removable structures printed under overhangs and steep angles to hold up later layers and prevent sagging."),
                    _L("When enabled, supports are generated for overhangs and steep angles, giving more stable prints, but they must be removed and may leave marks.\n"
                       "Recommended when the model has obvious overhangs."))},
        {"support_type",
         make_entry(
             "support_type", _L("Selects the support shape and how it is generated; Normal (auto) and Tree (auto) are generated automatically based on overhangs."),
             _L("Normal supports are more regular and stable; tree supports touch the model less and are easier to remove.\n"
                "Choose a manual mode when you need precise control."),
             _L("Manual mode is used together with the support-painting tool in the gizmo toolbar."))},
        {"support_threshold_angle",
         make_entry("support_threshold_angle", _L("Generates support for surfaces whose overhang angle is below the threshold; it is customizable in the automatic modes."),
                    _L("A smaller threshold supports only near-horizontal large overhangs, giving less support but possibly leaving gentle slopes unsupported.\n"
                       "A larger threshold also supports more slopes, giving more support."),
                    _L("A common default of 30 degrees balances print stability against the amount of support used."))},
        {"support_on_build_plate_only", make_entry("support_on_build_plate_only", _L("Generates support only from the build plate, not on the model's surfaces."),
                                                   _L("When off, support may grow on model surfaces for more complete coverage but is more likely to leave marks.\n"
                                                      "When on, all support starts from the plate, keeping model surfaces cleaner, but some overhangs may go unsupported."),
                                                   _L("Recommended when appearance matters."))},
        {"support_interface_filament", make_entry("support_interface_filament",
                                                  _L("The support interface is a denser contact layer where support meets the model; it holds the model steady, eases removal, "
                                                     "and reduces surface marks. This sets the filament used for that contact surface."),
                                                  _L("By default the interface uses the same filament as the model, which can make removal harder.\n"
                                                     "Assigning an easy-release filament makes the contact surface easier to remove and cleaner, but adds filament changes."))},
        {"support_filament", make_entry("support_filament", _L("The filament used to print the support body and the raft."),
                                        _L("By default the support uses the same filament as the model, matching the model filament of each layer in multi-filament prints.\n"
                                           "Assigning a different filament can make removal easier or lower cost, but adds filament-change time."))},
        // --- Others ---
        {"skirt_loops",
         make_entry("skirt_loops", _L("Prints detached outline loops (a skirt) around the model to purge the nozzle and stabilize extrusion before printing starts."),
                    _L("More loops purge more thoroughly but use more material; set to 0 to disable the skirt."))},
        {"skirt_height", make_entry("skirt_height", _L("Controls how many layers the skirt is printed."),
                                    _L("During the first few layers, a taller skirt can partly block cold air around the printer, keeping a warm microclimate at the base of the model and reducing the early warping risk caused by shrinkage.\n"
                                       "The default is 1 layer."),
                                    _L("Requires the skirt loop count to be greater than 0."))},
        {"brim_type",
         make_entry("brim_type",
                    _L("A brim is a single-layer band of material added around the base of the model and joined to its first layer, a reinforcing border that fights warping and "
                       "keeps adhesion."),
                    _L("This controls the type of brim (a band joined to the model's first layer) used to improve bed adhesion and reduce warping.\n"
                       "The default is Auto."))},
        {"brim_width", make_entry("brim_width", _L("The distance from the model to the outermost brim line."),
                                  _L("A larger width improves bed adhesion and resists warping but uses more material and is harder to clean up."))},
        {"enable_prime_tower",
         make_entry("enable_prime_tower",
                    _L("After a color or filament change, the printer purges residual filament and stabilizes nozzle pressure on a separate prime tower before resuming the "
                       "model, reducing color mixing, starvation, and surface defects."),
                    _L("Off: saves material and takes less bed space, but residual filament during a color change may land on the model and cause color mixing, stringing, or local starvation.\n"
                       "On: cleaner color transitions and more consistent multi-color prints, but it uses extra material and time and takes up bed space."),
                    _L("Mutually exclusive with by-object printing and independent support layer height."))},
        {"prime_tower_width", make_entry("prime_tower_width", _L("Controls the horizontal size of the prime tower."),
                                         _L("A larger width gives more purge margin and a more stable tower, but uses more material and bed space."),
                                         _L("The width cannot be customized when the rib outer wall is enabled."))},
        {"prime_tower_rib_wall", make_entry("prime_tower_rib_wall", _L("Adds four corner ribs to the prime tower's outer wall to improve its stability."),
                                            _L("Recommended for tall prime towers, stickier materials such as PETG, or frequent color changes; turn it off when bed space is "
                                               "tight and the tower is short so the width can be narrowed."))},
        {"flush_into_infill",
         make_entry("", _L("Uses part of the transition filament after a change to print the object's internal infill, reducing waste and print time."),
                    _L("Enable it on opaque parts to save material; turn it off for transparent or thin-walled parts, or when the internal color matters."))},
        {"flush_into_support",
         make_entry("", _L("Uses the transition filament after a change to print the model's supports, reducing waste and time."),
                    _L("Enabling it saves material, but the supports may show mixed colors."), _L("Takes effect when printing multiple filaments through the same nozzle."))},
        {"print_sequence", make_entry("print_sequence", _L("Chooses whether to print the whole plate layer by layer, or to finish one object before starting the next."),
                                      _L("Use By Layer for typical multi-part or multi-color prints; consider By Object for many small parts or to limit the impact of a "
                                         "mid-print failure, and check the clearances and prime-tower restrictions."))},
        {"spiral_mode",
         make_entry("spiral_mode", _L("Prints in a continuous spiral along the outer contour, building the whole model in one line, usually with no seam on the sides."),
                    _L("When on, the model has no top shell, no infill, and usually only a single wall, so it is weak and conflicts with support, top shell layers, and several other settings.\n"
                       "It is best for single, thin-walled, open-top models."),
                    _L("Requires top shell layers to be 0 and is not compatible with support."))},
        {"timelapse_type", make_entry("", _L("Takes one photo per layer during printing and compiles a timelapse video when the print finishes."),
                                      _L("Selects the timelapse mode here; whether the feature actually runs is toggled on the send-to-print page."))},
        {"fuzzy_skin", make_entry("fuzzy_skin", _L("Randomly jitters the wall toolpaths so the outer surface takes on a rough, matte, fuzzy texture."),
                                  _L("It affects only walls; the top and bottom surfaces are unchanged.\n"
                                     "Useful for decorative parts and figure shells; turn it off when dimensions are critical or a smooth surface is needed."))},
        {"fuzzy_skin_mode",
         make_entry("fuzzy_skin_mode", _L("Selects how the fuzzy texture is generated."), _L("There are three generator modes; pick the one that suits your needs."))},
        {"fuzzy_skin_noise_type", make_entry("fuzzy_skin_noise_type", _L("Determines the randomness of the fuzzy texture, which affects the style of the surface texture."),
                                             _L("Several noise types are available; pick the one that suits your needs."))},
        {"fuzzy_skin_point_distance",
         make_entry("fuzzy_skin_point_distance", _L("Controls the average distance between the random points inserted along the wall path when generating fuzzy skin."),
                    _L("A larger value spaces the random points farther apart for a coarser grain, but uses fewer path points, so slicing and printing stay faster and the "
                       "G-code file stays smaller.\n"
                       "A smaller value packs the points closer together for a finer, more even grain, but adds path points, so slicing and printing slow down and the G-code "
                       "file grows."),
                    _L("If it is too small, the software disables fuzzy skin because the point spacing is too small (to avoid abnormal paths)."))},
        {"fuzzy_skin_thickness",
         make_entry("fuzzy_skin_thickness", _L("Controls the width of the wall-path jitter when generating fuzzy skin, that is, the depth of the texture."),
                    _L("A larger value makes the texture deeper and more pronounced with a stronger grain, but the outline expands, dimensional accuracy drops, and the wall may develop gaps and lose strength.\n"
                       "The default is about 0.3 mm; keep it below the outer wall's line width."),
                    _L("When it exceeds the line width, the generator's Extrusion and Combined modes stop working and only Displacement is used."))},
        {"fuzzy_skin_first_layer", make_entry("fuzzy_skin_first_layer", _L("Determines whether the fuzzy effect is also applied to the first layer."),
                                              _L("When on, the first-layer side walls also get the fuzzy texture.\n"
                                                 "Usually keep it off; enable it only when the bottom edge needs a consistent texture and bed adhesion is good."))},
    };

    // Surfaced as ComboBox item tooltips (Choice::BUILD -> SetItemTooltip), keyed by the enum
    // value string used in PrintConfig (not the display label).
    m_map["seam_position"].value_tips = {
        {"nearest", _L("Places the seam preferentially at concave or convex corners and shortens travel moves; suited to models with distinct edges.")},
        {"aligned", _L("Stacks each layer's seam into one vertical line, keeping the marks concentrated and easy to hide.")},
        {"back", _L("Puts the seam on the model's back, suited to display models with a fixed orientation.")},
        {"random", _L("Scatters the seam across layers to avoid a vertical line, though small stray bumps may appear on the surface.")},
    };
    m_map["top_one_wall_type"].value_tips = {
        {"not apply", _L("All top surfaces print with the normal number of wall loops.")},
        {"all top", _L("Applies to every flat upward-facing top surface, for the widest coverage.")},
        {"topmost", _L("Applies only to the model's uppermost surface; middle-layer top surfaces are still handled normally.")},
    };
    m_map["brim_type"].value_tips = {
        {"auto_brim", _L("Analyzes the model and computes the needed brim width and placement; recommended for most models.")},
        {"outer_only", _L("Generates a brim only on the outer contour to strengthen outer adhesion.")},
        {"inner_only", _L("Generates a brim in inner holes only.")},
        {"outer_and_inner", _L("Generates a brim on both the outer contour and inner holes.")},
        {"brim_ears", _L("Generates the brim by painting or local reinforcement, suited to sharp corners prone to warping.")},
        {"no_brim", _L("No brim is generated; the bottom stays cleaner but bed adhesion is weaker.")},
    };
    m_map["print_sequence"].value_tips = {
        {"by layer", _L("All objects rise together, layer by layer.")},
        {"by object", _L("Finishes one object before starting the next; has placement clearance requirements.")},
    };
    m_map["timelapse_type"].value_tips = {
        {"0", _L("A snapshot is taken directly after each layer.")},
        {"1", _L("After each layer the toolhead moves to the excess chute before the snapshot for a cleaner shot; filament may leak during the shot, so a prime tower is "
                 "required to wipe the nozzle and is generated even for single-color jobs.")},
    };
    m_map["fuzzy_skin"].value_tips = {
        {"none", _L("No fuzzy skin is added automatically, but you can still paint it on locally.")},
        {"external", _L("Adds fuzzy skin only to the outermost contour; inner holes stay smooth.")},
        {"all", _L("Adds fuzzy skin to both the outer contour and inner holes.")},
        {"allwalls", _L("Every wall line jitters, for the strongest effect but possibly larger dimensional deviation.")},
        {"disabled_fuzzy", _L("Turns fuzzy skin fully off; even painting has no effect.")},
    };
    m_map["fuzzy_skin_mode"].value_tips = {
        {"displacement", _L("The nozzle shifts sideways to form the texture, giving a strong 3D feel but possible gaps between loops.")},
        {"extrusion", _L("The path stays straight and only the extruded amount varies, keeping walls denser.")},
        {"combined", _L("Displacement plus extrusion; looks close to displacement while filling the gaps between loops.")},
    };
    m_map["fuzzy_skin_noise_type"].value_tips = {
        {"classic", _L("Uniform random noise with a fine, grainy texture and no clear direction.")},
        {"perlin", _L("Smooth transitions between layers for a more even, continuous texture with soft, regular undulation.")},
        {"billow", _L("Similar to Perlin but clumpier, with clustered bumps and flat areas, like clouds.")},
        {"ridgedmulti", _L("Sharp jagged ridges with peaks and fine grooves, giving a marble-like texture.")},
        {"voronoi", _L("Divides the surface into cells and offsets each as a whole, forming a patchwork stepped texture.")},
    };
}

// Heap singleton (not Meyers) so Shutdown() can drop and rebuild it for a new language.
ParamTipStore *&store_slot()
{
    static ParamTipStore *s = nullptr;
    return s;
}

const ParamTipStore &ParamTipStore::get()
{
    ParamTipStore *&s = store_slot();
    if (s == nullptr) s = new ParamTipStore();
    return *s;
}

void ParamTipStore::reset()
{
    ParamTipStore *&s = store_slot();
    delete s;
    s = nullptr;
}

const ParamTipEntry *ParamTipStore::find(const std::string &opt_key) const
{
    auto it = m_map.find(opt_key);
    return it == m_map.end() ? nullptr : &it->second;
}

// The card title is never curated in the store. It prefers the anchoring row's own label
// (line_label) — already localized and what the row visibly shows — then the def's localized label,
// and finally the raw opt_key when neither exists.
wxString resolve_title(const std::string &opt_key, const ConfigOptionDef *def, const wxString &line_label)
{
    if (!line_label.IsEmpty()) return line_label;
    wxString title = def ? _(def->label) : wxString();
    return title.IsEmpty() ? from_u8(opt_key) : title;
}

// Description fallback chain: a curated store entry wins; otherwise the anchoring row's own tooltip
// (line_tooltip, already localized) — the string the old native hover showed — then the def tooltip.
ParamTipEntry resolve_entry(const ParamTipEntry *stored, const ConfigOptionDef *def, const wxString &line_tooltip)
{
    ParamTipEntry r = stored ? *stored : ParamTipEntry{};
    if (r.description.IsEmpty()) r.description = line_tooltip;
    if (r.description.IsEmpty() && def) r.description = _(def->tooltip);
    return r;
}

/**
 * \brief Split a details block into its individual bullet points.
 *
 * Breaks on either newline character so CRLF translations split the same as LF ones, and trims each
 * point: a stray CR or trailing space in a .po string would otherwise render as a blank bullet or as
 * a visible artifact at the end of the line.
 *
 * \param s Details text, authored as one point per line (newline-separated).
 * \return  One entry per non-blank line, trimmed, in order.
 */
std::vector<wxString> split_lines(const wxString &s)
{
    std::vector<wxString> lines;
    size_t                from = 0;
    for (;;) {
        const size_t nl   = s.find_first_of("\r\n", from);
        const size_t end  = (nl == wxString::npos) ? s.length() : nl;
        wxString     line = s.Mid(from, end - from);
        line.Trim(true).Trim(false);
        if (!line.IsEmpty()) lines.push_back(line);
        if (nl == wxString::npos) break;
        from = nl + 1;
    }
    return lines;
}

/**
 * \brief Wrap an image in a bitmap that keeps the backing scale its pixels were rasterized for.
 *
 * The plain wxBitmap(wxImage) c-tor assumes scale 1, which on Retina shows a 2x-rasterized icon at
 * twice its intended size. Only the macOS wxBitmap has the scale-aware c-tor; elsewhere scale is
 * always 1 and the plain c-tor is already correct.
 *
 * \param img   Image whose pixels are sized for \p scale.
 * \param scale Backing scale the pixels were rasterized at.
 * \return      The bitmap, tagged with \p scale where the platform supports it.
 */
static wxBitmap bitmap_at_scale(const wxImage &img, double scale)
{
#ifdef __APPLE__
    // Contrary to intuition, this c-tor's scale argument is not 'scale the image to this' but
    // 'the image is already sized for this backing scale'.
    return wxBitmap(img, -1, scale);
#else
    (void) scale;
    return wxBitmap(img);
#endif
}

/**
 * \brief Linearly crossfade two equally sized RGBA images.
 *
 * \param a     First image, shown at t == 0.
 * \param b     Second image, shown at t == 1.
 * \param t     Blend factor in [0, 1].
 * \param scale Backing scale both images were rasterized at; carried into the result so the frames
 *              match the static icon on Retina instead of rendering at double size.
 * \return      The blended bitmap; the nearer endpoint when the images are unusable or mismatched.
 */
wxBitmap blend_bitmaps(const wxImage &a, const wxImage &b, double t, double scale)
{
    if (!a.IsOk() || !b.IsOk() || a.GetSize() != b.GetSize()) return bitmap_at_scale(t < 0.5 ? a : b, scale);
    if (t <= 0.0) return bitmap_at_scale(a, scale);
    if (t >= 1.0) return bitmap_at_scale(b, scale);

    wxImage out(a.GetSize());
    out.InitAlpha();
    const unsigned char *ad = a.GetData(), *bd = b.GetData();
    const unsigned char *aa = a.HasAlpha() ? a.GetAlpha() : nullptr;
    const unsigned char *ba = b.HasAlpha() ? b.GetAlpha() : nullptr;
    unsigned char       *od = out.GetData(), *oa = out.GetAlpha();

    // Weight each color by its own alpha (premultiplied blend); a straight RGB lerp would drag the
    // undefined color of the fully transparent pixels around each glyph into the result and fringe it.
    const int n = a.GetWidth() * a.GetHeight();
    for (int i = 0; i < n; ++i) {
        const double wa  = (aa ? aa[i] : 255) * (1.0 - t);
        const double wb  = (ba ? ba[i] : 255) * t;
        const double sum = wa + wb;
        oa[i]            = static_cast<unsigned char>(sum + 0.5);
        for (int c = 0; c < 3; ++c)
            od[i * 3 + c] = sum > 0.0 ? static_cast<unsigned char>((ad[i * 3 + c] * wa + bd[i * 3 + c] * wb) / sum + 0.5) : 0;
    }
    return bitmap_at_scale(out, scale);
}

// Apply one optional text row: fill + show when it has content, collapse otherwise.
// t is a Label (not a raw wxStaticText) so Label::Wrap runs — it breaks CJK runs that have no
// spaces, which the non-virtual wxStaticText::Wrap cannot, and it keeps the wrapped label inside
// the width passed here, which the native control would otherwise clip rather than re-wrap.
void set_row(wxSizer *sizer, Label *t, const wxString &s, const wxColour &fg, const wxColour &bg, int wrap)
{
    const bool has = !s.IsEmpty();
    if (has) {
        t->SetForegroundColour(fg);
        t->SetBackgroundColour(bg);
        t->SetLabel(s);
        t->Wrap(wrap);
    }
    sizer->Show(t, has, true);
}
} // namespace

// ----------------------------------------------------------------------------
// ParamTooltip — singleton popup card
// ----------------------------------------------------------------------------
ParamTooltip *ParamTooltip::s_self = nullptr;

ParamTooltip &ParamTooltip::instance()
{
    if (s_self == nullptr) s_self = new ParamTooltip();
    return *s_self;
}

ParamTooltip::ParamTooltip() : wxPopupTransientWindow(wxGetApp().mainframe, wxBORDER_NONE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(light_palette().card_bg);

    build_layout();

    m_timer = new wxTimer;
    m_timer->Bind(wxEVT_TIMER, &ParamTooltip::OnTimer, this);
    m_copy_timer = new wxTimer;
    m_copy_timer->Bind(wxEVT_TIMER, &ParamTooltip::OnCopyAnim, this);
    Bind(wxEVT_PAINT, &ParamTooltip::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
        ApplyShape();
        e.Skip();
    });
    // The soft drop shadow is a separate layered window behind the card. The card only ever moves
    // or resizes as part of our own show/rebuild sequence, so we sync the shadow explicitly at the
    // end of those (see OnTimer and DoShowFor) — NOT from MOVE/SIZE. Syncing mid-rebuild would paint
    // the shadow at the new size but the pre-move position and leave a smear during a switch.
    // wxEVT_SHOW stays as the catch-all that also hides the shadow on a click-outside dismiss.
    Bind(wxEVT_SHOW, [this](wxShowEvent &e) {
        update_shadow(e.IsShown());
        e.Skip();
    });
}

ParamTooltip::~ParamTooltip()
{
    if (s_self == this) s_self = nullptr; // never leave the singleton pointer dangling if the frame destroys us as its child
    delete m_timer;
    delete m_copy_timer;
}

int ParamTooltip::content_width() const { return FromDIP(CARD_WIDTH - 2 * PAD); }

wxWindow *ParamTooltip::build_optkey_row()
{
    // opt_key pill: grey rounded chip painted behind the opt_key text + copy icon.
    m_optkey_pill = new wxPanel(this);
    m_optkey_pill->Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
        wxPaintDC      dc(m_optkey_pill);
        const Palette &p = m_last_dark ? dark_palette() : light_palette();
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(p.optkey_bg));
        dc.DrawRoundedRectangle(m_optkey_pill->GetClientRect(), FromDIP(4));
    });

    // Dimmed opt_key text (left of the pill); ellipsized to whatever width Rebuild leaves it.
    // Use Label, not a raw wxStaticText: its DoGetBestClientSize adds the WIN32 margin that offsets
    // GetTextExtentPoint32's underestimate, which otherwise makes the native STATIC control ellipsize
    // even when the reserved width nominally fit the text.
    m_optkey = new Label(m_optkey_pill, Label::Body_12, wxEmptyString, wxST_ELLIPSIZE_END);

    // Copy icon (right of the pill): click copies the shown opt_key to the clipboard.
    m_copy = new wxStaticBitmap(m_optkey_pill, wxID_ANY, create_scaled_bitmap("tooltip_copy", this, COPY_ICON_PX));
    m_copy->SetCursor(wxCursor(wxCURSOR_HAND));
    m_copy->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
        if (m_last_key.empty()) return;
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(from_u8(m_last_key)));
            wxTheClipboard->Close();
            start_copy_feedback(); // only for a copy that actually landed on the clipboard
        }
    });
    // Hover feedback: the icon darkens (light) / brightens (dark) while the pointer is over it.
    // The copied-animation owns the bitmap while it runs, so hover must not overwrite a frame.
    m_copy->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) {
        if (!m_copy_timer->IsRunning()) m_copy->SetBitmap(create_scaled_bitmap("tooltip_copy_hover", this, COPY_ICON_PX));
    });
    m_copy->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) {
        if (!m_copy_timer->IsRunning()) m_copy->SetBitmap(create_scaled_bitmap("tooltip_copy", this, COPY_ICON_PX));
    });

    wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddSpacer(FromDIP(4));
    sizer->Add(m_optkey, 0, wxALIGN_CENTER_VERTICAL);
    sizer->AddSpacer(FromDIP(4));
    sizer->Add(m_copy, 0, wxALIGN_CENTER_VERTICAL);
    sizer->AddStretchSpacer();
    m_optkey_pill->SetSizer(sizer);

    return m_optkey_pill;
}

void ParamTooltip::build_layout()
{
    // Horizontal outer: [left pad][content column][right pad].
    wxBoxSizer *outer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

    m_title = new Label(this, Label::Head_14, wxEmptyString);
    m_title->SetFont(Label::Head_14);
    col->Add(m_title, 0, wxEXPAND | wxTOP, FromDIP(PAD));

    m_divider = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(1)));
    col->Add(m_divider, 0, wxEXPAND | wxTOP, FromDIP(GAP));

    m_desc = new Label(this, Label::Body_13, wxEmptyString);
    m_desc->SetFont(Label::Body_13);
    col->Add(m_desc, 0, wxTOP, FromDIP(GAP));

    m_image = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap);
    col->Add(m_image, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(GAP));

    // The details are a bullet list: one row per point, each row [marker][wrapped text], so the
    // wrapped lines hang under the text column instead of running back under the marker.
    m_details = new wxWindow(this, wxID_ANY);
    m_details->SetSizer(new wxBoxSizer(wxVERTICAL));
    col->Add(m_details, 0, wxEXPAND | wxTOP, FromDIP(GAP));

    m_note = new Label(this, Label::Body_13, wxEmptyString);
    m_note->SetFont(Label::Body_13);
    col->Add(m_note, 0, wxTOP, FromDIP(GAP));

    col->Add(build_optkey_row(), 0, wxTOP, FromDIP(GAP * 2));

    m_wiki = new Label(this, Label::Body_13, wxEmptyString);
    m_wiki->SetFont(Label::Body_13);
    m_wiki->SetCursor(wxCursor(wxCURSOR_HAND));
    m_wiki->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
        if (!m_wiki_url.IsEmpty()) wxLaunchDefaultBrowser(m_wiki_url);
    });
    col->Add(m_wiki, 0, wxTOP, FromDIP(GAP));

    col->AddSpacer(FromDIP(PAD)); // bottom padding

    outer->AddSpacer(FromDIP(PAD));
    outer->Add(col, 1, wxEXPAND);
    outer->AddSpacer(FromDIP(PAD));
    SetSizer(outer);
}

wxBitmap ParamTooltip::LoadImage(const std::string &image_id, bool dark)
{
    if (image_id == m_cached_image_id && dark == m_cached_dark && m_cached_bmp.IsOk()) return m_cached_bmp;

    wxBitmap    bmp;
    std::string path  = resources_dir() + "/tooltip/images/" + image_id + (dark ? "_dark" : "") + ".png";
    wxString    wpath = from_u8(path);
    if (wxFileExists(wpath)) {
        wxImage image;
        if (image.LoadFile(wpath, wxBITMAP_TYPE_PNG) && image.IsOk() && image.GetWidth() > 0) {
            // Scale every image to one uniform width (up or down) so they all look equal.
            const int target_w = FromDIP(IMAGE_W);
            const int target_h = image.GetHeight() * target_w / image.GetWidth();
            image.Rescale(target_w, target_h, wxIMAGE_QUALITY_HIGH);
            bmp = wxBitmap(image);
        }
    }
    m_cached_image_id = image_id;
    m_cached_dark     = dark;
    m_cached_bmp      = bmp;
    return bmp;
}

void ParamTooltip::Rebuild(const std::string &opt_key, const std::string &wiki_path, bool dark, const wxString &line_label, const wxString &line_tooltip)
{
    const Palette         &p   = dark ? dark_palette() : light_palette();
    const ConfigOptionDef *def = print_config_def.get(opt_key);
    const ParamTipEntry    e   = resolve_entry(ParamTipStore::get().find(opt_key), def, line_tooltip);

    SetBackgroundColour(p.card_bg);
    wxSizer  *sizer = GetSizer();
    const int wrap  = content_width();

    m_title->SetForegroundColour(p.title);
    m_title->SetBackgroundColour(p.card_bg);
    m_title->SetLabel(resolve_title(opt_key, def, line_label));
    m_title->Wrap(wrap); // title wraps to multiple lines when too long

    m_divider->SetBackgroundColour(p.divider);

    set_row(sizer, m_desc, e.description, p.description, p.card_bg, wrap);

    const wxBitmap bmp = e.image.empty() ? wxBitmap() : LoadImage(e.image, dark);
    if (bmp.IsOk()) m_image->SetBitmap(bmp);
    sizer->Show(m_image, bmp.IsOk(), true);

    set_details(e.details, p.details, p.card_bg, wrap);
    set_row(sizer, m_note, e.note, p.note, p.card_bg, wrap);

    // The wiki link uses the caller's live wiki slug (the same one the clickable label opens).
    m_wiki_url                = wiki_path.empty() ? wxString() : OptionsGroup::get_url(wiki_path);
    const wxString wiki_label = m_wiki_url.IsEmpty() ? wxString() : _L("View details on Wiki") + wxString::FromUTF8(" \xE2\x86\x92");
    set_row(sizer, m_wiki, wiki_label, p.link, p.card_bg, wrap);

    // The opt_key pill is a developer aid — shown only in Internal developer mode.
    update_optkey_row(opt_key, dark);

    // Fixed-width card: fit to content for the height, then pin the width to CARD_WIDTH.
    sizer->Layout();
    sizer->Fit(this);
    SetClientSize(FromDIP(CARD_WIDTH), GetClientSize().GetHeight());
    Layout();
}

void ParamTooltip::set_details(const wxString &s, const wxColour &fg, const wxColour &bg, int wrap)
{
    const std::vector<wxString> lines = split_lines(s);
    GetSizer()->Show(m_details, !lines.empty(), true);
    if (lines.empty()) return;

    m_details->SetBackgroundColour(bg);
    wxSizer *col = m_details->GetSizer();

    while (m_detail_rows.size() < lines.size()) {
        DetailRow row;
        row.marker = new Label(m_details, Label::Body_13, BULLET_MARKER);
        row.marker->SetFont(Label::Body_13);
        row.text = new Label(m_details, Label::Body_13, wxEmptyString);
        row.text->SetFont(Label::Body_13);

        row.sizer = new wxBoxSizer(wxHORIZONTAL);
        row.sizer->Add(row.marker, 0, wxALIGN_TOP); // the marker sits on the point's first line
        row.sizer->Add(row.text, 1, wxEXPAND);
        // Rows are only ever appended and always filled in order, so this index is the row's
        // permanent position: only the first one skips the inter-bullet gap.
        col->Add(row.sizer, 0, wxEXPAND | wxTOP, m_detail_rows.empty() ? 0 : FromDIP(2));
        m_detail_rows.push_back(row);
    }

    // Indent = the marker column, i.e. the bullet plus its trailing space. Take it from the marker's
    // best size, not from a raw text extent: the sizer reserves the former, and on macOS the
    // difference between the two is enough to push the last word of every point past the text
    // column, where the native control clips it away instead of re-wrapping it.
    const int marker_w  = m_detail_rows.front().marker->GetBestSize().GetWidth();
    const int text_wrap = wrap - marker_w;

    for (size_t i = 0; i < m_detail_rows.size(); ++i) {
        const DetailRow &row  = m_detail_rows[i];
        const bool       used = i < lines.size();
        col->Show(row.sizer, used, true);
        if (!used) continue;

        row.marker->SetForegroundColour(fg);
        row.marker->SetBackgroundColour(bg);
        row.text->SetForegroundColour(fg);
        row.text->SetBackgroundColour(bg);
        row.text->SetLabel(lines[i]);
        row.text->Wrap(text_wrap);
    }

    col->Layout();
    m_details->InvalidateBestSize();
}

// The opt_key pill (grey chip: option key + copy icon) is a developer aid, shown only in Internal
// developer mode.
// Off: the bottom row is just the Wiki link. On: colorize it for the theme, set the
// key text, and cap its width so it ellipsizes after the Wiki link and copy icon.
void ParamTooltip::update_optkey_row(const std::string &opt_key, bool dark)
{
    const bool dev = wxGetApp().app_config->get("developer_mode") == "true";
    if (wxSizer *row = m_optkey_pill->GetContainingSizer()) row->Show(m_optkey_pill, dev, true);
    if (!dev) return;

    const Palette &p = dark ? dark_palette() : light_palette();
    m_optkey_pill->SetBackgroundColour(p.card_bg); // corners outside the rounded pill blend in
    m_optkey->SetForegroundColour(p.optkey_fg);
    m_optkey->SetBackgroundColour(p.optkey_bg);
    m_optkey->SetLabel(from_u8(opt_key));
    m_copy->SetBackgroundColour(p.optkey_bg);
    m_copy_timer->Stop(); // a rebuild swaps the shown option, so any in-flight "copied!" is stale
    m_copy->SetBitmap(create_scaled_bitmap("tooltip_copy", this, COPY_ICON_PX)); // refresh for the current theme
    m_copy_from = m_copy_to = wxImage();                                         // re-rasterized for the new theme on the next copy
    m_optkey_pill->Refresh();
}

void ParamTooltip::start_copy_feedback()
{
    if (!m_copy_from.IsOk() || !m_copy_to.IsOk()) {
        // A click only lands while the pointer is over the icon, so hover art is the resting frame.
        const wxBitmap from = create_scaled_bitmap("tooltip_copy_hover", this, COPY_ICON_PX);
        const wxBitmap to   = create_scaled_bitmap("tooltip_copy_checked", this, COPY_ICON_PX);
        // ConvertToImage drops the backing scale, so keep it to re-tag the blended frames.
        m_copy_scale = from.IsOk() ? from.GetScaleFactor() : 1.0;
        m_copy_from  = from.ConvertToImage();
        m_copy_to    = to.ConvertToImage();
    }
    m_copy_step = 0;
    m_copy_timer->Stop(); // clicking again mid-animation restarts from the copy icon
    m_copy_timer->StartOnce(COPY_FRAME_MS);
}

void ParamTooltip::OnCopyAnim(wxTimerEvent &)
{
    ++m_copy_step;
    if (m_copy_step >= 2 * COPY_FADE_STEPS) { // faded all the way back; settle on the resting art
        const bool hover = m_copy->GetScreenRect().Contains(wxGetMousePosition());
        m_copy->SetBitmap(create_scaled_bitmap(hover ? "tooltip_copy_hover" : "tooltip_copy", this, COPY_ICON_PX));
        return;
    }

    const bool   fading_in = m_copy_step <= COPY_FADE_STEPS;
    const double t         = fading_in ? double(m_copy_step) / COPY_FADE_STEPS : double(2 * COPY_FADE_STEPS - m_copy_step) / COPY_FADE_STEPS;
    m_copy->SetBitmap(blend_bitmaps(m_copy_from, m_copy_to, t, m_copy_scale));
    // Dwell on the check mark at the top of the fade so the confirmation is readable.
    m_copy_timer->StartOnce(m_copy_step == COPY_FADE_STEPS ? COPY_HOLD_MS : COPY_FRAME_MS);
}

void ParamTooltip::ApplyShape()
{
    const wxSize sz = GetSize();
    if (sz.GetWidth() <= 0 || sz.GetHeight() <= 0) return;
    const int d = FromDIP(CARD_RADIUS * 2); // GDI ellipse size = 2*radius

#ifdef __WIN32__
    if (HWND hwnd = (HWND) GetHWND()) {
        HRGN body = CreateRoundRectRgn(0, 0, sz.GetWidth() + 1, sz.GetHeight() + 1, d, d);
        SetWindowRgn(hwnd, body, TRUE); // system takes ownership of body
    }
#else
    wxBitmap mask(sz.GetWidth(), sz.GetHeight());
    {
        wxMemoryDC dc(mask);
        dc.SetBackground(*wxBLACK_BRUSH);
        dc.Clear();
        dc.SetBrush(*wxWHITE_BRUSH);
        dc.SetPen(*wxWHITE_PEN);
        dc.DrawRoundedRectangle(0, 0, sz.GetWidth(), sz.GetHeight(), d / 2);
    }
    SetShape(wxRegion(mask, *wxBLACK));
#endif
}

// Position the card just to the right of the option row (anchor tip_pos), clamped to the display.
void ParamTooltip::place_card(const wxPoint &tip_pos)
{
    int disp = wxDisplay::GetFromPoint(tip_pos);
    if (disp == wxNOT_FOUND) disp = 0;
    wxRect area = wxDisplay(disp).GetClientArea();

    const wxSize sz = GetSize();
    wxPoint      pos(tip_pos.x + FromDIP(ANCHOR_GAP), tip_pos.y - sz.GetHeight() / 3); // row center ~1/3 down the card
    if (pos.y + sz.GetHeight() > area.GetBottom()) pos.y = area.GetBottom() - sz.GetHeight();
    if (pos.y < area.GetTop()) pos.y = area.GetTop();
    if (pos.x + sz.GetWidth() > area.GetRight()) pos.x = area.GetRight() - sz.GetWidth();
    if (pos.x < area.GetLeft()) pos.x = area.GetLeft();

    m_request_pos = pos;
    SetPosition(pos);
    ApplyShape();
}

// Sync the drop shadow with the card via the shared WindowShadow component.
void ParamTooltip::update_shadow(bool show) { m_shadow.Sync(this, show && IsShown()); }

bool ParamTooltip::DoShowFor(const std::string &opt_key, const std::string &wiki_path, const wxPoint &tip_pos, const wxString &line_label, const wxString &line_tooltip)
{
    if (opt_key.empty()) {
        DoHide(false);
        return false;
    }

    const bool dark = wxGetApp().dark_mode();

    const ConfigOptionDef *def      = print_config_def.get(opt_key);
    const ParamTipEntry   *e        = ParamTipStore::get().find(opt_key);
    const bool             has_text = def && (!def->label.empty() || !def->tooltip.empty());
    if (!has_text && e == nullptr) {
        DoHide(false);
        return false;
    }

    const bool changed = (opt_key != m_last_key) || (dark != m_last_dark);

    // Already visible: update content and slide to the new row in place. Freeze() suppresses the
    // intermediate repaints from Rebuild's relayout/resize, so the card never flashes stale or
    // half-built content, and skipping the hide/show + delay is what stops switching options from
    // blinking. See the else branch for the first, not-yet-shown appearance.
    if (IsShown()) {
        if (changed) {
            Freeze();
            Rebuild(opt_key, wiki_path, dark, line_label, line_tooltip);
            m_last_key  = opt_key;
            m_last_dark = dark;
            place_card(tip_pos);
            Thaw();
            update_shadow(true);
        }
        m_hide = false; // cancel any pending hide from leaving the previous row
        return true;
    }

    // Not visible yet: build off-screen (never a visible morph), position, then show on settle.
    if (changed) {
        Rebuild(opt_key, wiki_path, dark, line_label, line_tooltip);
        m_last_key  = opt_key;
        m_last_dark = dark;
    }
    place_card(tip_pos);
    if (changed || m_hide || !m_timer->IsRunning()) {
        m_hide = false;
        m_timer->StartOnce(SHOW_DELAY_MS);
    }
    return true;
}

void ParamTooltip::DoHide(bool now)
{
    if (now) {
        m_hide = true;
        wxPopupTransientWindow::Hide();
        update_shadow(false);
        return;
    }
    if (!m_hide) {
        m_hide = true;
        m_timer->StartOnce(HIDE_DELAY_MS);
    }
}

void ParamTooltip::OnTimer(wxTimerEvent &)
{
    if (m_hide) {
        wxPoint mp = ScreenToClient(wxGetMousePosition());
        if (GetClientRect().Contains(mp)) {
            m_timer->StartOnce(HIDE_DELAY_MS);
            return;
        }
        wxPopupTransientWindow::Hide();
        update_shadow(false);
    } else {
        Show();
        update_shadow(true);
    }
}

void ParamTooltip::OnPaint(wxPaintEvent &)
{
    const Palette    &p = m_last_dark ? dark_palette() : light_palette();
    wxBufferedPaintDC dc(this);
    wxGCDC            gdc(dc); // wraps a wxGraphicsContext so the rounded corners anti-alias

    // Clipped to the rounded-rect silhouette by ApplyShape. Paint the border as two filled
    // rounded rects (border color, then a card_bg fill inset by the border width) instead of a
    // stroked outline: a stroke straddles the clip boundary and, once the GC is DPI-scaled, its
    // outer half is trimmed unevenly between the top/left (at 0) and right/bottom (at W/H) edges.
    // Fills cover whole pixels, so the border stays a uniform width on all four sides.
    gdc.SetBackground(wxBrush(p.border));
    gdc.Clear();

    wxGraphicsContext *gc = gdc.GetGraphicsContext();
    if (gc == nullptr) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const double r  = FromDIP(CARD_RADIUS);
    const double bw = FromDIP(1); // border width
    const wxSize sz = GetClientSize();
    const double ir = r > bw ? r - bw : 0.0; // inner radius, kept concentric with the border

    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(p.card_bg));
    gc->DrawRoundedRectangle(bw, bw, sz.GetWidth() - 2.0 * bw, sz.GetHeight() - 2.0 * bw, ir);
}

bool ParamTooltip::ShowFor(const std::string &opt_key, const std::string &wiki_path, const wxPoint &tip_pos, const wxString &line_label, const wxString &line_tooltip)
{
    return instance().DoShowFor(opt_key, wiki_path, tip_pos, line_label, line_tooltip);
}

wxString ParamTooltip::ItemTooltip(const std::string &opt_key, const std::string &value_key)
{
    std::string key = opt_key;
    if (auto tag = key.find('#'); tag != std::string::npos) key.erase(tag);

    const ParamTipEntry *e = ParamTipStore::get().find(key);
    if (e == nullptr) return {};
    for (const auto &vt : e->value_tips)
        if (vt.first == value_key) return vt.second;
    return {};
}

void ParamTooltip::Hide()
{
    if (s_self) s_self->DoHide(false);
}

void ParamTooltip::Shutdown()
{
    ParamTipStore::reset(); // next show rebuilds the curated text in the current language
    if (s_self != nullptr) {
        ParamTooltip *self = s_self;
        s_self             = nullptr; // null first so a later instance() rebuilds against the new frame
        self->DoHide(true);           // hide the card and its shadow window now
        self->Destroy();              // deferred delete; ~ParamTooltip clears the timer and tears down the shadow
    }
}

} // namespace Slic3r::GUI
