#include "DragDropPanel.hpp"
#include "GUI_App.hpp"
#include "FilamentBitmapUtils.hpp"
#include <slic3r/GUI/wxExtensions.hpp>
#include <wx/graphics.h>

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(wxEVT_DRAG_DROP_COMPLETED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_FILAMENT_DRAG_HOVER, wxCommandEvent);

static void notify_filament_drag_hover(wxWindow *source, bool accepted)
{
    wxCommandEvent event(wxEVT_FILAMENT_DRAG_HOVER);
    event.SetEventObject(source);
    event.SetInt(accepted);

    for (wxWindow *parent = source->GetParent(); parent; parent = parent->GetParent()) {
        if (parent->GetName() == wxT("FilamentMapMultiNozzlePanel")) {
            parent->GetEventHandler()->ProcessEvent(event);
            break;
        }
    }
}

static wxColour drag_panel_background(bool enabled)
{
    if (wxGetApp().dark_mode())
        return enabled ? wxColour("#4C4B50") : wxColour("#424147");
    return enabled ? *wxWHITE : wxColour("#F2F2F2");
}

struct CustomData
{
    int filament_id;
    unsigned char r, g, b, a;
    char type[64];
};


wxColor Hex2Color(const std::string& str)
{
    if (str.empty() || (str.length() != 9 && str.length() != 7) || str[0] != '#')
        throw std::invalid_argument("Invalid hex color format");

    auto hexToByte = [](const std::string& hex)->unsigned char
        {
            unsigned int byte;
            std::istringstream(hex) >> std::hex >> byte;
            return static_cast<unsigned char>(byte);
        };
    auto r = hexToByte(str.substr(1, 2));
    auto g = hexToByte(str.substr(3, 2));
    auto b = hexToByte(str.substr(5, 2));
    unsigned char a = 255;
    if (str.size() == 9)
        a = hexToByte(str.substr(7, 2));
    return wxColor(r, g, b, a);
}

// Custom data object used to store information that needs to be backed up during drag and drop
class ColorDataObject : public wxCustomDataObject
{
public:
    ColorDataObject(const wxColour &color = *wxBLACK, int filament_id = 0, const std::string &type = "PLA")
        : wxCustomDataObject(wxDataFormat("application/customize_format"))
    {
        set_custom_data_filament_id(filament_id);
        set_custom_data_color(color);
        set_custom_data_type(type);
    }

    wxColour GetColor() const { return wxColor(m_data.r, m_data.g, m_data.b, m_data.a); }
    void     SetColor(const wxColour &color) { set_custom_data_color(color); }

    int      GetFilament() const { return m_data.filament_id; }
    void     SetFilament(int label) { set_custom_data_filament_id(label); }

    std::string     GetType() const { return m_data.type; }
    void     SetType(const std::string &type) { set_custom_data_type(type); }

    void set_custom_data_type(const std::string& type) {
        std::strncpy(m_data.type, type.c_str(), sizeof(m_data.type) - 1);
        m_data.type[sizeof(m_data.type) - 1] = '\0';
    }

    void set_custom_data_filament_id(int filament_id) {
        m_data.filament_id = filament_id;
    }

    void set_custom_data_color(const wxColor& color) {
        m_data.r           = color.Red();
        m_data.g           = color.Green();
        m_data.b           = color.Blue();
        m_data.a           = color.Alpha();
    }

    virtual size_t GetDataSize() const override { return sizeof(m_data); }
    virtual bool   GetDataHere(void *buf) const override
    {
        char *ptr = static_cast<char *>(buf);
        std::memcpy(buf, &m_data, sizeof(m_data));
        return true;
    }
    virtual bool SetData(size_t len, const void *buf) override
    {
        if (len == GetDataSize()) {
            std::memcpy(&m_data, buf, sizeof(m_data));
            return true;
        }
        return false;
    }
private:
    CustomData m_data;
};

///////////////   ColorPanel  start ////////////////////////
// The UI panel of drag item
ColorPanel::ColorPanel(DragDropPanel *parent, int filament_id, const std::string &type, const wxSize &size)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, size, wxBORDER_NONE), m_parent(parent), m_type(type), m_filament_id(filament_id)
{
    Bind(wxEVT_LEFT_DOWN, &ColorPanel::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &ColorPanel::OnLeftUp, this);
    Bind(wxEVT_PAINT, &ColorPanel::OnPaint, this);
}

void ColorPanel::OnLeftDown(wxMouseEvent &event)
{
    if (!m_parent->IsInteractionEnabled())
        return;
    m_parent->set_is_draging(true);
    m_parent->DoDragDrop(this, GetType(), GetFilamentId());
}

void ColorPanel::OnLeftUp(wxMouseEvent &event) { m_parent->set_is_draging(false); }

void ColorPanel::OnPaint(wxPaintEvent &event)
{
    wxPaintDC dc(this);
    wxSize    size = GetSize();
    // If it matches the parent's width, it will not be displayed completely
    int      svg_size          = size.GetWidth();
    int      type_label_height = FromDIP(10);
    wxString type_label(m_type);
    int      type_label_margin = FromDIP(3);

    // Derive the filament's color set from its id (the project config is the single source of truth).
    std::vector<wxColour> colors;
    bool                  is_gradient = false;
    get_filament_colors_by_id(m_filament_id - 1, colors, is_gradient); // m_filament_id is 1-based
    if (colors.empty()) colors.push_back(*wxBLACK);

    wxBitmap swatch = create_filament_bitmap(colors, wxSize(svg_size, svg_size), is_gradient);
    if (swatch.IsOk()) dc.DrawBitmap(swatch, wxPoint(0, 0));

    // Use app fonts: the window default font follows the Windows text-size setting, but the chip size does not.
    wxString label = wxString::Format(wxT("%d"), m_filament_id);
    dc.SetFont(Label::Body_12);
    dc.SetTextForeground(colors.front().GetLuminance() < 0.51 ? *wxWHITE : *wxBLACK); // set text color
    dc.DrawLabel(label, wxRect(0, 0, svg_size, svg_size), wxALIGN_CENTER);

    dc.SetFont(Label::Body_10);
    if (m_parent)
        dc.SetTextForeground(this->m_parent->GetBackgroundColour().GetLuminance() < 0.51 ? *wxWHITE : *wxBLACK);
    else
        dc.SetTextForeground(*wxBLACK);
    if (type_label.length() > 4) {
        // text is too long
        wxString first = type_label.Mid(0, 4);
        wxString rest  = type_label.Mid(4);
        dc.DrawLabel(first, wxRect(0, svg_size + type_label_margin, svg_size, type_label_height), wxALIGN_CENTER);
        dc.DrawLabel(rest, wxRect(0, svg_size + type_label_height + type_label_margin, svg_size, type_label_height), wxALIGN_CENTER);
    } else {
        dc.DrawLabel(type_label, wxRect(0, svg_size + type_label_margin, svg_size, type_label_height), wxALIGN_CENTER);
    }

    if (!m_parent->IsInteractionEnabled()) {
        if (wxGraphicsContext *gc = wxGraphicsContext::Create(dc)) {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(wxGetApp().dark_mode()
                ? wxColour(66, 65, 71, 190)
                : wxColour(245, 245, 245, 190)));
            gc->DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());
            delete gc;
        }
    }
}

