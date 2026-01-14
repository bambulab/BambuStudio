#include "ExtruderPanel.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/settings.h>
#include <wx/dcbuffer.h>

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Label.hpp"
#include "wx/graphics.h"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_NOZZLE_DIAMETER_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_NOZZLE_DIAMETER_SELECTED, wxCommandEvent);
wxDEFINE_EVENT(EVT_NOZZLE_PANELS_UPDATED, wxCommandEvent);
wxDEFINE_EVENT(EVT_NOZZLE_FLOW_UPDATED, wxCommandEvent);

static const wxColour BgNormalColor = wxColour("#FFFFFF");
static const wxColour BgDisabledColor = wxColour("#F4F4F4");

static const wxColour DiameterNormalColor = wxColour("#000000");
static const wxColour FlowNormalColor = wxColour("#999999");
static const wxColour LabelEditedColor = wxColour("#FF6F00");
static const wxColour DiameterDisabledColor = wxColour("#898989");
static const wxColour FlowDisabledColor = wxColour("#CECECE");
static const wxColour LabelEditedDisabledColor = wxColour("#F8AF79");
static const wxColour LabelErrorColor = wxColour("#E60034");

static const wxColour SelectedBgColor    = wxColour("#00AE42");
static const wxColour SelectedTextColor  = wxColour("#FFFFFE");
static const wxColour HasNozzleBgColor   = wxColour("#D9D9D9");
static const wxColour HasNozzleTextColor = wxColour("#6B6B6B");
static const wxColour NoNozzleBgColor    = wxColour("#D9D9D9");
static const wxColour NoNozzleTextColor  = wxColour("#A2A2A2");

void ExtruderNozzlePanel::OnPaint(wxPaintEvent &event)
{
    wxAutoBufferedPaintDC dc(this);
    wxGraphicsContext    *gc = wxGraphicsContext::Create(dc);
    if (gc) {
        dc.Clear();
        wxRect rect = GetClientRect();
        gc->SetBrush(StateColor::darkModeColorFor(wxColour("#F7F7F7")));
        gc->DrawRoundedRectangle(0, 0, rect.width, rect.height, 0);
        wxColour bg_color = (m_status == Normal) ? BgNormalColor : BgDisabledColor;
        bg_color     = StateColor::darkModeColorFor(bg_color);
        gc->SetBrush(wxBrush(bg_color));
        gc->DrawRoundedRectangle(0, 0, rect.width, rect.height, 12);

        if (m_defined) {
            wxColour border_color = /*m_status == Normal ? wxColour("#EEEEEE") : */wxColour("#DFDFE0");
            gc->SetPen(wxPen(border_color, 1));
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->DrawRoundedRectangle(0.5, 0.5, rect.width - 1, rect.height - 1, 12);
        }
        delete gc;
    }
}

