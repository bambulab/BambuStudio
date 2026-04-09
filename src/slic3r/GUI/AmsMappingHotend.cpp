#include "AmsMappingHotend.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "Widgets/Label.hpp"
#include <wx/dcgraph.h>

namespace Slic3r { namespace GUI {


// ============================================================================
// MappingItemHotend
// ============================================================================


static const wxSize HOTEND_ITEM_SIZE = wxSize(92, 92);

MappingItemHotend::MappingItemHotend(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif

    m_bitmap_spool             = ScalableBitmap(this, "ams_wheel_narrow", 20);
    m_bitmap_spool_transparent = ScalableBitmap(this, "filament_transparent", 20);
    m_bitmap_not_match         = ScalableBitmap(this, "filament_not_mactch", 20);
    m_bitmap_checked           = ScalableBitmap(this, "mapping_item_checked", FromDIP(20));
    m_bitmap_nozzle            = ScalableBitmap(this, "dev_rack_nozzle_print_job", 14);

    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    SetSize(FromDIP(HOTEND_ITEM_SIZE));
    SetMinSize(FromDIP(HOTEND_ITEM_SIZE));
    SetMaxSize(FromDIP(HOTEND_ITEM_SIZE));

    Bind(wxEVT_PAINT, &MappingItemHotend::paintEvent, this);
}

MappingItemHotend::~MappingItemHotend()
{
    destroy_sub_panel();
}

void MappingItemHotend::set_data(const wxString& index_str,
                                  wxColour colour,
                                  wxString name,
                                  TrayData data,
                                  bool unmatch)
{
    m_tray_index = index_str;
    m_colour     = colour;
    m_name       = name;
    m_tray_data  = data;
    m_unmatch    = unmatch;
    m_ams_id     = data.ams_id;
    m_slot_id    = data.slot_id;
    Refresh();
}

void MappingItemHotend::set_nozzle_info(const wxString& nozzle_dia, const wxString& flow_type)
{
    if (m_nozzle_dia != nozzle_dia || m_flow_type != flow_type) {
        m_nozzle_dia = nozzle_dia;
        m_flow_type  = flow_type;
        Refresh();
    }
}

void MappingItemHotend::set_slot_state(HotendSlotState state)
{
    if (m_slot_state != state) {
        m_slot_state = state;
        m_unmatch = (state != HotendSlotState::Normal);
        Refresh();
    }
}

void MappingItemHotend::set_ext_mode(bool is_ext, bool has_virtual_ams)
{
    if (m_is_ext != is_ext || m_has_virtual_ams != has_virtual_ams) {
        m_is_ext         = is_ext;
        m_has_virtual_ams = has_virtual_ams;
        Refresh();
    }
}

void MappingItemHotend::set_checked(bool checked)
{
    if (m_checked != checked) {
        m_checked = checked;
        Refresh();
    }
}

void MappingItemHotend::set_sub_ams(const std::vector<TrayData>& sub_trays,
                                     const wxString& ams_display_name,
                                     bool remain_detect)
{
    m_sub_tray_data  = sub_trays;
    m_sub_ams_name   = ams_display_name;
    m_remain_detect  = remain_detect;
}

void MappingItemHotend::send_event(int filament_id)
{
    wxCommandEvent event(EVT_SET_FINISH_MAPPING);
    event.SetInt(m_tray_data.id);

    wxString param = wxString::Format("%d|%d|%d|%d|%s|%d|%d|%d",
        m_colour.Red(), m_colour.Green(), m_colour.Blue(), m_colour.Alpha(),
        m_tray_index, filament_id,
        m_tray_data.ams_id, m_tray_data.slot_id);
    event.SetString(param);

    if (send_win) {
        event.SetEventObject(send_win);
        wxPostEvent(send_win, event);
    }
}

// --- Toggle expand/collapse for sub-AMS ---

void MappingItemHotend::toggle_expand(int /*filament_id*/)
{
    // No-op: expand/collapse is now managed by the popup via set_expanded() + sub_panel Show/Hide
}

void MappingItemHotend::collapse()
{
    // No-op: managed externally
}

void MappingItemHotend::create_sub_panel(int filament_id)
{
    if (m_sub_panel) return;
    if (m_sub_tray_data.empty()) return;

    // --- Find the vertical sizer that holds the rows ---
    // Layout hierarchy: parent->sizer -> vert_sizer -> row_sizers -> hotend_items
    // We need to insert the AMS panel into vert_sizer right after the row containing this item.
    wxWindow* parent = GetParent();
    wxSizer* parent_sizer = parent ? parent->GetSizer() : nullptr;
    if (!parent_sizer) return;

    wxSizer* vert_sizer = nullptr;
    wxSizer* our_row_sizer = nullptr;

    for (size_t si = 0; si < parent_sizer->GetItemCount(); si++) {
        auto* sitem = parent_sizer->GetItem(si);
        if (!sitem->IsSizer()) continue;
        wxSizer* candidate = sitem->GetSizer();
        for (size_t ri = 0; ri < candidate->GetItemCount(); ri++) {
            auto* ritem = candidate->GetItem(ri);
            if (!ritem->IsSizer()) continue;
            wxSizer* row = ritem->GetSizer();
            for (size_t ci = 0; ci < row->GetItemCount(); ci++) {
                if (row->GetItem(ci)->GetWindow() == this) {
                    vert_sizer = candidate;
                    our_row_sizer = row;
                    goto found;
                }
            }
        }
    }
found:
    if (!vert_sizer || !our_row_sizer) return;

    // Find our row's index
    int row_index = -1;
    for (size_t ri = 0; ri < vert_sizer->GetItemCount(); ri++) {
        auto* ritem = vert_sizer->GetItem(ri);
        if (ritem->IsSizer() && ritem->GetSizer() == our_row_sizer) {
            row_index = (int)ri;
            break;
        }
    }
    if (row_index < 0) return;

    // --- Create the AMS panel (full-width, inserted below our row) ---
    m_sub_panel = new wxPanel(parent, wxID_ANY);
    m_sub_panel->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

    auto* panel_sizer = new wxBoxSizer(wxVERTICAL);

    // MappingContainer: traditional AMS card with header ("AMS-A") and tray items
    m_sub_container = new MappingContainer(m_sub_panel, m_sub_ams_name, m_sub_tray_data.size());
    auto* container_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_sub_container->SetSizer(container_sizer);

    container_sizer->Add(0, 0, 0, wxLEFT, FromDIP(6));

    m_sub_mapping_items.clear();
    for (size_t i = 0; i < m_sub_tray_data.size(); i++) {
        auto* item = new MappingItem(m_sub_container);
        item->send_win = send_win;
        item->m_ams_id  = m_sub_tray_data[i].ams_id;
        item->m_slot_id = m_sub_tray_data[i].slot_id;
        item->set_tray_index(wxGetApp().transition_tridid(m_sub_tray_data[i].id));

        item->SetSize(wxSize(FromDIP(48), FromDIP(60)));
        item->SetMinSize(wxSize(FromDIP(48), FromDIP(60)));
        item->SetMaxSize(wxSize(FromDIP(48), FromDIP(60)));

        wxColour display_color = m_sub_tray_data[i].type == NORMAL
            ? m_sub_tray_data[i].colour
            : wxColour(0xCE, 0xCE, 0xCE);

        wxString display_name;
        if (m_sub_tray_data[i].type == THIRD) {
            display_name = "?";
        } else if (m_sub_tray_data[i].type == EMPTY) {
            display_name = "-";
        } else {
            display_name = m_sub_tray_data[i].name;
        }

        bool can_pick = (m_sub_tray_data[i].type == NORMAL);
        item->set_data(wxEmptyString, can_pick ? display_color : wxColour(0xEE, 0xEE, 0xEE),
                       display_name, m_remain_detect, m_sub_tray_data[i], !can_pick);

        item->Bind(wxEVT_LEFT_DOWN, [this, can_pick, item, filament_id](wxMouseEvent& e) {
            if (!can_pick) return;
            m_tray_data = item->m_tray_data;
            m_colour    = item->m_coloul;
            m_ams_id    = item->m_ams_id;
            m_slot_id   = item->m_slot_id;
            wxCommandEvent evt(EVT_SET_FINISH_MAPPING);
            evt.SetInt(item->m_tray_data.id);
            wxString param = wxString::Format("%d|%d|%d|%d|%s|%d|%d|%d",
                item->m_coloul.Red(), item->m_coloul.Green(), item->m_coloul.Blue(), item->m_coloul.Alpha(),
                item->m_tray_index, filament_id,
                item->m_ams_id, item->m_slot_id);
            evt.SetString(param);
            if (send_win) {
                evt.SetEventObject(send_win);
                wxPostEvent(send_win, evt);
            }
        });

        container_sizer->Add(0, 0, 0, wxRIGHT, FromDIP(6));
        container_sizer->Add(item, 0, wxTOP, FromDIP(1));
        m_sub_mapping_items.push_back(item);
    }

    m_sub_container->Layout();
    panel_sizer->Add(m_sub_container, 0, wxEXPAND | wxALL, FromDIP(4));
    m_sub_panel->SetSizer(panel_sizer);
    m_sub_panel->Layout();
    m_sub_panel->Fit();

    // Insert AMS panel into vert_sizer right after our row
    vert_sizer->Insert(row_index + 1, m_sub_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    m_sub_panel->Show();

    // Bubble layout up to the popup window so it resizes to fit new content
    wxWindow* w = parent;
    while (w) {
        w->Layout();
        w->Fit();
        // Stop when we reach the top-level popup (wxPopupWindow / AmsMapingPopup)
        if (w->IsTopLevel()) break;
        w = w->GetParent();
    }
}

void MappingItemHotend::destroy_sub_panel()
{
    if (!m_sub_panel) return;

    wxWindow* parent = GetParent();

    // Detach from whatever sizer it's in (could be vert_sizer)
    // Walk the parent's sizer tree to find and detach
    if (parent && parent->GetSizer()) {
        std::function<bool(wxSizer*)> detach_from = [&](wxSizer* sz) -> bool {
            for (size_t i = 0; i < sz->GetItemCount(); i++) {
                auto* sitem = sz->GetItem(i);
                if (sitem->GetWindow() == m_sub_panel) {
                    sz->Detach((int)i);
                    return true;
                }
                if (sitem->IsSizer() && detach_from(sitem->GetSizer())) {
                    return true;
                }
            }
            return false;
        };
        detach_from(parent->GetSizer());
    }

    m_sub_panel->Destroy();
    m_sub_panel     = nullptr;
    m_sub_container = nullptr;
    m_sub_mapping_items.clear();

    // Bubble layout up to popup
    wxWindow* w = parent;
    while (w) {
        w->Layout();
        w->Fit();
        if (w->IsTopLevel()) break;
        w = w->GetParent();
    }
}

// --- Click handler ---

void MappingItemHotend::on_left_down(wxMouseEvent& evt)
{
    if (m_unmatch) return;

    if (has_sub_ams()) {
        // Mode B: toggle expand
        toggle_expand();
    }
    // Mode A: direct selection is handled by the popup's Bind on this item
    // (same pattern as MappingItem - the popup binds wxEVT_LEFT_DOWN)
}

// --- Paint ---

void MappingItemHotend::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void MappingItemHotend::render(wxDC& dc)
{
    wxSize size = GetSize();

#ifdef __WXMSW__
    wxMemoryDC memdc;
    wxBitmap bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});
    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }
    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void MappingItemHotend::doRender(wxDC& dc)
{
    wxSize size = GetSize();

    bool is_disabled   = (m_slot_state == HotendSlotState::Unmatch    ||
                          m_slot_state == HotendSlotState::NozzleEmpty ||
                          m_slot_state == HotendSlotState::AllEmpty    ||
                          m_slot_state == HotendSlotState::Unavailable);

    // --- Background ---
    // Figma: fill=#FFFFFF, stroke=#C2C2C2 1px, borderRadius=4
    dc.SetPen(wxPen(wxColour(0xC2, 0xC2, 0xC2), 1));
    dc.SetBrush(wxBrush(wxColour(0xFF, 0xFF, 0xFF)));
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, FromDIP(4));