// Save the source object information to m_data when dragging
class ColorDropSource : public wxDropSource
{
public:
    ColorDropSource(wxPanel *parent, wxPanel *color_block, const std::string& type, int filament_id) : wxDropSource(parent)
    {
        m_data.SetFilament(filament_id);
        m_data.SetType(type);
        SetData(m_data);  // Set drag source data
    }

private:
    ColorDataObject m_data;
};

static int s_dragged_filament_id = 0;

class ScopedDraggedFilament
{
public:
    explicit ScopedDraggedFilament(int filament_id) { s_dragged_filament_id = filament_id; }
    ~ScopedDraggedFilament() { s_dragged_filament_id = 0; }
};

///////////////   ColorDropTarget  start ////////////////////////
// Get the data from the drag source when drop it
class ColorDropTarget : public wxDropTarget
{
public:
    ColorDropTarget(DragDropPanel *panel) : wxDropTarget(/*new wxDataObjectComposite*/), m_panel(panel)
    {
        m_data = new ColorDataObject();
        SetDataObject(m_data);
    }

    virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) override;
    virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) override;
    virtual bool         OnDrop(wxCoord x, wxCoord y) override {
        return true;
    }

private:
    DragDropPanel * m_panel;
    ColorDataObject* m_data;
};

wxDragResult ColorDropTarget::OnData(wxCoord x, wxCoord y, wxDragResult def)
{
    if (!GetData())
        return wxDragNone;

    // Multi-diameter restriction: refuse a filament whose preset does not support this panel's working
    // diameter. Returning wxDragNone leaves the block on its source panel (the drag snaps back).
    if (!m_panel->accepts_filament(m_data->GetFilament())) {
        notify_filament_drag_hover(m_panel, false);
        return wxDragNone;
    }

    notify_filament_drag_hover(m_panel, true);
    m_panel->AddColorBlock(m_data->GetType(), m_data->GetFilament());

    return wxDragCopy;
}

wxDragResult ColorDropTarget::OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
{
    const bool accepted = s_dragged_filament_id <= 0 || m_panel->accepts_filament(s_dragged_filament_id);
    notify_filament_drag_hover(m_panel, accepted);
    if (!accepted)
        return wxDragNone;
    return wxDropTarget::OnDragOver(x, y, def);
}
///////////////   ColorDropTarget  end ////////////////////////


// 顶层（非流量分组）耗材网格的尺寸参数。
static constexpr int GRID_ITEM_GAP = 8;
static constexpr int GRID_BORDER   = 8;
// 双挤出机对话框的参考布局，其列距决定图标间距。
static constexpr int GRID_REF_PANEL_W = 260;
static constexpr int GRID_REF_COLS    = 6;

