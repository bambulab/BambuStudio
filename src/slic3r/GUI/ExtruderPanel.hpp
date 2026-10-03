#pragma once

#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/wrapsizer.h>

#include "GUI_Utils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/AMSItem.hpp"

class wxStaticText;
class wxBoxSizer;
class ComboBox;

namespace Slic3r::GUI {

struct NozzleConfig;
class NozzleSummary;  // icon + diameter + flow + count tile; defined in ExtruderPanel.cpp
class NozzleSlotTile; // one numbered nozzle slot: icon + diameter + flow; defined in ExtruderPanel.cpp
class NozzleTileScrollbar;

enum class NozzleStatus : int
{
    nsNormal = 0,
    nsError,
    nsUnknown
};

// Single-/multi-diameter mode pill geometry, from the design. Shared because DiameterButtonPanel
// sizes the widget to its widest item while the nozzle card that hosts it both straddles its own top
// border across that height and lays out the label and chevron inside it.
constexpr int MODE_PILL_H      = 24;
constexpr int MODE_PILL_PAD_X  = 8;  // inset before the label and after the chevron
constexpr int MODE_PILL_GAP    = 6;  // label to chevron
constexpr int MODE_PILL_ICON_D = 16; // drop-down chevron

class CustomSeparator : public wxPanel
{
public:
    enum State { Normal, Error, Hidden };

    CustomSeparator(wxWindow *parent, bool is_small);

    void  SetState(State state);
    State GetState() const { return m_state; }
    void  UpdatePosition();
    void  SetIconYOffset(int offset)
    {
        m_icon_y_offset = offset;
        UpdatePosition();
    }

private:
    wxPanel        *m_separator_line{nullptr};
    wxStaticBitmap *m_icon{nullptr};

    ScalableBitmap m_normal_bitmap;
    ScalableBitmap m_error_bitmap;

    State m_state{Hidden};
    int   m_icon_y_offset{-1};

    void CreateLayout();
};

class DiameterButtonPanel : public wxPanel
{
public:
    using NozzleQueryCallback = std::function<bool(const wxString &diameter)>;
    using NoNozzleClickCallback = std::function<void(const wxString &diameter)>;
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
    void        SetNoNozzleClickCallback(NoNozzleClickCallback callback) { m_no_nozzle_click_callback = callback; }
    void        SetMultiDiameterMode(bool enabled);
    void        SetMultiDiameterModeCallback(MultiDiameterModeCallback callback) { m_multi_diameter_mode_callback = callback; }
    void        SetShowTitle(bool show);
    // Show or hide the Single-/Multi-diameter mode selector. Only the sidebar panel is built with one, so
    // this is a no-op elsewhere. The selector belongs to single-extruder multi-nozzle printers only.
    void        ShowModeSelector(bool show);
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

    std::vector<Button *> m_buttons;
    std::vector<wxString> m_choices;
    wxString              m_selected_diameter;
    bool                  m_show_title{true};
    bool                  m_show_multi_diameter_mode{false};
    bool                  m_multi_diameter_mode_enabled{false};
    DisabledTooltipContext m_tooltip_context{PrinterContext};
    NozzleQueryCallback   m_nozzle_query_callback{nullptr};
    NoNozzleClickCallback m_no_nozzle_click_callback{nullptr};
    MultiDiameterModeCallback m_multi_diameter_mode_callback{nullptr};
    ComboBox              *m_multi_diameter_mode_combo{nullptr}; // owned by the nozzle card, not by this panel
    wxBoxSizer            *m_title_sizer{nullptr};
    wxBoxSizer            *m_button_sizer{nullptr};
    bool                   m_mode_selector_visible{true}; // survives CreateLayout rebuilds
};

class HorizontalScrollablePanel : public wxScrolledWindow
{
public:
    HorizontalScrollablePanel(wxWindow* parent,
                             wxWindowID id = wxID_ANY,
                             const wxPoint& pos = wxDefaultPosition,
                             const wxSize& size = wxDefaultSize,
                             long style = wxHSCROLL);

