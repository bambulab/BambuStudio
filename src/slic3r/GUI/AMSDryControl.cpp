#include "AMSDryControl.hpp"
#include "DeviceCore/DevFilaSystem.h"
#include "GUI_App.hpp"
#include "I18N.hpp"

#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevUpgrade.h"

#include "slic3r/GUI/DeviceCore/DevManager.h"

#include "slic3r/GUI/MsgDialog.hpp"

#include "slic3r/GUI/Widgets/AnimaController.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/ComboBox.hpp"
#include "slic3r/GUI/Widgets/ProgressBar.hpp"
#include "slic3r/GUI/Widgets/TextInput.hpp"
#include "DeviceCore/DevUtilBackend.h"

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/weakref.h>
#include <wx/wupdlock.h>
#include "libslic3r/Log.hpp"

#include <algorithm>

namespace Slic3r { namespace GUI {

const int AMS_DRY_STATUS_IMAGE_SIZE = 96;
const int AMS_DRY_STATUS_IMAGE_SIZE_MULTI_ZONE = 200;

// The dryable zone this dialog controls; only one per AMS for now.
// nullopt: AMS without zone info, the whole AMS is dried.
static std::optional<int> resolve_dry_zone_id(const DevAms* dev_ams)
{
    const auto zone_ids = dev_ams ? dev_ams->GetDryableZoneIds() : std::vector<int>();
    return zone_ids.empty() ? std::nullopt : std::optional<int>(zone_ids.front());
}

// Drying duration range accepted by the input box. The validation message and
// the input hint are both derived from these bounds so they cannot drift apart.
const int AMS_DRY_MIN_HOUR = 1;
const int AMS_DRY_MAX_HOUR = 72;

static std::string get_humidity_level_img_path(int humidity_percent)
{
    const int num_levels = 5;
    int hum_level;

    if (humidity_percent <= 100 / num_levels) {
        hum_level = 5;
    } else if (humidity_percent <= 100 / num_levels * 2) {
        hum_level = 4;
    } else if (humidity_percent <= 100 / num_levels * 3) {
        hum_level = 3;
    } else if (humidity_percent <= 100 / num_levels * 4) {
        hum_level = 2;
    } else {
        hum_level = 1;
    }

    if (wxGetApp().dark_mode()) {
        return "hum_level" + std::to_string(hum_level) + "_no_num_light";
    } else {
        return "hum_level" + std::to_string(hum_level) + "_no_num_light";
    }

}

// AMS with both dryable and non-dryable zones (e.g. AMS 3H): the whole device is shown with a badge per zone
static bool is_multi_zone_ams(const DevAms* dev_ams)
{
    if (!dev_ams) return false;
    const auto zones = dev_ams->GetZones();
    return !zones.empty() && dev_ams->GetDryableZoneIds().size() < zones.size();
}

// Image names follow dev_ams_dry_ctr_<model>_<state>
static std::string get_dry_img_prefix(DevAmsType type)
{
    switch (type) {
        case DevAmsType::N3S:      return "dev_ams_dry_ctr_n3s_";
        case DevAmsType::N3F:      return "dev_ams_dry_ctr_n3f_";
        case DevAmsType::ZMULTI_A: return "dev_ams_dry_ctr_zmulti_a_";
        default:                   return "";
    }
}

// Active drying states, shared by all layouts; empty when the state has no image
static std::string get_dry_status_img_path(DevAmsType type, DevAms::DryStatus status, DevAms::DrySubStatus sub_status)
{
    const std::string prefix = get_dry_img_prefix(type);
    if (prefix.empty()) return "";

    if (status == DevAms::DryStatus::Error) return prefix + "error";
    if (sub_status == DevAms::DrySubStatus::Heating) return prefix + "heating";
    if (sub_status == DevAms::DrySubStatus::Dehumidify) return prefix + "dehumidifying";
    return "";
}

// Generic layout: humidity level while idle, status image while drying
static std::string get_generic_img_path(DevAmsType type, DevAms::DryStatus status, DevAms::DrySubStatus sub_status, int humidity_percent)
{
    if (status == DevAms::DryStatus::Off && sub_status == DevAms::DrySubStatus::Off) {
        return get_humidity_level_img_path(humidity_percent);
    }
    return get_dry_status_img_path(type, status, sub_status);
}

// Multi-zone layout: a device image for every state, so the image never goes blank
static std::string get_multi_zone_img_path(DevAmsType type, DevAms::DryStatus status, DevAms::DrySubStatus sub_status)
{
    const std::string prefix = get_dry_img_prefix(type);
    if (prefix.empty()) return "";

    if (status == DevAms::DryStatus::Off) return prefix + "idle";
    if (status == DevAms::DryStatus::Cooling) return prefix + "cooling";
    const std::string active = get_dry_status_img_path(type, status, sub_status);
    return active.empty() ? prefix + "heating" : active;
}

static std::string get_drying_filament_type_for_command(const FilamentBaseInfo& filament)
{
    if (filament.filament_id.rfind("GFS02", 0) == 0) {
        return "Support for PLA";
    }
    if (filament.filament_id.rfind("GFS03", 0) == 0) {
        return "Support for PA/PET";
    }
    if (filament.filament_id.rfind("GFS05", 0) == 0) {
        return "Support for PLA/PETG";
    }
    if (filament.filament_id.rfind("GFS06", 0) == 0) {
        return "Support for ABS";
    }
    return filament.filament_type;
}

FilamentItemPanel::FilamentItemPanel(wxWindow* parent, const wxString& text, const std::string& icon_name, wxWindowID id)
    : wxPanel(parent, id)
    , m_icon_name(icon_name)
{
    SetBackgroundColour(wxColour("#F7F7F7")); // Light gray background
    SetMinSize(wxSize(FromDIP(64), FromDIP(106))); // Width: 64, Height: 106
    SetSize(wxSize(FromDIP(64), FromDIP(106)));    // Fixed size

    // Create sizer for vertical layout
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    // Top section with text - moved to be closer to center
    wxBoxSizer* top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->AddStretchSpacer(5);

    m_text_label = new Label(this, text);
    m_text_label->SetForegroundColour(StateColor::darkModeColorFor(wxColour(*wxBLACK)));
    m_text_label->SetBackgroundColour(StateColor::darkModeColorFor(wxColour("#F7F7F7")));
    m_text_label->SetFont(Label::Body_12);
    m_text_label->Wrap(FromDIP(40));
    top_sizer->Add(m_text_label, 0, wxALIGN_CENTER_HORIZONTAL);

    top_sizer->AddStretchSpacer(1); // Increased bottom spacer to push text down
    sizer->Add(top_sizer, 1, wxEXPAND);

    // Bottom section with icon - vertically centered in bottom half
    wxBoxSizer* bottom_sizer = new wxBoxSizer(wxVERTICAL);
    bottom_sizer->AddStretchSpacer(1);

    m_icon_bitmap = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap);
    m_icon_bitmap->SetBackgroundColour(StateColor::darkModeColorFor(wxColour("#F7F7F7")));
    m_icon_bitmap->SetMinSize(wxSize(FromDIP(24), FromDIP(24)));
    m_icon_bitmap->SetMaxSize(wxSize(FromDIP(24), FromDIP(24)));
    bottom_sizer->Add(m_icon_bitmap, 0, wxALIGN_CENTER_HORIZONTAL);

    bottom_sizer->AddStretchSpacer(1);
    sizer->Add(bottom_sizer, 1, wxEXPAND);

    SetSizer(sizer);

    // Bind events
    Bind(wxEVT_PAINT, &FilamentItemPanel::OnPaint, this);
    Bind(wxEVT_SIZE, &FilamentItemPanel::OnSize, this);

    SetIcon(icon_name);
    wxGetApp().UpdateDarkUI(this);
}

void FilamentItemPanel::SetText(const wxString& text)
{
    m_text_label->SetLabel(text);
    Layout();
}

void FilamentItemPanel::SetIcon(const std::string& icon_name)
{
    m_icon_name = icon_name;
    if (!icon_name.empty()) {
        try {
            m_icon = ScalableBitmap(this, icon_name, 20);
            m_icon.msw_rescale();
            m_icon_bitmap->SetBitmap(m_icon.bmp());
        } catch (Exception& e) {
            BOOST_LOG_TRIVIAL(info) << "failed to load filament icon";
            m_icon_bitmap->SetBitmap(wxNullBitmap);
        }
    } else {
        m_icon_bitmap->SetBitmap(wxNullBitmap);
    }
    m_icon_bitmap->Refresh();
}

void FilamentItemPanel::msw_rescale()
{
    if (m_icon_bitmap && !m_icon_name.empty()) {
        m_icon.msw_rescale();
        m_icon_bitmap->SetBitmap(m_icon.bmp());
        m_icon_bitmap->Refresh();
    }
    Layout();
}

void FilamentItemPanel::OnPaint(wxPaintEvent& event)
{
    wxPaintDC dc(this);
    wxSize size = GetSize();

    // bool is_dark_mode = wxGetApp().dark_mode();
    wxColour backgroundColor = StateColor::darkModeColorFor(wxColour("#F7F7F7"));
    wxColour borderColor = StateColor::darkModeColorFor(wxColour("#DBDBDB"));

    // Draw white background rectangle with rounded corners inside the thick vertical lines
    dc.SetBrush(wxBrush(backgroundColor));
    dc.SetPen(wxPen(backgroundColor));
    dc.DrawRectangle(FromDIP(6), FromDIP(12), size.GetWidth() - FromDIP(12), size.GetHeight() - 2 * FromDIP(12));

    // Draw much thicker vertical rounded rectangles on left and right (3 times thicker)
    dc.SetBrush(wxBrush(wxColour(borderColor)));
    dc.SetPen(wxPen(wxColour(borderColor)));
    dc.DrawRoundedRectangle(FromDIP(0), FromDIP(0), FromDIP(6), size.GetHeight(), FromDIP(6)); // Left rounded rectangle
    dc.DrawRoundedRectangle(size.GetWidth() - FromDIP(6), FromDIP(0), FromDIP(6), size.GetHeight(), FromDIP(6)); // Right rounded rectangle

    // Draw thin horizontal rounded rectangles on top and bottom (moved closer to center by half the distance)
    dc.SetBrush(wxBrush(wxColour(borderColor)));
    dc.SetPen(wxPen(wxColour(borderColor)));
    dc.DrawRoundedRectangle(FromDIP(6), FromDIP(12), size.GetWidth() - FromDIP(12), FromDIP(3), FromDIP(1)); // Top rounded rectangle
    dc.DrawRoundedRectangle(FromDIP(6), size.GetHeight() - FromDIP(13), size.GetWidth() - FromDIP(12), FromDIP(3), FromDIP(1)); // Bottom rounded rectangle
}

void FilamentItemPanel::OnSize(wxSizeEvent& event)
{
    Refresh();
    event.Skip();
}

// class AMSFilamentPanel

AMSFilamentPanel::AMSFilamentPanel(wxWindow* parent, const wxString& ams_name, wxWindowID id)
    : wxPanel(parent, id)
{
    SetBackgroundColour(wxColour("#DBDBDB"));

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Filament items section
    m_filament_container = new wxPanel(this);
    m_filament_container->SetBackgroundColour(wxColour("#F7F7F7"));
    m_filament_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_filament_container->SetSizer(m_filament_sizer);

    // AMS name section
    m_ams_name_label = new Label(this, ams_name);
    m_ams_name_label->SetForegroundColour(wxColour("#858585"));
    m_ams_name_label->SetFont(Label::Body_14);
    m_ams_name_label->SetBackgroundColour(wxColour("#DBDBDB"));

    main_sizer->Add(m_filament_container, 1, wxEXPAND | wxALL, 0);
    main_sizer->Add(m_ams_name_label, 0, wxALIGN_LEFT | wxALL, FromDIP(5));

    SetSizer(main_sizer);
    wxGetApp().UpdateDarkUI(this);
}

void AMSFilamentPanel::AddFilamentItem(FilamentItemPanel* panel)
{
    if (panel) {
        panel->Reparent(m_filament_container);
        m_filament_sizer->Add(panel, 0, wxALL, FromDIP(5));
        m_filament_items.push_back(panel);
        Layout();
    }
}

void AMSFilamentPanel::AddFilamentItem(const wxString& text, const std::string& icon_name)
{
    FilamentItemPanel* item = new FilamentItemPanel(m_filament_container, text, icon_name);
    m_filament_items.push_back(item);
    m_filament_sizer->Add(item, 0, wxALL, FromDIP(5));
    Layout();
}

void AMSFilamentPanel::SetAmsName(const wxString& ams_name)
{
    m_ams_name_label->SetLabel(ams_name);
    Layout();
}

void AMSFilamentPanel::Clear()
{
    m_filament_sizer->Clear(true);
    m_filament_items.clear();
}

void AMSFilamentPanel::msw_rescale()
{
    for (auto item : m_filament_items) {
        item->msw_rescale();
    }
    Layout();
}


AMSDryCtrWin::AMSDryCtrWin(wxWindow *parent)
    :DPIDialog(parent, wxID_ANY, _L("AMS Dryness Control"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    create();
}

AMSDryCtrWin::~AMSDryCtrWin()
{
    if (m_refresh_timer) {
        m_refresh_timer->Stop();
        delete m_refresh_timer;
    }
    if (m_progress_timer) {
        delete m_progress_timer;
    }
}

// Tracks the single live dialog. wxWeakRef auto-nullifies on destruction, so
// the guard can never dangle even if the dialog is destroyed by wx teardown.
static wxWeakRef<AMSDryCtrWin> s_active_dry_ctr_win;

void AMSDryCtrWin::RunModal(wxWindow* parent, const std::string& ams_id,
                            const std::optional<wxPoint>& popup_pos)
{
    if (s_active_dry_ctr_win) {
        return;
    }

    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!obj || !obj->GetFilaSystem()) {
        return;
    }

    AMSDryCtrWin dlg(parent);
    s_active_dry_ctr_win = &dlg;

    dlg.set_ams_id(ams_id);
    if (popup_pos.has_value()) {
        dlg.Move(popup_pos.value());
    }
    dlg.ShowModal();
    s_active_dry_ctr_win = nullptr;
}


wxScrolledWindow* AMSDryCtrWin::create_preview_scrolled_window(wxWindow* parent)
{
    wxScrolledWindow* panel = new wxScrolledWindow(parent, wxID_ANY);
    panel->SetScrollRate(10, 0);
    panel->SetSize(AMS_ITEMS_PANEL_SIZE);
    panel->SetMinSize(AMS_ITEMS_PANEL_SIZE);
    panel->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    return panel;
}

wxBoxSizer* AMSDryCtrWin::create_humidity_status_section(wxPanel* parent)
{
    wxBoxSizer* image_sizer = new wxBoxSizer(wxVERTICAL);

    m_zmulti_humidity_icon = ScalableBitmap(this, "dev_ams_dry_ctr_humidity", 16);
    m_zmulti_temp_icon = ScalableBitmap(this, "dev_ams_dry_ctr_temp", 16);

    m_zmulti_image_overlay = new wxPanel(parent, wxID_ANY);
    m_zmulti_image_overlay->SetBackgroundColour(parent->GetBackgroundColour());
    m_zmulti_image_overlay->SetDoubleBuffered(true);
    m_zmulti_image_overlay->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_zmulti_image_overlay->Bind(wxEVT_PAINT, &AMSDryCtrWin::on_overlay_paint, this);
    m_zmulti_image_overlay->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&){});

    m_humidity_img = new wxStaticBitmap(m_zmulti_image_overlay, wxID_ANY, wxNullBitmap);
    m_humidity_img->Move(0, 0);

    image_sizer->Add(m_zmulti_image_overlay, 0, wxALIGN_CENTER_HORIZONTAL, 0);

    m_zmulti_m2y_pill = create_m2y_pill(parent);
    m_zmulti_m2y_pill->Show(false);
    // The overlay panel is wider than the bitmap (extra reserve on the right
    // for the dryable badge). To centre the pill under the bitmap rather than
    // the overlay, wrap it in a row sizer with an asymmetric right padding
    // equal to the overlay's right reserve.
    wxBoxSizer* pill_row = new wxBoxSizer(wxHORIZONTAL);
    pill_row->Add(m_zmulti_m2y_pill, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(40));
    image_sizer->Add(pill_row, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(6));

    m_image_desc_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_image_description_icon = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap);
    m_image_description_icon->SetBackgroundColour(*wxWHITE);
    m_image_description_icon->SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
    m_image_description_icon->SetMaxSize(wxSize(FromDIP(20), FromDIP(20)));
    m_image_description_icon->Show(false);
    m_image_desc_sizer->Add(m_image_description_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));

    m_image_description = new Label(parent, _L("Idle"));
    m_image_description->SetFont(Label::Head_14);
    m_image_description->SetForegroundColour(*wxBLACK);
    m_image_description->SetBackgroundColour(*wxWHITE);
    m_image_desc_sizer->Add(m_image_description, 0, wxALIGN_CENTER_VERTICAL);

    image_sizer->Add(m_image_desc_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    image_sizer->AddStretchSpacer(1);

    return image_sizer;
}