DragDropPanel::DragDropPanel(wxWindow *parent, const wxString &label, bool is_auto, bool has_title, bool is_sub,
                             bool use_fixed_grid_item_size)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_is_auto(is_auto)
    , m_use_fixed_grid_item_size(use_fixed_grid_item_size)
{
    SetBackgroundColour(0xF8F8F8);

    m_sizer    = new wxBoxSizer(wxVERTICAL);

    if (has_title) {
        auto title_panel = new wxPanel(this);
        title_panel->SetBackgroundColour(is_sub ? 0xF8F8F8 : 0xEEEEEE);
        auto title_sizer = new wxBoxSizer(wxHORIZONTAL);
        title_panel->SetSizer(title_sizer);

        m_title_label = new Label(this, label);
        m_title_label->SetFont(is_sub ? Label::Body_12 : Label::Head_13);
        m_title_label->SetForegroundColour(is_sub ? 0x6B6B6B : 0x000000);
        m_title_label->SetBackgroundColour(is_sub ? 0xF8F8F8 : 0xEEEEEE);

        title_sizer->Add(m_title_label, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

        m_sizer->Add(title_panel, 0, wxEXPAND);
        m_sizer->AddSpacer(10);
    }

    if (is_sub) {
        m_grid_item_sizer = new wxGridSizer(0, 3, FromDIP(6), FromDIP(6));
        m_grid_sizer_item = m_sizer->Add(m_grid_item_sizer, 0, wxEXPAND);
    } else {
        m_grid_item_sizer = new wxGridSizer(0, 6, FromDIP(GRID_ITEM_GAP), FromDIP(GRID_ITEM_GAP));
        m_grid_sizer_item = m_sizer->Add(
            m_grid_item_sizer, 0, wxEXPAND | wxALL, FromDIP(GRID_BORDER));
    }

    // set droptarget
    auto drop_target = new ColorDropTarget(this);
    SetDropTarget(drop_target);

    BindInteractionHoverEvents(this);
    for (wxWindow *child : GetChildren())
        BindInteractionHoverEvents(child);

    SetSizer(m_sizer);
    Layout();
    Fit();
}

void DragDropPanel::BindInteractionHoverEvents(wxWindow *window)
{
    window->Bind(wxEVT_ENTER_WINDOW, &DragDropPanel::OnInteractionHoverEnter, this);
    window->Bind(wxEVT_LEAVE_WINDOW, &DragDropPanel::OnInteractionHoverLeave, this);
}

void DragDropPanel::OnInteractionHoverEnter(wxMouseEvent &event)
{
    if (!m_interaction_enabled)
        notify_filament_drag_hover(this, false);
    event.Skip();
}

void DragDropPanel::OnInteractionHoverLeave(wxMouseEvent &event)
{
    if (!m_interaction_enabled) {
        CallAfter([this]() {
            for (wxWindow *window = wxFindWindowAtPoint(wxGetMousePosition()); window; window = window->GetParent()) {
                if (auto *panel = dynamic_cast<DragDropPanel *>(window)) {
                    notify_filament_drag_hover(panel, panel->IsInteractionEnabled());
                    return;
                }
            }
            notify_filament_drag_hover(this, true);
        });
    }
    event.Skip();
}

void DragDropPanel::AddColorBlock(const std::string &type, int filament_id, bool update_ui)
{
    const wxSize block_size(FromDIP(30), FromDIP(60));
    const wxSize initial_size = m_use_fixed_grid_item_size ? block_size : wxSize(32, 40);
    ColorPanel  *panel = new ColorPanel(this, filament_id, type, initial_size);
    BindInteractionHoverEvents(panel);
    panel->SetMinSize(block_size);
    if (m_use_fixed_grid_item_size)
        m_grid_item_sizer->Add(
            panel, 0, wxFIXED_MINSIZE | wxALIGN_CENTER_HORIZONTAL)->SetMinSize(block_size);
    else
        m_grid_item_sizer->Add(panel, 0);
    m_filament_blocks.push_back(panel);
    if (update_ui) {
        Freeze();

        m_grid_item_sizer->Layout();
        Layout();
        Fit();
        GetParent()->GetParent()->Layout();
        GetParent()->GetParent()->Fit();
        m_filament_blocks.front()->Refresh(); // FIX BUG: STUDIO-8467

        Thaw();
        NotifyDragDropCompleted();
    }
}

void DragDropPanel::RemoveColorBlock(ColorPanel *panel, bool update_ui)
{
    m_grid_item_sizer->Detach(panel);
    panel->Destroy();
    m_filament_blocks.erase(std::remove(m_filament_blocks.begin(), m_filament_blocks.end(), panel), m_filament_blocks.end());
    if (update_ui) {
        Freeze();

        Layout();
        Fit();
        GetParent()->GetParent()->Layout();
        GetParent()->GetParent()->Fit();

        Thaw();
        if (m_use_fixed_grid_item_size) {
            wxWindow *refresh_parent = GetParent();
            refresh_parent->CallAfter([refresh_parent]() {
                if (!refresh_parent->IsBeingDeleted())
                    refresh_parent->Refresh(true);
            });
        }
        NotifyDragDropCompleted();
    }
}

void DragDropPanel::DoDragDrop(ColorPanel *panel, const std::string &type, int filament_id)
{
    if (m_is_auto || !m_interaction_enabled)
        return;

    ColorDropSource source(this, panel, type, filament_id);
    ScopedDraggedFilament dragged_filament(filament_id);
    const wxDragResult result = source.DoDragDrop(wxDrag_CopyOnly);
    notify_filament_drag_hover(this, true);
    if (result == wxDragResult::wxDragCopy) {
        this->RemoveColorBlock(panel);
    }
}

void DragDropPanel::UpdateLabel(const wxString &label)
{
    if (m_title_label) {
        m_title_label->SetLabel(label);
        m_title_label->Refresh();
        Layout();
    }
}

void DragDropPanel::SetGridColumns(int cols)
{
    if (cols <= 0 || m_grid_item_sizer->GetCols() == cols)
        return;
    m_grid_item_sizer->SetCols(cols);
    Layout();
    Fit();
}

void DragDropPanel::SetGridColumnsForWidth(int panel_width)
{
    const int gap    = FromDIP(GRID_ITEM_GAP);
    const int border = FromDIP(GRID_BORDER);
    // wxGridSizer 把区域均分为等宽单元格加间隙，参考列宽按同样方式反推；列距即相邻图标的间隔。
    const int ref_cell_w = (FromDIP(GRID_REF_PANEL_W) - 2 * border - (GRID_REF_COLS - 1) * gap) / GRID_REF_COLS;
    const int pitch      = ref_cell_w + gap;

    const int available = panel_width - 2 * border;
    if (available + gap < pitch)
        return;

    const int cols = (available + gap) / pitch;
    // 必须钉死宽度，否则网格会把多余宽度摊到各单元格上，间距被拉散。
    SetMaxSize({cols * pitch - gap + 2 * border, -1});
    SetGridColumns(cols);
}

void DragDropPanel::SetGridHorizontalPadding(int padding, int gap)
{
    if (!m_grid_sizer_item)
        return;

    m_grid_item_sizer->SetHGap(gap);
    m_grid_sizer_item->SetFlag(wxEXPAND | wxLEFT | wxRIGHT);
    m_grid_sizer_item->SetBorder(padding);
    Layout();
    Fit();
}

void DragDropPanel::SetInteractionEnabled(bool enabled)
{
    m_interaction_enabled = enabled;
    const wxColour background = drag_panel_background(enabled);
    const wxColour foreground = enabled ? wxColour("#6B6B6B") : wxColour("#9A9A9A");
    SetBackgroundColour(background);
    for (wxWindow *child : GetChildren()) {
        child->SetBackgroundColour(background);
        child->SetForegroundColour(foreground);
        child->Refresh(true);
        for (wxWindow *grandchild : child->GetChildren()) {
            grandchild->SetBackgroundColour(background);
            grandchild->SetForegroundColour(foreground);
            grandchild->Refresh(true);
        }
    }
    Refresh(true);
}

std::vector<int> DragDropPanel::GetAllFilaments() const
{
    std::vector<int>          filaments;
    for (size_t i = 0; i < m_grid_item_sizer->GetItemCount(); ++i) {
        wxSizerItem *item = m_grid_item_sizer->GetItem(i);
        if (item != nullptr) {
            wxWindow *  window     = item->GetWindow();
            ColorPanel *colorPanel = dynamic_cast<ColorPanel *>(window);
            if (colorPanel != nullptr) {
                filaments.push_back(colorPanel->GetFilamentId());
            }
        }
    }

    return filaments;
}

void DragDropPanel::NotifyDragDropCompleted()
{
    wxCommandEvent event(wxEVT_DRAG_DROP_COMPLETED);
    event.SetEventObject(this);

    wxWindow *parent = GetParent();
    while (parent) {
        auto name = parent->GetName();
        if (name == wxT("FilamentMapManualPanel") || name == wxT("FilamentMapMultiNozzlePanel")) {
            parent->GetEventHandler()->ProcessEvent(event);
            break;
        }
        parent = parent->GetParent();
    }
}

class SeparatedColorDropTarget : public wxDropTarget
{
public:
    SeparatedColorDropTarget(SeparatedDragDropPanel *panel) : wxDropTarget(), m_panel(panel)
    {
        m_data = new ColorDataObject();
        SetDataObject(m_data);
    }

    virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) override;
    virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) override;
    virtual bool         OnDrop(wxCoord x, wxCoord y) override { return true; }

