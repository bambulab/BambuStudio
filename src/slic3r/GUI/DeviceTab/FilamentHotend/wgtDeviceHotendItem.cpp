#include "wgtDeviceHotendItem.h"

#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <wx/sizer.h>
#include <wx/valtext.h>

#define WX_DIP_SIZE_46 wxSize(FromDIP(46), FromDIP(46))
#define WX_DIP_SIZE(x, y) wxSize(FromDIP(x), FromDIP(y))
#define WGT_HOTEND_ITEM_SIZE WX_DIP_SIZE(88, 146)

namespace Slic3r::GUI {

// Copied from wgtDeviceNozzleRack.cpp (static internal function, not accessible across TUs)
// Replace yellow-ish pixels (r>=180, g>=180, b<=150) in wxBitmap with the given color
static wxBitmap SetNozzleBmpColor(const wxBitmap &bmp, const std::string &color_str)
{
    if (color_str.empty()) return bmp;

    wxImage  img = bmp.ConvertToImage();
    wxColour color("#" + color_str);

    for (int y = 0; y < img.GetHeight(); ++y) {
        for (int x = 0; x < img.GetWidth(); ++x) {
            unsigned char r = img.GetRed(x, y);
            unsigned char g = img.GetGreen(x, y);
            unsigned char b = img.GetBlue(x, y);

            if (r >= 180 && g >= 180 && b <= 150) { img.SetRGB(x, y, color.Red(), color.Green(), color.Blue()); }
        }
    }

    return wxBitmap(img, -1, bmp.GetScaleFactor());
}

wgtDeviceHotendItem::wgtDeviceHotendItem(wxWindow *parent, int hotend_id) : StaticBox(parent, wxID_ANY), m_hotend_id(hotend_id) { CreateGui(); }

void wgtDeviceHotendItem::CreateGui()
{
    SetCornerRadius(FromDIP(5));
    SetBackgroundColor(*wxWHITE);

    // ── Index circle badge ────────────────────────────────────
    m_nozzle_label_id = new wgtCircleItem(this, wxID_ANY);
    m_nozzle_label_id->SetValue(m_hotend_id);

    // ── Nozzle icon (centered, 20 DIP from top) ──────────────
    m_status             = NOZZLE_STATUS::NOZZLE_EMPTY;
    m_nozzle_empty_image = new ScalableBitmap(this, "dev_rack_nozzle_empty", 46);
    m_nozzle_icon        = new wxStaticBitmap(this, wxID_ANY, m_nozzle_empty_image->bmp(), wxDefaultPosition, WX_DIP_SIZE_46);
    m_nozzle_icon->SetBackgroundColour(*wxWHITE);

    // ── Nozzle diameter + flow type (centered) ────────────────
    wxSizer *label_h_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_nozzle_label_1       = new Label(this);
    m_nozzle_label_1->SetFont(Label::Body_12);
    m_nozzle_label_1->SetBackgroundColour(*wxWHITE);
    m_nozzle_label_1->SetLabel(_L("Empty"));
    label_h_sizer->Add(m_nozzle_label_1, 0, wxALIGN_LEFT);

    auto status_icon_bmp = create_scaled_bitmap("dev_rack_nozzle_error_icon", this, 14);
    m_nozzle_status_icon = new wxStaticBitmap(this, wxID_ANY, status_icon_bmp, wxDefaultPosition, WX_DIP_SIZE(14, 14));
    m_nozzle_status_icon->Bind(wxEVT_LEFT_DOWN, &wgtDeviceHotendItem::OnBtnNozzleStatus, this);
    m_nozzle_status_icon->Bind(wxEVT_ENTER_WINDOW, [this](auto &) { SetCursor(wxCURSOR_HAND); });
    m_nozzle_status_icon->Bind(wxEVT_LEAVE_WINDOW, [this](auto &) { SetCursor(wxCURSOR_ARROW); });
    m_nozzle_status_icon->SetBackgroundColour(*wxWHITE);
    m_nozzle_status_icon->Show(false);
    label_h_sizer->Add(m_nozzle_status_icon, 0, wxALIGN_CENTER | wxLEFT, FromDIP(2));

    m_nozzle_label_2 = new Label(this);
    m_nozzle_label_2->SetFont(Label::Body_12);
    m_nozzle_label_2->SetBackgroundColour(*wxWHITE);

    // ── bottom：temperature edit area  cur / target ℃ ──────────────────
    wxBoxSizer *temp_h = new wxBoxSizer(wxHORIZONTAL);

    m_label_cur_temp = new Label(this);
    m_label_cur_temp->SetFont(Label::Body_12);
    m_label_cur_temp->SetBackgroundColour(*wxWHITE);
    m_label_cur_temp->SetLabel("--");

    m_label_sep = new Label(this);
    m_label_sep->SetFont(Label::Body_12);
    m_label_sep->SetBackgroundColour(*wxWHITE);
    m_label_sep->SetLabel("/");

    m_text_target_temp = new wxTextCtrl(this, wxID_ANY, "--", wxDefaultPosition,
        WX_DIP_SIZE(28, -1),
        wxTE_PROCESS_ENTER | wxBORDER_NONE | wxTE_CENTRE);
    m_text_target_temp->SetFont(Label::Body_12);
    m_text_target_temp->SetMaxLength(3);
    m_text_target_temp->SetBackgroundColour(*wxWHITE);
    m_text_target_temp->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

    m_text_target_temp->Bind(wxEVT_TEXT_ENTER, &wgtDeviceHotendItem::OnTempTextEnter, this);
    m_text_target_temp->Bind(wxEVT_KILL_FOCUS, &wgtDeviceHotendItem::OnTempKillFocus, this);
    m_text_target_temp->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) {
        e.Skip();
        m_temp_editing = true;
        if (m_text_target_temp->GetValue() == "--")
            m_text_target_temp->SetValue(wxEmptyString);
    });

    m_label_degree = new Label(this);
    m_label_degree->SetFont(Label::Body_12);
    m_label_degree->SetBackgroundColour(*wxWHITE);
    m_label_degree->SetLabel(wxString::FromUTF8("°C")); // °C

    temp_h->Add(m_label_cur_temp, 0, wxALIGN_CENTER_VERTICAL);
    temp_h->Add(m_label_sep, 0, wxALIGN_CENTER_VERTICAL);
    temp_h->Add(m_text_target_temp, 0, wxALIGN_CENTER_VERTICAL);
    temp_h->Add(m_label_degree, 0, wxALIGN_CENTER_VERTICAL);

    // ── Top row: badge (top-left) + nozzle icon (centered, 20 DIP from top)
    // Right spacer matches badge width to keep the icon truly centered
    wxSizer *top_h_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_h_sizer->Add(m_nozzle_label_id, 0, wxLEFT | wxTOP, FromDIP(2));
    top_h_sizer->AddStretchSpacer(1);
    top_h_sizer->Add(m_nozzle_icon, 0, wxTOP, FromDIP(20));
    top_h_sizer->AddStretchSpacer(1);
    top_h_sizer->AddSpacer(m_nozzle_label_id->GetMinSize().x + FromDIP(2));

    // ── Main sizer ───────────────────────────────────────────
    wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(top_h_sizer, 0, wxEXPAND);
    main_sizer->AddStretchSpacer(1);
    main_sizer->Add(label_h_sizer, 0, wxALIGN_CENTER_HORIZONTAL);
    main_sizer->Add(m_nozzle_label_2, 0, wxALIGN_CENTER_HORIZONTAL);
    main_sizer->Add(temp_h, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(8));
    main_sizer->AddSpacer(FromDIP(4));

    SetSizer(main_sizer);
    SetMinSize(WGT_HOTEND_ITEM_SIZE);
    SetMaxSize(WGT_HOTEND_ITEM_SIZE);
    SetSize(WGT_HOTEND_ITEM_SIZE);
    Layout();
}