namespace {

// Geometry / styling constants for the ZMULTI_A overlay drawn entirely in
// AMSDryCtrWin::on_overlay_paint. Pixel positions are derived from the figma
// design at https://figma.com/.../node-id=965-32760 (200x180 device image
// inside a 230x221 frame) and scaled to the runtime bitmap size.
struct ZMultiBadgeStyle {
    wxColour bg;
    wxColour fg;
    wxColour separator;
    int      radius_dip;
    int      pad_h_dip; // horizontal padding (each side)
    int      pad_v_dip; // vertical padding (each side)
};

constexpr double kFigmaImageH = 180.0;
// Figma badge anchor Y positions, in figma image-local coords.
// Both badges are horizontally centred on the image instead of using an X anchor.
constexpr int kFigmaDryableY = 0;
constexpr int kFigmaNormalY  = 130;

} // namespace

wxSize AMSDryCtrWin::compute_zmulti_overlay_size() const
{
    if (!m_humidity_image.bmp().IsOk()) return wxSize(0, 0);
    wxSize bmp_sz = m_humidity_image.bmp().GetSize();
    // Conservative reserve for the dryable badge that overhangs the top edge
    // of the bitmap (figma anchor y=0, badge centred there) and the dashed
    // connector that descends below it. The actual badge size is computed in
    // on_overlay_paint, so we add a generous upper bound here to guarantee
    // the panel never clips its overlay drawing.
    int extra_top = FromDIP(28);
    int extra_right = FromDIP(40); // dryable badge anchored near right side may extend past bitmap width
    return wxSize(bmp_sz.GetWidth() + extra_right, bmp_sz.GetHeight() + extra_top);
}

void AMSDryCtrWin::on_overlay_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC bdc(m_zmulti_image_overlay);
    bdc.SetBackground(wxBrush(m_zmulti_image_overlay->GetBackgroundColour()));
    bdc.Clear();

    if (m_image_layout != DryImageLayout::MultiZone || !m_humidity_image.bmp().IsOk()) {
        return;
    }

    wxBitmap bmp = m_humidity_image.bmp();
    wxSize bmp_sz = bmp.GetSize();
    // Must match compute_zmulti_overlay_size(): the panel reserves 28dp at
    // the top so the dryable badge (anchored at figma y=0 i.e. on the upper
    // edge of the device image) is fully visible.
    int extra_top = FromDIP(28);
    int bmp_x = 0;
    int bmp_y = extra_top;
    bdc.DrawBitmap(bmp, bmp_x, bmp_y, true);

    wxGCDC dc(bdc);
    wxGraphicsContext* gc = dc.GetGraphicsContext();
    if (!gc) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    ZMultiBadgeStyle style_normal{
        StateColor::darkModeColorFor(wxColour("#141414")),
        StateColor::darkModeColorFor(wxColour("#A3A3A3")),
        StateColor::darkModeColorFor(wxColour("#474747")),
        FromDIP(8), FromDIP(8), FromDIP(3)
    };
    ZMultiBadgeStyle style_dryable{
        StateColor::darkModeColorFor(wxColour("#474747")),
        StateColor::darkModeColorFor(wxColour("#A3A3A3")),
        StateColor::darkModeColorFor(wxColour("#797979")),
        FromDIP(8), FromDIP(8), FromDIP(3)
    };

    auto fmt_value = [](int v) -> wxString {
        return v < 0 ? wxString("--") : wxString::Format("%d", v);
    };
    wxString dryable_hum_text = fmt_value(m_ams_info.m_humidity_percent);
    wxString dryable_temp_text = fmt_value(m_ams_info.m_temperature);
    wxString normal_hum_text = fmt_value(m_ambient_humidity_percent);
    wxString normal_temp_text = fmt_value(m_ambient_temperature);

    wxFont font_value = Label::Body_13;
    wxFont font_unit = Label::Body_10;

    auto measure_badge = [&](const ZMultiBadgeStyle& s, const wxString& hum_text, const wxString& temp_text) -> wxSize {
        gc->SetFont(font_value, s.fg);
        wxDouble vw_h = 0, vh_h = 0, vd, ve;
        gc->GetTextExtent(hum_text, &vw_h, &vh_h, &vd, &ve);
        wxDouble vw_t = 0, vh_t = 0;
        gc->GetTextExtent(temp_text, &vw_t, &vh_t, &vd, &ve);
        gc->SetFont(font_unit, s.fg);
        wxDouble uw_p = 0, uh_p = 0;
        gc->GetTextExtent("%", &uw_p, &uh_p, &vd, &ve);
        wxDouble uw_c = 0, uh_c = 0;
        gc->GetTextExtent(wxString::FromUTF8("℃"), &uw_c, &uh_c, &vd, &ve);

        int icon_w = m_zmulti_humidity_icon.bmp().GetWidth();
        int icon_h = m_zmulti_humidity_icon.bmp().GetHeight();
        int gap = FromDIP(2);
        int sep_w = FromDIP(1);
        int sep_pad = FromDIP(6);

        int content_w =
            icon_w + gap + static_cast<int>(vw_h) + gap + static_cast<int>(uw_p)
          + sep_pad + sep_w + sep_pad
          + icon_w + gap + static_cast<int>(vw_t) + gap + static_cast<int>(uw_c);
        int content_h = std::max(icon_h, static_cast<int>(std::max(vh_h, uh_p)));
        return wxSize(content_w + 2 * s.pad_h_dip,
                      content_h + 2 * s.pad_v_dip);
    };

    auto draw_badge = [&](double bx, double by, const ZMultiBadgeStyle& s, wxSize badge_size,
                          const wxString& hum_text, const wxString& temp_text) {
        // Rounded background.
        gc->SetBrush(wxBrush(s.bg));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawRoundedRectangle(bx, by, badge_size.GetWidth(), badge_size.GetHeight(), s.radius_dip);

        int icon_w = m_zmulti_humidity_icon.bmp().GetWidth();
        int icon_h = m_zmulti_humidity_icon.bmp().GetHeight();
        int gap = FromDIP(2);
        int sep_w = FromDIP(1);
        int sep_pad = FromDIP(6);

        gc->SetFont(font_value, s.fg);
        wxDouble vw_h, vh_h, vd, ve;
        gc->GetTextExtent(hum_text, &vw_h, &vh_h, &vd, &ve);
        wxDouble vw_t, vh_t;
        gc->GetTextExtent(temp_text, &vw_t, &vh_t, &vd, &ve);
        gc->SetFont(font_unit, s.fg);
        wxDouble uw_p, uh_p;
        gc->GetTextExtent("%", &uw_p, &uh_p, &vd, &ve);
        wxDouble uw_c, uh_c;
        gc->GetTextExtent(wxString::FromUTF8("℃"), &uw_c, &uh_c, &vd, &ve);

        double cy = by + badge_size.GetHeight() / 2.0;
        double cur_x = bx + s.pad_h_dip;

        gc->DrawBitmap(m_zmulti_humidity_icon.bmp(), cur_x, cy - icon_h / 2.0, icon_w, icon_h);
        cur_x += icon_w + gap;
        gc->SetFont(font_value, s.fg);
        gc->DrawText(hum_text, cur_x, cy - vh_h / 2.0);
        cur_x += vw_h + gap;
        gc->SetFont(font_unit, s.fg);
        gc->DrawText("%", cur_x, cy - uh_p / 2.0 + FromDIP(1));
        cur_x += uw_p + sep_pad;

        gc->SetPen(wxPen(s.separator, sep_w));
        double sep_h = badge_size.GetHeight() - 2 * s.pad_v_dip - FromDIP(2);
        gc->StrokeLine(cur_x, cy - sep_h / 2.0, cur_x, cy + sep_h / 2.0);
        cur_x += sep_w + sep_pad;
        gc->SetPen(*wxTRANSPARENT_PEN);

        gc->DrawBitmap(m_zmulti_temp_icon.bmp(), cur_x, cy - icon_h / 2.0, icon_w, icon_h);
        cur_x += icon_w + gap;
        gc->SetFont(font_value, s.fg);
        gc->DrawText(temp_text, cur_x, cy - vh_t / 2.0);
        cur_x += vw_t + gap;
        gc->SetFont(font_unit, s.fg);
        gc->DrawText(wxString::FromUTF8("℃"), cur_x, cy - uh_c / 2.0 + FromDIP(1));
    };

    wxSize sz_normal = measure_badge(style_normal, normal_hum_text, normal_temp_text);
    wxSize sz_dryable = measure_badge(style_dryable, dryable_hum_text, dryable_temp_text);

    double sy = bmp_sz.GetHeight() / kFigmaImageH;

    // Place the dryable badge so its centre line sits at figma y=0 (i.e. on
    // the upper edge of the device image). Half of it spills into the reserved
    // top strip we added in compute_zmulti_overlay_size().
    double dryable_x = bmp_x + (bmp_sz.GetWidth() - sz_dryable.GetWidth()) / 2.0;
    double dryable_y = bmp_y + kFigmaDryableY * sy - sz_dryable.GetHeight() / 2.0;
    double normal_x  = bmp_x + (bmp_sz.GetWidth() - sz_normal.GetWidth()) / 2.0;
    double normal_y  = bmp_y + kFigmaNormalY * sy;

    draw_badge(normal_x, normal_y, style_normal, sz_normal, normal_hum_text, normal_temp_text);
    draw_badge(dryable_x, dryable_y, style_dryable, sz_dryable, dryable_hum_text, dryable_temp_text);

    // Green dashed connector descending from the dryable badge.
    double cx = dryable_x + sz_dryable.GetWidth() / 2.0;
    double y_top = dryable_y + sz_dryable.GetHeight();
    double y_bot = y_top + FromDIP(18);
    wxPen pen(wxColour(0, 174, 66), FromDIP(1));
    wxDash dashes[2] = { 2, 2 };
    pen.SetStyle(wxPENSTYLE_USER_DASH);
    pen.SetDashes(2, dashes);
    gc->SetPen(pen);
    gc->StrokeLine(cx, y_top, cx, y_bot);
}