private:
    SeparatedDragDropPanel *m_panel;
    ColorDataObject        *m_data;
};

wxDragResult SeparatedColorDropTarget::OnData(wxCoord x, wxCoord y, wxDragResult def)
{
    if (!GetData()) return wxDragNone;

    // Multi-diameter restriction: refuse a filament whose preset does not support this extruder's
    // working diameter (see ColorDropTarget::OnData).
    if (!m_panel->accepts_filament(m_data->GetFilament())) {
        notify_filament_drag_hover(m_panel, false);
        return wxDragNone;
    }

    notify_filament_drag_hover(m_panel, true);
    m_panel->AddColorBlock(m_data->GetType(), m_data->GetFilament(), nvtStandard);
    return wxDragCopy;
}

wxDragResult SeparatedColorDropTarget::OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
{
    const bool accepted = s_dragged_filament_id <= 0 || m_panel->accepts_filament(s_dragged_filament_id);
    notify_filament_drag_hover(m_panel, accepted);
    if (!accepted)
        return wxDragNone;
    return wxDropTarget::OnDragOver(x, y, def);
}

SeparatedDragDropPanel::SeparatedDragDropPanel(wxWindow *parent, const wxString &label, bool use_separation,
                                               bool show_unified_title, bool expand_drop_target,
                                               bool use_fixed_grid_item_size)
    : wxPanel(parent)
    , m_use_separation(use_separation)
    , m_use_fixed_grid_item_size(use_fixed_grid_item_size)
{
    SetBackgroundColour(0xF8F8F8);

    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    auto title_panel = new wxPanel(this);
    title_panel->SetBackgroundColour(0xEEEEEE);
    auto title_sizer = new wxBoxSizer(wxHORIZONTAL);
    title_panel->SetSizer(title_sizer);

    m_label = new Label(title_panel, label);
    m_label->SetFont(Label::Head_13);
    m_label->SetBackgroundColour(0xEEEEEE);

    title_sizer->Add(m_label, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    if (expand_drop_target) {
        title_panel->SetDropTarget(new SeparatedColorDropTarget(this));
        m_label->SetDropTarget(new SeparatedColorDropTarget(this));
    }

    m_main_sizer->Add(title_panel, 0, wxEXPAND);
    m_main_sizer->AddSpacer(10);

    m_content_panel = new wxPanel(this);
    m_content_panel->SetBackgroundColour(0xF8F8F8);
    if (expand_drop_target)
        m_content_panel->SetDropTarget(new SeparatedColorDropTarget(this));
    m_content_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_diameter_volume_container = new wxPanel(m_content_panel);
    m_diameter_volume_container->SetBackgroundColour(0xF8F8F8);
    if (expand_drop_target)
        m_diameter_volume_container->SetDropTarget(new SeparatedColorDropTarget(this));
    m_diameter_volume_sizer = new wxFlexGridSizer(0, 3, FromDIP(8), FromDIP(8));
    for (int col = 0; col < 3; ++col)
        m_diameter_volume_sizer->AddGrowableCol(col, 1);
    m_diameter_volume_container->SetSizer(m_diameter_volume_sizer);
    m_content_panel->SetSizer(m_content_sizer);

    m_high_flow_panel = new DragDropPanel(m_content_panel, _L("High Flow"), false, true, true, m_use_fixed_grid_item_size);
    m_standard_panel  = new DragDropPanel(m_content_panel, _L("Standard"), false, true, true, m_use_fixed_grid_item_size);
    m_tpu_panel       = new DragDropPanel(m_content_panel, _L("TPU285"), false, true, true, m_use_fixed_grid_item_size);
    m_unified_panel   = new DragDropPanel(m_content_panel, wxEmptyString, false, show_unified_title,
                                          show_unified_title, m_use_fixed_grid_item_size);

    m_high_flow_panel->SetBackgroundColour(0xF8F8F8);
    m_standard_panel->SetBackgroundColour(0xF8F8F8);
    m_tpu_panel->SetBackgroundColour(0xF8F8F8);
    m_unified_panel->SetBackgroundColour(0xF8F8F8);
    m_unified_panel->SetMinSize({FromDIP(260), -1});

    m_main_sizer->Add(m_content_panel, 1, wxEXPAND);

    UpdateLayout();

    auto drop_target = new SeparatedColorDropTarget(this);
    SetDropTarget(drop_target);

    SetSizer(m_main_sizer);
    Layout();
    wxGetApp().UpdateDarkUIWin(this);
}

void SeparatedDragDropPanel::UpdateLayout()
{
    m_content_sizer->Clear(false);
    for (wxWindow *sep : m_separators)
        sep->Destroy();
    m_separators.clear();

    if (!m_diameter_volume_panels.empty()) {
        m_high_flow_panel->Hide();
        m_standard_panel->Hide();
        m_tpu_panel->Hide();
        m_unified_panel->Hide();
        m_diameter_volume_container->Show();
        for (auto &group : m_diameter_volume_panels)
            group.panel->Show();
        m_content_sizer->Add(m_diameter_volume_container, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));
    } else if (m_use_separation) {
        m_diameter_volume_container->Hide();
        m_unified_panel->Hide();

        auto preset_bundle = wxGetApp().preset_bundle;
        const NozzleInventory &nozzles = preset_bundle->nozzle_inventory;

        auto has_nozzle_type = [&](NozzleVolumeType type) {
            for (int eid = 0; eid < nozzles.extruder_count(); ++eid)
                if (nozzles.count(eid, type) > 0)
                    return true;
            return false;
        };

        // Also check extruder_variant_list to determine supported nozzle types
        bool variant_has_std = false, variant_has_hf = false, variant_has_tpu = false;
        auto variant_opt = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionStrings>("extruder_variant_list");
        if (variant_opt) {
            for (const auto &v : variant_opt->values) {
                if (v.find("Standard") != std::string::npos) variant_has_std = true;
                if (v.find("High Flow") != std::string::npos) variant_has_hf = true;
                if (v.find("TPU") != std::string::npos) variant_has_tpu = true;
            }
        }

        // 若外部已传入各流量类型的可用喷嘴数（如多喷嘴固定组），仅展示数量 > 0 的流量列
        bool has_std = m_has_volume_type_counts ? (m_std_count > 0) : (!m_standard_panel->get_filament_blocks().empty() || has_nozzle_type(NozzleVolumeType::nvtStandard) || variant_has_std);
        bool has_hf  = m_has_volume_type_counts ? (m_hf_count > 0)  : (!m_high_flow_panel->get_filament_blocks().empty() || has_nozzle_type(NozzleVolumeType::nvtHighFlow) || variant_has_hf);
        bool has_tpu = m_has_volume_type_counts ? (m_tpu_count > 0) : (!m_tpu_panel->get_filament_blocks().empty() || ((has_nozzle_type(NozzleVolumeType::nvtTPUHighFlow) || has_nozzle_type(NozzleVolumeType::nvtTPU285)) && variant_has_tpu));

        m_standard_panel->Show(has_std);
        m_high_flow_panel->Show(has_hf);
        m_tpu_panel->Show(has_tpu);

        auto add_separator = [this]() {
            auto separator = new wxPanel(m_content_panel, wxID_ANY);
            separator->SetBackgroundColour(0xCCCCCC);
            separator->SetMinSize(wxSize(FromDIP(1), -1));
            m_content_sizer->Add(separator, 0, wxEXPAND | wxALL, FromDIP(4));
            m_separators.push_back(separator);
        };

        std::vector<DragDropPanel *> visible_panels;
        if (has_std) visible_panels.push_back(m_standard_panel);
        if (has_hf)  visible_panels.push_back(m_high_flow_panel);
        if (has_tpu) visible_panels.push_back(m_tpu_panel);

        for (size_t i = 0; i < visible_panels.size(); ++i) {
            if (i > 0)
                add_separator();
            int flags = wxEXPAND;
            if (i == 0)
                flags |= wxLEFT;
            if (i + 1 == visible_panels.size())
                flags |= wxRIGHT;
            m_content_sizer->Add(visible_panels[i], 1, flags, FromDIP(8));
        }
    } else {
        m_diameter_volume_container->Hide();
        m_high_flow_panel->Hide();
        m_standard_panel->Hide();
        m_tpu_panel->Hide();
        m_unified_panel->Show();

        m_content_sizer->Add(m_unified_panel, 1, wxEXPAND);
    }
    m_content_sizer->Layout();
    Layout();
    Fit();
    if (GetParent()) {
        GetParent()->Layout();
        if (GetParent()->GetParent()) {
            GetParent()->GetParent()->Layout();
        }
    }
}