ExtruderNozzlePanel::ExtruderNozzlePanel(wxWindow *parent, const wxString &diameter, NozzleVolumeType flow, const std::vector<wxString> &diameters, const std::vector<wxString> &flows)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_diameter(diameter), m_flow(flow)
{
    SetBackgroundStyle(wxBG_STYLE_CUSTOM);

    SetMinSize(wxSize(FromDIP(90), FromDIP(50)));
    SetMaxSize(wxSize(FromDIP(90), FromDIP(50)));

    wxBoxSizer *main_sizer = new wxBoxSizer(wxHORIZONTAL);

    icon   = create_scaled_bitmap("dev_rack_nozzle_error", this, 24);
    m_icon = new wxStaticBitmap(this, wxID_ANY, icon);

    wxBoxSizer *label_sizer = new wxBoxSizer(wxVERTICAL);

    label_diameter = new Label(this, diameter);
    label_diameter->SetFont(Label::Body_12.Bold());
    label_diameter->SetForegroundColour(DiameterNormalColor);
    label_diameter->SetBackgroundColour(*wxWHITE);

    wxString flow_text = flow == nvtStandard ? _L("Standard") : _L("High Flow");

    label_volume = new Label(this, flow_text);
    label_volume->SetFont(Label::Body_12);
    label_volume->SetForegroundColour(FlowNormalColor);
    label_volume->SetBackgroundColour(*wxWHITE);

    label_sizer->Add(label_diameter, 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(2));
    label_sizer->Add(label_volume, 0, wxALIGN_LEFT);

    main_sizer->Add(m_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    main_sizer->Add(label_sizer, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND);

    wxBoxSizer *outer_sizer = new wxBoxSizer(wxVERTICAL);
    outer_sizer->Add(main_sizer, 1, wxEXPAND | wxALL, FromDIP(8));

    SetSizer(outer_sizer);

    auto forward_click_to_parent = [this](wxMouseEvent &event) {
        wxCommandEvent click_event(wxEVT_LEFT_DOWN, GetId());
        click_event.SetEventObject(this);
        this->ProcessEvent(click_event);
    };

    m_icon->Bind(wxEVT_LEFT_DOWN, forward_click_to_parent);
    label_diameter->Bind(wxEVT_LEFT_DOWN, forward_click_to_parent);
    label_volume->Bind(wxEVT_LEFT_DOWN, forward_click_to_parent);

    Bind(wxEVT_PAINT, &ExtruderNozzlePanel::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, [this, &diameters, &flows](wxMouseEvent&) {
        NozzleConfigDialog dlg(static_cast<wxWindow*>(wxGetApp().mainframe), this, diameters, flows);
        if (dlg.ShowModal() == wxID_OK) {
            SetNozzleConfig(dlg.GetSelectedDiameter(), dlg.GetSelectedFlow(), true);
        }
    });

    UpdateContent();
    UpdateVisualState();

    Layout();
    wxGetApp().UpdateDarkUIWin(this);
}

void ExtruderNozzlePanel::SetDiameter(const wxString &diameter, bool user_edit)
{
    if (m_diameter == diameter) {
        return;
    }

    m_diameter = diameter;
    if (user_edit) {
        if (!m_defined) {
            m_defined = true;
        }
        MarkAsModified();
        SetUserEdited(true);
    }
    UpdateContent();
    NotifyContentChanged(true, false, user_edit);
}

void ExtruderNozzlePanel::SetFlow(NozzleVolumeType flow, bool user_edit)
{
    if (m_flow == flow) {
        return;
    }

    m_flow = flow;
    if (user_edit) {
        if (!m_defined) {
            m_defined = true;
        }
        MarkAsModified();
        SetUserEdited(true);
    }
    UpdateContent();
    NotifyContentChanged(false, true, user_edit);
}

void ExtruderNozzlePanel::SetNozzleConfig(const wxString &diameter, NozzleVolumeType flow, bool user_edit)
{
    bool diameter_changed = (m_diameter != diameter);
    bool flow_changed = (m_flow != flow);
    bool content_changed = diameter_changed || flow_changed;
    
    if (!content_changed) {
        return;
    }

    m_diameter = diameter;
    m_flow = flow;

    if (user_edit) {
        if (!m_defined) {
            m_defined = true;
        }
        MarkAsModified();
        SetStatus(Normal);
        SetUserEdited(true);
    }
    UpdateContent();
    NotifyContentChanged(diameter_changed, flow_changed, user_edit);
}

void ExtruderNozzlePanel::UpdateContent()
{
    switch (m_status) {
        case Normal:
        case Disabled:
            if (label_diameter) {
                label_diameter->SetLabel(m_diameter);
            }
            if (label_volume) {
                std::vector<wxString> flows_texts = {
                    _L("Standard"),
                    _L("High Flow"),
                    _L("Hybrid"),
                    _L("TPU High Flow"),
                    _L("TPU285")
                };
                wxString flow_text = m_flow >= flows_texts.size() ? _L("Standard") : flows_texts[m_flow];
                label_volume->SetLabel(flow_text);
            }
            break;
        case Error:
            if (label_diameter) {
                label_diameter->SetLabel("-");
            }
            if (label_volume) {
                label_volume->SetLabel(_L("Error"));
            }
            break;
        case Empty:
            if (label_diameter) {
                label_diameter->SetLabel("-");
            }
            if (label_volume) {
                label_volume->SetLabel(_L("Empty"));
            }
            break;
        case Unknown:
            if (label_diameter) {
                label_diameter->SetLabel("-");
            }
            if (label_volume) {
                label_volume->SetLabel(_L("Unknown"));
            }
            break;
    }

    Layout();
    Refresh();
}

void ExtruderNozzlePanel::MarkAsModified()
{
    if (!m_defined) {
        return;
    }
    UpdateVisualState();
}

void ExtruderNozzlePanel::NotifyContentChanged(bool diameter_changed, bool flow_changed, bool user_edit)
{
    ExtruderPanel *extruder_panel = dynamic_cast<ExtruderPanel *>(GetGrandParent());

    if (diameter_changed && extruder_panel) {
        wxCommandEvent evt(EVT_NOZZLE_DIAMETER_CHANGED, GetId());
        evt.SetEventObject(this);
        evt.SetString(m_diameter);
        wxPostEvent(GetGrandParent(), evt);
    }

    if (extruder_panel) {
        extruder_panel->NotifyNozzlePanelsUpdated();
    }

    if (flow_changed) {
        wxCommandEvent evt(EVT_NOZZLE_FLOW_UPDATED, GetId());
        evt.SetEventObject(this);
        evt.SetInt(user_edit ? 1 : 0);
        GetEventHandler()->ProcessEvent(evt);
    }
}

void ExtruderNozzlePanel::SetStatus(Status status)
{
    if (m_status == status) {
        return;
    }

    m_status = status;
    UpdateContent();
    UpdateVisualState();
}

void ExtruderNozzlePanel::UpdateStatus(Status status)
{
    SetStatus(status);
}

void ExtruderNozzlePanel::UpdateVisualState()
{
    wxColour diameter_color, flow_color, bg_color;
    wxString tip;

    switch (m_status) {
        case Normal:
            diameter_color = DiameterNormalColor;
            flow_color = FlowNormalColor;
            bg_color = BgNormalColor;
            tip = m_defined ? _L("Already set") : _L("Default value");
            break;
        case Disabled:
            diameter_color = DiameterDisabledColor;
            flow_color = FlowDisabledColor;
            bg_color = BgDisabledColor;
            tip            = _L("Not available: different from the currently selected diameter");
            break;
        case Error:
            diameter_color = DiameterDisabledColor;
            flow_color = LabelErrorColor;
            bg_color = BgDisabledColor;
            tip            = _L("Not available: nozzle has an error");
            break;
        case Empty:
            diameter_color = DiameterDisabledColor;
            flow_color     = FlowDisabledColor;
            bg_color       = BgDisabledColor;
            tip            = _L("Not available: no nozzle installed in this slot");
            break;
        case Unknown:
            diameter_color = DiameterDisabledColor;
            flow_color = FlowDisabledColor;
            bg_color = BgDisabledColor;
            tip            = _L("Not availabel: cannot detect current nozzle information");
            break;
        default:
            diameter_color = DiameterNormalColor;
            flow_color = FlowNormalColor;
            bg_color = BgNormalColor;
            tip            = "";
            break;
    }

    if (label_diameter) {
        label_diameter->SetForegroundColour(diameter_color);
        label_diameter->SetBackgroundColour(bg_color);
        label_diameter->Refresh();
    }

    if (label_volume) {
        label_volume->SetForegroundColour(flow_color);
        label_volume->SetBackgroundColour(bg_color);
        label_volume->Refresh();
    }

    SetToolTip(tip);

    wxGetApp().UpdateDarkUIWin(this);
    Refresh();
}

HorizontalScrollablePanel::HorizontalScrollablePanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) : wxScrolledWindow(parent, id, pos, size, wxHSCROLL)
{
    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(sizer);

    SetScrollRate(FromDIP(10), 0);
    SetBackgroundColour(*wxWHITE);
}

