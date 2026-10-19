/*****************************************************************//**
 * \file   BlockCodeUtil.cpp
 * \brief  The block-code interaction factory.
 *
 * Copy and behaviour are transcribed from the interaction design's summary
 * table; the comment on each case names the row it came from.
 *********************************************************************/
#include "BlockCodeUtil.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

#include <sstream>

namespace Slic3r { namespace GUI { namespace AutoRefillAddRefillFilamentsBlockCodeUtil {

namespace {

RefillBlockCodeUx selectable() // No restriction at all.
{
    RefillBlockCodeUx ux;
    ux.policy       = SelectPolicy::Selectable;
    ux.feedback     = FeedbackKind::None;
    return ux;
}

RefillBlockCodeUx blocked_tooltip(const char* tooltip) // Blocked, dimmed, hover bubble only.
{
    RefillBlockCodeUx ux;
    ux.policy       = SelectPolicy::Blocked;
    ux.feedback     = FeedbackKind::Tooltip;
    ux.tooltip      = tooltip;
    return ux;
}

/**
 * \brief Blocked, explained by a dialog on click.
 *
 * \param confirm Right-hand button; `None` leaves the dialog with only Cancel.
 */
RefillBlockCodeUx blocked_dialog(const char*  title,
                                 const char*  message,
                                 const char*  confirm_label,
                                 FollowUp     confirm,
                                 DetailFields details = DetailFields::None)
{
    RefillBlockCodeUx ux;
    ux.policy        = SelectPolicy::Blocked;
    ux.feedback      = FeedbackKind::Dialog;
    ux.follow_up     = confirm;
    ux.details       = details;
    ux.title         = title;
    ux.message       = message;
    ux.confirm_label = confirm_label;
    return ux;
}

// Selectable once confirmed; confirming keeps the tick.
RefillBlockCodeUx confirm_dialog(const char*  title,
                                 const char*  message,
                                 DetailFields details,
                                 bool         allow_dont_show_again)
{
    RefillBlockCodeUx ux;
    ux.policy                = SelectPolicy::ConfirmThenSelect;
    ux.feedback              = FeedbackKind::Dialog;
    ux.allow_dont_show_again = allow_dont_show_again;
    ux.follow_up             = FollowUp::SelectAnyway;
    ux.details               = details;
    ux.title                 = title;
    ux.message               = message;
    ux.confirm_label         = "Select anyway";
    return ux;
}

int severity(SelectPolicy policy)
{
    switch (policy) {
    case SelectPolicy::Selectable:        return 0;
    case SelectPolicy::ConfirmThenSelect: return 1;
    case SelectPolicy::Blocked:           return 2;
    }
    return 2;
}

// ---- Payload names: only ToJson needs these, the web layer reads the strings ----

const char* policy_name(SelectPolicy policy)
{
    switch (policy) {
    case SelectPolicy::Selectable:        return "selectable";
    case SelectPolicy::ConfirmThenSelect: return "confirm_then_select";
    case SelectPolicy::Blocked:           return "blocked";
    }
    return "blocked";
}

const char* feedback_name(FeedbackKind feedback)
{
    switch (feedback) {
    case FeedbackKind::None:    return "none";
    case FeedbackKind::Tooltip: return "tooltip";
    case FeedbackKind::Toast:   return "toast";
    case FeedbackKind::Dialog:  return "dialog";
    }
    return "none";
}

const char* follow_up_name(FollowUp follow_up)
{
    switch (follow_up) {
    case FollowUp::None:              return "none";
    case FollowUp::SelectAnyway:      return "select_anyway";
    case FollowUp::GoToCalibration:   return "go_to_calibration";
    case FollowUp::ViewHmsDetail:     return "view_hms_detail";
    case FollowUp::GoToRefreshHotend: return "go_to_refresh_hotend";
    case FollowUp::GoToEditHotend:    return "go_to_edit_hotend";
    case FollowUp::GoToEditFilament:  return "go_to_edit_filament";
    case FollowUp::GoToUnbind:        return "go_to_unbind";
    case FollowUp::GoToLoad:          return "go_to_load";
    }
    return "none";
}

const char* detail_fields_name(DetailFields details)
{
    switch (details) {
    case DetailFields::None:               return "none";
    case DetailFields::FilamentComparison: return "filament_comparison";
    case DetailFields::HotendComparison:   return "hotend_comparison";
    case DetailFields::HotendIndex:        return "hotend_index";
    }
    return "none";
}

} // namespace

RefillBlockCodeUx Create(DevFilaRefillBlockCode block_code)
{
    switch (block_code) {
    case DevFilaRefillBlockCode::Available:
        return selectable();

    // Not a restriction: the device reports this for a slot that already
    // occupies one of the requested target positions, i.e. one the user has
    // already picked. Treating it as blocked cleared `selectable`, and
    // `selected` is derived from it, so the slot the code describes as chosen
    // was the one rendered unchosen. Staying selectable keeps its tick.
    case DevFilaRefillBlockCode::DuplicateTargetPosition:
        return selectable();

    // ---- Hotend faults ----

    // Design row 2: never docking-calibrated.
    case DevFilaRefillBlockCode::ToolRackSlotNotCalibrated:
        return blocked_tooltip("PrintTool rack position is not calibrated, cannot select.");

    case DevFilaRefillBlockCode::ToolheadDisconnected: // Design row 3: cable is down.
        return blocked_tooltip("Hotend communication error, cannot select.");

    case DevFilaRefillBlockCode::ToolheadIsNotNozzle: // Design row 4: not a nozzle on the toolhead.
        return blocked_tooltip("Tool type doesn't match, cannot select.");

    // Design row 30: the tool is mounted but its identity was never read, so
    // the group cannot be matched against it.
    case DevFilaRefillBlockCode::NozzleInfoUnknown:
        return blocked_tooltip("Hotend type unknown, cannot select.");

    // ---- Hotend mismatch: one dialog for both diameter and flow ----

    case DevFilaRefillBlockCode::NozzleDiameterMismatch:
    // Design row 9. The hotend settings are read-only in Studio, so the
    // guidance stays in the message instead of exposing a dead-end button.
    case DevFilaRefillBlockCode::NozzleFlowMismatch:
        return blocked_dialog("The hotend type doesn't match",
                              "Please confirm whether the hotend type in the current slot matches the refill group. If it doesn't, please go to the printer and edit the hotend type before trying again.",
                              nullptr, FollowUp::None,
                              DetailFields::HotendComparison);

    // ---- Filament mismatch: one bubble for all three category codes ----

    // Design row 7. `CustomFilamentNotFound` lands here because the design
    // groups third-party / external spools with the category mismatch case.
    case DevFilaRefillBlockCode::FilamentCategoryMismatch:
    case DevFilaRefillBlockCode::SupportPropertyMismatch:
    case DevFilaRefillBlockCode::CustomFilamentNotFound:
        return blocked_tooltip("Filament type mismatch, cannot select.");

    // ---- Already grouped ----

    // Design row 12. The auto-generated variant reads the same; neither
    // offers an unbind entry any more.
    case DevFilaRefillBlockCode::AlreadyInAnotherChain:
    case DevFilaRefillBlockCode::AutoAssignedToAnotherChain:
        return blocked_tooltip("Filament is in another refill group, cannot select.");

    // ---- Filament state ----

    case DevFilaRefillBlockCode::FilamentNotLoadedToExtruder: // Design row 14.
    // Reported when the nozzle currently excludes the slot, which the user
    // resolves the same way as row 14 - by loading the filament - so it
    // shares that row's copy.
    case DevFilaRefillBlockCode::HiddenByNozzleExclusion:
        return blocked_tooltip("Filament not loaded, cannot select.");

    // Design row 15: the slot's filament cannot be identified. The design
    // also lists a printing-state variant that only warns; that one is not
    // distinguishable by `block` code alone and is left to the caller.
    case DevFilaRefillBlockCode::FilamentIdentityUnavailable:
    case DevFilaRefillBlockCode::SourceFilamentTagUnknown:
        return blocked_tooltip("Filament type unknown, cannot select.");

    // ---- Warnings: selectable behind a confirmation ----

    case DevFilaRefillBlockCode::VisibleLayerLines: // Design row 10: the only row with "Don't show again".
        return confirm_dialog("Filament type mismatch",
                              "Printing these two filaments together may cause surface layering. Choose carefully.",
                              DetailFields::FilamentComparison, true);

    case DevFilaRefillBlockCode::FilamentSubtypeMismatch: // Design row 11.
        return confirm_dialog("Filament type mismatch",
                              "Printing these two filaments together may affect print quality. Choose carefully.",
                              DetailFields::FilamentComparison, false);

    case DevFilaRefillBlockCode::NozzleNotCalibrated: // Design row 13: the hotend index fills both strings.
        return confirm_dialog("Hotend %1 is missing nozzle offset calibration data",
                              "Hotend %1 is missing nozzle offset calibration data. Using it for refill may affect print quality. Do you still want to use it?",
                              DetailFields::HotendIndex, true);

    // ---- Codes the design does not cover ----

    case DevFilaRefillBlockCode::EmptySlot:
        return blocked_tooltip("Empty slot.");

    // The design only states the rule in prose: when the toolhead holds a
    // filament that cannot be loaded automatically, nothing but that
    // filament may join a group. No copy was given for it.
    case DevFilaRefillBlockCode::InvalidFeedChannel:
        break;
    }

    // Unknown or undesigned code: the wording that shipped before this factory.
    return blocked_tooltip("This filament cannot be added to an auto-refill group.");
}

bool IsBlocked(DevFilaRefillBlockCode block_code)
{
    return Create(block_code).policy == SelectPolicy::Blocked;
}

bool NeedsConfirm(DevFilaRefillBlockCode block_code)
{
    return Create(block_code).policy == SelectPolicy::ConfirmThenSelect;
}

bool IsSuppressed(DevFilaRefillBlockCode block_code)
{
    if (!Create(block_code).allow_dont_show_again || !wxGetApp().app_config) {
        return false;
    }
    std::istringstream stored(wxGetApp().app_config->get(kSuppressedCodesKey));
    for (std::string entry; std::getline(stored, entry, ',');) {
        if (!entry.empty() && std::atoi(entry.c_str()) == static_cast<int>(block_code)) {
            return true;
        }
    }
    return false;
}

void Suppress(DevFilaRefillBlockCode block_code)
{
    if (!Create(block_code).allow_dont_show_again || !wxGetApp().app_config || IsSuppressed(block_code)) {
        return;
    }
    const std::string stored = wxGetApp().app_config->get(kSuppressedCodesKey);
    const std::string code   = std::to_string(static_cast<int>(block_code));
    wxGetApp().app_config->set(kSuppressedCodesKey, stored.empty() ? code : stored + "," + code);
}

DevFilaRefillBlockCode SelectPrimary(const std::vector<DevFilaRefillBlockCode>& block_codes)
{
    DevFilaRefillBlockCode primary = DevFilaRefillBlockCode::Available;
    for (const DevFilaRefillBlockCode code : block_codes) {
        if (code == DevFilaRefillBlockCode::Available) {
            continue;
        }
        if (primary == DevFilaRefillBlockCode::Available ||
            severity(Create(code).policy) > severity(Create(primary).policy)) {
            primary = code;
        }
    }
    return primary;
}

nlohmann::json ToJson(DevFilaRefillBlockCode block_code)
{
    const RefillBlockCodeUx ux = Create(block_code);

    const auto text = [](const char* value) {
        return value ? nlohmann::json(value) : nlohmann::json(nullptr);
    };

    return {
        {"block_code",            static_cast<int>(block_code)},
        {"policy",                policy_name(ux.policy)},
        {"feedback",              feedback_name(ux.feedback)},
        {"allow_dont_show_again", ux.allow_dont_show_again},
        {"follow_up",             follow_up_name(ux.follow_up)},
        {"details",               detail_fields_name(ux.details)},
        {"tooltip",               text(ux.tooltip)},
        {"title",                 text(ux.title)},
        {"message",               text(ux.message)},
        {"confirm_label",         text(ux.confirm_label)},
    };
}

}}} // namespace Slic3r::GUI::AutoRefillAddRefillFilamentsBlockCodeUtil