void wgtDeviceHotendItem::UpdateNozzleInfo()
{
    const wxString    &diameter_str = m_nozzle_info.GetNozzleDiameterStr();
    const wxString    &flowtype_str = m_nozzle_info.GetNozzleFlowTypeStr();
    const std::string &color        = m_nozzle_info.GetFilamentColor();

    if (m_nozzle_info.IsEmpty()) {
        SetNozzleStatus(NOZZLE_EMPTY, _L("Empty"), wxEmptyString, color);
    } else if (m_nozzle_info.IsNormal()) {
        SetNozzleStatus(NOZZLE_NORMAL, diameter_str, flowtype_str, color);
    } else if (m_nozzle_info.IsAbnormal()) {
        SetNozzleStatus(NOZZLE_ERROR, _L("Error"), wxEmptyString, color);
    } else if (m_nozzle_info.IsUnknown()) {
        SetNozzleStatus(NOZZLE_UNKNOWN, _L("Unknown"), wxEmptyString, color);
    } else {
        SetNozzleStatus(NOZZLE_UNKNOWN, _L("Unknown"), wxEmptyString, color);
    }
}

void wgtDeviceHotendItem::UpdateByObj(Slic3r::MachineObject *obj)
{
    m_obj = obj;

    if (!m_obj) return;

    // Read nozzle temperature data from device
    m_nozzle_info = m_obj->GetNozzleSystem()->GetRackNozzle(GetHotendId() - 1);

    m_nozzle_temp_dirty_handler.UpdateValue(m_nozzle_info.GetTargetTemp());
    UpdateTempDisplay(m_nozzle_info.GetCurrentTemp(), m_nozzle_temp_dirty_handler.GetValue());

    UpdateNozzleInfo();
}