void SeparatedDragDropPanel::UpdateDiameterVolumeLayout()
{
    if (m_diameter_volume_panels.empty())
        return;

    const int card_min_width = FromDIP(110);
    std::vector<int> content_heights;
    content_heights.reserve(m_diameter_volume_panels.size());
    for (auto &group : m_diameter_volume_panels) {
        group.panel->SetMaxSize(wxDefaultSize);
        group.panel->SetMinSize({card_min_width, -1});
        const int content_height = group.panel->GetSizer()
            ? group.panel->GetSizer()->CalcMin().GetHeight()
            : FromDIP(100);
        const int panel_height = std::max(FromDIP(100), content_height);
        content_heights.push_back(panel_height);
        group.panel->SetMinSize({card_min_width, panel_height});
        group.panel->SetMaxSize({-1, panel_height});
        group.panel->Layout();
    }

    m_diameter_volume_container->Layout();
    std::vector<int> row_positions;
    std::vector<int> row_heights;
    for (size_t i = 0; i < m_diameter_volume_panels.size(); ++i) {
        const int y = m_diameter_volume_panels[i].panel->GetPosition().y;
        auto row = std::find(row_positions.begin(), row_positions.end(), y);
        if (row == row_positions.end()) {
            row_positions.push_back(y);
            row_heights.push_back(content_heights[i]);
        } else {
            const size_t row_idx = std::distance(row_positions.begin(), row);
            row_heights[row_idx] = std::max(row_heights[row_idx], content_heights[i]);
        }
    }
    for (auto &group : m_diameter_volume_panels) {
        const int y = group.panel->GetPosition().y;
        const auto row = std::find(row_positions.begin(), row_positions.end(), y);
        const size_t row_idx = std::distance(row_positions.begin(), row);
        const int row_height = row_heights[row_idx];
        group.panel->SetMinSize({card_min_width, row_height});
        group.panel->SetMaxSize({-1, row_height});
        group.panel->Layout();
    }

    m_diameter_volume_sizer->Layout();
    m_diameter_volume_container->Layout();
    const int bottom_padding = FromDIP(8);
    m_diameter_volume_container->SetMinSize(
        {-1, m_diameter_volume_sizer->CalcMin().GetHeight() + bottom_padding});
    m_content_panel->Layout();
    Layout();
    Fit();
}