    // For disabled states (ban/bban1/喷嘴空): Figma uses entire card opacity=0.30
    // We paint a semi-transparent white overlay at the end to simulate this.
    // For now, render all content normally, then overlay if disabled.

    render_header(dc);
    render_filament(dc);
    render_separator(dc);
    render_nozzle_info(dc);

    // --- Selection border ---
    if (m_checked) {
        // Figma choose: stroke=#5C5C5C (actually shown as green in app screenshots)
        dc.SetPen(wxPen(wxColour(0x00, 0xAE, 0x42), FromDIP(2)));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, FromDIP(4));

        // Check badge (top-right dark triangle + white checkmark)
        dc.DrawBitmap(m_bitmap_checked.bmp(),
                      size.x - m_bitmap_checked.GetBmpWidth() - 1, 1);
    }

    // --- Disabled overlay: simulate opacity=0.30 ---
    if (is_disabled) {
        // Draw a white rect with ~70% alpha over everything to simulate 30% opacity
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(0xFF, 0xFF, 0xFF, 178))); // 255*0.70 ≈ 178
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, FromDIP(4));
        // Redraw border on top so it's visible
        dc.SetPen(wxPen(wxColour(0xC2, 0xC2, 0xC2), 1));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, FromDIP(4));
    }
}

void MappingItemHotend::render_header(wxDC& dc)
{
    wxSize size = GetSize();

    // --- Index number ---
    // Figma: position (8, 4), font=12px, color=#262E30
    dc.SetFont(::Label::Body_12);
    dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0x26, 0x2E, 0x30)));
    dc.DrawText(m_tray_index, wxPoint(FromDIP(8), FromDIP(4)));

    // --- Ext label (top-right, near index row) ---
    if (m_is_ext) {
        dc.SetFont(::Label::Body_10);
        wxString ext_label = "Ext";
        auto ext_sz = dc.GetTextExtent(ext_label);
        dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0xA3, 0xA3, 0xA3)));
        dc.DrawText(ext_label, wxPoint(size.x - ext_sz.x - FromDIP(8), FromDIP(4)));
    }
}