void HorizontalScrollablePanel::Clear()
{
    GetSizer()->Clear(true);
    UpdateScrollbars();
}

void HorizontalScrollablePanel::UpdateScrollbars()
{
    Layout();

    wxSize content_size = GetSizer()->GetMinSize();
    wxSize client_size  = GetClientSize();

    SetVirtualSize(content_size.GetWidth(), client_size.GetHeight());

    Refresh();
}

ExtruderPanel::ExtruderPanel(wxWindow *parent, GroupType type, bool wide)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_type(type)
    , m_wide_layout(wide)
{
    SetForegroundColour(wxColour(206, 206, 206));
    SetBackgroundColour(wxColour("#F7F7F7"));

#ifdef __WXMSW__
    Bind(wxEVT_PAINT, &ExtruderPanel::OnPaint, this);
#endif
#ifdef __WXOSX__
    Bind(wxEVT_SIZE, &ExtruderPanel::OnSize, this);
#endif

    CreateLayout();
}

void ExtruderPanel::CreateLayout()
{
    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    if (m_type != SingleExtruder) {
        wxBoxSizer* title_sizer = new wxBoxSizer(wxHORIZONTAL);

        wxString title = (m_type == LeftExtruder) ? _L("Left Extruder") : _L("Right Extruder");
        m_title_label = new Label(this, title);
        m_title_label->SetFont(Label::Body_12);
        m_title_label->SetForegroundColour("#999999");

        title_sizer->Add(m_title_label, 0, wxALIGN_CENTER_VERTICAL);

        m_add_btn = new ScalableButton(this, wxID_ANY, "add_filament", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);
        m_add_btn->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &evt) {
            ShowAMSCountPopup();
            if (m_ams_popup) {
                m_ams_popup->Popup();
            }
            evt.Skip();
        });

        m_add_btn->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &evt) {
            wxTimer *timer = new wxTimer();

            timer->Bind(wxEVT_TIMER, [this, timer](wxTimerEvent &) {
                if (m_ams_popup) {
                    wxPoint mouse_pos  = wxGetMousePosition();
                    wxRect  popup_rect = m_ams_popup->GetScreenRect();
                    wxRect  btn_rect   = m_add_btn->GetScreenRect();

                    wxRect safe_zone = btn_rect;
                    safe_zone.Union(popup_rect);
                    safe_zone.Inflate(FromDIP(10));

                    if (!safe_zone.Contains(mouse_pos)) {
                        m_ams_popup->Dismiss();
                        m_ams_popup = nullptr;
                    }
                }

                timer->Stop();
                delete timer;
            });

            timer->StartOnce(300);

            evt.Skip();
        });

        title_sizer->Add(m_add_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        m_main_sizer->Add(title_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
    }

    m_scroll = new HorizontalScrollablePanel(this);
    m_scroll->SetBackgroundColour(wxColour("#F7F7F7"));

    m_main_sizer->Add(m_scroll, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(2));

    SetSizer(m_main_sizer);

    for (size_t i = 0; i < 4; ++i) {
        auto preview = new AMSPreview(m_scroll, wxID_ANY, AMSinfo(), AMSModel::GENERIC_AMS);
        preview->Close();

        preview->Bind(wxEVT_ENTER_WINDOW, [this, preview](wxMouseEvent &evt) {
            ShowAMSDeletePopup(preview);
        });

        m_ams_previews.push_back(preview);
    }
    
    for (size_t i = 0; i < 8; ++i) {
        auto preview = new AMSPreview(m_scroll, wxID_ANY, AMSinfo(), AMSModel::N3S_AMS);
        preview->Close();
        
        preview->Bind(wxEVT_ENTER_WINDOW, [this, preview](wxMouseEvent &evt) {
            ShowAMSDeletePopup(preview);
        });
        
        m_ams_previews.push_back(preview);
    }

    SetNozzlePanelCount(0);
    Bind(EVT_NOZZLE_DIAMETER_CHANGED, [this](wxCommandEvent &event) {
        CallAfter([this]() {
            SortNozzlePanels();
        });
        event.Skip();
    });

    CallAfter([this]() {
        if (m_ams_sizer) {
            update_ams();
        }
    });
}

