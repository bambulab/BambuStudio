#include "wgtDeviceAmsPreview.h"

#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceManager.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <wx/dcbuffer.h>
#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {

class AmsPreviewSlotWidget : public wxPanel
{
public:
    struct SlotPreviewData
    {
        bool has_filament = false;
        wxColour color = wxColour("#E6E6E6");
        bool operator!=(const SlotPreviewData& other)  const { return !(other == *this); };
        bool operator==(const SlotPreviewData& other) const
        {
            return color == other.color && has_filament == other.has_filament;
        };
    };

    explicit AmsPreviewSlotWidget(wxWindow *parent) : wxPanel(parent, wxID_ANY)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(wxColour("#F0F0F0"));
        SetMinSize(wxSize(FromDIP(8), FromDIP(22)));
        SetMaxSize(wxSize(FromDIP(8), FromDIP(22)));
        Bind(wxEVT_PAINT, &AmsPreviewSlotWidget::OnPaint, this);
    }

    void SetData(const SlotPreviewData& data)
    {
        if (m_data != data) {
            m_data = data;
            Refresh();
        }
    }

private:
    void OnPaint(wxPaintEvent &evt)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();

        wxRect rect = GetClientRect();
        if (rect.width <= 0 || rect.height <= 0) {
            evt.Skip();
            return;
        }

        const wxColour inner_fill   = m_data.has_filament ? m_data.color : wxColour("#CFCFCF");
        const int      border_width = 1;

        rect.Deflate(0, FromDIP(1));
        dc.SetPen(wxPen(inner_fill, border_width));
        dc.SetBrush(wxBrush(inner_fill));
        dc.DrawRoundedRectangle(rect, FromDIP(3));

        wxRect inner_rect = rect;
        inner_rect.Deflate(FromDIP(2), FromDIP(3));
        if (inner_rect.width > 0 && inner_rect.height > 0) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(inner_fill));
            dc.DrawRoundedRectangle(inner_rect, FromDIP(2));
        }
    }

private:
    SlotPreviewData m_data;
};

} // namespace

wgtDeviceAmsPreview::wgtDeviceAmsPreview(wxWindow *parent) : wxPanel(parent, wxID_ANY) { CreateGui(); }

void wgtDeviceAmsPreview::CreateGui()
{
    const wxColour panel_bg(0xF0, 0xF0, 0xF0);
    SetBackgroundColour(panel_bg);
    SetMinSize(wxSize(FromDIP(124), FromDIP(30)));

    wxBoxSizer *root = new wxBoxSizer(wxHORIZONTAL);

    m_label_title    = new Label(this, "AMS");
    m_label_title->SetFont(Label::Head_13);
    root->Add(m_label_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    root->AddSpacer(FromDIP(8));

    m_panel_slots = new wxPanel(this, wxID_ANY);
    m_panel_slots->SetBackgroundColour(panel_bg);
    m_sizer_slots = new wxBoxSizer(wxHORIZONTAL);
    m_panel_slots->SetSizer(m_sizer_slots);
    root->Add(m_panel_slots, 0, wxALIGN_CENTER_VERTICAL);
    root->AddSpacer(FromDIP(8));

    m_label_expand = new Label(this, ">");
    m_label_expand->SetFont(Label::Body_12);
    m_label_expand->SetForegroundColour(wxColour("#909090"));
    root->Add(m_label_expand, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    SetSizer(root);
    Layout();
}

void wgtDeviceAmsPreview::UpdateByMachine(Slic3r::MachineObject* machine_obj,
                                          const std::map<int, std::optional<DevAmsSlotId>>& nozzle_slots)
{
    auto fila_system = machine_obj ? machine_obj->GetFilaSystem() : nullptr;
    if (!fila_system) {
        return;
    }

    if (m_nozzle_slots.size() != nozzle_slots.size()) {
        m_sizer_slots->Clear(true);
        m_slot_widgets.clear();

        for (size_t i = 0; i < nozzle_slots.size(); ++i) {
            auto* slot_widget = new AmsPreviewSlotWidget(m_panel_slots);
            m_sizer_slots->Add(slot_widget, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxEXPAND, FromDIP(3));
            m_slot_widgets.push_back(slot_widget);
        }
    }

    m_nozzle_slots = nozzle_slots;
    auto iter = m_nozzle_slots.begin();
    for (int idx = 0; idx < m_slot_widgets.size(); idx++) {
        AmsPreviewSlotWidget::SlotPreviewData preview_dd;
        if (iter->second.has_value()) {
            auto slot_data = fila_system->GetOwner()->get_tray(iter->second->first, iter->second->second);
            if (slot_data.has_value()) {
                preview_dd.color = slot_data->get_color();
                if (slot_data->ams_type != DevAmsType::EXT_SPOOL) {
                    preview_dd.has_filament = slot_data->is_exists && slot_data->is_tray_info_ready();
                } else {
                    preview_dd.has_filament = slot_data->is_tray_info_ready();
                }
            }
        }

        auto slot_widget = dynamic_cast<AmsPreviewSlotWidget*>(m_slot_widgets[idx]);
        if (slot_widget) {
            slot_widget->SetData(preview_dd);
        }

        iter++;
    }

    if (!fila_system->GetAmsList().empty()) {
        m_label_title->SetLabel("AMS");
    } else {
        m_label_title->SetLabel("External Spools");
    }

    Layout();
    Fit();
};

} // namespace Slic3r::GUI