void MappingItemHotend::render_filament(wxDC& dc)
{
    wxSize size = GetSize();

    if (has_sub_ams()) {
        render_ams_preview(dc);
        return;
    }

    // --- Spool icon ---
    // Figma: position (8, 21), 20×20 SVG
    int spool_x = FromDIP(8);
    int spool_y = FromDIP(21);

    if (m_tray_data.type == EMPTY || m_slot_state == HotendSlotState::AllEmpty) {
        dc.DrawBitmap(m_bitmap_not_match.bmp(), spool_x, spool_y);
    } else if (m_colour.Alpha() == 0) {
        dc.DrawBitmap(m_bitmap_spool_transparent.bmp(), spool_x, spool_y);
    } else {
        // Color fill behind the spool icon outline
        auto bmp_sz = m_bitmap_spool.GetBmpSize();
        int fill_x = spool_x + FromDIP(4);
        int fill_y = spool_y + FromDIP(2);
        int fill_w = bmp_sz.x - FromDIP(7);
        int fill_h = bmp_sz.y - FromDIP(5);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(m_colour));
        dc.DrawRoundedRectangle(fill_x, fill_y, fill_w, fill_h, FromDIP(2));
        dc.DrawBitmap(m_bitmap_spool.bmp(), spool_x, spool_y);
    }

    // --- Filament type name ---
    // Figma: position (38, 22), font=14px, color=#262E30
    wxString display_name = m_name;
    if (m_tray_data.type == EMPTY || m_slot_state == HotendSlotState::AllEmpty ||
        m_slot_state == HotendSlotState::NozzleEmpty) {
        display_name = _L("Empty");
    } else if (m_tray_data.type == THIRD) {
        display_name = "?";
    }

    dc.SetFont(::Label::Head_14);
    dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0x26, 0x2E, 0x30)));
    dc.DrawText(display_name, wxPoint(FromDIP(38), FromDIP(22)));
}