void ExtruderPanel::SetNozzlePanelCount(int count)
{
    if (count < 1) count = 1;
    if (count > 6) count = 6;

    if (m_nozzle_count == count) {
        return;
    }
    m_nozzle_count = count;

    wxSizer *old_sizer = m_scroll->GetSizer();
    if (old_sizer) {
        if (m_ams_sizer) {
            old_sizer->Detach(m_ams_sizer);
        }
        if (m_type == SingleExtruder && m_add_btn) {
            old_sizer->Detach(m_add_btn);
        }
        old_sizer->Clear(false);
        m_scroll->SetSizer(nullptr);
    }

    for (auto *panel : m_nozzle_panels) {
        panel->Destroy();
    }
    m_nozzle_panels.clear();

    auto panel_size = wxSize(FromDIP(90), FromDIP(50));

    wxSizer *new_sizer = nullptr;
    if (m_type == SingleExtruder) {
        // SingleExtruder: AMS on the right side
        new_sizer = new wxBoxSizer(wxHORIZONTAL);
        if (!m_ams_sizer) {
            m_ams_sizer = new wxBoxSizer(wxHORIZONTAL);
        }

        // create nozzle sizer
        auto nozzle_sizer = new wxBoxSizer(wxHORIZONTAL);
        new_sizer->Add(nozzle_sizer, 0, wxALIGN_CENTER_VERTICAL);

        // AMS on the right side
        new_sizer->Add(m_ams_sizer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));

        if (!m_add_btn) {
            m_add_btn = new ScalableButton(m_scroll, wxID_ANY, "add_filament", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);
            m_add_btn->SetToolTip(_L("Add AMS"));
            m_add_btn->SetBackgroundColour(wxColour("#F7F7F7"));
            m_add_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { ShowAMSCountPopup(); });
        }
        new_sizer->Add(m_add_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
        new_sizer->AddStretchSpacer(1);

        int height = panel_size.GetHeight() + FromDIP(8);
        m_scroll->SetMinSize(wxSize(-1, height));

    } else {
        // LeftExtruder/RightExtruder: AMS on the top
        new_sizer = new wxBoxSizer(wxVERTICAL);
        if (!m_ams_sizer) {
            m_ams_sizer = new wxBoxSizer(wxHORIZONTAL);
        }

        new_sizer->Add(m_ams_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(2));

        // determine layout type by wide flag
        if (m_wide_layout) {
            if (m_nozzle_count > 1) {
                // create grid container
                auto nozzle_grid = new wxGridSizer(2, 3, FromDIP(4), FromDIP(4));
                new_sizer->Add(nozzle_grid, 0, wxEXPAND);
            } else {
                // create horizontal container
                auto nozzle_sizer = new wxBoxSizer(wxHORIZONTAL);
                new_sizer->Add(nozzle_sizer, 0, wxEXPAND);
            }
            int total_height = 2 * panel_size.GetHeight() + FromDIP(4) + FromDIP(60);
            m_scroll->SetMinSize(wxSize(-1, total_height));
        } else {
            // normal horizontal layout
            auto nozzle_sizer = new wxBoxSizer(wxHORIZONTAL);
            new_sizer->Add(nozzle_sizer, 1, wxEXPAND);

            int height = panel_size.GetHeight() + FromDIP(8);
            m_scroll->SetMinSize(wxSize(-1, -1));
        }
    }

    m_scroll->SetSizer(new_sizer);

    for (int i = 0; i < count; ++i) {
        auto *panel = new ExtruderNozzlePanel(m_scroll, "0.4", nvtStandard, m_nozzle_diameter_choices, m_nozzle_flow_choices);
        m_nozzle_panels.push_back(panel);
    }

    AddPanels();
    UpdateLayout();
    NotifyNozzlePanelsUpdated();
}

void ExtruderPanel::AddPanels()
{
    wxSizer *sizer = m_scroll->GetSizer();
    if (!sizer) return;

    auto panel_size = wxSize(FromDIP(90), FromDIP(50));

    wxBoxSizer *main_sizer = dynamic_cast<wxBoxSizer *>(sizer);
    if (!main_sizer) return;

    wxSizer *nozzle_sizer = nullptr;
    if (m_type == SingleExtruder) {
        // SingleExtruder: nozzle_sizer in index 0
        if (main_sizer->GetItemCount() >= 1) {
            nozzle_sizer = main_sizer->GetItem(size_t(0))->GetSizer();
        }
    } else {
        // LeftExtruder/RightExtruder: nozzle_sizer in index 1（AMS is in index 0）
        if (main_sizer->GetItemCount() >= 2) {
            nozzle_sizer = main_sizer->GetItem(size_t(1))->GetSizer();
        }
    }
    if (!nozzle_sizer) return;
    nozzle_sizer->Clear(false);

    if (m_wide_layout) {
        if (m_nozzle_count > 1) {
            wxGridSizer *grid_sizer = dynamic_cast<wxGridSizer *>(nozzle_sizer);
            if (grid_sizer) {
                for (auto *panel : m_nozzle_panels) {
                    grid_sizer->Add(panel, 0, wxEXPAND);
                }
            }
        } else {
            wxBoxSizer *box_sizer = dynamic_cast<wxBoxSizer *>(nozzle_sizer);
            if (box_sizer) {
                wxBoxSizer *first_row = new wxBoxSizer(wxHORIZONTAL);

                if (!m_nozzle_panels.empty()) {
                    first_row->Add(m_nozzle_panels[0], 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(2));
                }

                first_row->AddSpacer(FromDIP(4));
                first_row->AddStretchSpacer();
                box_sizer->Add(first_row, 0, wxEXPAND);

                box_sizer->AddStretchSpacer();
            }
        }
    } else {
        wxBoxSizer *box_sizer = dynamic_cast<wxBoxSizer *>(nozzle_sizer);
        if (box_sizer) {
            for (auto *panel : m_nozzle_panels) {
                box_sizer->Add(panel, 0, wxEXPAND | wxALL, FromDIP(4));
            }
        }
    }
}

void ExtruderPanel::set_ams_count(int n4, int n1)
{
    if (n4 == m_ams_n4 && n1 == m_ams_n1)
        return;

    m_ams_n4 = n4;
    m_ams_n1 = n1;
    update_ams();
}

