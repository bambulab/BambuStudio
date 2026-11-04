#pragma once
/**
 * \file NozzleInventory.hpp
 * \brief The nozzles installed in each extruder slot of the selected printer, resolved from preset
 *        defaults, per-printer history, the open project, machine sync and user edits
 *
 * Project, Machine and User are separate layers. Each write replaces one layer and stamps it. The
 * effective nozzles are computed on read: the newest layer of the selected printer model wins if it
 * holds the selected diameter, else the saved slots of the model, else the saved volume types of the
 * selected variant, else the preset defaults. On a printer with a nozzle rack the selected diameter
 * only filters the slots, so there any installed nozzle holds it. A printer switch writes no layer, so
 * the order of switch and sync does not matter.
 *
 * The result is the nozzles that the next slice assumes. It matches the real machine only after a
 * sync of the same printer model, and only until a newer project or user write shadows that sync.
 *
 * TODO: the command line slicer (BambuStudio.cpp) and CalibUtils still build the nozzle keys from
 * ExtruderNozzleStat. To move them here, add a select_printer() that reads a printer config instead
 * of PresetCollection; write the nozzles from the command line with sync_machine(), and skip
 * open_project() when the command line selects another printer.
 */

#include "PrintConfig.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

class AppConfig;
class PresetCollection;

/**
 * \brief State of a nozzle slot
 *
 * All states but Empty hold a nozzle, with its health as the machine reports it.
 */
enum class NozzleSlotStatus { Empty, Normal, Error, Unknown };

/**
 * \brief One physical nozzle slot of an extruder
 *
 * A default slot is empty. An empty slot has an empty diameter, so a diameter compare never matches it.
 */
struct NozzleSlot
{
    std::string      diameter;                              ///< nozzle diameter such as "0.4"; empty for an empty slot
    NozzleVolumeType volume_type = nvtStandard;             ///< flow type of the nozzle
    NozzleSlotStatus status      = NozzleSlotStatus::Empty; ///< Empty, or the health of the nozzle

    /**
     * \brief Check if the slot holds no nozzle
     */
    bool empty() const { return status == NozzleSlotStatus::Empty; }
};

/**
 * \brief All nozzle slots of one extruder, in slot order
 */
using ExtruderSlots = std::vector<NozzleSlot>;

/**
 * \brief Nozzle inventory of the selected printer
 *
 * Each method changes one input of the resolve, and only its own event may call it:
 *
 * | Method         | Layer   | Effect                                 | Only called by                             |
 * |----------------|---------|----------------------------------------|--------------------------------------------|
 * | select_printer | none    | new model, diameter, history, defaults | a printer, variant or diameter mode change |
 * | open_project   | Project | the file's nozzles win                 | opening a project file                     |
 * | close_project  | Project | the file's nozzles stop winning        | a new or closed project                    |
 * | sync_machine   | Machine | machine nozzles win, saved as history  | a sync with a connected machine            |
 * | user_*         | User    | user nozzles win, saved as history     | a user action in a nozzle dialog or combo  |
 *
 * The newest layer wins until a newer write, but only for its own printer model, and only if it
 * holds the selected diameter; else the history wins, else the preset defaults. On a printer with a
 * nozzle rack any installed nozzle holds the selected diameter, so a diameter selection never changes
 * the slots.
 *
 * A call from the wrong event puts its data in the wrong layer, and shadowing then gives wrong
 * nozzles; for example user_set_slots() in a sync handler hides the next project open.
 *
 * Every write updates the effective nozzles, writes the nozzle keys into the mirror config, saves
 * the history and writes one log line. Callers never order these steps.
 */
class NozzleInventory
{
public:
    /**
     * \brief Create an inventory that writes every change into \p mirror
     *
     * \param mirror  project config; receives nozzle_volume_type, the per-slot keys and the legacy counts.
     *                It must outlive the inventory.
     */
    explicit NozzleInventory(DynamicPrintConfig &mirror);
    ~NozzleInventory();
    /// A copy would write into the mirror of its source; PresetBundle constructs it on its own mirror, then assigns.
    NozzleInventory(const NozzleInventory &) = delete;

    /**
     * \brief Copy the layers and the selected printer, for a scratch copy of PresetBundle
     *
     * This inventory keeps its own mirror and history, so a scratch copy never saves history.
     *
     * \param rhs  inventory to copy from
     */
    NozzleInventory &operator=(const NozzleInventory &rhs);

    /**
     * \brief Set the store of the per-printer history
     *
     * The history is two sections of BambuStudio.conf, and each write saves both:
     * - nozzle_slots, keyed by printer model: the full slots with their diameters. A rack can mix
     *   diameters and serves every variant, so only this restores it after a model switch or a restart.
     * - nozzle_volume_types, keyed by base preset name: the volume types only; each slot takes the
     *   variant diameter. A printer without a rack reads it on a variant switch, so each diameter keeps
     *   its own types. Older versions read only this section, and reject the whole value if it has a
     *   token they do not know, so its format must stay as it is.
     *
     * \param history  application config that stores the history; null (the default) means nothing is
     *                 read or saved. It must outlive the inventory.
     */
    void set_history(AppConfig *history);