void SeparatedDragDropPanel::UpdateLabel(const wxString &label)
{
    if (m_label) {
        m_label->SetLabel(label);
        m_label->Refresh();
        Layout();
    }
}

void SeparatedDragDropPanel::UpdateUnifiedLabel(const wxString &label)
{
    if (m_unified_panel)
        m_unified_panel->UpdateLabel(label);
}

// 根据流量类型解析目标子面板；若该流量类型喷嘴数量为 0（列已隐藏），则自动回退到首个数量 > 0 的可见面板
DragDropPanel *SeparatedDragDropPanel::ResolveSeparatedTargetPanel(NozzleVolumeType volume_type) const
{
    DragDropPanel *target = m_standard_panel;
    if (volume_type == NozzleVolumeType::nvtHighFlow)
        target = m_high_flow_panel;
    else if (volume_type == NozzleVolumeType::nvtTPUHighFlow || volume_type == NozzleVolumeType::nvtTPU285)
        target = m_tpu_panel;

    if (!m_has_volume_type_counts)
        return target;
    if ((target == m_standard_panel && m_std_count > 0) ||
        (target == m_high_flow_panel && m_hf_count > 0) ||
        (target == m_tpu_panel && m_tpu_count > 0))
        return target;
    if (m_std_count > 0) return m_standard_panel;
    if (m_hf_count > 0)  return m_high_flow_panel;
    if (m_tpu_count > 0) return m_tpu_panel;
    return target;
}

void SeparatedDragDropPanel::UpdateVolumeTypeCounts(int standard_count, int high_flow_count, int tpu_count)
{
    m_has_volume_type_counts = true;
    m_std_count              = standard_count;
    m_hf_count               = high_flow_count;
    m_tpu_count              = tpu_count;

    // 当某流量类型的喷嘴数变为 0 时，将该隐藏列中已有的耗材色块迁移到可见列
    if (m_use_separation) {
        DragDropPanel *fallback = ResolveSeparatedTargetPanel();
        auto migrate_if_hidden = [fallback](DragDropPanel *panel, int count) {
            if (count > 0 || panel == fallback)
                return;
            auto blocks = panel->get_filament_blocks();
            for (auto *block : blocks) {
                fallback->AddColorBlock(block->GetType(), block->GetFilamentId(), false);
                panel->RemoveColorBlock(block, false);
            }
        };
        migrate_if_hidden(m_standard_panel, m_std_count);
        migrate_if_hidden(m_high_flow_panel, m_hf_count);
        migrate_if_hidden(m_tpu_panel, m_tpu_count);
    }

    auto with_count = [](const wxString &name, int count) { return wxString::Format("%s: %d", name, count); };
    m_standard_panel->UpdateLabel(with_count(_L("Standard"), standard_count));
    m_high_flow_panel->UpdateLabel(with_count(_L("High Flow"), high_flow_count));
    m_tpu_panel->UpdateLabel(with_count(_L("TPU285"), tpu_count));
    UpdateLayout();
}

void SeparatedDragDropPanel::SetUnifiedGridColumns(int cols)
{
    if (!m_unified_panel)
        return;
    m_unified_panel->SetMinSize(wxDefaultSize);
    m_unified_panel->SetMaxSize(wxDefaultSize);
    m_unified_panel->SetGridColumns(cols);
    Layout();
}

void SeparatedDragDropPanel::SetUnifiedGridWidth(int box_width)
{
    // 分流量类型时各分组沿用自己调好的固定列数与间距。
    if (!m_unified_panel || m_use_separation)
        return;
    m_unified_panel->SetGridColumnsForWidth(box_width);
    Layout();
}

void SeparatedDragDropPanel::SetUnifiedGridHorizontalPadding(int padding, int gap)
{
    if (!m_unified_panel)
        return;
    m_unified_panel->SetGridHorizontalPadding(padding, gap);
    Layout();
}

void SeparatedDragDropPanel::SetDiameterVolumeGroups(const std::vector<DiameterVolumeGroup> &groups, bool use_white_cards)
{
    for (auto &group : m_diameter_volume_panels)
        group.panel->Destroy();
    m_diameter_volume_panels.clear();
    m_diameter_volume_sizer->Clear(false);

    for (const auto &group : groups) {
        auto *panel = new DragDropPanel(
            m_diameter_volume_container, group.label, false, true, true, m_use_fixed_grid_item_size);
        const wxColour background = wxGetApp().dark_mode()
            ? drag_panel_background(true)
            : (use_white_cards ? *wxWHITE : wxColour("#F8F8F8"));
        panel->SetBackgroundColour(background);
        for (wxWindow *child : panel->GetChildren()) {
            child->SetBackgroundColour(background);
            for (wxWindow *grandchild : child->GetChildren())
                grandchild->SetBackgroundColour(background);
        }
        panel->SetGridHorizontalPadding(FromDIP(4), FromDIP(6));
        panel->SetMinSize({FromDIP(110), FromDIP(100)});
        m_diameter_volume_sizer->Add(panel, 0, wxEXPAND);
        m_diameter_volume_panels.push_back({group.diameter, group.volume_type, panel});
    }
    UpdateLayout();
    UpdateDiameterVolumeLayout();
}

void SeparatedDragDropPanel::SetEnabledDiameterVolumeGroup(const std::string &diameter, NozzleVolumeType volume_type)
{
    for (auto &group : m_diameter_volume_panels)
        group.panel->SetInteractionEnabled(group.diameter == diameter && group.volume_type == volume_type);
    m_diameter_volume_container->Refresh(true);
}

void SeparatedDragDropPanel::set_accept_filament_predicate(std::function<bool(int)> pred)
{
    m_accept_filament = pred;
    // All sub-panels belong to the same extruder, so they share the same working diameter / predicate.
    if (m_high_flow_panel) m_high_flow_panel->set_accept_filament_predicate(pred);
    if (m_standard_panel)  m_standard_panel->set_accept_filament_predicate(pred);
    if (m_tpu_panel)       m_tpu_panel->set_accept_filament_predicate(pred);
    if (m_unified_panel)   m_unified_panel->set_accept_filament_predicate(std::move(pred));
    for (auto &group : m_diameter_volume_panels)
        group.panel->set_accept_filament_predicate(m_accept_filament);
}