void ExtruderPanel::update_ams()
{
    if (!m_ams_sizer) return;

    static AMSinfo info4;
    static AMSinfo info1;
    static AMSinfo info_ext;
    if (info4.cans.empty()) {
        for (size_t i = 0; i < 4; ++i) {
            info4.cans.push_back({});
        }
        info1.ams_type = AMSModel::N3S_AMS;
        info1.cans.push_back({});

        info_ext.ams_type = AMSModel::EXT_AMS;
        info_ext.cans.push_back({});
    }

    m_ams_sizer->Clear(false);

    for (auto preview : m_ams_previews) {
        preview->Close();
    }

    size_t preview_idx = 0;

    // update 4-slot AMS（first 4 previews）
    for (size_t i = 0; i < m_ams_n4 && i < 4; ++i) {
        AMSinfo &ams_info = i < m_ams_4.size() ? m_ams_4[i] : info4;

        m_ams_previews[preview_idx]->Update(ams_info);
        m_ams_previews[preview_idx]->Refresh();
        m_ams_previews[preview_idx]->Open();

        m_ams_sizer->Add(m_ams_previews[preview_idx], 0, wxALL, FromDIP(2));
        preview_idx++;
    }

    // update 1-slot AMS（last 8 previews）
    size_t ams1_start_idx = 4;
    for (size_t i = 0; i < m_ams_n1 && i < 8; ++i) {
        AMSinfo &ams_info = i < m_ams_1.size() ? m_ams_1[i] : info1;

        size_t idx = ams1_start_idx + i;
        m_ams_previews[idx]->Update(ams_info);
        m_ams_previews[idx]->Refresh();
        m_ams_previews[idx]->Open();

        m_ams_sizer->Add(m_ams_previews[idx], 0, wxALL, FromDIP(2));
    }

    if (!m_ext.empty()) {
        while (m_ext_previews.size() < m_ext.size()) {
            auto ext_preview = new AMSPreview(m_scroll, wxID_ANY, AMSinfo(), AMSModel::N3S_AMS);
            ext_preview->Close();
            m_ext_previews.push_back(ext_preview);
        }

        for (size_t i = 0; i < m_ext.size(); ++i) {
            if (i < m_ext_previews.size()) {
                AMSinfo &ext_info = m_ext[i];
                m_ext_previews[i]->Update(ext_info);
                m_ext_previews[i]->Refresh();
                m_ext_previews[i]->Open();

                m_ams_sizer->Add(m_ext_previews[i], 0, wxALL, FromDIP(2));
            }
        }
    } else {
        if (m_ext_previews.empty()) {
            auto ext_preview = new AMSPreview(m_scroll, wxID_ANY, AMSinfo(), AMSModel::EXT_AMS);
            ext_preview->Close();
            m_ext_previews.push_back(ext_preview);
        }

        m_ext_previews[0]->Update(info_ext);
        m_ext_previews[0]->Refresh();
        m_ext_previews[0]->Open();
        m_ams_sizer->Add(m_ext_previews[0], 0, wxALL, FromDIP(2));
    }

    UpdateLayout();
}


void ExtruderPanel::SortNozzlePanels()
{
    if (m_nozzle_count <= 1 || m_nozzle_panels.empty()) {
        return;
    }

    std::sort(m_nozzle_panels.begin(), m_nozzle_panels.end(), [](ExtruderNozzlePanel *a, ExtruderNozzlePanel *b) {
        auto status_a = a->GetStatus();
        auto status_b = b->GetStatus();

        // define priority for each status
        auto get_priority = [](ExtruderNozzlePanel::Status status) -> int {
            switch (status) {
                case ExtruderNozzlePanel::Status::Normal:
                case ExtruderNozzlePanel::Status::Disabled:
                    return 0;
                case ExtruderNozzlePanel::Status::Error:
                    return 1;
                case ExtruderNozzlePanel::Status::Unknown:
                    return 2;
                case ExtruderNozzlePanel::Status::Empty:
                    return 3;
                default:
                    return 4;
            }
        };

        int priority_a = get_priority(status_a);
        int priority_b = get_priority(status_b);

        // sort by priority first
        if (priority_a != priority_b) {
            return priority_a < priority_b;
        }

        // sort by diameter if both are Normal or Disabled
        if (priority_a == 0) {
            return a->GetDiameter() < b->GetDiameter();
        }

        // keep original order for other statuses
        return false;
    });

    AddPanels();
    UpdateLayout();
}

std::unordered_map<NozzleVolumeType, int> ExtruderPanel::GetNozzleFlowCounts(const wxString& diameter) const
{
    std::unordered_map<NozzleVolumeType, int> ret;
    for (const auto* panel : m_nozzle_panels) {
        if (panel->GetDiameter() == diameter) {
            auto flow = panel->GetFlow();
            ret[flow]++;
        }
    }
    return ret;
}

void ExtruderPanel::SetWideLayout(bool wide)
{
    if (m_wide_layout != wide) {
        m_wide_layout = wide;
    }
    CallAfter([this]() {
        UpdateLayout();
    });
}

void ExtruderPanel::UpdateLayout()
{
    if (m_scroll) {
        m_scroll->UpdateScrollbars();
        m_scroll->Layout();
    }

    wxWindow* parent = GetParent();
    if (parent && m_wide_layout && m_type == RightExtruder) {
        wxSize parent_size = parent->GetSize();
        int target_width = parent_size.GetWidth() * 0.7;
        SetMinSize(wxSize(target_width, -1));
    } else {
        SetMinSize(wxSize(-1, -1));
    }

    if (m_type != SingleExtruder && m_scroll) {
        int ams_height    = FromDIP(40);
        int nozzle_height = m_wide_layout ? FromDIP(50) * 2 : FromDIP(54);
        int total_height  = ams_height + nozzle_height + FromDIP(4);

        m_scroll->SetMinSize(wxSize(-1, total_height));
        m_scroll->SetSize(wxSize(-1, total_height));
    }

    CallAfter([this]() {
        if (m_scroll) {
            m_scroll->GetSizer()->Layout();
            m_scroll->Layout();
            m_scroll->Refresh();
        }

        Layout();
        Refresh();

        wxWindow *current = GetParent();
        while (current) {
            current->Layout();
            current->Refresh();

            wxSizer *parent_sizer = current->GetSizer();
            if (parent_sizer) { parent_sizer->Layout(); }
            current = current->GetParent();
        }
    });
}