void wgtDeviceHotendItem::UpdateTempDisplay(int cur_temp, int target_temp)
{
    bool need_layout   = false;
    // bool has_data      = target_temp > 0; // 0 means no target temp data

    if (!m_nozzle_info.IsExist()) {
        // Invalid nozzle, show placeholder
        if (m_label_cur_temp->GetLabel() != "--") {
            m_label_cur_temp->SetLabel("--");
            need_layout = true;
        }
        if (!m_temp_editing && m_text_target_temp->GetValue() != "--") {
            m_text_target_temp->SetValue("--");
            need_layout = true;
        }
    } else {
        // Current temperature
        wxString cur_str = cur_temp > 0 ? wxString::Format("%d", cur_temp) : wxString("--");
        if (m_label_cur_temp->GetLabel() != cur_str) {
            m_label_cur_temp->SetLabel(cur_str);
            need_layout = true;
        }

        // Target temperature: only update when user is not editing
        if (!m_temp_editing) {
            wxString tgt_str = target_temp > 0 ? wxString::Format("%d", target_temp) : wxString("--");
            if (m_text_target_temp->GetValue() != tgt_str) {
                m_text_target_temp->SetValue(tgt_str);
                need_layout = true;
            }
        }
    }

    if (need_layout) Layout();
}

void wgtDeviceHotendItem::OnTempTextEnter(wxCommandEvent &evt)
{
    m_temp_editing = false;
    SendTempCommand();
    m_text_target_temp->Navigate();
}

void wgtDeviceHotendItem::OnTempKillFocus(wxFocusEvent &evt)
{
    evt.Skip();
    if (!m_temp_editing) return;
    m_temp_editing = false;
    SendTempCommand();
}

void wgtDeviceHotendItem::SendTempCommand()
{
    long temp;

    wxString val = m_text_target_temp->GetValue().Trim();
    if (val.IsEmpty() || val == "--" || !val.ToLong(&temp)) return;

    m_nozzle_temp_dirty_handler.SetOptimisticValue(static_cast<int>(temp));

    // Send temperature control command
    if (m_obj) {
        m_obj->GetNozzleRack()->SetNozzleTargetTemp(GetNozzlePosId(), static_cast<int>(temp));
    }
}