void MappingItemHotend::render_ams_preview(wxDC& dc)
{
    // Figma: sub-AMS color blocks fill the filament area between index and separator
    // Layout: n equal-width blocks in one row, height=18dp, filling x=8..84
    // Matches the "外接 AMS" thumbnail style in the Figma send page screenshots
    int n           = (int)m_sub_tray_data.size();
    if (n == 0) return;

    int area_x  = FromDIP(8);
    int area_y  = FromDIP(18);   // just below index text
    int area_w  = FromDIP(76);   // 92 - 8 - 8
    int block_h = FromDIP(20);   // block height
    int gap     = FromDIP(2);

    // Each block width = (total_w - gaps) / n
    int total_gap = gap * (n - 1);
    int block_w   = (area_w - total_gap) / n;
    if (block_w < 1) block_w = 1;

    // Vertical center in area (area_y .. separator y=50)
    int available_h = FromDIP(50) - area_y;
    int block_y = area_y + (available_h - block_h) / 2;

    for (int i = 0; i < n; i++) {
        wxColour col_clr = (m_sub_tray_data[i].type == NORMAL)
                           ? m_sub_tray_data[i].colour
                           : wxColour(0xCE, 0xCE, 0xCE);
        int bx = area_x + i * (block_w + gap);

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(col_clr));
        dc.DrawRoundedRectangle(bx, block_y, block_w, block_h, FromDIP(3));
    }

    // Ext label top-right
    if (m_is_ext) {
        wxSize size = GetSize();
        dc.SetFont(::Label::Body_10);
        wxString ext_label = "Ext";
        auto ext_sz = dc.GetTextExtent(ext_label);
        dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0xA3, 0xA3, 0xA3)));
        dc.DrawText(ext_label, wxPoint(size.x - ext_sz.x - FromDIP(8), FromDIP(4)));
    }
}