wxPanel* AMSDryCtrWin::create_m2y_pill(wxPanel* parent)
{
    wxPanel* pill = new wxPanel(parent, wxID_ANY);
    pill->SetBackgroundColour(parent->GetBackgroundColour());
    pill->SetBackgroundStyle(wxBG_STYLE_PAINT);
    pill->SetDoubleBuffered(true);
    pill->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&){});

    auto on_paint = [pill](wxPaintEvent&) {
        wxAutoBufferedPaintDC bdc(pill);
        bdc.SetBackground(wxBrush(pill->GetBackgroundColour()));
        bdc.Clear();
        wxGCDC dc(bdc);
        wxGraphicsContext* gc = dc.GetGraphicsContext();
        if (!gc) return;
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

        wxColour bg = StateColor::darkModeColorFor(wxColour(51, 51, 51));
        wxColour fg = StateColor::darkModeColorFor(wxColour("#E0E0E0"));
        wxSize sz = pill->GetSize();
        gc->SetBrush(wxBrush(bg));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawRoundedRectangle(0, 0, sz.GetWidth(), sz.GetHeight(), pill->FromDIP(12));

        wxBitmap icon = ScalableBitmap(pill, "dev_ams_dry_ctr_plugedin", 20).bmp();
        wxString text("AMS 3H");
        wxFont font = Label::Body_14;
        gc->SetFont(font, fg);
        wxDouble tw, th, td, te;
        gc->GetTextExtent(text, &tw, &th, &td, &te);

        int gap = pill->FromDIP(6);
        int icon_w = icon.GetWidth();
        int icon_h = icon.GetHeight();
        double total_w = tw + gap + icon_w;
        double cur_x = (sz.GetWidth() - total_w) / 2.0;
        double cy = sz.GetHeight() / 2.0;

        gc->DrawText(text, cur_x, cy - th / 2.0);
        cur_x += tw + gap;
        gc->DrawBitmap(icon, cur_x, cy - icon_h / 2.0, icon_w, icon_h);
    };
    pill->Bind(wxEVT_PAINT, on_paint);

    int icon_w = ScalableBitmap(pill, "dev_ams_dry_ctr_plugedin", 20).bmp().GetWidth();
    wxClientDC dc(pill);
    dc.SetFont(Label::Body_14);
    wxSize text_size = dc.GetTextExtent("AMS 3H");
    int width = text_size.GetWidth() + icon_w + pill->FromDIP(6) + 2 * pill->FromDIP(14);
    int height = pill->FromDIP(28);
    pill->SetMinSize(wxSize(width, height));
    pill->SetSize(wxSize(width, height));
    return pill;
}

wxBoxSizer* AMSDryCtrWin::create_description_item(wxPanel* parent, const wxString& title, Label*& dataLabel)
{
    wxBoxSizer* item_sizer = new wxBoxSizer(wxVERTICAL);

    Label* titleLabel = new Label(parent, title);
    titleLabel->SetForegroundColour(*wxBLACK);
    titleLabel->SetFont(Label::Body_16);

    dataLabel = new Label(parent, wxT("--"));
    dataLabel->SetForegroundColour(*wxBLACK);
    dataLabel->SetFont(Label::Body_14);

    item_sizer->AddStretchSpacer();
    item_sizer->Add(titleLabel, 0, wxALIGN_CENTER | wxALL, FromDIP(2));
    item_sizer->Add(dataLabel, 0, wxALIGN_CENTER | wxALL, FromDIP(2));
    item_sizer->AddStretchSpacer();

    return item_sizer;
}

wxBoxSizer* AMSDryCtrWin::create_status_descriptions_section(wxPanel* parent)
{
    wxBoxSizer* desc_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* hum_desc_sizer = create_description_item(parent, _L("Humidity"), m_humidity_data_label);
    desc_sizer->Add(hum_desc_sizer, 1, wxEXPAND, FromDIP(1));

    wxStaticLine* vert_separator = new wxStaticLine(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL);
    desc_sizer->Add(vert_separator, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(5));

    wxBoxSizer* temp_desc_sizer = create_description_item(parent, _L("Temperature"), m_temperature_data_label);
    desc_sizer->Add(temp_desc_sizer, 1, wxEXPAND, FromDIP(1));

    m_time_descrition_container = new wxBoxSizer(wxHORIZONTAL);
    wxStaticLine* vert_separator2 = new wxStaticLine(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL);
    m_time_descrition_container->Add(vert_separator2, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(5));

    wxBoxSizer* time_desc_sizer = create_description_item(parent, _L("Left Time"), m_time_data_label);
    m_time_descrition_container->Add(time_desc_sizer, 1, wxEXPAND, FromDIP(1));

    desc_sizer->Add(m_time_descrition_container, 1, wxEXPAND, 0);
    // m_time_descrition_container->Show(false);

    return desc_sizer;
}

wxBoxSizer* AMSDryCtrWin::create_left_panel(wxPanel* parent)
{
    wxBoxSizer* left_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* image_section = create_humidity_status_section(parent);
    left_sizer->Add(image_section, 1, wxEXPAND | wxALL, FromDIP(5));

    m_descriptions_section_sizer = create_status_descriptions_section(parent);
    left_sizer->Add(m_descriptions_section_sizer, 0, wxEXPAND | wxALL, FromDIP(20));

    return left_sizer;
}

Button* AMSDryCtrWin::create_button(wxPanel* parent, const wxString& title,
    const wxColour& background_color, const wxColour& border_color, const wxColour& text_color)
{
    Button* button = new Button(parent, title);

    // Create state colors for background
    StateColor bg_color(
        std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
        std::pair<wxColour, int>(background_color.ChangeLightness(80), StateColor::Pressed),
        std::pair<wxColour, int>(background_color.ChangeLightness(120), StateColor::Hovered),
        std::pair<wxColour, int>(background_color, StateColor::Normal)
    );

    // Create state colors for border
    StateColor bd_color(
        std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled),
        std::pair<wxColour, int>(border_color, StateColor::Enabled)
    );

    button->SetBackgroundColor(bg_color);
    button->SetBorderColor(bd_color);
    button->SetTextColor(text_color);
    button->SetFont(Label::Body_14);

    // Auto-size button based on text content with padding
    wxSize best_size = button->GetBestSize();
    int padding_width = FromDIP(4);
    int padding_height = FromDIP(2);
    wxSize final_size(best_size.GetWidth() + padding_width, best_size.GetHeight() + padding_height);
    button->SetMinSize(final_size);

    return button;
}

wxBoxSizer* AMSDryCtrWin::create_normal_state_panel(wxPanel* parent)
{
    wxBoxSizer* normal_state_sizer = new wxBoxSizer(wxVERTICAL);

    Label* description_label = new Label(parent, _L("Filament Drying Settings"));
    description_label->SetForegroundColour(*wxBLACK);
    description_label->SetFont(Label::Head_14);
    normal_state_sizer->Add(description_label, 0, wxALL, FromDIP(5));

    // Part 2: ComboBox for material selection
    wxBoxSizer* combo_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_trays_combo = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, FromDIP(26)), 0, nullptr, wxCB_READONLY);
    m_trays_combo->SetFont(Label::Body_14);

    combo_sizer->Add(m_trays_combo, 1, wxEXPAND);
    normal_state_sizer->Add(combo_sizer, 0, wxEXPAND | wxALL, FromDIP(5));

    m_trays_combo->Bind(wxEVT_COMBOBOX, &AMSDryCtrWin::OnFilamentSelectionChanged, this);

    // part 3 time and temperature
    // Use TextInput (self-drawn border/background) instead of a bare wxTextCtrl: on macOS the
    // native NSTextField background is turned off globally and its bezel is nearly invisible on
    // a white page, so a bare wxTextCtrl cannot be told apart from the background in light mode.
    // Temperature part
    wxBoxSizer* temp_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_temperature_box = new TextInput(parent, "", "", "", wxDefaultPosition, wxSize(FromDIP(80), FromDIP(26)));
    m_temperature_input = m_temperature_box->GetTextCtrl();
    m_temperature_input->SetMaxLength(3); // Limit to 3 digits

    m_temperature_input->Bind(wxEVT_CHAR, [this](wxKeyEvent& event) {
        int keycode = event.GetKeyCode();
        if (keycode >= '0' && keycode <= '9') {
            event.Skip();
        } else if (keycode == WXK_BACK || keycode == WXK_DELETE || keycode == WXK_LEFT || keycode == WXK_RIGHT) {
            event.Skip();
        }
    });

    m_temperature_input->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        m_next_button->Disable();
        m_start_button->Disable();
    });

    Label* temp_unit_label = new Label(parent, wxString::FromUTF8("℃"));
    temp_unit_label->SetForegroundColour(*wxBLACK);
    temp_sizer->Add(m_temperature_box, 1, wxRIGHT, FromDIP(1));
    temp_sizer->Add(temp_unit_label, 0, wxALIGN_CENTER_VERTICAL);

    // Time part
    wxBoxSizer* time_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_time_box = new TextInput(parent, "", "", "", wxDefaultPosition, wxSize(FromDIP(100), FromDIP(26)));
    m_time_input = m_time_box->GetTextCtrl();
    m_time_input->SetMaxLength(3); // Limit to 3 digits

    m_time_input->Bind(wxEVT_CHAR, [this](wxKeyEvent& event) {
        int keycode = event.GetKeyCode();
        if (keycode >= '0' && keycode <= '9') {
            event.Skip();
        } else if (keycode == WXK_BACK || keycode == WXK_DELETE || keycode == WXK_LEFT || keycode == WXK_RIGHT) {
            event.Skip();
        }
    });

    m_time_input->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        m_next_button->Disable();
        m_start_button->Disable();
    });

    Label* time_unit_label = new Label(parent, "H");
    time_unit_label->SetForegroundColour(*wxBLACK);
    time_sizer->Add(m_time_box, 1, wxRIGHT, FromDIP(1));
    time_sizer->Add(time_unit_label, 0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer* input_sizer = new wxBoxSizer(wxHORIZONTAL);
    input_sizer->Add(temp_sizer, 1, wxRIGHT, FromDIP(10));
    input_sizer->Add(time_sizer, 1, 0);
    normal_state_sizer->Add(input_sizer, 0, wxEXPAND | wxALL, FromDIP(5));

    // Part 4: Abnormal description/message area
    m_normal_description = new Label(parent, "");
    m_normal_description->SetForegroundColour(wxColour("#F09A17"));
    m_normal_description->SetFont(Label::Body_12);
    normal_state_sizer->Add(m_normal_description, 0, wxALL, FromDIP(5));

    // Part 5: Start button
    m_next_button = create_button(
        parent,
        _L("Start"),
        AMS_CONTROL_BRAND_COLOUR,  // Background color - green
        AMS_CONTROL_BRAND_COLOUR,  // Border color - green
        wxColour("#FFFFFE")        // Text color - white
    );

    m_next_button->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& event) {
        m_main_simplebook->SetSelection(1);
    });

    normal_state_sizer->Add(m_next_button, 0, wxALL, FromDIP(5));
    m_next_button->Disable();

    m_stop_button = create_button(
        parent,
        _L("Stop"),
        wxColour(255, 0, 0),       // Background color - red
        wxColour(255, 0, 0),       // Border color - red
        wxColour("#FFFFFE")        // Text color - white
    );

    m_stop_button->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& event) {
        auto fila_system = get_fila_system();
        if (!fila_system) {
            BOOST_LOG_TRIVIAL(info) << "AMSDryCtrWin::start_sending_drying_command: Invalid FilaSystem Pointer";
            return;
        }

        fila_system->CtrlAmsStopDrying(std::stoi(m_ams_info.m_ams_id));
        // Temporarily show stopping state, then restore after 2 seconds
        if (m_stop_button) {
            m_stop_button->SetLabel(_L("Stopping"));
            update_button_size(m_stop_button);  // Adjust button size for longer text
            m_stop_button->Disable();
            m_stop_button->Layout();
            m_stop_button->GetParent()->Layout();  // Relayout parent container
            m_stop_button->Refresh();

            m_stop_button_restore_deadline.reset();
            m_stop_button_restore_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        }
    });

    m_stop_button->Show(false);
    normal_state_sizer->Add(m_stop_button, 0, wxALL, FromDIP(5));

    return normal_state_sizer;
}

wxBoxSizer* AMSDryCtrWin::create_cannot_dry_panel(wxPanel* parent)
{
    wxBoxSizer* abnormal_sizer = new wxBoxSizer(wxVERTICAL);

    Label* description_label = new Label(parent, _L("Unable to dry temporarily due to ..."));
    description_label->SetForegroundColour(*wxBLACK);
    description_label->SetFont(Label::Head_16);
    description_label->Wrap(FromDIP(300));
    abnormal_sizer->Add(description_label, 0, wxALIGN_CENTER_VERTICAL, FromDIP(10));

    // Add a description label for the abnormal state
    m_cannot_dry_description_label = new Label(parent, (""));
    m_cannot_dry_description_label->SetForegroundColour(*wxBLACK);
    m_cannot_dry_description_label->SetFont(Label::Body_14);
    m_cannot_dry_description_label->Wrap(FromDIP(250)); // Wrap text to fit within panel

    abnormal_sizer->Add(m_cannot_dry_description_label, 0, wxALIGN_CENTER_VERTICAL, FromDIP(10));

    // Unload button shown only when ConsumableAtAmsOutlet reason is active
    m_unload_button = create_button(
        parent,
        _L("Unload"),
        AMS_CONTROL_BRAND_COLOUR,       // Background color - light gray
        AMS_CONTROL_BRAND_COLOUR,       // Border color - gray
        *wxWHITE                   // Text color - white
    );

    m_unload_button->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& event) {
        auto fila_system = get_fila_system();
        if (!fila_system) {
            BOOST_LOG_TRIVIAL(info) << "AMSDryCtrWin::unload_button: Invalid FilaSystem Pointer";
            return;
        }

        MachineObject* obj = fila_system->GetOwner();
        if (!obj) {
            BOOST_LOG_TRIVIAL(info) << "AMSDryCtrWin::unload_button: Invalid MachineObject Pointer";
            return;
        }

        obj->command_ams_change_filament(false, m_ams_info.m_ams_id, "255");

        m_unload_button->Disable();
        m_unload_button_restore_deadline.reset();
        m_unload_button_restore_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    });

    m_unload_button->Show(false);
    abnormal_sizer->Add(m_unload_button, 0, wxALL, FromDIP(5));

    return abnormal_sizer;
}