void wgtDeviceHotendItem::SetNozzleStatus(NOZZLE_STATUS status, const wxString &str1, const wxString &str2, const std::string &color)
{
    static wxColour s_red_clr("#D01B1B");

    if (m_status != status || m_filament_color != color) {
        m_status         = status;
        m_filament_color = color;

        switch (status) {
        case NOZZLE_EMPTY: {
            if (!m_nozzle_empty_image) m_nozzle_empty_image = new ScalableBitmap(this, "dev_rack_nozzle_empty", 46);
            m_nozzle_icon->SetBitmap(m_nozzle_empty_image->bmp());
            break;
        }
        case NOZZLE_NORMAL: {
            if (!m_nozzle_normal_image) m_nozzle_normal_image = new ScalableBitmap(this, "dev_rack_nozzle_normal", 46);
            m_nozzle_icon->SetBitmap(SetNozzleBmpColor(m_nozzle_normal_image->bmp(), m_filament_color));
            break;
        }
        case NOZZLE_UNKNOWN: {
            if (!m_nozzle_unknown_image) m_nozzle_unknown_image = new ScalableBitmap(this, "dev_rack_nozzle_unknown", 46);
            m_nozzle_icon->SetBitmap(m_nozzle_unknown_image->bmp());
            break;
        }
        case NOZZLE_ERROR: {
            if (!m_nozzle_error_image) m_nozzle_error_image = new ScalableBitmap(this, "dev_rack_nozzle_error", 46);
            m_nozzle_icon->SetBitmap(m_nozzle_error_image->bmp());
            break;
        }
        default: break;
        }

        if (status == NOZZLE_ERROR) {
            m_nozzle_label_1->SetForegroundColour(StateColor::darkModeColorFor(s_red_clr));
            m_nozzle_status_icon->Show(true);
        } else {
            m_nozzle_label_1->SetForegroundColour(StateColor::darkModeColorFor(*wxBLACK));
            m_nozzle_status_icon->Show(false);
        }
    }

    bool update_layout = (m_nozzle_label_1->GetLabel() != str1 || m_nozzle_label_2->GetLabel() != str2);
    m_nozzle_label_1->SetLabel(str1);
    m_nozzle_label_2->SetLabel(str2);

    if (update_layout) { Layout(); }
}

void wgtDeviceHotendItem::OnBtnNozzleStatus(wxMouseEvent &evt)
{
    if (m_status == NOZZLE_ERROR) {
        MessageDialog dlg(nullptr,
                          _L("The hotend is in an abnormal state and currently unavailable. "
                             "Please go to 'Device -> Upgrade' to upgrade firmware."),
                          _L("Abnormal Hotend"), wxICON_WARNING);
        dlg.AddButton(wxID_CANCEL, _L("Cancel"), false);
        dlg.AddButton(wxID_OK, _L("Jump to the upgrade page"), true);

        if (dlg.ShowModal() == wxID_OK) { wxGetApp().mainframe->m_monitor->jump_to_Upgrade(); }
    }
    evt.Skip();
}

void wgtDeviceHotendItem::Rescale()
{
    if (m_nozzle_normal_image) { m_nozzle_normal_image->msw_rescale(); }
    if (m_nozzle_empty_image) { m_nozzle_empty_image->msw_rescale(); }
    if (m_nozzle_unknown_image) { m_nozzle_unknown_image->msw_rescale(); }
    if (m_nozzle_error_image) { m_nozzle_error_image->msw_rescale(); }

    auto status_icon = create_scaled_bitmap("dev_rack_nozzle_error_icon", this, 14);
    m_nozzle_status_icon->SetBitmap(status_icon);
    m_nozzle_status_icon->Refresh();

    switch (m_status) {
    case NOZZLE_EMPTY:
        if (m_nozzle_empty_image) { m_nozzle_icon->SetBitmap(m_nozzle_empty_image->bmp()); }
        break;
    case NOZZLE_NORMAL:
        if (m_nozzle_normal_image) { m_nozzle_icon->SetBitmap(SetNozzleBmpColor(m_nozzle_normal_image->bmp(), m_filament_color)); }
        break;
    case NOZZLE_UNKNOWN:
        if (m_nozzle_unknown_image) { m_nozzle_icon->SetBitmap(m_nozzle_unknown_image->bmp()); }
        break;
    case NOZZLE_ERROR:
        if (m_nozzle_error_image) { m_nozzle_icon->SetBitmap(m_nozzle_error_image->bmp()); }
        break;
    default: break;
    }
}

} // namespace Slic3r::GUI