void MappingItemHotend::render_separator(wxDC& dc)
{
    // Figma: y=50, from x=8 to x=87 (width=79), dashes=[2,2], color=#A3A3A3
    wxPen dashed_pen(wxColour(0xA3, 0xA3, 0xA3), 1, wxPENSTYLE_SHORT_DASH);
    dc.SetPen(dashed_pen);
    dc.DrawLine(FromDIP(8), FromDIP(50), FromDIP(87), FromDIP(50));
}

void MappingItemHotend::render_nozzle_info(wxDC& dc)
{
    wxSize size = GetSize();

    if (m_slot_state == HotendSlotState::Unavailable) {
        // Figma Variant5: show "喷嘴不可用" centered in nozzle area
        dc.SetFont(::Label::Body_12);
        dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0xA3, 0xA3, 0xA3)));
        wxString unavail_text = _L("Nozzle N/A");
        auto ts = dc.GetTextExtent(unavail_text);
        dc.DrawText(unavail_text, wxPoint((size.x - ts.x) / 2, FromDIP(60)));
        return;
    }

    if (m_nozzle_dia.IsEmpty() && m_flow_type.IsEmpty()) return;

    // Figma: position (8, 56), row layout, gap=5
    // [nozzle icon 14×24] + column("0.4 mm" / "Standard")
    int nx = FromDIP(8);
    int ny = FromDIP(56);

    // Nozzle icon
    int nozzle_bmp_w = m_bitmap_nozzle.GetBmpWidth();
    dc.DrawBitmap(m_bitmap_nozzle.bmp(), nx, ny);
    int text_x = nx + nozzle_bmp_w + FromDIP(5);  // Figma gap=5

    // "0.4 mm" — first line
    dc.SetFont(::Label::Body_12);
    wxColour nozzle_text_clr = StateColor::darkModeColorFor(wxColour(0x1F, 0x1F, 0x1F));
    dc.SetTextForeground(nozzle_text_clr);
    dc.DrawText(m_nozzle_dia, wxPoint(text_x, ny));

    // "Standard" — second line
    if (!m_flow_type.IsEmpty()) {
        auto dia_h = dc.GetTextExtent(m_nozzle_dia).y;
        dc.DrawText(m_flow_type, wxPoint(text_x, ny + dia_h));
    }
}