wxBoxSizer* AMSDryCtrWin::create_drying_error_panel(wxPanel* parent)
{
    wxBoxSizer* err_sizer = new wxBoxSizer(wxVERTICAL);
    Label* description_label = new Label(parent, _L("Drying Error"));
    description_label->SetForegroundColour(*wxRED);
    description_label->SetFont(Label::Body_14); // Wrap text to fit within panel
    err_sizer->Add(description_label, 0, wxALIGN_CENTER_VERTICAL, FromDIP(10));

    Label* additional_description_label = new Label(parent, _L("Please check the Assistant for troubleshooting"));
    additional_description_label->SetForegroundColour(*wxBLACK);
    additional_description_label->SetFont(Label::Body_14); // Wrap text to fit within panel
    err_sizer->Add(additional_description_label, 0, wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return err_sizer;
}

wxBoxSizer* AMSDryCtrWin::create_right_panel(wxPanel* parent)
{
    wxBoxSizer* right_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_normal_state_sizer = create_normal_state_panel(parent);
    m_cannot_dry_sizer = create_cannot_dry_panel(parent);
    m_dry_error_sizer = create_drying_error_panel(parent);

    right_sizer->Add(m_normal_state_sizer, 1, wxEXPAND);
    right_sizer->Add(m_cannot_dry_sizer, 1, wxEXPAND);
    right_sizer->Add(m_dry_error_sizer, 1, wxEXPAND);

    m_cannot_dry_sizer->Show(false);
    m_dry_error_sizer->Show(false);

    return right_sizer;
}

wxBoxSizer* AMSDryCtrWin::create_main_content_section(wxPanel* parent)
{
    wxBoxSizer* content_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* left_panel = create_left_panel(parent);
    content_sizer->Add(left_panel, 2, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    left_panel->SetMinSize(wxSize(FromDIP(250), -1));

    wxBoxSizer* right_panel = create_right_panel(parent);
    content_sizer->Add(right_panel, 1, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    right_panel->SetMinSize(wxSize(FromDIP(250), -1));

    return content_sizer;
}

wxBoxSizer* AMSDryCtrWin::create_main_page_sizer(wxPanel* parent)
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    m_amswin = new wxPanel(parent, wxID_ANY);
    m_amswin->SetBackgroundColour(*wxWHITE);
    m_sizer_ams_items = new wxBoxSizer(wxVERTICAL);
    m_amswin->SetSizer(m_sizer_ams_items);
    m_panel_prv_left = create_preview_scrolled_window(m_amswin);
    m_sizer_prv_left = new wxBoxSizer(wxHORIZONTAL);
    m_panel_prv_left->SetSizer(m_sizer_prv_left);
    m_sizer_ams_items->Add(m_panel_prv_left, 0, wxALIGN_LEFT | wxTOP | wxBOTTOM, FromDIP(4));
    main_sizer->Add(m_amswin, 0, wxEXPAND | wxALL, FromDIP(5));

    wxBoxSizer* content_section = create_main_content_section(parent);
    main_sizer->Add(content_section, 1, wxEXPAND | wxALL, FromDIP(5));

    return main_sizer;
}

wxBoxSizer* AMSDryCtrWin::create_guide_info_filament(wxPanel* parent)
{
    wxBoxSizer* filament_section = new wxBoxSizer(wxHORIZONTAL);

    m_ams_filament_panel = new AMSFilamentPanel(parent, "");

    filament_section->Add(m_ams_filament_panel, 0, wxEXPAND | wxALL | wxALIGN_CENTER_HORIZONTAL, FromDIP(5));

    return filament_section;
}

wxBoxSizer* AMSDryCtrWin::create_guide_info_section(wxPanel* parent)
{
    wxBoxSizer* info_section = new wxBoxSizer(wxVERTICAL);

    // Part 1: Points (no title, so the first line starts level with the image's top)
    m_guide_description_label = new Label(parent, "");
    m_guide_description_label->SetForegroundColour(*wxBLACK);
    m_guide_description_label->SetFont(Label::Body_14);
    add_guide_text(m_guide_description_label);
    info_section->Add(m_guide_description_label, 0, wxEXPAND | wxALL, FromDIP(5));

    // Part 2: filament panel
    wxBoxSizer* fila_section = create_guide_info_filament(parent);
    info_section->Add(fila_section, 0, wxEXPAND | wxALL, FromDIP(5));

    m_guide_note_label = new Label(parent, "*" + _L("Unknown filaments will be treated as PLA."));
    m_guide_note_label->SetForegroundColour(wxColour("#6B6B6B"));
    m_guide_note_label->SetFont(Label::Body_12);
    add_guide_text(m_guide_note_label);
    m_guide_note_label->Hide();
    info_section->Add(m_guide_note_label, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(5));

    // Part 3: Circular toggle with description
    wxBoxSizer* toggle_section = new wxBoxSizer(wxHORIZONTAL);

    m_rotate_spool_toggle = new wxCheckBox(parent, wxID_ANY, "");
    m_rotate_spool_toggle->SetValue(false);

    m_rotate_spool_toggle->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
        bool is_checked = event.IsChecked();
        // Add toggle behavior logic here
    });

    toggle_section->Add(m_rotate_spool_toggle, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    // Toggle description
    m_rotate_spool_toggle_description = new Label(parent, _L("Rotate spool when drying"));
    m_rotate_spool_toggle_description->SetForegroundColour(*wxBLACK);
    m_rotate_spool_toggle_description->SetFont(Label::Body_12);
    add_guide_text(m_rotate_spool_toggle_description, m_rotate_spool_toggle, 10);

    const wxString rotate_tip = _L("After turning on, the AMS automatically rotates slots where filament ends are stowed on the spool, for better drying");
    m_rotate_spool_toggle->SetToolTip(rotate_tip);
    m_rotate_spool_toggle_description->SetToolTip(rotate_tip);
    toggle_section->Add(m_rotate_spool_toggle_description, 1, wxALIGN_CENTER_VERTICAL, 0);

    info_section->Add(toggle_section, 0, wxEXPAND | wxALL, FromDIP(5));

    return info_section;
}

wxBoxSizer* AMSDryCtrWin::create_guide_error_section(wxPanel* parent)
{
    // Error icon + ThemeColor::Danger text
    m_guide_error_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_guide_error_bmp = ScalableBitmap(parent, "error", 16);
    m_guide_error_icon = new wxStaticBitmap(parent, wxID_ANY, m_guide_error_bmp.bmp());
    m_guide_error_sizer->Add(m_guide_error_icon, 0, wxTOP, FromDIP(2));
    m_guide_error_label = new Label(parent, "");
    m_guide_error_label->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::Danger));
    m_guide_error_label->SetFont(Label::Body_14);
    add_guide_text(m_guide_error_label, m_guide_error_icon, 4);
    m_guide_error_sizer->Add(m_guide_error_label, 1, wxLEFT, FromDIP(4));
    m_guide_error_sizer->ShowItems(false);

    return m_guide_error_sizer;
}

wxBoxSizer* AMSDryCtrWin::create_guide_image_section(wxPanel* parent)
{
    m_image_placeholder = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap);
    m_guide_image = ScalableBitmap(parent, "dev_ams_dry_ctr_filament_in_chamber", 256);
    m_image_placeholder->SetBitmap(m_guide_image.bmp());

    m_guide_image_sizer = new wxBoxSizer(wxVERTICAL);
    m_guide_image_sizer->Add(m_image_placeholder, 0, wxALIGN_RIGHT | wxALL, FromDIP(5));

    return m_guide_image_sizer;
}

wxBoxSizer* AMSDryCtrWin::create_guide_buttons_section(wxPanel* parent)
{
    wxBoxSizer* buttons_container = new wxBoxSizer(wxHORIZONTAL);
    buttons_container->AddStretchSpacer(1);

    m_back_button = create_button(
        parent,
        wxString::FromUTF8(_CTX_utf8(L_CONTEXT("Back", "amsdrying"), "amsdrying")),
        wxColour("#F8F8F8"),       // Background color - light gray
        wxColour("#D0D0D0"),       // Border color - gray
        *wxBLACK                   // Text color - black
    );

    m_back_button->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& event) {
        m_main_simplebook->SetSelection(0);
    });

    buttons_container->Add(m_back_button, 0, wxALL, FromDIP(5));

    m_start_button = create_button(
        parent,
        _L("Start"),
        AMS_CONTROL_BRAND_COLOUR,  // Background color - green
        AMS_CONTROL_BRAND_COLOUR,  // Border color - green
        wxColour("#FFFFFE")        // Text color - white
    );

    m_start_button->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& event) {
        m_main_simplebook->SetSelection(2);
        m_progress_gauge->SetValue(0);

        m_progress_timer->Start(70);
        m_progress_value = 0;
        start_sending_drying_command();
    });

    buttons_container->Add(m_start_button, 0, wxALL, FromDIP(5));

    m_guide_buttons_sizer = buttons_container;
    return buttons_container;
}

wxBoxSizer* AMSDryCtrWin::create_guide_page_sizer(wxPanel* parent)
{
    // 2x2 grid: info | image
    //           error | buttons
    // Only the left column and the top row stretch, so the error row stays level with the buttons.
    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 0, 0);
    grid->AddGrowableCol(0, 1);
    grid->AddGrowableRow(0, 1);

    grid->Add(create_guide_info_section(parent), 1, wxEXPAND | wxALL, FromDIP(10));
    grid->Add(create_guide_image_section(parent), 0, wxALIGN_RIGHT | wxALL, FromDIP(10));
    // 15dp horizontally so the error text lines up with the info labels (10dp cell + 5dp label border)
    grid->Add(create_guide_error_section(parent), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(15));
    grid->Add(create_guide_buttons_section(parent), 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(10));

    // Lift the bottom row off the page's bottom edge
    wxBoxSizer* guide_sizer = new wxBoxSizer(wxVERTICAL);
    guide_sizer->Add(grid, 1, wxEXPAND | wxBOTTOM, FromDIP(30));
    return guide_sizer;
}

void AMSDryCtrWin::add_guide_text(Label* label, wxWindow* leading, int gap_dip)
{
    m_guide_texts.push_back({label, leading, gap_dip});
}

int AMSDryCtrWin::guide_text_width() const
{
    // Right column = the wider of the image and buttons cells.
    // 20dp = book border, 40dp = both columns' 10dp cell borders, 10dp = the labels' 5dp side borders.
    const int right_w = std::max(m_guide_image_sizer->CalcMin().GetWidth(), m_guide_buttons_sizer->CalcMin().GetWidth());
    const int width = GetClientSize().GetWidth() - FromDIP(20) - right_w - FromDIP(40) - FromDIP(10);
    return width < FromDIP(100) ? FromDIP(300) : width;
}

void AMSDryCtrWin::wrap_guide_texts()
{
    const int width = guide_text_width();
    for (const GuideText& text : m_guide_texts) {
        int text_width = width;
        if (text.leading) {
            text_width -= text.leading->GetBestSize().GetWidth() + FromDIP(text.gap_dip);
        }
        text.label->Wrap(text_width);
    }
}

void AMSDryCtrWin::start_sending_drying_command()
{
    auto fila_system = get_fila_system();
    if (!fila_system) {
        BOOST_LOG_TRIVIAL(info) << "Invalid FilaSystem Pointer";
        return;
    }

    if (m_temperature_input->GetValue().IsEmpty() || m_time_input->GetValue().IsEmpty()) {
        // Show error message to user
        BOOST_LOG_TRIVIAL(info) << "Time or temperature is empty";
        return;
    }

    long temperature, time;
    if (!m_temperature_input->GetValue().ToLong(&temperature) ||
        !m_time_input->GetValue().ToLong(&time)) {
        BOOST_LOG_TRIVIAL(info) << "Failed to convert temperature or time";
        return;
    }

    int tray_index;
    tray_index = m_trays_combo->GetSelection();
    if (m_tray_ids.empty() || tray_index < 0 || tray_index >= m_tray_ids.size()) {
        BOOST_LOG_TRIVIAL(warning) << "Invalid tray_index " << tray_index
                                   << ", m_tray_ids.size=" << m_tray_ids.size();
        return;
    }

    int cooling_temp = 50;
    std::optional<DevFilamentDryingPreset> preset = DevUtilBackend::GetFilamentDryingPreset(m_tray_ids[tray_index].filament_id);
    if (preset.has_value()) {
        cooling_temp = static_cast<int>(preset.value().filament_dev_drying_softening_temperature[m_ams_info.m_model]);
    }

    bool rotate_spool = false;
    // Types without auto-retract cannot rotate the spool; never ask the
    // firmware to, even if the (hidden) toggle somehow retained a value.
    rotate_spool = m_can_auto_retract && m_rotate_spool_toggle->GetValue();

    fila_system->CtrlAmsStartDryingHour(std::stoi(m_ams_info.m_ams_id), m_ams_info.m_dry_zone_id.value_or(0), get_drying_filament_type_for_command(m_tray_ids[tray_index]),
        temperature, time, rotate_spool, cooling_temp, false);

    m_dry_setting.m_filament_names[m_ams_info.m_ams_id] = m_tray_ids[tray_index].filament_name;
    m_dry_setting.m_filament_type[m_ams_info.m_ams_id] = m_tray_ids[tray_index].filament_type;
    m_dry_setting.m_dry_temp[m_ams_info.m_ams_id] = temperature;
    m_dry_setting.m_dry_time[m_ams_info.m_ams_id] = time;
}