    // ---- No layer: changes only what the layers resolve against ----

    /**
     * \brief The selected printer or its variant changed
     *
     * Reads from the selected preset the inputs of the resolve: printer model, diameter, history key,
     * slot counts and preset defaults. The layers stay as they are, so a later switch back gives the
     * same nozzles. The multi-diameter mode is read from the mirror on use, so a mode change needs
     * only refresh_mirror().
     *
     * \param printers  printer presets, with the new printer selected
     */
    void select_printer(const PresetCollection &printers);

    // ---- Project layer: only project open, new and close ----

    /**
     * \brief A project file was opened; its nozzle keys replace the Project layer
     *
     * Also selects the printer, so the file's printer must already be selected in \p printers.
     * A file with no nozzle keys clears the layer.
     *
     * \param printers   printer presets, with the project's printer selected
     * \param file_keys  the config as read from the file, so a missing key is really missing
     */
    void open_project(const PresetCollection &printers, const DynamicPrintConfig &file_keys);

    /**
     * \brief The project was closed or a new project started; clears the Project layer
     */
    void close_project();

    // ---- Machine layer: only a sync with a connected machine ----

    /**
     * \brief A machine sync replaces the Machine layer, even when nothing changed
     *
     * \param printer_model  printer_model of the synced machine; the layer applies only to that model
     * \param layout         slots per extruder, as the machine reports them; a diameter in any spelling
     *                       of a standard size, such as "0.40", reads as that size
     */
    void sync_machine(const std::string &printer_model, std::vector<ExtruderSlots> layout);

    // ---- User layer: only a user action in a nozzle dialog or combo ----

    /**
     * \brief The user set all slots of one extruder
     *
     * \param extruder_id  0-based extruder
     * \param slots        the new slots of that extruder
     */
    void user_set_slots(int extruder_id, ExtruderSlots slots);

    /**
     * \brief The user set the nozzle count per volume type of the selected diameter
     *
     * \param extruder_id  0-based extruder
     * \param counts       nozzle count per volume type; a missing type means no nozzle of that type
     */
    void user_set_counts(int extruder_id, const std::map<NozzleVolumeType, int> &counts);

    /**
     * \brief The user picked the volume type of an extruder with one nozzle slot
     *
     * An extruder with a rack derives its volume type from its slots; there this logs a warning
     * and does nothing.
     *
     * \param extruder_id  0-based extruder
     * \param type         the picked volume type
     */
    void user_set_volume_type(int extruder_id, NozzleVolumeType type);

    /**
     * \brief The user picked one diameter for all extruders
     *
     * Every installed nozzle of the newest layer takes \p diameter and keeps its volume type, so a
     * nozzle of another diameter is kept as the user meant it, not replaced by the history.
     * Without a layer for the selected printer this logs a warning and does nothing.
     *
     * \param diameter  the picked diameter, in any spelling of a standard size
     */
    void user_unify_diameter(const std::string &diameter);

    // ---- Read ----

    /**
     * \brief Number of extruders of the selected printer
     */
    int extruder_count() const;

    /**
     * \brief Effective slots of one extruder
     *
     * \param extruder_id  0-based extruder
     * \return             the slots; empty for an unknown extruder
     */
    const ExtruderSlots &slots(int extruder_id) const;

    /**
     * \brief Number of slots per volume type
     *
     * Counts the slots of the selected diameter, or all installed slots if none has it.
     *
     * \param extruder_id  0-based extruder
     * \param diameter     count only the slots of this diameter, without that fallback; use it to ask how many
     *                     nozzles a print of one diameter can really use
     */
    std::map<NozzleVolumeType, int> counts(int extruder_id, const std::optional<std::string> &diameter = std::nullopt) const;

    /**
     * \brief One entry of counts()
     *
     * \param extruder_id  0-based extruder
     * \param type         volume type to count; none or Hybrid gives the total
     * \param diameter     as in counts()
     */
    int count(int extruder_id, std::optional<NozzleVolumeType> type = std::nullopt,
              const std::optional<std::string> &diameter = std::nullopt) const;

    /**
     * \brief Volume type of one extruder
     *
     * \param extruder_id  0-based extruder
     * \return             the single type of counts(); Hybrid if they differ, Standard if there is none
     */
    NozzleVolumeType volume_type(int extruder_id) const;

    /**
     * \brief Write the nozzle keys into the mirror again
     *
     * Call it after a bulk copy into the mirror replaced the nozzle keys, or after the multi-diameter
     * mode changed, because the counts follow the mode.
     */
    void refresh_mirror();

    /**
     * \brief Write the nozzle keys that slicing and project files read
     *
     * Writes nothing before a printer is selected, so the keys keep their values.
     *
     * \param config  config that receives the keys; other keys stay as they are
     */
    void export_to(DynamicPrintConfig &config) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Slic3r