void MappingItemHotend::msw_rescale()
{
    // Figma: spool 20×20, nozzle 14px wide, checked 20×20
    m_bitmap_spool             = ScalableBitmap(this, "ams_wheel_narrow", 20);
    m_bitmap_spool_transparent = ScalableBitmap(this, "filament_transparent", 20);
    m_bitmap_not_match         = ScalableBitmap(this, "filament_not_mactch", 20);
    m_bitmap_checked           = ScalableBitmap(this, "mapping_item_checked", FromDIP(20));
    m_bitmap_nozzle            = ScalableBitmap(this, "dev_rack_nozzle_print_job", 14);

    SetSize(FromDIP(HOTEND_ITEM_SIZE));
    SetMinSize(FromDIP(HOTEND_ITEM_SIZE));
    SetMaxSize(FromDIP(HOTEND_ITEM_SIZE));

    Refresh();
}

// ============================================================================
// MaterialItemHotend implementation (moved from AmsMappingPopup.cpp)
// ============================================================================

MaterialItemHotend::MaterialItemHotend(wxWindow *parent, wxColour mcolour, wxString mname, std::string filament_id)
    : MaterialItem(parent, mcolour, mname, filament_id)
{
    m_arraw_bitmap_gray          = ScalableBitmap(this, "drop_down2", FromDIP(8));
    m_arraw_bitmap_white         = ScalableBitmap(this, "topbar_dropdown", 12);
    m_transparent_mitem          = ScalableBitmap(this, "transparent_material_up", 20);
    m_filament_wheel_transparent = ScalableBitmap(this, "filament_transparent", 25);
    m_ams_wheel_mitem            = ScalableBitmap(this, "ams_wheel_narrow", 25);
    m_ams_not_match              = ScalableBitmap(this, "filament_not_mactch", 25);
    m_rack_nozzle_bitmap         = ScalableBitmap(this, "dev_rack_nozzle_print_job", 22);

    m_material_coloul = mcolour;
    m_material_name   = mname;
    m_ams_coloul      = wxColour(0xEE, 0xEE, 0xEE);
    m_filament_id     = filament_id;

#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif

    messure_size();
    SetBackgroundColour(*wxWHITE);

    Bind(wxEVT_PAINT, &MaterialItem::paintEvent, this);
    wxGetApp().UpdateDarkUI(this);
}

MaterialItemHotend::~MaterialItemHotend() {}

void MaterialItemHotend::msw_rescale()
{
    MaterialItem::msw_rescale();
    m_rack_nozzle_bitmap.msw_rescale();
}

void MaterialItemHotend::set_ams_filament_type(const wxString& ams_filament_type)
{
    if (m_ams_filament_type != ams_filament_type) {
        m_ams_filament_type = ams_filament_type;
        Refresh();
    }
}

void MaterialItemHotend::set_nozzle_dia_and_type(const wxString& dia_type_info)
{
    if (dia_type_info != m_nozzle_dia_and_type) {
        m_nozzle_dia_and_type = dia_type_info;
        Refresh();
    }
}