std::set<wxString> ExtruderPanel::GetExistingDiameters() const
{
    std::set<wxString> diameters;
    for (auto panel : m_nozzle_panels) {
        if (panel) {
            diameters.insert(panel->GetDiameter());
        }
    }

    return diameters;
}

void ExtruderPanel::SetNozzleBySelection(const wxString &diameter)
{
    bool changed = false;
    for (auto *panel : m_nozzle_panels) {
        if (panel) {
            if (panel->GetDiameter() == diameter) {
                if (panel->GetStatus() == ExtruderNozzlePanel::Disabled) {
                    panel->SetStatus(ExtruderNozzlePanel::Normal);
                }
            } else {
                if (panel->GetStatus() == ExtruderNozzlePanel::Normal) {
                    panel->SetStatus(ExtruderNozzlePanel::Disabled);
                }
            }
            if (!panel->IsDefined()) {
                panel->SetNozzleConfig(diameter, panel->GetFlow());
            }
        }
    }
}

void ExtruderPanel::NotifyNozzlePanelsUpdated()
{
    wxCommandEvent evt(EVT_NOZZLE_PANELS_UPDATED, GetId());
    evt.SetEventObject(this);
    GetEventHandler()->ProcessEvent(evt);
}

NozzleVolumeType ExtruderPanel::GetVolumeType(const wxString& diameter) const
{
    if (m_nozzle_panels.empty()) {
        return nvtStandard;
    }

    if (m_nozzle_panels.size() == 1) {
        ExtruderNozzlePanel* panel = m_nozzle_panels[0];
        return panel->GetFlow();
    } else {
        std::set<NozzleVolumeType> matching_flows;

        for (ExtruderNozzlePanel* panel : m_nozzle_panels) {
            if (panel && panel->GetDiameter() == diameter) {
                matching_flows.insert(panel->GetFlow());
            }
        }

        if (matching_flows.empty()) {
            return nvtStandard;
        }

        if (matching_flows.size() == 1) {
            return *matching_flows.begin();
        }
        return nvtHybrid;
    }
}

void ExtruderPanel::Rescale()
{
    if (m_add_btn) {
        m_add_btn->msw_rescale();
    }

    for (auto* panel : m_nozzle_panels) {
        if (panel) {
            panel->SetMinSize(wxSize(FromDIP(60), FromDIP(60)));
            panel->Refresh();
        }
    }

    for (auto* ams : m_ams_previews) {
        if (ams) {
            ams->msw_rescale();
        }
    }

    for (auto* ext : m_ext_previews) {
        if (ext) {
            ext->msw_rescale();
        }
    }

    if (m_scroll) {
        m_scroll->SetScrollRate(FromDIP(5), FromDIP(5));
        m_scroll->FitInside();
    }

    UpdateLayout();
    Layout();
    Refresh();
}

void ExtruderPanel::ShowBadge(bool show)
{
#ifdef __WXMSW__
    if (show && m_badge.name() != "badge") {
        m_badge = ScalableBitmap(this, "badge", 18);
        Refresh();
    } else if (!show && !m_badge.name().empty()) {
        m_badge = ScalableBitmap{};
        Refresh();
    }
#endif
#ifdef __WXOSX__
    if (show && m_badge == nullptr) {
        m_badge = new ScalableButton(this, wxID_ANY, "badge", wxEmptyString, 
                                     wxDefaultSize, wxDefaultPosition, 
                                     wxBU_EXACTFIT | wxNO_BORDER, false, 18);
        m_badge->SetSize(m_badge->GetBestSize());
        m_badge->SetBackgroundColour("#F7F7F7");

        LayoutBadge();
    }
    if (m_badge) {
        m_badge->Show(show);
    }
#endif
}

#ifdef __WXOSX__
void ExtruderPanel::LayoutBadge()
{
    if (!m_badge) return;

    wxSize panel_size = GetSize();
    wxSize badge_size = m_badge->GetBestSize();

    int x = panel_size.x - badge_size.x - FromDIP(8);
    int y = FromDIP(8);

    m_badge->SetPosition(wxPoint(x, y));
}

void ExtruderPanel::OnSize(wxSizeEvent &evt)
{
    LayoutBadge();
    evt.Skip();
}
#endif

#ifdef __WXMSW__
void ExtruderPanel::OnPaint(wxPaintEvent &evt)
{
    wxPanel::OnPaint(evt);

    if (m_badge.bmp().IsOk()) {
        auto      s = m_badge.bmp().GetScaledSize();
        wxPaintDC dc(this);
        dc.DrawBitmap(m_badge.bmp(), GetSize().x - s.x - 8, 8);
    }
}
#endif

