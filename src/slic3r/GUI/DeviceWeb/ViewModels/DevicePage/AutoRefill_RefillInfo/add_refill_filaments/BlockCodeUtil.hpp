/*****************************************************************//**
 * \file   BlockCodeUtil.hpp
 * \brief  Per-code interaction of the add-refill-filaments picker.
 *
 * `DevFilaRefillClassifyBlock()` only answers "may this slot be used". The
 * picker also needs to know, for every `block` code of refill_command 5
 * (Query) / 6 (QueryNext), how the card reacts: dimmed or not, tooltip /
 * toast / dialog, the dialog copy, its follow-up button, and the dynamic
 * lines it has to fill. \ref RefillBlockCodeUx carries all of that; \ref Create is the
 * only way to obtain one.
 *
 * Copy is English and doubles as an i18n key: the web layer feeds it to
 * i18next `t()`, a native wx dialog to `wxGetTranslation()`.
 *********************************************************************/
#ifndef AUTOREFILLADDREFILLFILAMENTSBLOCKCODEUTIL_HPP
#define AUTOREFILLADDREFILLFILAMENTSBLOCKCODEUTIL_HPP

#include <nlohmann/json.hpp>

#include "slic3r/GUI/DeviceCore/DevFilaAutoRefillEdit.h"

#include <vector>

namespace Slic3r { namespace GUI { namespace AutoRefillAddRefillFilamentsBlockCodeUtil {

// ---- Interaction definition ----

/// Whether the slot may end up in the group.
enum class SelectPolicy
{
    Selectable,        ///< No restriction.
    ConfirmThenSelect, ///< Selectable; confirming the dialog keeps the tick.
    Blocked,           ///< Never selectable, whatever the user answers.
};

/// What a click on the card is answered with.
enum class FeedbackKind
{
    None,    ///< Silently not selectable; no copy at all.
    Tooltip, ///< Hover bubble only.
    Toast,   ///< Toast on click; the same text serves as the hover bubble.
    Dialog,  ///< Modal dialog on click.
};

/// Right-hand dialog button. `None` leaves the dialog with only Cancel.
enum class FollowUp
{
    None,
    SelectAnyway,       ///< Keep the tick and close.
    GoToCalibration,    ///< Tool rack docking calibration page.
    ViewHmsDetail,      ///< Matching HMS entry.
    GoToRefreshHotend,  ///< Hotend page, to re-read the tool.
    GoToEditHotend,     ///< Hotend edit page.
    GoToEditFilament,   ///< Filament edit page.
    GoToUnbind,         ///< Refill group currently holding the slot.
    GoToLoad,           ///< Filament page, to load the slot.
};

/// Lines the dialog has to fill from live device data.
enum class DetailFields
{
    None,
    FilamentComparison, ///< "Refill group filament:" + "Current filament:".
    HotendComparison,   ///< "Refill group hotend:" + "Current hotend:".
    HotendIndex,        ///< Hotend display index substituted into the copy.
};

/**
 * \brief How the card behaves for one `block` code.
 *
 * The card is dimmed whenever it is not selectable, so that follows from
 * \ref policy and is not restated here. A dimmed card still has to accept
 * clicks: most blocked codes answer with a dialog naming the reason. The
 * four copy members are null when the code does not use them, so a caller
 * can tell "no copy defined" from "empty copy".
 */
struct RefillBlockCodeUx
{
    SelectPolicy policy                = SelectPolicy::Blocked;
    FeedbackKind feedback              = FeedbackKind::Tooltip;
    FollowUp     follow_up             = FollowUp::None;
    DetailFields details               = DetailFields::None;
    bool         allow_dont_show_again = false; ///< Dialog carries "Don't show again".

    const char* tooltip       = nullptr; ///< Hover bubble / toast text.
    const char* title         = nullptr; ///< Dialog title.
    const char* message       = nullptr; ///< Dialog body.
    const char* confirm_label = nullptr; ///< Right-hand button label.
};

// ---- Factory ----

/**
 * \brief Build the interaction of one `block` code.
 *
 * Codes this file does not know - undesigned ones and anything the
 * firmware may add later - yield a generic blocked interaction.
 */
RefillBlockCodeUx Create(DevFilaRefillBlockCode block_code);

// ---- Queries ----

bool IsBlocked(DevFilaRefillBlockCode block_code);    ///< No answer can make the slot selectable.
bool NeedsConfirm(DevFilaRefillBlockCode block_code); ///< Needs a confirmation before it may be ticked.

// ---- "Don't show again" ----
// Persisted in AppConfig under `kSuppressedCodesKey`, so a dismissal survives
// a restart. Only codes whose interaction allows it are ever stored.

inline constexpr const char* kSuppressedCodesKey = "auto_refill_no_warn_block_codes";

bool IsSuppressed(DevFilaRefillBlockCode block_code); ///< User ticked "Don't show again".
void Suppress(DevFilaRefillBlockCode block_code);     ///< Ignored unless the code allows it.

/**
 * \brief Pick the code that should drive the UI when a slot reports several.
 *
 * A filament may mismatch both its category (25) and its support property
 * (26); a hotend both its diameter (22) and its flow (23). The most severe
 * policy wins, ties keep the first code - safe because the codes that can
 * collide share the same interaction.
 *
 * \return `Available` when \p block_codes holds no restriction.
 */
DevFilaRefillBlockCode SelectPrimary(const std::vector<DevFilaRefillBlockCode>& block_codes);

// ---- Payload serialization ----

/**
 * \brief One code's interaction as JSON, for the web payload.
 *
 * Enum members serialize as stable snake_case names so the web layer never
 * switches on raw ints; null copy members serialize as null.
 */
nlohmann::json ToJson(DevFilaRefillBlockCode block_code);

}}} // namespace Slic3r::GUI::AutoRefillAddRefillFilamentsBlockCodeUtil

#endif // AUTOREFILLADDREFILLFILAMENTSBLOCKCODEUTIL_HPP