void AMSDryCtrWin::OnProgressTimer(wxTimerEvent& event)
{
    m_progress_value += 1;

    if (m_progress_value <= 100)
        m_progress_gauge->SetValue(m_progress_value);

    if (m_progress_value == 1) { // First tick, reset message index
        m_progress_message_index = 0;
        if (!m_progress_text.empty()) {
            m_progress_title->SetLabel(m_progress_text[0]);
        }
    }
    else if (m_progress_value % 15 == 0) { // Approximately every second
        m_progress_message_index++;
        if (m_progress_message_index < m_progress_text.size()) {
            m_progress_title->SetLabel(m_progress_text[m_progress_message_index]);
            m_progress_title->Refresh();
        }
    }

    if (m_progress_value >= 100 && !is_dry_ctr_idle()) {
        m_progress_value = 0;
        m_progress_timer->Stop();
        m_main_simplebook->SetSelection(0);
    }
}

void AMSDryCtrWin::OnRefreshTimer(wxTimerEvent&)
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!obj || !obj->GetFilaSystem()) {
        close_once();
        return;
    }
    update(obj->GetFilaSystem(), obj);
}

void AMSDryCtrWin::restore_stop_button_if_deadline_passed()
{
    if (m_stop_button_restore_deadline.has_value()) {
        if (std::chrono::steady_clock::now() >= m_stop_button_restore_deadline.value()) {
            if (m_stop_button) {
                m_stop_button->SetLabel(_L("Stop"));
                update_button_size(m_stop_button);  // Adjust button size back to original
                m_stop_button->Enable();
                m_stop_button->Layout();
                m_stop_button->GetParent()->Layout();
                m_stop_button->Refresh();
            }
            m_stop_button_restore_deadline.reset();
        }
    }
}

void AMSDryCtrWin::restore_unload_button_if_deadline_passed()
{
    if (m_unload_button_restore_deadline.has_value()) {
        if (std::chrono::steady_clock::now() >= m_unload_button_restore_deadline.value()) {
            if (m_unload_button) {
                m_unload_button->Enable();
            }
            m_unload_button_restore_deadline.reset();
        }
    }
}

void AMSDryCtrWin::update_button_size(Button* button)
{
    if (!button) return;

    // Reset MinSize first to avoid accumulation of size
    button->SetMinSize(wxSize(-1, -1));

    // Recalculate button size based on current text and DPI settings
    wxSize best_size = button->GetBestSize();
    int padding_width = FromDIP(4);
    int padding_height = FromDIP(2);
    wxSize final_size(best_size.GetWidth() + padding_width, best_size.GetHeight() + padding_height);
    button->SetMinSize(final_size);
}

void AMSDryCtrWin::OnShow(wxShowEvent& event)
{
    if (event.IsShown()) {
        wxGetApp().UpdateDlgDarkUI(this);
        msw_rescale();

        enableSelfRefresh(true);

        wxWeakRef<AMSDryCtrWin> weak_this(this);
        CallAfter([weak_this]() {
            if (!weak_this) {
                return;
            }
            auto* dev_mgr = wxGetApp().getDeviceManager();
            MachineObject* obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
            if (!obj || !obj->GetFilaSystem()) {
                weak_this->close_once();
                return;
            }
            weak_this->update(obj->GetFilaSystem(), obj);
        });
    } else {
        if (m_refresh_timer && m_refresh_timer->IsRunning()) {
            m_refresh_timer->Stop();
        }
    }
    event.Skip();
}

void AMSDryCtrWin::enableSelfRefresh(bool enable)
{
    if (!m_refresh_timer) return;
    if (enable) {
        if (!m_refresh_timer->IsRunning()) {
            m_refresh_timer->Start(1000);
        }
    } else {
        m_refresh_timer->Stop();
    }
}

void AMSDryCtrWin::close_once()
{
    if (m_is_closing) {
        return;
    }
    m_is_closing = true;
    Close();
}

void AMSDryCtrWin::OnClose(wxCloseEvent& event)
{
    if (m_refresh_timer && m_refresh_timer->IsRunning()) {
        m_refresh_timer->Stop();
    }

    if (m_progress_timer && m_progress_timer->IsRunning()) {
        m_progress_timer->Stop();
    }

    if (m_progress_gauge) {
        m_progress_gauge->SetValue(0);
    }

    m_progress_value = 0;
    m_progress_message_index = 0;

    if (m_main_simplebook) {
        m_main_simplebook->SetSelection(0);
    }

    if (m_progress_title && !m_progress_text.empty()) {
        m_progress_title->SetLabel(m_progress_text[0]);
    }

    // Clean up and restore stop button state
    if (m_stop_button) {
        m_stop_button->SetLabel(_L("Stop"));
        update_button_size(m_stop_button);  // Adjust button size back to original
        m_stop_button->Enable();
        m_stop_button->Layout();
        m_stop_button->GetParent()->Layout();
        m_stop_button->Refresh();
    }
    m_stop_button_restore_deadline.reset();

    // Clean up unload button state
    if (m_unload_button) {
        m_unload_button->Enable();
    }
    m_unload_button_restore_deadline.reset();

    event.Skip();
}

wxBoxSizer* AMSDryCtrWin::create_progress_page_sizer(wxPanel* parent)
{
    wxBoxSizer* progress_sizer = new wxBoxSizer(wxVERTICAL);

    m_progress_title = new Label(parent, m_progress_text[0]);
    m_progress_title->SetForegroundColour(*wxBLACK);
    m_progress_title->SetFont(Label::Body_16);

    progress_sizer->Add(0, 0, 1, wxEXPAND, 0); // Spacer
    progress_sizer->Add(m_progress_title, 0, wxALIGN_CENTER | wxALL, FromDIP(30));

    m_progress_gauge = new ProgressBar(parent, wxID_ANY, 100, wxDefaultPosition, wxSize(FromDIP(300), FromDIP(8)));
    m_progress_gauge->SetValue(0);
    m_progress_gauge->SetHeight(FromDIP(8));

    progress_sizer->Add(m_progress_gauge, 0, wxALIGN_CENTER | wxALL, FromDIP(10));
    progress_sizer->Add(0, 0, 1, wxEXPAND, 0);

    return progress_sizer;
}

void AMSDryCtrWin::create()
{
    // set title icon
    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    this->SetDoubleBuffered(true);
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetSize(wxSize(FromDIP(700), FromDIP(500)));
    SetMinSize(wxSize(FromDIP(700), FromDIP(500)));
    SetMaxSize(wxSize(FromDIP(700), FromDIP(500)));

    m_main_simplebook = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);

    // Create main page
    m_original_page = new wxPanel(m_main_simplebook, wxID_ANY);
    m_original_page->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* main_sizer = create_main_page_sizer(m_original_page);
    m_original_page->SetSizer(main_sizer);

    m_main_simplebook->AddPage(m_original_page, "Main Page");

    m_guide_page = new wxPanel(m_main_simplebook, wxID_ANY);
    m_guide_page->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* guide_sizer = create_guide_page_sizer(m_guide_page);
    m_guide_page->SetSizer(guide_sizer);
    m_main_simplebook->AddPage(m_guide_page, "Guide Page");
    wrap_guide_texts();

    // Create progress page
    m_progress_page = new wxPanel(m_main_simplebook, wxID_ANY);
    m_progress_page->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* progress_sizer = create_progress_page_sizer(m_progress_page);
    m_progress_page->SetSizer(progress_sizer);
    m_main_simplebook->AddPage(m_progress_page, "Progress Page");

    m_progress_timer = new wxTimer(this, wxID_ANY);
    Bind(wxEVT_TIMER, &AMSDryCtrWin::OnProgressTimer, this, m_progress_timer->GetId());

    m_refresh_timer = new wxTimer(this, wxID_ANY);
    Bind(wxEVT_TIMER, &AMSDryCtrWin::OnRefreshTimer, this, m_refresh_timer->GetId());

    Bind(wxEVT_CLOSE_WINDOW, &AMSDryCtrWin::OnClose, this);

    wxBoxSizer* top_level_sizer = new wxBoxSizer(wxVERTICAL);
    top_level_sizer->Add(m_main_simplebook, 1, wxEXPAND | wxALL, FromDIP(10));

    Bind(wxEVT_SHOW, &AMSDryCtrWin::OnShow, this);

    SetSizer(top_level_sizer);
    Layout();
    Refresh();
    wxGetApp().UpdateDlgDarkUI(this);
}

void AMSDryCtrWin::on_dpi_changed(const wxRect &suggested_rect)
{
    msw_rescale();
}

void AMSDryCtrWin::msw_rescale()
{
    if (!IsShown()) {
        return;
    }

    if (m_ams_filament_panel) {m_ams_filament_panel->msw_rescale();}
    if (m_trays_combo) {m_trays_combo->Rescale(); m_trays_combo->Layout(); m_trays_combo->SetFont(Label::Body_14), m_trays_combo->Refresh();}
    if (m_temperature_box) {m_temperature_box->Rescale();}
    if (m_time_box) {m_time_box->Rescale();}
    if (m_humidity_img && m_humidity_image.bmp().IsOk()) {m_humidity_image.msw_rescale(); m_humidity_img->SetBitmap(m_humidity_image.bmp());}
    if (m_image_placeholder && m_guide_image.bmp().IsOk()) {m_guide_image.msw_rescale();m_image_placeholder->SetBitmap(m_guide_image.bmp());}
    if (m_guide_error_icon) {m_guide_error_bmp.msw_rescale();m_guide_error_icon->SetBitmap(m_guide_error_bmp.bmp());}
    if (m_image_description_icon && m_description_icon_bitmap.bmp().IsOk()) {m_description_icon_bitmap.msw_rescale(); m_image_description_icon->SetBitmap(m_description_icon_bitmap.bmp());}

    // Rescale and update button sizes based on text content
    if (m_next_button) {
        m_next_button->Rescale();
        update_button_size(m_next_button);
        m_next_button->Layout();
        m_next_button->Refresh();
    }
    if (m_stop_button) {
        m_stop_button->Rescale();
        update_button_size(m_stop_button);
        m_stop_button->Layout();
        m_stop_button->Refresh();
    }
    if (m_start_button) {
        m_start_button->Rescale();
        update_button_size(m_start_button);
        m_start_button->Layout();
        m_start_button->Refresh();
    }
    if (m_back_button) {
        m_back_button->Rescale();
        update_button_size(m_back_button);
        m_back_button->Layout();
        m_back_button->Refresh();
    }
    if (m_unload_button) {
        m_unload_button->Rescale();
        update_button_size(m_unload_button);
        m_unload_button->Layout();
        m_unload_button->Refresh();
    }

    for (const auto& [ams_id, prv] : m_ams_preview_list) {
        if (prv) { prv->msw_rescale(); }
    }

    if (m_guide_page) {wrap_guide_texts(); m_guide_page->Layout();}
    if (m_original_page) {m_original_page->Layout();}

    Fit();
    Layout();
    Refresh();
}

void AMSDryCtrWin::set_ams_id(const std::string& ams_id)
{
    m_ams_info.m_ams_id = ams_id;
    m_is_ams_changed = true;
}

void AMSDryCtrWin::SwitchAms(const std::string& ams_id)
{
    if (ams_id == m_ams_info.m_ams_id) {
        return;
    }

    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!obj || !obj->GetFilaSystem()) {
        return;
    }

    wxWindowUpdateLocker no_updates(m_original_page);
    set_ams_id(ams_id);
    update(obj->GetFilaSystem(), obj);
}

void AMSDryCtrWin::reconcile_ams_previews(DevFilaSystem* fila_system, MachineObject* obj)
{
    if (!fila_system || !m_panel_prv_left || !m_sizer_prv_left) {
        return;
    }

    std::vector<AMSinfo> dryable_infos;
    for (const auto& [ams_id, dev_ams] : fila_system->GetAmsList()) {
        if (!dev_ams || !dev_ams->IsSupportRemoteDry(obj)) continue;
        AMSinfo info;
        if (!info.parse_ams_info(obj, dev_ams)) continue;
        dryable_infos.emplace_back(std::move(info));
    }

    // ZMULTI_A goes leftmost
    std::stable_partition(dryable_infos.begin(), dryable_infos.end(),
                          [](const AMSinfo& info) { return info.ams_type == DevAmsType::ZMULTI_A; });

    for (auto it = m_ams_preview_list.begin(); it != m_ams_preview_list.end();) {
        const std::string& id = it->first;
        bool still_present = false;
        for (const auto& info : dryable_infos) {
            if (info.ams_id == id) { still_present = true; break; }
        }
        if (!still_present) {
            m_sizer_prv_left->Detach(it->second);
            it->second->Destroy();
            it = m_ams_preview_list.erase(it);
        } else {
            ++it;
        }
    }

    for (auto& info : dryable_infos) {
        auto it = m_ams_preview_list.find(info.ams_id);
        if (it != m_ams_preview_list.end()) {
            it->second->Update(info);
        } else {
            AMSPreview* prv = new AMSPreview(m_panel_prv_left, wxID_ANY, info);
            prv->Bind(wxEVT_LEFT_DOWN, [this, prv](wxMouseEvent& e) {
                SwitchAms(prv->get_ams_id());
                e.Skip();
            });
            m_sizer_prv_left->Add(prv, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(6));
            m_ams_preview_list[info.ams_id] = prv;
        }
    }

    update_ams_preview_selection();
    fit_preview_strip_width();
}

void AMSDryCtrWin::fit_preview_strip_width()
{
    const int content_w = m_sizer_prv_left->CalcMin().x;
    const int strip_w   = std::min(content_w, AMS_ITEMS_PANEL_SIZE.x);
    const wxSize strip_size(strip_w, AMS_ITEMS_PANEL_SIZE.y);
    if (m_panel_prv_left->GetMinSize() != strip_size) {
        m_panel_prv_left->SetMinSize(strip_size);
        m_panel_prv_left->SetMaxSize(strip_size);
        if (m_amswin) m_amswin->Layout();
    }

    m_panel_prv_left->FitInside();
    m_panel_prv_left->Layout();
    m_panel_prv_left->Refresh();
}

void AMSDryCtrWin::update_ams_preview_selection()
{
    for (const auto& [ams_id, prv] : m_ams_preview_list) {
        if (!prv) continue;
        if (ams_id == m_ams_info.m_ams_id) {
            prv->OnSelected();
        } else {
            prv->UnSelected();
        }
    }
}