void MaterialItemHotend::render(wxDC& dc)
{
    if (m_ams_name.empty() || m_ams_name == "-") {
        m_match = false;
    } else {
        m_match = true;
    }

#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});
    { wxGCDC dc2(memdc); doRender(dc2); }
    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void MaterialItemHotend::doRender(wxDC& dc)
{
    wxSize size   = GetSize();
    auto   mcolor = m_material_coloul;
    auto   acolor = m_ams_coloul;
    change_the_opacity(acolor);

    if (mcolor.Alpha() == 0) { dc.DrawBitmap(m_transparent_mitem.bmp(), FromDIP(1), FromDIP(1)); }

    if (!IsEnabled()) {
        mcolor = wxColour(0x90, 0x90, 0x90);
        acolor = wxColour(0x90, 0x90, 0x90);
    }

    // top color bar
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(mcolor));
    dc.DrawRoundedRectangle(0, 0, size.x, FromDIP(20), 5);
    dc.SetBrush(wxBrush(mcolor));
    dc.DrawRectangle(0, FromDIP(10), size.x, FromDIP(10));

    // material name
    auto material_name_colour = mcolor.GetLuminance() < 0.6 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30);
    if (mcolor.Alpha() == 0) { material_name_colour = wxColour(0x26, 0x2E, 0x30); }
    dc.SetTextForeground(material_name_colour);
    dc.SetFont(::Label::Body_12);
    if (dc.GetTextExtent(m_material_name).x > GetSize().x - 10) { dc.SetFont(::Label::Body_10); }
    auto material_txt_size = dc.GetTextExtent(m_material_name);
    dc.DrawText(m_material_name, wxPoint((GetSize().x - material_txt_size.x) / 2, (FromDIP(20) - material_txt_size.y) / 2));

    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.DrawLine(FromDIP(1), FromDIP(20), FromDIP(size.x), FromDIP(20));

    auto up = FromDIP(20) + FromDIP(2);

    const int rec_w      = FromDIP(17);
    const int rec_h      = FromDIP(16);
    const int left_off   = FromDIP(2);

    // spool area
    auto paint_recty = up + (m_ams_wheel_mitem.GetBmpSize().y - rec_h) / 2;
    auto left        = (size.x / 2 - rec_w) / 2 + FromDIP(3);
    auto right       = left + rec_w - FromDIP(3);
    dc.SetPen(*wxTRANSPARENT_PEN);

    if (m_ams_cols.size() > 1) {
        int gwidth = std::round(rec_w / (m_ams_cols.size() - 1));
        if (m_ams_ctype == 0) {
            for (size_t i = 0; i < m_ams_cols.size() - 1; i++) {
                auto rect = wxRect(left, paint_recty, right - left, rec_h);
                dc.GradientFillLinear(rect, m_ams_cols[i], m_ams_cols[i + 1], wxEAST);
                left += gwidth;
            }
        } else {
            int cols_size = (int)m_ams_cols.size();
            for (int i = 0; i < cols_size; i++) {
                dc.SetBrush(wxBrush(m_ams_cols[i]));
                float x = left + ((float)rec_w) * i / cols_size;
                if (i != cols_size - 1)
                    dc.DrawRoundedRectangle(x - left_off, paint_recty, ((float)rec_w) / cols_size + FromDIP(3), rec_h, 3);
                else
                    dc.DrawRoundedRectangle(x - left_off, paint_recty, ((float)rec_w) / cols_size - FromDIP(1), rec_h, 3);
            }
        }
    } else {
        if (m_match) {
            dc.SetBrush(wxBrush(acolor));
            dc.DrawRectangle((size.x / 2 - rec_w) / 2 + FromDIP(3) - left_off,
                             paint_recty, rec_w - FromDIP(1), rec_h);
        }
    }

    // border
    if (m_match)
        dc.SetPen(wxPen(wxGetApp().dark_mode() ? wxColour(107,107,107) : wxColour(0xAC,0xAC,0xAC), FromDIP(1)));
    else
        dc.SetPen(wxPen(wxColour(234,31,48), FromDIP(1)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, 5);

    if (m_selected) {
        dc.SetPen(wxPen(wxColour(0x00,0xAE,0x42), FromDIP(2)));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), 5);
    } else if (m_warning) {
        dc.SetPen(wxPen(wxColour(0xFF,0x6F,0x00), FromDIP(2)));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), 5);
    }

    // spool icon
    auto wheel_left = (GetSize().x / 2 - m_ams_wheel_mitem.GetBmpSize().x) / 2 + FromDIP(2) - left_off;
    auto wheel_top  = up;
    if (!m_match) {
        wheel_left = (size.x - m_ams_not_match.GetBmpWidth()) / 2 - left_off;
        dc.DrawBitmap(m_ams_not_match.bmp(), wheel_left, wheel_top);
    } else {
        if (acolor.Alpha() == 0)
            dc.DrawBitmap(m_filament_wheel_transparent.bmp(), wheel_left, wheel_top);
        else
            dc.DrawBitmap(m_ams_wheel_mitem.bmp(), wheel_left, wheel_top);
    }

    // filament type text + arrow
    dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0x26, 0x2E, 0x30)));
    int  text_pos_x = 0, text_pos_y = 0;
    auto ams_filament_type_size = wxSize(0, 0);
    if (m_ams_filament_type.size() >= 4) {
        m_ams_filament_type.insert(m_mapping_text.size() / 2, "\n");
        // Choose a smaller font when the text is long
        dc.SetFont(::Label::Body_10);
        ams_filament_type_size = dc.GetTextExtent(m_ams_filament_type);
        text_pos_y = up + (m_ams_wheel_mitem.GetBmpSize().y - ams_filament_type_size.y) / 2;
        text_pos_x = ams_filament_type_size.x / 4;
    } else {
        dc.SetFont(::Label::Head_12);
        ams_filament_type_size = dc.GetTextExtent(m_ams_filament_type);
        text_pos_y = up + (m_ams_wheel_mitem.GetBmpSize().y - ams_filament_type_size.y) / 2;
        text_pos_x = 0;
    }
    int arrow_left = size.x - m_arraw_bitmap_white.GetBmpSize().x - FromDIP(4) - left_off;
    if (m_match) {
        int text_x = GetSize().x / 2 + (GetSize().x / 2 - ams_filament_type_size.x) / 2 - FromDIP(8) - left_off + text_pos_x;
        dc.DrawText(m_ams_filament_type, wxPoint(text_x, text_pos_y));
        int arrow_y = text_pos_y + (ams_filament_type_size.y - m_arraw_bitmap_white.GetBmpHeight()) / 2;
        if ((acolor.Red() > 160 && acolor.Green() > 160 && acolor.Blue() > 160) &&
            (acolor.Red() < 180 && acolor.Green() < 180 && acolor.Blue() < 180))
            dc.DrawBitmap(m_arraw_bitmap_white.bmp(), arrow_left, arrow_y);
        else
            dc.DrawBitmap(m_arraw_bitmap_gray.bmp(), arrow_left, arrow_y);
    }

    up += m_ams_wheel_mitem.GetBmpSize().y;

    // nozzle info row
    if (!m_mapped_nozzle_str.IsEmpty()) {
        wxPen dashed_pen(WXCOLOUR_GREY400, 1, wxPENSTYLE_SHORT_DASH);
        dc.SetPen(dashed_pen);
        up += FromDIP(4);
        dc.DrawLine(FromDIP(1), up, FromDIP(size.x), up);

        int buttom_h = size.y - up;
        dc.SetFont(::Label::Head_12);
        auto nozzle_pos_sz  = dc.GetTextExtent(m_mapped_nozzle_str);
        int  nozzle_bmp_w   = m_rack_nozzle_bitmap.GetBmpWidth();
        int  nozzle_bmp_h   = m_rack_nozzle_bitmap.GetBmpHeight();
        dc.SetFont(::Label::Body_10);
        auto dia_and_type_sz = dc.GetTextExtent(m_nozzle_dia_and_type);

        int gap     = FromDIP(2);
        int total_w = nozzle_pos_sz.x + gap + nozzle_bmp_w + dia_and_type_sz.x;
        int cur_x   = (size.x - total_w) / 2;

        dc.SetFont(::Label::Head_12);
        dc.DrawText(m_mapped_nozzle_str, cur_x, up + (buttom_h - nozzle_pos_sz.y) / 2);
        cur_x += nozzle_pos_sz.x + gap;

        dc.DrawBitmap(m_rack_nozzle_bitmap.bmp(), cur_x, up + (buttom_h - nozzle_bmp_h) / 2);
        cur_x += nozzle_bmp_w + gap;

        dc.SetFont(::Label::Body_10);
        dc.DrawText(m_nozzle_dia_and_type, cur_x, up + (buttom_h - dia_and_type_sz.y) / 2);
    }
}

void MaterialItemHotend::messure_size()
{
    SetSize(wxSize(FromDIP(65), FromDIP(84)));
    SetMinSize(wxSize(FromDIP(65), FromDIP(84)));
    SetMaxSize(wxSize(FromDIP(65), FromDIP(84)));
}

}} // namespace Slic3r::GUI