void SeparatedDragDropPanel::set_diameter_group_accept_filament_predicate(
    std::function<bool(int, const std::string &)> pred)
{
    for (auto &group : m_diameter_volume_panels) {
        const std::string diameter = group.diameter;
        group.panel->set_accept_filament_predicate(
            [pred, diameter](int filament_id) { return !pred || pred(filament_id, diameter); });
    }
}

void SeparatedDragDropPanel::SetUseSeparation(bool use_separation)
{
    if (!m_diameter_volume_panels.empty())
        return;
    if (m_use_separation != use_separation) {
        m_use_separation = use_separation;

        if (use_separation) {
            DragDropPanel *target = ResolveSeparatedTargetPanel();
            auto blocks = m_unified_panel->get_filament_blocks();
            for (auto &block : blocks) {
                target->AddColorBlock(block->GetType(), block->GetFilamentId(), false);
                m_unified_panel->RemoveColorBlock(block, false);
            }
        } else {
            auto high_flow_blocks = m_high_flow_panel->get_filament_blocks();
            auto standard_blocks  = m_standard_panel->get_filament_blocks();
            auto tpu_blocks       = m_tpu_panel->get_filament_blocks();

            for (auto &block : high_flow_blocks) {
                m_unified_panel->AddColorBlock(block->GetType(), block->GetFilamentId(), false);
                m_high_flow_panel->RemoveColorBlock(block, false);
            }

            for (auto &block : standard_blocks) {
                m_unified_panel->AddColorBlock(block->GetType(), block->GetFilamentId(), false);
                m_standard_panel->RemoveColorBlock(block, false);
            }

            for (auto &block : tpu_blocks) {
                m_unified_panel->AddColorBlock(block->GetType(), block->GetFilamentId(), false);
                m_tpu_panel->RemoveColorBlock(block, false);
            }
        }

        UpdateLayout();
    }
}

void SeparatedDragDropPanel::AddColorBlock(const std::string &type, int filament_id, NozzleVolumeType volume_type,
                                           bool update_ui, const std::string &diameter)
{
    if (!m_diameter_volume_panels.empty()) {
        auto matches = [volume_type, &diameter](const DiameterVolumePanel &group) {
            return group.volume_type == volume_type && (diameter.empty() || group.diameter == diameter);
        };
        auto group = std::find_if(m_diameter_volume_panels.begin(), m_diameter_volume_panels.end(), matches);
        if (group == m_diameter_volume_panels.end())
            group = m_diameter_volume_panels.begin();
        group->panel->AddColorBlock(type, filament_id, update_ui);
        UpdateDiameterVolumeLayout();
    } else if (m_use_separation) {
        ResolveSeparatedTargetPanel(volume_type)->AddColorBlock(type, filament_id, update_ui);
    } else {
        m_unified_panel->AddColorBlock(type, filament_id, update_ui);
    }
}

void SeparatedDragDropPanel::RemoveColorBlock(ColorPanel *panel, bool update_ui)
{
    for (auto &group : m_diameter_volume_panels) {
        auto blocks = group.panel->get_filament_blocks();
        if (std::find(blocks.begin(), blocks.end(), panel) != blocks.end()) {
            group.panel->RemoveColorBlock(panel, update_ui);
            UpdateDiameterVolumeLayout();
            return;
        }
    }

    auto high_flow_blocks = m_high_flow_panel->get_filament_blocks();
    auto standard_blocks  = m_standard_panel->get_filament_blocks();
    auto tpu_blocks       = m_tpu_panel->get_filament_blocks();
    auto unified_blocks   = m_unified_panel->get_filament_blocks();

    if (std::find(high_flow_blocks.begin(), high_flow_blocks.end(), panel) != high_flow_blocks.end()) {
        m_high_flow_panel->RemoveColorBlock(panel, update_ui);
    } else if (std::find(standard_blocks.begin(), standard_blocks.end(), panel) != standard_blocks.end()) {
        m_standard_panel->RemoveColorBlock(panel, update_ui);
    } else if (std::find(tpu_blocks.begin(), tpu_blocks.end(), panel) != tpu_blocks.end()) {
        m_tpu_panel->RemoveColorBlock(panel, update_ui);
    } else if (std::find(unified_blocks.begin(), unified_blocks.end(), panel) != unified_blocks.end()) {
        m_unified_panel->RemoveColorBlock(panel, update_ui);
    }

}

std::vector<int> SeparatedDragDropPanel::GetAllFilaments() const
{
    if (!m_diameter_volume_panels.empty()) {
        std::vector<int> result;
        for (const auto &group : m_diameter_volume_panels) {
            auto filaments = group.panel->GetAllFilaments();
            result.insert(result.end(), filaments.begin(), filaments.end());
        }
        return result;
    } else if (m_use_separation) {
        auto high_flow = m_high_flow_panel->GetAllFilaments();
        auto standard  = m_standard_panel->GetAllFilaments();
        auto tpu       = m_tpu_panel->GetAllFilaments();

        std::vector<int> result;
        result.insert(result.end(), high_flow.begin(), high_flow.end());
        result.insert(result.end(), standard.begin(), standard.end());
        result.insert(result.end(), tpu.begin(), tpu.end());
        return result;
    } else {
        return m_unified_panel->GetAllFilaments();
    }
}

