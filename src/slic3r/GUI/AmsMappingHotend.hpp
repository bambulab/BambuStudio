#ifndef slic3r_GUI_AmsMappingHotend_hpp_
#define slic3r_GUI_AmsMappingHotend_hpp_

#include "AmsMappingPopup.hpp"

namespace Slic3r { namespace GUI {

enum class HotendSlotState {
    Normal,         // selectable (choose / regular)
    Unmatch,        // filament type mismatch, 30% opacity (ban)
    NozzleEmpty,    // nozzle present but no filament, 30% opacity
    AllEmpty,       // both nozzle and filament absent, 30% opacity (bban1)
    Unavailable     // nozzle unavailable, grey index + 50% opacity nozzle area (Variant5)
};

class MappingItemHotend : public wxPanel
{
public:
    MappingItemHotend(wxWindow* parent);
    ~MappingItemHotend();

    // --- Data setters ---
    void set_data(const wxString& index_str,
                  wxColour colour,
                  wxString name,
                  TrayData data,
                  bool unmatch = false);

    void set_nozzle_info(const wxString& nozzle_dia, const wxString& flow_type);
    void set_checked(bool checked);
    void set_tray_index(const wxString& index) { m_tray_index = index; }

    // Slot state
    void set_slot_state(HotendSlotState state);
    HotendSlotState get_slot_state() const { return m_slot_state; }

    // Ext mode
    void set_ext_mode(bool is_ext, bool has_virtual_ams = false);
    bool is_ext() const { return m_is_ext; }

    // Sub-AMS (mode B)
    void set_sub_ams(const std::vector<TrayData>& sub_trays,
                     const wxString& ams_display_name,
                     bool remain_detect);
    bool has_sub_ams() const { return !m_sub_tray_data.empty(); }

    // Expand / collapse state (panel managed externally by popup)
    void set_expanded(bool v)    { m_expanded = v; }
    bool is_expanded() const     { return m_expanded; }

    // Sub-panel reference (owned by popup's sizer, not by this item)
    void     set_sub_panel_ref(wxPanel* p) { m_sub_panel = p; }
    wxPanel* get_sub_panel_ref() const     { return m_sub_panel; }

    // Sub-AMS data accessors for external panel creation
    const std::vector<TrayData>& get_sub_tray_data() const { return m_sub_tray_data; }
    const wxString& get_sub_ams_name() const { return m_sub_ams_name; }
    bool get_remain_detect() const { return m_remain_detect; }

    // Legacy expand/collapse (kept for compatibility, no longer used internally)
    void toggle_expand(int filament_id = -1);
    void collapse();

    // Event
    wxWindow* send_win{nullptr};
    void send_event(int filament_id);

    // Data access
    TrayData    get_tray_data() const { return m_tray_data; }
    int         get_ams_id() const { return m_tray_data.ams_id; }
    int         get_slot_id() const { return m_tray_data.slot_id; }

    void msw_rescale();

public:
    wxString    m_tray_index;
    TrayData    m_tray_data;
    int         m_ams_id{255};
    int         m_slot_id{255};
    bool        m_unmatch{false};

private:
    // Paint
    void paintEvent(wxPaintEvent& evt);
    void render(wxDC& dc);
    void doRender(wxDC& dc);

    // Sub-renderers (all use fixed Figma coordinates, no cumulative y)
    void render_header(wxDC& dc);              // index + ext label
    void render_filament(wxDC& dc);            // spool icon + color rect + type name
    void render_ams_preview(wxDC& dc);         // mode B: multi-slot color thumbnails
    void render_separator(wxDC& dc);           // dashed line at y=50
    void render_nozzle_info(wxDC& dc);         // nozzle dia + flow type at y=56

    // Legacy overloads for compatibility (delegate to no-arg versions)
    void render_header(wxDC& dc, int& y)      { render_header(dc); }
    void render_filament(wxDC& dc, int& y)    { render_filament(dc); }
    void render_ams_preview(wxDC& dc, int& y) { render_ams_preview(dc); }
    void render_separator(wxDC& dc, int& y)   { render_separator(dc); }
    void render_nozzle_info(wxDC& dc, int& y) { render_nozzle_info(dc); }

    // Click
    void on_left_down(wxMouseEvent& evt);

    // Sub-panel management
    void create_sub_panel(int filament_id = -1);
    void destroy_sub_panel();

private:
    wxColour    m_colour;
    wxString    m_name;
    bool        m_checked{false};

    // Slot state & Ext mode
    HotendSlotState m_slot_state{HotendSlotState::Normal};
    bool            m_is_ext{false};
    bool            m_has_virtual_ams{false};

    // Nozzle info
    wxString    m_nozzle_dia;       // "0.4 mm"
    wxString    m_flow_type;        // "Standard"

    // Sub-AMS data (mode B)
    std::vector<TrayData>       m_sub_tray_data;
    wxString                    m_sub_ams_name;
    bool                        m_remain_detect{false};
    bool                        m_expanded{false};

    // Sub-panel widgets
    wxPanel*                    m_sub_panel{nullptr};
    MappingContainer*           m_sub_container{nullptr};
    std::vector<MappingItem*>   m_sub_mapping_items;

    // Bitmaps
    ScalableBitmap m_bitmap_spool;              // "ams_wheel_narrow"
    ScalableBitmap m_bitmap_spool_transparent;  // "filament_transparent"
    ScalableBitmap m_bitmap_not_match;          // "filament_not_mactch"
    ScalableBitmap m_bitmap_checked;            // "mapping_item_checked"
    ScalableBitmap m_bitmap_nozzle;             // "dev_rack_nozzle_print_job"
};


class MaterialItemHotend : public MaterialItem
{
public:
    MaterialItemHotend(wxWindow *parent, wxColour mcolour, wxString mname, std::string filament_id);
    ~MaterialItemHotend() override;

    void msw_rescale() override;
    void set_ams_filament_type(const wxString& ams_filament_type) override;
    void set_nozzle_dia_and_type(const wxString& dia_type_info) override;

private:
    void render(wxDC& dc) override;
    void doRender(wxDC& dc) override;
    void messure_size() override;

private:
    wxString m_rack_nozzle_type;
    wxString m_ams_filament_type;
    wxString m_nozzle_dia_and_type;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_AmsMappingHotend_hpp_