void AMSDryCtrWin::update_img_description(DevAms::DryStatus status, DevAms::DrySubStatus sub_status)
{
    if (status == DevAms::DryStatus::Off || status == DevAms::DryStatus::Cooling) {
        m_image_description->SetLabel(_L("Idle"));
        m_image_description_icon->Show(false);
        return;
    }

    // Determine label text for non-idle states
    wxString label_text;
    if (status == DevAms::DryStatus::Error) {
        label_text = _L("Drying");
    } else if (sub_status == DevAms::DrySubStatus::Heating) {
        label_text = _L("Drying-Heating");
    } else if (sub_status == DevAms::DrySubStatus::Dehumidify) {
        label_text = _L("Drying-Dehumidifying");
    } else {
        m_image_description_icon->Show(false);
        return;
    }

    m_image_description->SetLabel(label_text);

    try {
        m_description_icon_bitmap = ScalableBitmap(this, "dev_ams_dry_ctr_heating_icon", 20);
        m_description_icon_bitmap.msw_rescale();
        m_image_description_icon->SetBitmap(m_description_icon_bitmap.bmp());
        m_image_description_icon->Show(true);
    } catch (Exception& e) {
        BOOST_LOG_TRIVIAL(info) << "Error loading drying icon";
        m_image_description_icon->Show(false);
    }
}

int AMSDryCtrWin::update_image(DevAms* dev_ams)
{
    const DryView dry = get_dry_view(dev_ams);
    const DevAmsType model = dev_ams->GetAmsType();
    const DevAms::DryStatus status = dry.dry_status.value_or(DevAms::DryStatus::Off);
    const DevAms::DrySubStatus sub_status = dry.dry_sub_status.value_or(DevAms::DrySubStatus::Off);
    const DryImageLayout layout = is_multi_zone_ams(dev_ams) ? DryImageLayout::MultiZone : DryImageLayout::Generic;

    if (layout == m_image_layout && model == m_ams_info.m_model && status == m_ams_info.m_dry_status
        && sub_status == m_ams_info.m_dry_sub_status && dry.humidity_percent == m_ams_info.m_humidity_percent) {
        return 0;
    }
    m_image_layout = layout;

    return (layout == DryImageLayout::MultiZone) ? update_multi_zone_image(model, status, sub_status)
                                                 : update_generic_image(model, status, sub_status, dry.humidity_percent);
}

int AMSDryCtrWin::update_generic_image(DevAmsType model, DevAms::DryStatus status, DevAms::DrySubStatus sub_status, int humidity_percent)
{
    // Description/icon follow the status even when there is no image for it
    update_img_description(status, sub_status);

    const std::string img_path = get_generic_img_path(model, status, sub_status, humidity_percent);
    if (img_path.empty()) {
        return 0;
    }

    m_humidity_image = ScalableBitmap(this, img_path, AMS_DRY_STATUS_IMAGE_SIZE);
    const wxSize bmp_sz = m_humidity_image.bmp().GetSize();
    m_humidity_img->Show(true);
    m_humidity_img->SetBitmap(m_humidity_image.bmp());
    m_humidity_img->SetMinSize(bmp_sz);
    m_humidity_img->SetSize(bmp_sz);
    resize_image_overlay(bmp_sz);
    return 1;
}

int AMSDryCtrWin::update_multi_zone_image(DevAmsType model, DevAms::DryStatus status, DevAms::DrySubStatus sub_status)
{
    // The zone badges carry the status, so no description icon
    m_image_description_icon->Show(false);

    const std::string img_path = get_multi_zone_img_path(model, status, sub_status);
    if (img_path.empty()) {
        return 0;
    }

    m_humidity_image = ScalableBitmap(this, img_path, AMS_DRY_STATUS_IMAGE_SIZE_MULTI_ZONE);
    m_humidity_img->Show(false); // the overlay paints the bitmap itself, together with the badges
    resize_image_overlay(compute_zmulti_overlay_size());
    m_zmulti_image_overlay->Refresh();
    return 1;
}

void AMSDryCtrWin::resize_image_overlay(const wxSize& size)
{
    if (m_zmulti_image_overlay->GetMinSize() == size) {
        return;
    }
    m_zmulti_image_overlay->SetMinSize(size);
    m_zmulti_image_overlay->SetSize(size);
    if (m_original_page) m_original_page->Layout();
}

bool AMSDryCtrWin::check_values_changed(DevAms* dev_ams)
{
    bool changed = false;

    if (m_ams_info.m_model != dev_ams->GetAmsType()) {
        m_ams_info.m_model = dev_ams->GetAmsType();
        changed = true;
    }

    DryView v = get_dry_view(dev_ams);

    if (v.dry_status.has_value() && m_ams_info.m_dry_status != v.dry_status.value()) {
        m_ams_info.m_dry_status = v.dry_status.value();
        changed = true;
    }

    if (v.dry_sub_status.has_value() && m_ams_info.m_dry_sub_status != v.dry_sub_status.value()) {
        m_ams_info.m_dry_sub_status = v.dry_sub_status.value();
        changed = true;
    }

    if (m_ams_info.m_humidity_percent != v.humidity_percent) {
        m_ams_info.m_humidity_percent = v.humidity_percent;
        changed = true;
    }

    return changed;
}

void AMSDryCtrWin::update_normal_description(DevAms* dev_ams)
{
    if (m_tray_ids.empty() || m_trays_combo->GetSelection() >= m_tray_ids.size()) { return; }

    FilamentBaseInfo info = m_tray_ids[m_trays_combo->GetSelection()];
    wxString warning_text;
    bool can_enable_button = true;
    long temp_val = 0, time_val = 0;
    m_temperature_input->GetValue().ToLong(&temp_val);
    m_time_input->GetValue().ToLong(&time_val);
    std::optional<Slic3r::DevFilamentDryingPreset> preset = DevUtilBackend::GetFilamentDryingPreset(info.filament_id);
    auto total_dry = preset.has_value() ? preset.value().ams_limitations : std::unordered_set<DevAmsType>();

    struct AmsTempLimit { DevAmsType type; int min_temp; int max_temp; const char* name; };
    static const AmsTempLimit ams_limits[] = {
        { DevAmsType::N3F, 45, 65, "AMS2" },
        { DevAmsType::N3S, 45, 85, "AMS-S" },
        { DevAmsType::ZMULTI_A, 45, 85, "AMS 3H" }
    };

    for (const auto& lim : ams_limits) {
        if (dev_ams->GetAmsType() == lim.type) {
            if (temp_val > lim.max_temp) {
                wxString msg = wxString(lim.name) + _L(" maximum drying temperature is ") + wxString::Format(wxT("%d"), lim.max_temp) + wxString::FromUTF8("°C.");
                warning_text += msg + "\n";
                can_enable_button = false;
            } else if (temp_val < lim.min_temp) {
                wxString msg = wxString(lim.name) + _L(" minimum drying temperature is ") + wxString::Format(wxT("%d"), lim.min_temp) + wxString::FromUTF8("°C.");
                warning_text += msg + "\n";
                can_enable_button = false;
            }
            if (total_dry.find(lim.type) == total_dry.end()) {
                warning_text += _L("This filament may not be completely dried.") + "\n\n";
            }
            break;
        }
    }

    if (m_printer_status.m_is_printing && temp_val > m_ams_info.m_recommand_dry_temp) {
        warning_text += _L("This AMS is currently printing. To ensure print quality, the drying temperature cannot exceed the recommended drying temperature.")
            + wxString::Format(wxT(" (%d"), static_cast<int>(m_ams_info.m_recommand_dry_temp)) + wxString::FromUTF8("°C)") + "\n";
        can_enable_button = false;
    }

    if (time_val < AMS_DRY_MIN_HOUR) {
        warning_text += wxString::Format(_L("Minimum time value cannot be less than %d."), AMS_DRY_MIN_HOUR) + "\n";
        can_enable_button = false;
    } else if (time_val > AMS_DRY_MAX_HOUR) {
        warning_text += wxString::Format(_L("Maximum time value cannot be greater than %d."), AMS_DRY_MAX_HOUR) + "\n";
        can_enable_button = false;
    }

    m_next_button->Enable(can_enable_button);
    m_normal_description->SetLabel(warning_text);
    m_normal_description->Wrap(FromDIP(250));
    m_normal_description->GetParent()->Layout();
}

void AMSDryCtrWin::update_normal_state(DevAms* dev_ams)
{
    if (is_dry_ctr_idle(dev_ams)) {
        m_next_button->Show(true);
        m_normal_description->Show(true);
        m_trays_combo->Enable();
        m_temperature_input->SetEditable(true);
        m_time_input->SetEditable(true);

        m_stop_button->Hide();
        m_dry_error_sizer->Show(false);
    } else if (is_dry_ctr_err(dev_ams)) {
        m_dry_error_sizer->Show(true);
        m_next_button->Hide();
        m_normal_description->Hide();

        m_trays_combo->Hide();
        m_temperature_box->Hide();
        m_time_box->Hide();
        m_stop_button->Show(true);
    } else {
        m_next_button->Hide();
        m_normal_description->Hide();

        m_trays_combo->Disable();
        m_temperature_input->SetEditable(false);
        m_time_input->SetEditable(false);
        m_stop_button->Show(true);
        m_dry_error_sizer->Show(false);
    }

    if (m_temperature_input->GetValue().IsEmpty() || m_time_input->GetValue().IsEmpty()) {
        m_next_button->Disable();
        m_start_button->Disable();
    } else {
        m_next_button->Enable();
    }

    update_normal_description(dev_ams);
}

void AMSDryCtrWin::OnFilamentSelectionChanged(wxCommandEvent& event)
{
    int selectionIndex = event.GetSelection();
    wxString selectedFilament = event.GetString();

    auto fila_system = get_fila_system();
    if (!fila_system) {
        BOOST_LOG_TRIVIAL(info) << "Invalid FilaSystem Pointer";
        return;
    }

    DevAms* dev_ams = fila_system->GetAmsById(m_ams_info.m_ams_id);
    if (!dev_ams) {
        BOOST_LOG_TRIVIAL(info) << "Invalid AMS id";
        return;
    }

    std::optional<Slic3r::DevFilamentDryingPreset> preset = DevUtilBackend::GetFilamentDryingPreset(m_tray_ids[selectionIndex].filament_id);
    if (preset.has_value()) {
        DevFilamentDryingPreset info = preset.value();
        if (m_printer_status.m_is_printing) {
            m_temperature_input->SetValue(std::to_string(static_cast<int>(info.filament_dev_ams_drying_temperature_on_print[dev_ams->GetAmsType()])));
            m_time_input->SetValue(std::to_string(static_cast<int>(info.filament_dev_ams_drying_time_on_print[dev_ams->GetAmsType()])));
        } else {
            m_temperature_input->SetValue(std::to_string(static_cast<int>(info.filament_dev_ams_drying_temperature_on_idle[dev_ams->GetAmsType()])));
            m_time_input->SetValue(std::to_string(static_cast<int>(info.filament_dev_ams_drying_time_on_idle[dev_ams->GetAmsType()])));
        }
    }

    update_filament_guide_info(dev_ams);
}

wxString get_cannot_reason_text(DevAms::CannotDryReason reason)
{
    wxString cannot_reason_text;
    switch (reason)
    {
    case DevAms::CannotDryReason::InsufficientPower:
        cannot_reason_text = "*" + _L("Insufficient power") + "\n";
        cannot_reason_text += _L("  Too many AMS drying simultaneously. Please plug in the power or stop other drying processes before starting.") + "\n";
        break;
    case DevAms::CannotDryReason::AmsBusy:
        cannot_reason_text = "*" + _L("AMS is busy") + "\n";
        cannot_reason_text += _L("  AMS is calibrating | reading RFID | loading/unloading material, please wait.") + "\n";
        break;
    case DevAms::CannotDryReason::ConsumableAtAmsOutlet:
        cannot_reason_text = "*" + _L("Filament in AMS outlet") + "\n";
        cannot_reason_text += _L("  The high drying temperature may cause AMS blockage, please unload first.");
        break;
    case DevAms::CannotDryReason::InitiatingAmsDrying:
        cannot_reason_text = "*" + _L("Initiating AMS drying") + "\n";
        break;
    case DevAms::CannotDryReason::NotSupportedIn2dMode:
        cannot_reason_text = "*" + _L("Not supported in 2D mode") + "\n";
        break;
    case DevAms::CannotDryReason::DryingInProgress:
        cannot_reason_text = "*" + _L("Task in progress") + "\n";
        cannot_reason_text += _L("  The AMS might be in use during Task.") + "\n";
        break;
    case DevAms::CannotDryReason::Upgrading:
        cannot_reason_text = "*" + _L("Upgrading") + "\n";
        cannot_reason_text += _L("  Firmware update in progress, please wait...") + "\n";
        break;
    case DevAms::CannotDryReason::InsufficientPowerNeedPluginPower:
        cannot_reason_text = "*" + _L("Insufficient power") + "\n";
        cannot_reason_text += _L("  Please plug in the power and then use the drying function.") + "\n";
        break;
    case DevAms::CannotDryReason::FilamentAtAmsOutletManualUnload:
        cannot_reason_text = "*" + _L("Filament in AMS outlet") + "\n";
        cannot_reason_text += _L("  The high drying temperature may cause AMS blockage. Please unload the filament manually before proceeding.") + "\n";
        break;
    default:
        cannot_reason_text = "*" + _L("System is busy") + "\n";
        cannot_reason_text += _L("  Initiating other drying processes, please wait a few seconds...") + "\n";
        BOOST_LOG_TRIVIAL(info) << "unknown cannot dry reason";
        break;
    }
    return cannot_reason_text;
}