NozzleConfigDialog::NozzleConfigDialog(wxWindow                    *parent,
                                       ExtruderNozzlePanel         *nozzle_panel,
                                       const std::vector<wxString> &diameter_choices,
                                       const std::vector<wxString> &flow_choices)
    : DPIDialog(parent, wxID_ANY, _L("Nozzle Configuration"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_nozzle_panel(nozzle_panel)
    , m_diameter_choices(diameter_choices)
    , m_flow_choices(flow_choices)
{
    if (m_nozzle_panel) {
        m_original_diameter = m_nozzle_panel->GetDiameter();
        m_original_flow     = m_nozzle_panel->GetFlow();
        m_original_defined   = m_nozzle_panel->IsDefined();
    }

    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    CreateLayout();

    Bind(wxEVT_COMMAND_COMBOBOX_SELECTED, &NozzleConfigDialog::OnDiameterChanged, this, m_diameter_combo->GetId());
    Bind(wxEVT_COMMAND_COMBOBOX_SELECTED, &NozzleConfigDialog::OnFlowChanged, this, m_flow_combo->GetId());
}

void NozzleConfigDialog::CreateLayout()
{
    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *diameter_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_diameter_label = new Label(this, _L("Diameter"));
    m_diameter_label->SetFont(Label::Body_14);
    diameter_sizer->Add(m_diameter_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    diameter_sizer->AddStretchSpacer(1);

    m_diameter_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(120), -1), 0, nullptr, wxCB_READONLY);
    for (const auto &diameter : m_diameter_choices) {
        m_diameter_combo->Append(diameter);
    }
    if (m_nozzle_panel) {
        m_diameter_combo->SetValue(m_nozzle_panel->GetDiameter());
    }
    diameter_sizer->Add(m_diameter_combo, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    main_sizer->Add(diameter_sizer, 0, wxEXPAND | wxALL, FromDIP(16));

    wxBoxSizer *flow_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_flow_label = new Label(this, _L("Flow"));
    m_flow_label->SetFont(Label::Body_14);
    flow_sizer->Add(m_flow_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    flow_sizer->AddStretchSpacer(1);

    m_flow_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(120), -1), 0, nullptr, wxCB_READONLY);
    for (const auto &flow : m_flow_choices) {
        m_flow_combo->Append(flow);
    }
    if (m_nozzle_panel) {
        wxString flow_text = m_nozzle_panel->GetFlow() == nvtStandard ? _L("Standard") : _L("High Flow");
        m_flow_combo->SetValue(flow_text);
    }
    flow_sizer->Add(m_flow_combo, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    main_sizer->Add(flow_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));

    wxBoxSizer *bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto wiki_panel = new WikiPanel(this);
    bottom_sizer->Add(wiki_panel, 1, wxALIGN_CENTER_VERTICAL);

    StateColor ok_btn_bg(
        std::pair<wxColour, int>(wxColour("#1B8844"), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour("#3DCB73"), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour("#00AE42"), StateColor::Normal)
    );
    StateColor ok_btn_text(
        std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Normal)
    );

    m_ok_btn = new Button(this, _L("OK"));
    m_ok_btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
    m_ok_btn->SetCornerRadius(FromDIP(12));
    m_ok_btn->SetBackgroundColor(ok_btn_bg);
    m_ok_btn->SetFont(Label::Body_12);
    m_ok_btn->SetBorderColor(wxColour("#00AE42"));
    m_ok_btn->SetTextColor(ok_btn_text);
    m_ok_btn->SetId(wxID_OK);
    bottom_sizer->Add(m_ok_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

    main_sizer->Add(bottom_sizer, 0, wxEXPAND | wxALL, FromDIP(16));

    SetSizer(main_sizer);
    main_sizer->Fit(this);

    wxGetApp().UpdateDlgDarkUI(this);
}

void NozzleConfigDialog::OnDiameterChanged(wxCommandEvent& event)
{
    m_diameter_changed = m_diameter_combo->GetValue() != m_original_diameter;
    wxString selected_diameter = m_diameter_combo->GetValue();
    wxString current_flow      = m_flow_combo->GetValue();
    std::vector<wxString> available_flows;
    auto preset_bundle = wxGetApp().preset_bundle;
    auto printer_preset = preset_bundle->get_similar_printer_preset({}, selected_diameter.ToStdString());

    auto extruder_variants  = printer_preset->config.option<ConfigOptionStrings>("extruder_variant_list");
    auto extruders_def      = printer_preset->config.def()->get("extruder_type");
    auto extruders          = printer_preset->config.option<ConfigOptionEnumsGeneric>("extruder_type");
    auto nozzle_volumes_def = preset_bundle->project_config.def()->get("nozzle_volume_type");

    int extruder_index = 0;
    if (m_nozzle_panel) {
        ExtruderPanel* panel = dynamic_cast<ExtruderPanel*>(m_nozzle_panel->GetGrandParent());
        if (panel) {
            if (panel->GetType() == ExtruderPanel::RightExtruder) {
                extruder_index = 1;
            }
        }
    }

    if (extruder_index < extruder_variants->size() && extruder_index < extruders->values.size()) {
        auto type = extruders_def->enum_labels[extruders->values[extruder_index]];
        for (size_t i = 0; i < nozzle_volumes_def->enum_labels.size(); ++i) {
            if (boost::algorithm::contains(extruder_variants->values[extruder_index], type + " " + nozzle_volumes_def->enum_labels[i])) {
                if (selected_diameter == "0.2" && 
                    nozzle_volumes_def->enum_keys_map->at(nozzle_volumes_def->enum_values[i]) == NozzleVolumeType::nvtHighFlow) {
                    continue;
                }
                available_flows.push_back(_L(nozzle_volumes_def->enum_labels[i]));
            }
        }
    }

    if (available_flows.empty()) {
        available_flows = m_flow_choices;
    }
    
    m_flow_combo->Clear();

    for (const auto& flow : available_flows) {
        m_flow_combo->Append(flow);
    }

    if (std::find(available_flows.begin(), available_flows.end(), current_flow) != available_flows.end()) {
        m_flow_combo->SetValue(current_flow);
    } else if (!available_flows.empty()) {
        m_flow_combo->SetValue(available_flows[0]);
    }
}

void NozzleConfigDialog::OnFlowChanged(wxCommandEvent& event)
{
    wxString current_flow = m_flow_combo->GetValue();
    wxString original_flow = m_original_flow == nvtStandard ? _L("Standard") : _L("High Flow");
    m_flow_changed = current_flow != original_flow;
}

void NozzleConfigDialog::OnOKClicked(wxCommandEvent &event)
{
    if (!m_nozzle_panel) {
        EndModal(wxID_OK);
        return;
    }

    wxString new_diameter = m_diameter_combo->GetValue();
    wxString flow_text    = m_flow_combo->GetValue();

    NozzleVolumeType new_flow = nvtStandard;
    if (flow_text == _L("High Flow")) { new_flow = nvtHighFlow; }

    m_nozzle_panel->SetNozzleConfig(new_diameter, new_flow);

    EndModal(wxID_OK);
}

void NozzleConfigDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int em = em_unit();
    msw_buttons_rescale(this, em, { wxID_OK });

    Fit();
    Refresh();
}