    void Clear();

private:
    wxBoxSizer *m_content_sizer{nullptr};
};

/**
 * \brief Abstract sidebar extruder panel: shared widgets + a uniform public API.
 *
 * Not instantiable on its own — a concrete subclass supplies the variant's middle
 * content via \ref CreateContent. Layout is deferred to the non-virtual \ref Build,
 * which each subclass constructor calls as its final statement.
 *   - \ref DualExtruderPanel   — one side (left/right) of a dual card: scroll + AMS previews
 *   - \ref SingleExtruderPanel — single head with multiple nozzles: tiles only, no AMS scroll
 */
class ExtruderPanel : public wxPanel
{
public:
    enum GroupType {
        LeftExtruder,
        RightExtruder,
        SingleExtruder
    };

    GroupType GetType() const { return m_type; }

    // Project the extruder's nozzle configs + current selection into NozzleSummary tiles.
    // The base builds the single-nozzle-diameter view (filtered by selected_diameter);
    // SingleExtruderPanel overrides this to add the experimental multi-diameter mode. Rebuilds the
    // tiles, so call it on any data or selection change.
    virtual void UpdateNozzles(const std::vector<NozzleConfig> &configs, const wxString &selected_diameter);

    // Store the AMS counts, then trigger the variant's visual rebuild via update_ams().
    void set_ams_count(int n4, int n1);

    // Variant behavior. The base defaults are no-ops so a single-extruder panel safely
    // ignores the scroll calls that Plater issues uniformly on every panel.
    virtual void update_ams() {}
    virtual void UpdateScrollButtons() {}
    virtual void Rescale();

    /**
     * \brief Re-read the machine's AMS units into \ref m_ams_4 and friends, then redraw if changed.
     *
     * Parsing device state into AMSinfo is the same work for every card variant, so it lives here;
     * only the drawing it triggers through the virtual \ref update_ams differs between them.
     */
    void sync_ams(MachineObject const           *obj,
                  std::vector<DevAms *> const   &ams_internal,
                  std::vector<DevAms *> const   &ams4,
                  std::vector<DevAms *> const   &ams1,
                  std::vector<DevAmsTray> const &ext);

    // AMS slot counts for this extruder. Read by the sidebar AMS-count popup on every
    // panel (single included), so they live in the base rather than the dual subclass.
    size_t m_ams_n4{0};
    size_t m_ams_n1{0};

protected:
    explicit ExtruderPanel(wxWindow *parent, GroupType type);

    /**
     * \brief Run the shared construction sequence. Must be called by each subclass ctor.
     *
     * Builds the shared tiles container, invokes the virtual \ref CreateContent for the
     * variant's middle content, appends the trailing disabled icon, sizes the panel, and
     * applies dark-mode theming.
     */
    void Build();

    /**
     * \brief Build the variant's middle content into \p main_sizer.
     * \param main_sizer the horizontal sizer holding the variant content next to the tile row
     */
    virtual void CreateContent(wxBoxSizer *main_sizer) = 0;

    /**
     * \brief Append the variant's own full-width rows below the content row.
     *
     * Runs once the content row is in place, so anything added here stays under it. The tile row
     * still comes first when it needs a line of its own — \ref update_tile_row_line inserts it
     * ahead of these rows rather than appending.
     *
     * \param main_sizer the card's vertical sizer
     */
    virtual void CreateExtraRows(wxBoxSizer *main_sizer) {}

    // Append one NozzleSummary tile to the summary row.
    void add_summary_tile(const wxString &diameter, const wxString &flow, int count, bool show_count);

    // Shared tail of a projection: mixed-diameter warning tooltip, scroll buttons, re-layout.
    void finish_projection(bool enabled);

    // Re-layout the tile row after the card changed size, so the tiles re-wrap to the new width.
    void OnSize(wxSizeEvent &evt);

    // Width the tiles occupy when all of them are kept on one line.
    int tiles_single_line_width() const;

    /**
     * \brief Give the tile row a line of its own once it no longer fits beside the variant content.
     *
     * A row of tiles that has outgrown the space left of the content would otherwise be squeezed
     * onto one line and clipped, so it moves to a second line instead. Call after any change that
     * can alter the tile count or the available width.
     */
    void update_tile_row_line();