wxString organize_cannot_reasons_text(std::vector<DevAms::CannotDryReason>& reasons)
{
    wxString cannot_reasons_text;
    if (std::find(reasons.begin(), reasons.end(), DevAms::CannotDryReason::DryingInProgress) != reasons.end()) {
        cannot_reasons_text += get_cannot_reason_text(DevAms::CannotDryReason::DryingInProgress);
    } else if (std::find(reasons.begin(), reasons.end(), DevAms::CannotDryReason::AmsBusy) != reasons.end()) {
        cannot_reasons_text += get_cannot_reason_text(DevAms::CannotDryReason::AmsBusy);
    } else if (std::find(reasons.begin(), reasons.end(), DevAms::CannotDryReason::ConsumableAtAmsOutlet) != reasons.end()) {
        cannot_reasons_text += get_cannot_reason_text(DevAms::CannotDryReason::ConsumableAtAmsOutlet);
    } else if (std::find(reasons.begin(), reasons.end(), DevAms::CannotDryReason::FilamentAtAmsOutletManualUnload) != reasons.end()) {
        cannot_reasons_text += get_cannot_reason_text(DevAms::CannotDryReason::FilamentAtAmsOutletManualUnload);
    }

    if (std::find(reasons.begin(), reasons.end(), DevAms::CannotDryReason::InsufficientPower) != reasons.end()) {
        cannot_reasons_text += get_cannot_reason_text(DevAms::CannotDryReason::InsufficientPower);
    }

    for (auto reason : reasons) {
        if (reason == DevAms::CannotDryReason::InsufficientPowerNeedPluginPower) {
            cannot_reasons_text += get_cannot_reason_text(DevAms::CannotDryReason::InsufficientPowerNeedPluginPower);
        } else if (reason == DevAms::CannotDryReason::NotSupportedIn2dMode) {
            cannot_reasons_text += get_cannot_reason_text(DevAms::CannotDryReason::NotSupportedIn2dMode);
        } else if (reason == DevAms::CannotDryReason::InitiatingAmsDrying) {
            cannot_reasons_text += get_cannot_reason_text(DevAms::CannotDryReason::InitiatingAmsDrying);
        } else if (reason == DevAms::CannotDryReason::Upgrading) {
            cannot_reasons_text += get_cannot_reason_text(DevAms::CannotDryReason::Upgrading);
        }
    }

    if (cannot_reasons_text.empty()) {
        cannot_reasons_text += "*" + _L("System is busy") + "\n";
        cannot_reasons_text += _L("  Initiating other drying processes, please wait a few seconds...") + "\n";
    }

    return cannot_reasons_text;
}

int AMSDryCtrWin::update_state(DevAms* dev_ams)
{
    DryView v = get_dry_view(dev_ams);
    std::vector<DevAms::CannotDryReason> cannot_reasons = v.cannot_dry_reasons.has_value()?
        v.cannot_dry_reasons.value(): std::vector<DevAms::CannotDryReason>();

    if (cannot_reasons.size() == 0 ||
        (cannot_reasons.size() == 1 && cannot_reasons[0] == DevAms::CannotDryReason::DryingInProgress)) {
        m_normal_state_sizer->Show(true);
        m_cannot_dry_sizer->Show(false);
        m_unload_button->Show(false);
        update_normal_state(dev_ams);
    } else if (cannot_reasons.size() > 0) {
        m_normal_state_sizer->Show(false);
        m_cannot_dry_sizer->Show(true);

        m_cannot_dry_description_label->SetLabel(organize_cannot_reasons_text(cannot_reasons));
        m_cannot_dry_description_label->Wrap(FromDIP(300));

        // Show unload button when ConsumableAtAmsOutlet reason is present
        bool has_consumable_at_outlet = std::find(cannot_reasons.begin(), cannot_reasons.end(),
            DevAms::CannotDryReason::ConsumableAtAmsOutlet) != cannot_reasons.end();
        m_unload_button->Show(has_consumable_at_outlet);
    }

    restore_stop_button_if_deadline_passed();
    restore_unload_button_if_deadline_passed();

    return 1;
}

int AMSDryCtrWin::update_ams_change(DevAms* dev_ams)
{
    if (!is_ams_changed(dev_ams)) {
        return 0;
    }

    m_ams_info.m_ams_id = dev_ams->GetAmsId();
    if (dev_ams->GetAmsType() == DevAmsType::N3F) {
        m_temperature_input->SetHint("45-65" + wxString::FromUTF8("°C"));
    } else if (dev_ams->GetAmsType() == DevAmsType::N3S) {
        m_temperature_input->SetHint("45-85" + wxString::FromUTF8("°C"));
    } else if (dev_ams->GetAmsType() == DevAmsType::ZMULTI_A) {
        m_temperature_input->SetHint("45-85" + wxString::FromUTF8("°C"));
    }

    m_time_input->SetHint(wxString::Format("%d-%d h", AMS_DRY_MIN_HOUR, AMS_DRY_MAX_HOUR));

    return 0;
}

int AMSDryCtrWin::update_dryness_status(DevAms* dev_ams)
{
    int updated = 0;

    DryView dry = get_dry_view(dev_ams);
    AmbientView ambient = get_ambient_view(dev_ams);

    if (m_ams_info.m_humidity_percent != dry.humidity_percent) {
        updated += 1;
        m_ams_info.m_humidity_percent = dry.humidity_percent;
        m_humidity_data_label->SetLabel(std::to_string(m_ams_info.m_humidity_percent) + "%");
    }

    if (m_ams_info.m_temperature != dry.temperature) {
        updated += 1;
        m_ams_info.m_temperature = dry.temperature;
        m_temperature_data_label->SetLabel(std::to_string(m_ams_info.m_temperature) + wxString::FromUTF8("°C"));
    }

    if (m_ambient_humidity_percent != ambient.humidity_percent) {
        m_ambient_humidity_percent = ambient.humidity_percent;
        updated += 1;
    }
    if (m_ambient_temperature != ambient.temperature) {
        m_ambient_temperature = ambient.temperature;
        updated += 1;
    }

    const bool is_multi_zone = (m_image_layout == DryImageLayout::MultiZone);
    const bool idle = is_dry_ctr_idle(dev_ams);
    bool need_layout = false;

    const bool show_desc_section = !is_multi_zone || !idle;
    if (m_descriptions_section_sizer && m_last_desc_section_shown != (show_desc_section ? 1 : 0)) {
        m_last_desc_section_shown = show_desc_section ? 1 : 0;
        m_descriptions_section_sizer->ShowItems(show_desc_section);
        m_last_time_container_shown = -1;
        need_layout = true;
    }

    if (idle) {
        if (m_last_time_container_shown != 0) {
            m_last_time_container_shown = 0;
            m_time_descrition_container->Show(false);
            need_layout = true;
        }
    } else {
        if (m_last_time_container_shown != 1) {
            m_last_time_container_shown = 1;
            m_time_descrition_container->Show(true);
            need_layout = true;
        }

        if (m_ams_info.m_left_dry_time != dry.left_dry_time) {
            updated += 1;
            m_ams_info.m_left_dry_time = dry.left_dry_time;
            m_time_data_label->SetLabel(wxString::Format("%02d : %02d",
                m_ams_info.m_left_dry_time / 60, m_ams_info.m_left_dry_time % 60));
        }
    }

    int visible_now = is_multi_zone ? 1 : 0;
    if (m_last_multi_zone_layout != visible_now) {
        m_last_multi_zone_layout = visible_now;
        if (m_image_description) m_image_description->Show(!is_multi_zone);
        if (m_zmulti_m2y_pill) m_zmulti_m2y_pill->Show(is_multi_zone);
        need_layout = true;
    }
    if (need_layout && m_original_page) m_original_page->Layout();

    if (is_multi_zone && updated > 0 && m_zmulti_image_overlay) {
        m_zmulti_image_overlay->Refresh();
    }

    return updated;
}

bool AMSDryCtrWin::is_tray_changed(DevAms* dev_ams)
{
    return false;
}

void AMSDryCtrWin::update_filament_guide_info(DevAms* dev_ams)
{
    if (!IsShown()) {
        return;
    }

    m_guide_page->Freeze();
    m_ams_filament_panel->Clear();
    m_ams_filament_panel->SetAmsName(dev_ams->GetDisplayName());

    if (m_rotate_spool_toggle && m_rotate_spool_toggle_description) {
        m_rotate_spool_toggle->Show(m_can_auto_retract);
        m_rotate_spool_toggle_description->Show(m_can_auto_retract);
    }

    // Get the temperature input value
    long input_temp = 0;
    bool valid_temp = !m_temperature_input->GetValue().IsEmpty() &&
                      m_temperature_input->GetValue().ToLong(&input_temp);
    bool can_start = true;

    int slot_count = 0, empty_count = 0, total_count = 0;
    bool has_unknown = false;
    wxString dryable_slot_names;
    for (auto& tray_pair : dev_ams->GetTrays()) {
        if (!tray_pair.second) {
            continue;
        }

        int slot_id = 0;
        try { slot_id = std::stoi(tray_pair.first); } catch (...) { continue; }
        ++total_count;
        if (!dev_ams->IsDryableSlot(slot_id)) continue;

        ++slot_count;
        if (!dryable_slot_names.IsEmpty()) dryable_slot_names += ", ";
        dryable_slot_names += wxString::Format("%d", slot_id + 1);
        if (!tray_pair.second->is_exists) {
            m_ams_filament_panel->AddFilamentItem("/", "dev_ams_dry_ctr_enable");
            ++empty_count;
            continue;
        }
        auto preset_opt = tray_pair.second->get_ams_drying_preset();
        wxString filament_type = tray_pair.second->get_display_filament_type();
        DevFilamentDryingPreset preset;
        if (filament_type.IsEmpty()) {
            auto fallback_preset = DevUtilBackend::GetFilamentDryingPreset("GFA00");
            preset = fallback_preset.value();
            filament_type = "?";
            has_unknown = true;
        } else if (preset_opt.has_value()) {
            preset = preset_opt.value();
        } else {
            auto fallback_preset = DevUtilBackend::GetFilamentDryingPreset("GFA00");
            preset = fallback_preset.value();
        }
        std::string icon_path = "dev_ams_dry_ctr_enable";
        int distortion_temp = static_cast<int>(preset.filament_dev_ams_drying_heat_distortion_temperature[dev_ams->GetAmsType()]);
        if (valid_temp && distortion_temp < input_temp) {
            icon_path = "dev_ams_dry_ctr_disable";
            can_start = false;
        }
        m_ams_filament_panel->AddFilamentItem(filament_type, icon_path);
    }
    if (slot_count == 0) {
        can_start = false;
    }

    const wxString description = build_guide_points(slot_count, total_count, dryable_slot_names);

    wxString error;
    if (!can_start) {
        error = _L("The current drying temperature is higher than the softening temperature of some filaments. Filament ends left in the inlet may soften and clog it. Pull out the filaments marked with an exclamation mark before drying.");
    }
    m_guide_error_label->SetLabel(error);
    m_guide_error_sizer->ShowItems(!error.IsEmpty());
    m_guide_description_label->SetLabel(description);
    m_guide_description_label->Show(!description.IsEmpty());
    m_guide_note_label->Show(has_unknown);

    wrap_guide_texts();

    m_start_button->Enable(can_start);
    m_guide_page->Layout();
    m_guide_page->Thaw();
}

wxString AMSDryCtrWin::build_guide_points(int dryable_count, int total_count, const wxString& dryable_slot_names) const
{
    std::vector<wxString> points;
    // Rack AMS only dries part of its slots; say which ones so the panel isn't mistaken for the whole AMS.
    if (dryable_count > 0 && dryable_count < total_count) {
        points.push_back(_L("Slots that currently support drying:") + " " + dryable_slot_names);
    }
    points.push_back(_L("It is recommended to pull the filament end out of the inlet before drying to prevent it from softening due to high temperature."));
    if (m_can_auto_retract) {
        points.push_back(_L("For better drying results, pull the filament end out of the inlet and allow it to rotate."));
    }

    wxString text;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i > 0) text += "\n";
        text += (points.size() > 1) ? wxString(L"• ") + points[i] : points[i];
    }
    return text;
}