DiameterButtonPanel::DiameterButtonPanel(wxWindow* parent, const std::vector<wxString>& diameter_choices)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_choices(diameter_choices)
{
    SetBackgroundColour(wxColour("#F7F7F7"));
    if (!m_choices.empty()) {
        m_selected_diameter = m_choices[0];
    }

    CreateLayout();

    wxGetApp().UpdateDarkUIWin(this);
}

void DiameterButtonPanel::CreateLayout()
{
    if (GetSizer()) {
        GetSizer()->Clear(true);
    }
    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    auto title      = new Label(this, _L("Nozzle diameter"));
    title->SetFont(Label::Body_12);
    title->SetForegroundColour(wxColour("#999999"));
    main_sizer->Add(title, 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(4));
    auto button_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_buttons.clear();
    for (const auto &diameter : m_choices) {
        auto btn = new Button(this, diameter);
        btn->SetMinSize(wxSize(FromDIP(36), FromDIP(24)));
        btn->SetMaxSize(wxSize(FromDIP(36), FromDIP(24)));
        btn->SetFont(Label::Body_12);
        btn->SetCornerRadius(FromDIP(6));

        UpdateSingleButtonState(btn, diameter);
        btn->Bind(wxEVT_BUTTON, &DiameterButtonPanel::OnButtonClicked, this);
        m_buttons.push_back(btn);
        button_sizer->Add(btn, 0, wxRIGHT, FromDIP(4));
    }

    main_sizer->Add(button_sizer, 0, wxALIGN_LEFT);
    SetSizer(main_sizer);
    Layout();
}

void DiameterButtonPanel::RefreshLayout(const std::vector<wxString>& choices)
{
    if (choices == m_choices) {
        return;
    }
    m_choices = choices;
    bool selected_exists = std::find(m_choices.begin(), m_choices.end(), m_selected_diameter) != m_choices.end();
    if (!selected_exists && !m_choices.empty()) {
        m_selected_diameter = m_choices[0];
    }

    CreateLayout();

    Refresh();
    Update();
}

void DiameterButtonPanel::OnButtonClicked(wxCommandEvent& event)
{
    Button* clicked_btn = dynamic_cast<Button*>(event.GetEventObject());
    if (!clicked_btn) return;

    wxString diameter = clicked_btn->GetLabel();
    SetSelectedDiameter(diameter);
    event.Skip();
}

void DiameterButtonPanel::SetSelectedDiameter(const wxString &diameter)
{
    if (m_selected_diameter == diameter) return;

    wxString original_diameter = m_selected_diameter;
    m_selected_diameter = diameter;
    UpdateButtonStates();

    wxCommandEvent evt(EVT_NOZZLE_DIAMETER_SELECTED, GetId());
    evt.SetEventObject(this);
    evt.SetString(diameter);
    evt.SetClientData(new wxString(original_diameter));
    GetEventHandler()->ProcessEvent(evt);
}

void DiameterButtonPanel::UpdateButtonStates()
{
    assert(m_buttons.size() == m_choices.size());
    for (size_t i = 0; i < m_buttons.size() && i < m_choices.size(); ++i) {
        UpdateSingleButtonState(m_buttons[i], m_choices[i]);
    }
}

void DiameterButtonPanel::UpdateSingleButtonState(Button *btn, const wxString &diameter)
{
    if (!btn) return;

    ButtonState state = NoNozzle;

    if (diameter == m_selected_diameter) {
        state = Selected;
    } else {
        if (m_nozzle_query_callback) {
            bool has_nozzle = m_nozzle_query_callback(diameter);
            state = has_nozzle ? HasNozzle : NoNozzle;
        } else {
            state = NoNozzle;
        }
    }

    StateColor bg_color, text_color;

    switch (state) {
        case Selected:
            bg_color.append(SelectedBgColor, StateColor::Normal);
            text_color.append(SelectedTextColor, StateColor::Normal);
            break;
        case HasNozzle:
            bg_color.append(HasNozzleBgColor, StateColor::Normal);
            text_color.append(HasNozzleTextColor, StateColor::Normal);
            break;
        case NoNozzle:
            bg_color.append(NoNozzleBgColor, StateColor::Normal);
            text_color.append(NoNozzleTextColor, StateColor::Normal);
            break;
    }

    btn->SetBackgroundColor(bg_color);
    btn->SetTextColor(text_color);
    btn->Refresh();
}

DiameterButtonPanel::ButtonState DiameterButtonPanel::GetButtonState(const wxString &diameter) const
{
    for (size_t i = 0; i < m_choices.size(); ++i) {
        if (m_choices[i] == diameter && i < m_buttons.size()) {
            Button *btn = m_buttons[i];

            if (i == m_selected_index) {
                return ButtonState::Selected;
            }

            if (m_nozzle_query_callback && m_nozzle_query_callback(diameter)) {
                return ButtonState::HasNozzle;
            } else {
                return ButtonState::NoNozzle;
            }
        }
    }

    return ButtonState::NoNozzle;
}

}} // namespace Slic3r::GUI