std::vector<int> SeparatedDragDropPanel::GetHighFlowFilaments() const
{
    if (!m_diameter_volume_panels.empty()) {
        std::vector<int> result;
        for (const auto &group : m_diameter_volume_panels) {
            if (group.volume_type != nvtHighFlow && group.volume_type != nvtE3DHighFlow)
                continue;
            auto filaments = group.panel->GetAllFilaments();
            result.insert(result.end(), filaments.begin(), filaments.end());
        }
        return result;
    }
    if (m_use_separation) {
        return m_high_flow_panel->GetAllFilaments();
    }
    auto nozzle_volumes = wxGetApp().preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    const int right_eid = 1;
    if (nozzle_volumes->values.size() > right_eid) {
        int volume_type = nozzle_volumes->values[right_eid];
        if (volume_type == static_cast<int>(NozzleVolumeType::nvtHighFlow) ||
            volume_type == static_cast<int>(NozzleVolumeType::nvtE3DHighFlow)) {
            return m_unified_panel->GetAllFilaments();
        }
    }
    return {};
}

std::vector<int> SeparatedDragDropPanel::GetStandardFilaments() const
{
    if (!m_diameter_volume_panels.empty()) {
        std::vector<int> result;
        for (const auto &group : m_diameter_volume_panels) {
            if (group.volume_type != nvtStandard)
                continue;
            auto filaments = group.panel->GetAllFilaments();
            result.insert(result.end(), filaments.begin(), filaments.end());
        }
        return result;
    }
    if (m_use_separation) {
        return m_standard_panel->GetAllFilaments();
    }
    auto nozzle_volumes = wxGetApp().preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    const int right_eid = 1;
    if (nozzle_volumes->values.size() > right_eid) {
        int volume_type = nozzle_volumes->values[right_eid];
        if (volume_type == static_cast<int>(NozzleVolumeType::nvtStandard)) {
            return m_unified_panel->GetAllFilaments();
        }
    }
    return {};
}

std::vector<int> SeparatedDragDropPanel::GetTPUHighFlowFilaments() const
{
    if (!m_diameter_volume_panels.empty()) {
        std::vector<int> result;
        for (const auto &group : m_diameter_volume_panels) {
            if (group.volume_type != nvtTPUHighFlow && group.volume_type != nvtTPU285)
                continue;
            auto filaments = group.panel->GetAllFilaments();
            result.insert(result.end(), filaments.begin(), filaments.end());
        }
        return result;
    }
    if (m_use_separation) {
        return m_tpu_panel->GetAllFilaments();
    }
    auto nozzle_volumes = wxGetApp().preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    const int right_eid = 1;
    if (nozzle_volumes->values.size() > right_eid) {
        int volume_type = nozzle_volumes->values[right_eid];
        if (volume_type == static_cast<int>(NozzleVolumeType::nvtTPUHighFlow)) {
            return m_unified_panel->GetAllFilaments();
        }
    }
    return {};
}

std::vector<ColorPanel *> SeparatedDragDropPanel::get_filament_blocks() const
{
    if (!m_diameter_volume_panels.empty()) {
        std::vector<ColorPanel *> result;
        for (const auto &group : m_diameter_volume_panels) {
            auto blocks = group.panel->get_filament_blocks();
            result.insert(result.end(), blocks.begin(), blocks.end());
        }
        return result;
    } else if (m_use_separation) {
        auto high_flow = m_high_flow_panel->get_filament_blocks();
        auto standard  = m_standard_panel->get_filament_blocks();
        auto tpu       = m_tpu_panel->get_filament_blocks();

        std::vector<ColorPanel *> result;
        result.insert(result.end(), high_flow.begin(), high_flow.end());
        result.insert(result.end(), standard.begin(), standard.end());
        result.insert(result.end(), tpu.begin(), tpu.end());
        return result;
    } else {
        return m_unified_panel->get_filament_blocks();
    }
}

std::vector<ColorPanel *> SeparatedDragDropPanel::get_high_flow_blocks() const
{
    if (!m_diameter_volume_panels.empty()) {
        std::vector<ColorPanel *> result;
        for (const auto &group : m_diameter_volume_panels) {
            if (group.volume_type != nvtHighFlow && group.volume_type != nvtE3DHighFlow)
                continue;
            auto blocks = group.panel->get_filament_blocks();
            result.insert(result.end(), blocks.begin(), blocks.end());
        }
        return result;
    }
    return m_high_flow_panel->get_filament_blocks();
}

std::vector<ColorPanel *> SeparatedDragDropPanel::get_standard_blocks() const
{
    if (!m_diameter_volume_panels.empty()) {
        std::vector<ColorPanel *> result;
        for (const auto &group : m_diameter_volume_panels) {
            if (group.volume_type != nvtStandard)
                continue;
            auto blocks = group.panel->get_filament_blocks();
            result.insert(result.end(), blocks.begin(), blocks.end());
        }
        return result;
    }
    return m_standard_panel->get_filament_blocks();
}

std::vector<ColorPanel *> SeparatedDragDropPanel::get_tpu_blocks() const
{
    if (!m_diameter_volume_panels.empty()) {
        std::vector<ColorPanel *> result;
        for (const auto &group : m_diameter_volume_panels) {
            if (group.volume_type != nvtTPUHighFlow && group.volume_type != nvtTPU285)
                continue;
            auto blocks = group.panel->get_filament_blocks();
            result.insert(result.end(), blocks.begin(), blocks.end());
        }
        return result;
    }
    return m_tpu_panel->get_filament_blocks();
}

void SeparatedDragDropPanel::ClearAllBlocks()
{
    for (auto &group : m_diameter_volume_panels) {
        auto blocks = group.panel->get_filament_blocks();
        for (auto &block : blocks)
            group.panel->RemoveColorBlock(block, false);
    }

    auto high_flow_blocks = m_high_flow_panel->get_filament_blocks();
    for (auto &block : high_flow_blocks) {
        m_high_flow_panel->RemoveColorBlock(block, false);
    }

    auto standard_blocks = m_standard_panel->get_filament_blocks();
    for (auto &block : standard_blocks) {
        m_standard_panel->RemoveColorBlock(block, false);
    }

    auto tpu_blocks = m_tpu_panel->get_filament_blocks();
    for (auto &block : tpu_blocks) {
        m_tpu_panel->RemoveColorBlock(block, false);
    }

    auto unified_blocks = m_unified_panel->get_filament_blocks();
    for (auto &block : unified_blocks) {
        m_unified_panel->RemoveColorBlock(block, false);
    }
}

}} // namespace Slic3r::GUI