int AMSDryCtrWin::update_filament_list(DevAms* dev_ams, MachineObject* obj)
{
    const bool ams_changed = is_ams_changed(dev_ams);
    bool rebuilt_filament_list = false;

    // Build `m_tray_ids` and the combobox items from the preset bundle.
    // This list is also used as a lookup table in non-idle state (type -> name),
    // so it must be available even when the combobox is disabled.
    auto rebuild_filament_list = [&]() -> bool {
        m_trays_combo->Clear();
        m_tray_ids.clear();

        PresetBundle *preset_bundle = wxGetApp().preset_bundle;
        if (!preset_bundle || !obj) {
            return false;
        }

        std::set<std::string> filament_id_set;
        auto & filaments = preset_bundle->filaments;

        // Get nozzle diameter using the same method as AMSMaterialsSetting::Popup
        std::ostringstream stream;
        int extruder_id = obj->GetFilaSystem()->GetExtruderIdByAmsId(m_ams_info.m_ams_id);
        if (!obj->GetExtderSystem()->GetExtderById(extruder_id)) {
            BOOST_LOG_TRIVIAL(info) << "get extruder id failed";
            extruder_id = 0;
        }
        stream << std::fixed << std::setprecision(1) << obj->GetExtderSystem()->GetNozzleDiameter(extruder_id);
        std::string nozzle_diameter_str = stream.str();
        std::set<std::string> printer_names = preset_bundle->get_printer_names_by_printer_type_and_nozzle(
            DevPrinterConfigUtil::get_printer_display_name(obj->printer_type), nozzle_diameter_str);

        for (auto filament_it = filaments.begin(); filament_it != filaments.end(); ++filament_it) {
            Preset& preset = *filament_it;
            // Filter by system preset: root preset and (system preset or user preset is supported)
            if (filaments.get_preset_base(*filament_it) != &preset || (!filament_it->is_system && !obj->is_support_user_preset)) {
                continue;
            }

            ConfigOption *       printer_opt  = filament_it->config.option("compatible_printers");
            ConfigOptionStrings *printer_strs = dynamic_cast<ConfigOptionStrings *>(printer_opt);
            if (!printer_strs) continue;

            for (auto printer_str : printer_strs->values) {
                if (printer_names.find(printer_str) != printer_names.end()) {
                    if (filament_id_set.find(filament_it->filament_id) != filament_id_set.end()) {
                        continue;
                    }

                    filament_id_set.insert(filament_it->filament_id);
                    auto filament_alias = filaments.get_preset_alias(*filament_it, true);
                    if (!filament_alias.empty()) {
                        auto opt_info = preset_bundle->get_filament_by_filament_id(filament_it->filament_id);
                        if (opt_info.has_value()) {
                            auto real_info = opt_info.value();
                            real_info.filament_name = filament_alias;
                            m_tray_ids.push_back(std::move(real_info));
                            m_trays_combo->Append(wxString::FromUTF8(filament_alias));
                        }
                    }
                }
            }
        }

        if (m_tray_ids.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "AMSDryCtrWin::update_filament_list: No valid filaments found";
            return false;
        }

        return true;
    };

    if (ams_changed || m_tray_ids.empty() || m_trays_combo->GetCount() == 0) {
        // Disable start button during rebuild
        if (m_start_button) {
            m_start_button->Disable();
        }

        rebuilt_filament_list = rebuild_filament_list();
    }

    if (!is_dry_ctr_idle(dev_ams)) {
        const std::string& ams_id = dev_ams->GetAmsId();
        const auto settings_opt = get_dry_view(dev_ams).dry_settings;

        // Set the combobox display label by the filament "type" string.
        // Priority: last saved selection for this AMS (if the type matches) -> fallback to current `m_tray_ids`.
        auto set_filament_label_by_type = [&](const std::string& filament_type) {
            if (filament_type.empty()) {
                return;
            }

            auto it_name = m_dry_setting.m_filament_names.find(ams_id);
            auto it_type = m_dry_setting.m_filament_type.find(ams_id);
            if (it_name != m_dry_setting.m_filament_names.end() && it_type != m_dry_setting.m_filament_type.end()
                && it_type->second == filament_type) {
                m_trays_combo->SetLabel(wxString::FromUTF8(it_name->second));
                return;
            }

            const auto it = std::find_if(m_tray_ids.begin(), m_tray_ids.end(), [&](const FilamentBaseInfo& tray) {
                return tray.filament_type == filament_type;
            });
            if (it != m_tray_ids.end()) {
                m_trays_combo->SetLabel(wxString::FromUTF8(it->filament_name));
            }
        };

        if (settings_opt.has_value()) {
            const auto& settings = settings_opt.value();
            m_temperature_input->SetValue(std::to_string(settings.dry_temp));
            m_time_input->SetValue(std::to_string(settings.dry_hour));
            if (!settings.dry_filament.empty()) {
                m_trays_combo->SetLabel(wxString::FromUTF8(settings.dry_filament));
            } else {
                BOOST_LOG_TRIVIAL(info)
                    << "dry_settings.dry_filament is empty, fallback to local filament list";
                set_filament_label_by_type(settings.dry_filament);
            }
        } else {
            auto it_name = m_dry_setting.m_filament_names.find(ams_id);
            if (it_name != m_dry_setting.m_filament_names.end()) {
                m_trays_combo->SetLabel(wxString::FromUTF8(it_name->second));
                m_temperature_input->SetValue(std::to_string(m_dry_setting.m_dry_temp[ams_id]));
                m_time_input->SetValue(std::to_string(m_dry_setting.m_dry_time[ams_id]));
            }
        }

        return 0;
    }

    if (!rebuilt_filament_list && !ams_changed && !m_printer_status.m_need_refresh_recommend && is_dry_ctr_idle()) {
        return 0;
    }

    if (m_tray_ids.empty()) {
        return 0;
    }

    // Select recommended drying temperature and default filament
    float min_dry_temp = std::numeric_limits<float>::max();
    std::string default_filament_id = "GFA00";
    bool has_ready = false;
    const auto fallback_preset = DevUtilBackend::GetFilamentDryingPreset("GFA00");
    for (const auto& tray_pair : dev_ams->GetTrays()) {
        if (!tray_pair.second || !tray_pair.second->is_exists) continue;

        int slot_id = 0;
        try { slot_id = std::stoi(tray_pair.first); } catch (...) { continue; }
        if (!dev_ams->IsDryableSlot(slot_id)) continue;

        has_ready = true;
        wxString filament_type = tray_pair.second->get_display_filament_type();
        Slic3r::DevFilamentDryingPreset preset;
        if (filament_type.IsEmpty()) {
            // no filament type, is PLA
            if (!fallback_preset) continue;
            preset = fallback_preset.value();
        } else {
            auto preset_opt = tray_pair.second->get_ams_drying_preset();
            if (preset_opt.has_value()) {
                preset = preset_opt.value();
            } else {
                // no preset，is PLA
                if (!fallback_preset) continue;
                preset = fallback_preset.value();
            }
        }
        float cur_temp = m_printer_status.m_is_printing
            ? std::min({preset.filament_dev_ams_drying_temperature_on_print[dev_ams->GetAmsType()],
                preset.filament_dev_drying_softening_temperature[dev_ams->GetAmsType()], preset.filament_dev_ams_drying_heat_distortion_temperature[dev_ams->GetAmsType()]})
            : preset.filament_dev_ams_drying_temperature_on_idle[dev_ams->GetAmsType()];
        if (cur_temp < min_dry_temp) {
            min_dry_temp = cur_temp;
            default_filament_id = preset.filament_id;
        }
    }
    // if no tray is ready, use PLA as default
    if (!has_ready) {
        if (fallback_preset.has_value()) {
            Slic3r::DevFilamentDryingPreset preset = fallback_preset.value();
            float cur_temp = m_printer_status.m_is_printing
                ? std::min({preset.filament_dev_ams_drying_temperature_on_print[dev_ams->GetAmsType()],
                    preset.filament_dev_drying_softening_temperature[dev_ams->GetAmsType()], preset.filament_dev_ams_drying_heat_distortion_temperature[dev_ams->GetAmsType()]})
                : preset.filament_dev_ams_drying_temperature_on_idle[dev_ams->GetAmsType()];
            min_dry_temp = cur_temp;
            default_filament_id = preset.filament_id;
        }
    }
    m_ams_info.m_recommand_dry_temp = min_dry_temp;
    m_printer_status.m_need_refresh_recommend = false;

    // Set default selection
    unsigned int default_index = 0;
    for (unsigned int i = 0; i < m_tray_ids.size(); i++) {
        if (m_tray_ids[i].filament_id == default_filament_id) {
            default_index = i;
            break;
        }
    }
    m_trays_combo->SetSelection(default_index);
    wxCommandEvent evt(wxEVT_COMBOBOX, m_trays_combo->GetId());
    evt.SetInt(default_index);
    OnFilamentSelectionChanged(evt);
    return 1;
}

std::shared_ptr<DevFilaSystem> AMSDryCtrWin::get_fila_system() const
{
    std::shared_ptr<DevFilaSystem> fila_system = m_fila_system.lock();
    if (!fila_system) {
        const_cast<AMSDryCtrWin*>(this)->close_once();
    }
    return fila_system;
}

void AMSDryCtrWin::update_dry_zone(DevAms* dev_ams)
{
    const std::optional<int> zone_id = resolve_dry_zone_id(dev_ams);
    if (m_ams_info.m_dry_zone_id != zone_id) {
        BOOST_LOG_TRIVIAL(info) << "dry zone changed " << KV(log_key::ams_id, dev_ams->GetAmsId())
                                << " zone=" << (zone_id ? std::to_string(*zone_id) : "none")
                                << " dryable_zones=" << dev_ams->GetDryableZoneIds().size();
        m_ams_info.m_dry_zone_id = zone_id;
    }
}

void AMSDryCtrWin::update_printer_state(MachineObject* obj)
{
    const bool is_printing = obj->GetFilaSystem() && obj->GetFilaSystem()->IsDryAreaInPrintUse(m_ams_info.m_ams_id, m_ams_info.m_dry_zone_id);
    if (m_printer_status.m_is_printing != is_printing) {
        BOOST_LOG_TRIVIAL(info) << "dry area print use changed " << KV(log_key::ams_id, m_ams_info.m_ams_id) << " in_use=" << is_printing;
        m_printer_status.m_is_printing = is_printing;
        m_printer_status.m_need_refresh_recommend = true;
    }
}

void AMSDryCtrWin::update(std::shared_ptr<DevFilaSystem> fila_system, MachineObject* obj)
{
    if (m_is_closing) {
        return;
    }
    if (!fila_system || !obj) {
        return;
    }

    m_fila_system = fila_system;

    DevAms* dev_ams = fila_system->GetAmsById(m_ams_info.m_ams_id);
    if (!dev_ams) {
        BOOST_LOG_TRIVIAL(info) << "Invalid AMS id";
        m_ams_info.m_ams_id = "";
        close_once();
        return;
    }

    update_dry_zone(dev_ams);
    update_printer_state(obj);

    if (!dev_ams->IsSupportRemoteDry(fila_system->GetOwner())) {
        BOOST_LOG_TRIVIAL(info) << "Selected AMS does not support remote drying";
        close_once();
        return;
    }

    m_can_auto_retract = devAmsTypeUtil::CanAutoRetract(dev_ams->GetAmsType());

    reconcile_ams_previews(fila_system.get(), obj);

    update_ams_change(dev_ams);

    update_image(dev_ams);

    update_state(dev_ams);
    update_dryness_status(dev_ams);

    update_filament_list(dev_ams, obj);
    update_filament_guide_info(dev_ams);

    m_is_ams_changed = false;

    check_values_changed(dev_ams);

    // Dialog Layout() won't relayout the page when its size is unchanged
    if (m_original_page) m_original_page->Layout();
    Layout();
    Refresh();
}

bool AMSDryCtrWin::is_ams_changed(DevAms* dev_ams)
{
    return m_ams_info.m_ams_id != dev_ams->GetAmsId() || m_is_ams_changed;
}

AMSDryCtrWin::DryView AMSDryCtrWin::get_dry_view(DevAms* dev_ams) const
{
    DryView v;
    if (!dev_ams) return v;

    if (const auto zone_id = resolve_dry_zone_id(dev_ams)) {
        auto z = dev_ams->GetZone(*zone_id);
        if (z.has_value()) {
            v.dry_status        = z->dry_status;
            v.dry_sub_status    = z->dry_sub_status;
            v.humidity_percent  = z->humidity_percent;
            v.temperature       = static_cast<int>(z->current_temperature);
            v.left_dry_time     = z->left_dry_time;
            v.dry_settings      = z->dry_settings;
            v.cannot_dry_reasons = z->dry_cannot_reasons;
            return v;
        }
    }

    v.dry_status        = dev_ams->GetDryStatus();
    v.dry_sub_status    = dev_ams->GetDrySubStatus();
    v.humidity_percent  = dev_ams->GetHumidityPercent();
    v.temperature       = static_cast<int>(dev_ams->GetCurrentTemperature());
    v.left_dry_time     = dev_ams->GetLeftDryTime();
    v.dry_settings      = dev_ams->GetDrySettings();
    v.cannot_dry_reasons = dev_ams->GetCannotDryReason();
    return v;
}

AMSDryCtrWin::AmbientView AMSDryCtrWin::get_ambient_view(DevAms* dev_ams) const
{
    AmbientView v;
    if (!dev_ams) return v;

    if (!dev_ams->GetZones().empty()) {
        // Average humidity/temperature across all non-dryable zones. If the
        // unit only has dryable zones, fall through to legacy unit-wide values.
        long long temp_sum = 0;
        long long hum_sum = 0;
        int        count = 0;
        for (const auto& [zone_id, zone] : dev_ams->GetZones()) {
            if (zone.SupportDrying()) continue;
            temp_sum += static_cast<long long>(zone.current_temperature);
            hum_sum  += zone.humidity_percent;
            ++count;
        }
        if (count > 0) {
            v.temperature      = static_cast<int>(temp_sum / count);
            v.humidity_percent = static_cast<int>(hum_sum / count);
            return v;
        }
    }

    v.humidity_percent = dev_ams->GetHumidityPercent();
    v.temperature      = static_cast<int>(dev_ams->GetCurrentTemperature());
    return v;
}

bool AMSDryCtrWin::is_dry_status_changed(DevAms* dev_ams)
{
    DryView v = get_dry_view(dev_ams);
    if (!v.dry_status.has_value() || !v.dry_sub_status.has_value()) {
        return true;
    }
    return v.dry_status.value() != m_ams_info.m_dry_status
        || v.dry_sub_status.value() != m_ams_info.m_dry_sub_status;
}

bool AMSDryCtrWin::is_dry_ctr_idle(DevAms* dev_ams)
{
    DryView v = get_dry_view(dev_ams);
    if (!v.dry_status.has_value() || !v.dry_sub_status.has_value()) {
        return true;
    }
    return v.dry_status.value() == DevAms::DryStatus::Off
        || v.dry_status.value() == DevAms::DryStatus::Cooling;
}

bool AMSDryCtrWin::is_dry_ctr_idle()
{
    return m_ams_info.m_dry_status == DevAms::DryStatus::Off || m_ams_info.m_dry_status == DevAms::DryStatus::Cooling;
}

bool AMSDryCtrWin::is_dry_ctr_err(DevAms* dev_ams)
{
    DryView v = get_dry_view(dev_ams);
    if (!v.dry_status.has_value() || !v.dry_sub_status.has_value()) {
        return false;
    }
    return v.dry_status.value() == DevAms::DryStatus::Error;
}

} // GUI
} // Slic3r