    /**
     * \brief Which extruder slot this panel represents.
     *
     * The single/dual layout is chosen by the concrete subclass; this is kept only to
     * tell left from right within a dual card (the right slot shows a count when empty)
     * and to identify the panel to callers through \ref GetType.
     */
    GroupType                    m_type;
    bool                         m_show_title{false};
    int                          m_nozzle_count{0};
    bool                         m_tiles_on_own_line{false}; // true once the tile row sits below the content
    wxWrapSizer                 *m_summary_sizer{nullptr};
    std::vector<NozzleSummary *> m_summaries;
    wxStaticBitmap              *m_warning_icon{nullptr}; // shown at the right end when no nozzle matches the selection
    wxBoxSizer                  *m_content_sizer{nullptr}; // variant content + warning icon, kept together
    wxBoxSizer                  *m_content_row{nullptr};   // tiles + content, split across lines when the tiles don't fit
    wxBoxSizer                  *m_main_sizer{nullptr};

    // The machine's AMS units for this extruder, as parsed by sync_ams. Kept in the base because
    // the parsing is shared; each variant decides for itself how much of it to draw.
    std::vector<AMSinfo> m_ams_4;
    std::vector<AMSinfo> m_ams_1;
    std::vector<AMSinfo> m_ams_internal;
    std::vector<AMSinfo> m_ext;
};

/**
 * \brief One side (left or right) of a dual-extruder card: scroll + AMS previews.
 */
class DualExtruderPanel : public ExtruderPanel
{
public:
    DualExtruderPanel(wxWindow *parent, GroupType type) : ExtruderPanel(parent, type) { Build(); }

    void update_ams() override;
    void UpdateScrollButtons() override;
    void Rescale() override;

protected:
    void CreateContent(wxBoxSizer *main_sizer) override;

private:
    void OnLeftScrollClick(wxCommandEvent &evt);
    void OnRightScrollClick(wxCommandEvent &evt);
    void OnScrollSizeChanged(wxSizeEvent &evt);
    void OnScrollTimer(wxTimerEvent &evt);
    void OnLeftScrollDown(wxMouseEvent &evt);
    void OnLeftScrollUp(wxMouseEvent &evt);
    void OnRightScrollDown(wxMouseEvent &evt);
    void OnRightScrollUp(wxMouseEvent &evt);

    ScalableButton *m_left_scroll{nullptr};
    ScalableButton *m_right_scroll{nullptr};

    wxTimer *m_scroll_timer{nullptr};
    bool                       m_scroll_left{false};
    bool                       m_scroll_right{false};
    HorizontalScrollablePanel *m_scroll{nullptr};
    wxSizer                   *m_ams_sizer{nullptr};

    std::vector<AMSPreview *> m_ams_previews;
    std::vector<AMSPreview *> m_ext_previews;
};

/**
 * \brief Single head with multiple nozzles: one NozzleSlotTile per nozzle slot, no AMS scroll.
 */
class SingleExtruderPanel : public ExtruderPanel
{
public:
    explicit SingleExtruderPanel(wxWindow *parent) : ExtruderPanel(parent, SingleExtruder) { Build(); }

    // Projects each slot as its own numbered tile instead of the base's aggregated summary tile.
    void UpdateNozzles(const std::vector<NozzleConfig> &configs, const wxString &selected_diameter) override;

    // Draws the AMS units attached to this extruder on the row under the nozzle tiles.
    void update_ams() override;

protected:
    void CreateContent(wxBoxSizer *main_sizer) override;
    void CreateExtraRows(wxBoxSizer *main_sizer) override;

private:
    void UpdateTileScrollLayout();

    // Slot tiles keep their design minimum width in one row; the scroll window exposes overflow
    // when the sidebar becomes narrower than the complete rack.
    HorizontalScrollablePanel *m_tile_scroll{nullptr};
    NozzleTileScrollbar        *m_tile_scrollbar{nullptr};
    wxBoxSizer                 *m_tile_row{nullptr};
    std::vector<NozzleSlotTile *> m_nozzle_tiles; // one per configured slot, rebuilt by UpdateNozzles

    wxBoxSizer               *m_ams_sizer{nullptr}; // the AMS row under the tiles
    std::vector<AMSPreview *> m_ams_previews;       // fixed pool, shown or hidden by update_ams
};

} // namespace Slic3r::GUI
