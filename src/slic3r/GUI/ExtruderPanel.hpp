#pragma once

#include <array>
#include <memory>
#include <string>

#include <wx/dialog.h>
#include <wx/panel.h>

#include "GUI_Utils.hpp"
#include "ExtruderHelpers.hpp" // NozzleConfig and MachineSlot, held by NozzleRackPanel
#include "libslic3r/PrintConfig.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/AMSItem.hpp"

class wxStaticText;
class wxBoxSizer;
class wxEventFilter;
class wxSizerItem;
class ComboBox;

namespace Slic3r::GUI {

class NozzleSlotTile; // one labelled nozzle slot: icon + diameter + flow; defined in ExtruderPanel.cpp
class SlimScrollbar;

// Single-/multi-diameter mode pill geometry, from the design. Shared because DiameterButtonPanel
// sizes the widget to its widest item while the nozzle card that hosts it both straddles its own top
// border across that height and lays out the label and chevron inside it.
constexpr int MODE_PILL_H      = 24;
constexpr int MODE_PILL_PAD_X  = 8;  // inset before the label and after the chevron
constexpr int MODE_PILL_GAP    = 6;  // label to chevron
constexpr int MODE_PILL_ICON_D = 16; // drop-down chevron

/**
 * \brief Vertical divider between two extruder columns, with an optional filament switcher icon on it
 *
 * Clicking the icon shows the filament switcher tip for the current state.
 */
class CustomSeparator : public wxPanel
{
public:
    enum State { Normal, Error, Hidden };

    CustomSeparator(wxWindow *parent, bool is_small);

    void  SetState(State state);
    State GetState() const { return m_state; }

    /**
     * \brief Centre the icon vertically on `offset` (client y); -1 centres it in the panel
     */
    void SetIconYOffset(int offset);

private:
    ScalableBitmap m_normal_bitmap;
    ScalableBitmap m_error_bitmap;

    State m_state{Hidden};
    int   m_icon_y_offset{-1};

    wxRect icon_rect() const; // empty when Hidden
    void   on_paint(wxPaintEvent &evt);
};

class DiameterButtonPanel : public wxPanel
{
public:
    using NozzleQueryCallback = std::function<bool(const wxString &diameter)>;
    using MultiDiameterModeCallback = std::function<void(bool enabled)>;

    enum ButtonState { Selected, HasNozzle, NoNozzle };
    enum DisabledTooltipContext { PrinterContext, FilamentContext };

    DiameterButtonPanel(wxWindow *parent, const std::vector<wxString> &diameter_choices = std::vector<wxString>(),
                        bool show_title = true, DisabledTooltipContext tooltip_context = PrinterContext,
                        bool show_multi_diameter_mode = false);
    wxString    GetSelectedDiameter() const { return m_selected_diameter; }
    bool        IsMultiDiameterMode() const { return m_multi_diameter_mode_enabled; }
    void        SetSelectedDiameter(const wxString &diameter);
    void        SetNozzleQueryCallback(NozzleQueryCallback callback) { m_nozzle_query_callback = callback; }
    void        SetMultiDiameterMode(bool enabled);
    void        SetMultiDiameterModeCallback(MultiDiameterModeCallback callback) { m_multi_diameter_mode_callback = callback; }
    void        SetShowTitle(bool show);
    // Show or hide the Single-/Multi-diameter mode selector. Only the sidebar panel is built with one, so
    // this is a no-op elsewhere. The selector belongs to single-extruder multi-nozzle printers only.
    void        ShowModeSelector(bool show);
    /**
     * \brief Whether the diameter buttons show.
     *
     * Multi-diameter mode hides them only while the mode selector is shown: a printer without the
     * selector keeps its buttons whatever the project's mode says.
     */
    bool        ShowsDiameterButtons() const;
    /**
     * \brief Adopt the mode selector the nozzle card owns and drives from then on.
     *
     * The design draws the selector as a pill straddling the nozzle card's top border, and a
     * wxWidgets child cannot be drawn outside its parent, so the widget has to be the card's child
     * rather than this panel's. This panel still owns the mode itself -- it hides its own diameter
     * buttons in multi-diameter mode -- so it keeps the state and only borrows the widget.
     *
     * Populates the two items, wires the change callback and pushes the current state onto the pill.
     * Safe to call before the mode is known: \ref SetMultiDiameterMode and \ref ShowModeSelector
     * forward to it afterwards.
     */
    void        AttachModePill(ComboBox *pill);
    void        UpdateSingleButtonState(Button *btn, const wxString &diameter);
    void        UpdateButtonStates();
    void        RefreshLayout(const std::vector<wxString> &choices);
    std::vector<wxString> GetChoices() const { return m_choices; }
    void        Rescale();

private:
    void OnButtonClicked(wxCommandEvent &event);
    void CreateLayout();
    /**
     * \brief Widen the mode selector so its widest item shows without ellipsis
     *
     * No-op until the pill is attached and its items are populated.
     */
    void FitModeComboToWidestItem();
    // In multi-diameter mode every configured diameter is valid, so the diameter selector is meaningless;
    // hide its title + buttons. Sidebar panel only (the one carrying the mode selector).
    void UpdateSelectorVisibility();
    void ApplyMode(); // re-applies the mode to the buttons and to a hosting card

    std::vector<Button *> m_buttons;
    std::vector<wxString> m_choices;
    wxString              m_selected_diameter;
    bool                  m_show_title{true};
    bool                  m_show_multi_diameter_mode{false};
    bool                  m_multi_diameter_mode_enabled{false};
    DisabledTooltipContext m_tooltip_context{PrinterContext};
    NozzleQueryCallback   m_nozzle_query_callback{nullptr};
    MultiDiameterModeCallback m_multi_diameter_mode_callback{nullptr};
    ComboBox              *m_multi_diameter_mode_combo{nullptr}; // owned by the nozzle card, not by this panel
    wxBoxSizer            *m_title_sizer{nullptr};
    wxBoxSizer            *m_button_sizer{nullptr};
    bool                   m_mode_selector_visible{true}; // survives CreateLayout rebuilds
};

/**
 * \brief Content that scrolls sideways under a slim scrollbar once it is wider than the view.
 *
 * Parent children to \ref Content and add them to \ref ContentSizer. The view is as tall as its
 * content, plus the scrollbar while the content overflows, and can be squeezed to any width: the
 * content keeps its minimum width and scrolls instead of being clipped or shrunk. While it overflows,
 * any mouse wheel scrolls it sideways; otherwise the wheel goes on to the enclosing window.
 *
 * Call \ref Layout after changing the content: a view whose own size did not change is not
 * re-measured otherwise.
 */
class HorizontalScrollView : public wxPanel
{
public:
    explicit HorizontalScrollView(wxWindow *parent);
    ~HorizontalScrollView() override;

    wxWindow   *Content() const;
    wxBoxSizer *ContentSizer() const;

    bool Layout() override;

private:
    // Scrolls sideways when the content overflows; returns false to let the wheel go elsewhere.
    bool ScrollByWheel(const wxMouseEvent &evt);

    wxScrolledWindow              *m_content{nullptr};
    SlimScrollbar                 *m_scrollbar{nullptr};
    std::unique_ptr<wxEventFilter> m_wheel_filter; // catches wheel turns over the view wherever they are sent
};

/**
 * \brief Every extruder's nozzle rack as one row of slot tiles, with the AMS units under it.
 *
 * One extruder numbers its slots 1..N. Two extruders number them L then R1..Rn, draw the dual-head
 * hotend, and show their AMS units as a left half and a right half split by a divider. All of this
 * follows from the extruder count of the last \ref UpdateNozzles call.
 */
class NozzleRackPanel : public wxPanel
{
public:
    explicit NozzleRackPanel(wxWindow *parent);

    /**
     * \brief Rebuild the slot tiles from every extruder's nozzle configs.
     *
     * \param extruders         nozzle configs per extruder, left first; its size sets the extruder count
     * \param selected_diameter the printing diameter; slots on another one are dimmed unless the
     *                          single-extruder multi-diameter mode is on
     */
    void UpdateNozzles(const std::vector<std::vector<NozzleConfig>> &extruders, const wxString &selected_diameter);

    /// \brief Number of extruders given to the last \ref UpdateNozzles call; 1 before the first one.
    int extruder_count() const { return m_extruder_count; }

    /// \brief Set how many AMS units of each kind \p extruder has, then redraw the AMS row.
    void set_ams_count(int extruder, int n4, int n1);

    /// \brief The AMS unit counts of \p extruder, as {4-slot units, 1-slot units}.
    std::pair<int, int> ams_count(int extruder) const;

    /**
     * \brief Re-read the machine's AMS units of \p extruder, then redraw if any changed.
     *
     * A null \p obj is allowed only with empty lists, which forgets what was read before.
     */
    void sync_ams(int                            extruder,
                  MachineObject const           *obj,
                  std::vector<DevAms *> const   &ams_internal,
                  std::vector<DevAms *> const   &ams4,
                  std::vector<DevAms *> const   &ams1,
                  std::vector<DevAmsTray> const &ext);

    /**
     * \brief Show what the printer reports in the slots the project leaves empty
     *
     * Cheap to call on every printer message: the tiles redraw only when \p slots changes.
     *
     * \param extruder  0-based extruder
     * \param slots     per slot from machine_slots(); empty forgets them
     */
    void sync_machine_slots(int extruder, std::vector<MachineSlot> slots);

    void Rescale();

private:
    static constexpr int MAX_EXTRUDERS = 2;

    // One extruder's AMS units: the count the user configured, what the machine reported, and the
    // previews that draw them.
    struct ExtruderAms
    {
        int                       n4{0};
        int                       n1{0};
        std::vector<AMSinfo>      ams_4;
        std::vector<AMSinfo>      ams_1;
        std::vector<AMSinfo>      ams_internal;
        std::vector<AMSinfo>      ext;
        std::string               device_id;
        Label                    *side{nullptr}; // L or R ahead of the units, two extruders only
        wxBoxSizer               *sizer{nullptr};
        std::vector<AMSPreview *> previews; // fixed pool, opened or closed by update_ams
    };

    wxSizer *CreateAmsRow();
    void     update_ams();
    void     show_ams_units(int extruder, ExtruderAms &ams, bool visible, bool enabled);
    void     rebuild_tiles(); // from the project slots and the printer's slots
    void     add_extruder_tiles(int extruder, bool multi_diameter);

    // Two sources at two rates: the project slots change on user edits and sync (UpdateNozzles), the
    // printer's slots on every printer message (sync_machine_slots). Either one rebuilds the tiles.
    std::vector<std::vector<NozzleConfig>> m_extruders;
    wxString                               m_selected_diameter;
    std::array<std::vector<MachineSlot>, MAX_EXTRUDERS> m_machine_slots;

    int                                   m_extruder_count{1};
    wxBoxSizer                           *m_tile_row{nullptr};
    std::array<ExtruderAms, MAX_EXTRUDERS> m_ams;
    std::array<bool, MAX_EXTRUDERS>        m_ams_enabled{{true, true}};
    wxSizerItem                          *m_ams_divider{nullptr}; // the line between the left and right AMS units, with its gaps
};

} // namespace Slic3r::GUI
