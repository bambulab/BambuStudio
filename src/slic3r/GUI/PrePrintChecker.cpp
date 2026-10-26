#include "PrePrintChecker.hpp"

#include "MainFrame.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "I18N.hpp"
#include "SelectMachine.hpp"

#include "DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevPrintOptions.h"
#include "slic3r/GUI/DeviceCore/DevUpgrade.h"

#include <algorithm>
#include <set>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>

namespace Slic3r { namespace GUI {

class WikiWarningLabel : public wxPanel
{
public:
    WikiWarningLabel(
        wxWindow* parent,
        const wxString& prefix,
        const wxString& link,
        const wxString& suffix,
        const wxString& url,
        const wxColour& colour,
        int width)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(width, 1), wxBORDER_NONE)
        , m_prefix(prefix)
        , m_link(link)
        , m_suffix(suffix)
        , m_url(url)
        , m_colour(colour)
        , m_width(width)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(parent->GetBackgroundColour());
        SetCursor(wxCURSOR_ARROW);
        layout_text();

        Bind(wxEVT_PAINT, &WikiWarningLabel::on_paint, this);
        Bind(wxEVT_MOTION, &WikiWarningLabel::on_motion, this);
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
            m_link_hovered = false;
            SetCursor(wxCURSOR_ARROW);
        });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
            if (hit_link(event.GetPosition()))
                wxLaunchDefaultBrowser(m_url);
        });
    }

private:
    struct TextRun {
        wxString text;
        wxPoint position;
        wxSize size;
        bool is_link;
    };

    void layout_text()
    {
        wxClientDC dc(this);
        dc.SetFont(Label::Body_13);
        const int line_height = dc.GetCharHeight();
        int x = 0;
        int y = 0;
        int line_count = 1;

        auto place_token = [&](const wxString& token, bool is_link) {
            if (token.empty()) return;
            dc.SetFont(is_link ? Label::Body_13.Underlined() : Label::Body_13);
            const wxSize size = dc.GetTextExtent(token);
            const bool is_space = token == " " || token == "\t";
            if (!is_space && x > 0 && x + size.x > m_width) {
                x = 0;
                y += line_height;
                ++line_count;
            }
            if (is_space && x == 0) return;
            m_runs.push_back({token, wxPoint(x, y), wxSize(size.x, line_height), is_link});
            if (is_link)
                m_link_rects.emplace_back(x, y, size.x, line_height);
            x += size.x;
        };

        auto append_text = [&](const wxString& text, bool is_link) {
            wxString token;
            auto flush_token = [&]() {
                if (!token.empty()) {
                    place_token(token, is_link);
                    token.clear();
                }
            };

            for (const wxUniChar ch : text) {
                if (ch == '\n') {
                    flush_token();
                    x = 0;
                    y += line_height;
                    ++line_count;
                } else if (ch == ' ' || ch == '\t') {
                    flush_token();
                    wxString whitespace;
                    whitespace += ch;
                    place_token(whitespace, is_link);
                } else if (ch.GetValue() >= 0x2E80) {
                    flush_token();
                    wxString character;
                    character += ch;
                    place_token(character, is_link);
                } else {
                    token += ch;
                }
            }
            flush_token();
        };

        append_text(m_prefix, false);
        append_text(m_link, true);
        append_text(m_suffix, false);

        const int height = std::max(line_height, y + line_height);
        SetMinSize(wxSize(m_width, height));
        SetMaxSize(wxSize(m_width, height));
    }

    void on_paint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();
        dc.SetTextForeground(m_colour);
        for (const auto& run : m_runs) {
            dc.SetFont(run.is_link ? Label::Body_13.Underlined() : Label::Body_13);
            dc.DrawText(run.text, run.position);
        }
    }

    void on_motion(wxMouseEvent& event)
    {
        const bool is_link = hit_link(event.GetPosition());
        if (is_link != m_link_hovered) {
            m_link_hovered = is_link;
            SetCursor(is_link ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
        }
    }

    bool hit_link(const wxPoint& point) const
    {
        return std::any_of(m_link_rects.begin(), m_link_rects.end(), [&point](const wxRect& rect) {
            return rect.Contains(point);
        });
    }

private:
    wxString m_prefix;
    wxString m_link;
    wxString m_suffix;
    wxString m_url;
    wxColour m_colour;
    int m_width;
    bool m_link_hovered{false};
    std::vector<TextRun> m_runs;
    std::vector<wxRect> m_link_rects;
};

std::string PrePrintChecker::get_print_status_info(PrintDialogStatus status)
{
    switch (status)
    {
    case PrintStatusInit: return "PrintStatusInit";
    case PrintStatusNoUserLogin: return "PrintStatusNoUserLogin";
    case PrintStatusInvalidPrinter: return "PrintStatusInvalidPrinter";
    case PrintStatusConnectingServer: return "PrintStatusConnectingServer";
    case PrintStatusReadingTimeout: return "PrintStatusReadingTimeout";
    case PrintStatusReading: return "PrintStatusReading";
    case PrintStatusConnecting: return "PrintStatusConnecting";
    case PrintStatusReconnecting: return "PrintStatusReconnecting";
    case PrintStatusInUpgrading: return "PrintStatusInUpgrading";
    case PrintStatusModeNotFDM: return "PrintStatusModeNotFDM";
    case PrintStatusInSystemPrinting: return "PrintStatusInSystemPrinting";
    case PrintStatusInColdPull: return "PrintStatusInColdPull";
    case PrintStatusInPrinting: return "PrintStatusInPrinting";
    case PrintStatusNozzleMatchInvalid: return "PrintStatusNozzleMatchInvalid";
    case PrintStatusNozzleNoMatchedHotends: return "PrintStatusNozzleNoMatchedHotends";
    case PrintStatusNozzleRackMaximumInstalled: return "PrintStatusNozzleRackMaximumInstalled";
    case PrintStatusNozzleDataInvalid: return "PrintStatusNozzleDataInvalid";
    case PrintStatusNozzleDiameterMismatch: return "PrintStatusNozzleDiameterMismatch";
    case PrintStatusNozzleHRCMismatch: return "PrintStatusNozzleTypeMismatch";
    case PrintStatusRefreshingMachineList: return "PrintStatusRefreshingMachineList";
    case PrintStatusSending: return "PrintStatusSending";
    case PrintStatusLanModeNoSdcard: return "PrintStatusLanModeNoSdcard";
    case PrintStatusNoSdcard: return "PrintStatusNoSdcard";
    case PrintStatusLanModeSDcardNotAvailable: return "PrintStatusLanModeSDcardNotAvailable";
    case PrintStatusNeedForceUpgrading: return "PrintStatusNeedForceUpgrading";
    case PrintStatusNeedConsistencyUpgrading: return "PrintStatusNeedConsistencyUpgrading";
    case PrintStatusNotSupportedPrintAll: return "PrintStatusNotSupportedPrintAll";
    case PrintStatusNotSupportedMultiNozzleDiameterFromSdcard: return "PrintStatusNotSupportedMultiNozzleDiameterFromSdcard";
    case PrintStatusBlankPlate: return "PrintStatusBlankPlate";
    case PrintStatusSliceResultInvalid: return "PrintStatusSliceResultInvalid";
    case PrintStatusUnsupportedPrinter: return "PrintStatusUnsupportedPrinter";
    case PrintStatusColorQuantityExceed: return "PrintStatusColorQuantityExceed";
    // Handle filament errors
    case PrintStatusRackReading: return "PrintStatusRackReading";
    case PrintStatusRackMoving: return "PrintStatusRackMoving";
    case PrintStatusNoAvailableHotend: return "PrintStatusNoAvailableHotend";
    case PrintStatusHotendLost: return "PrintStatusHotendLost";
    case PrintStatusCutterLost: return "PrintStatusCutterLost";
    case PrintStatusPenLost: return "PrintStatusPenLost";
    case PrintStatusNoneHotendDockingCali: return "PrintStatusNoneHotendDockingCali";
    case PrintStatusRackNozzleMappingWaiting: return "PrintStatusRackNozzleMappingWaiting";
    case PrintStatusRackNozzleMappingError: return "PrintStatusRackNozzleMappingError";
    case PrintStatusInvalidMapping: return "PrintStatusInvalidMapping";
    case PrintStatusAmsOnSettingup: return "PrintStatusAmsOnSettingup";
    case PrintStatusAmsMappingInvalid: return "PrintStatusAmsMappingInvalid";
    case PrintStatusAmsMappingU0Invalid: return "PrintStatusAmsMappingU0Invalid";
    case PrintStatusAmsMappingMixInvalid: return "PrintStatusAmsMappingMixInvalid";
    case PrintStatusTPUUnsupportAutoCali: return "PrintStatusTPUUnsupportAutoCali";
    case PrintStatusHasFilamentInBlackListError: return "PrintStatusHasFilamentInBlackListError";
    case PrintStatusTimelapseNoSdcard: return "PrintStatusTimelapseNoSdcard";
    case PrintStatusTimelapseWarning: return "PrintStatusTimelapseWarning";
    case PrintStatusMixAmsAndVtSlotWarning: return "PrintStatusMixAmsAndVtSlotWarning";
    case PrintStatusToolHeadCoolingFanWarning: return "PrintStatusToolHeadCoolingFanWarning";
    case PrintStatusHasUnreliableNozzleWarning: return "PrintStatusRackHasUnreliableNozzleWarning";
    case PrintStatusRackNozzleNumUnmeetWarning: return "PrintStatusRackNozzleNumUnmeetWarning";
    case PrintStatusRackNozzleMappingWarning: return "PrintStatusRackNozzleMappingWarning";
    case PrintStatusWarningKvalueNotUsed: return "PrintStatusWarningKvalueNotUsed";
    case PrintStatusHasFilamentInBlackListWarning: return "PrintStatusHasFilamentInBlackListWarning";
    case PrintStatusHasFilamentInBlackListCriticalWarning: return "PrintStatusHasFilamentInBlackListCriticalWarning";
    case PrintStatusFilamentWarningHighChamberTemp: return "PrintStatusFilamentWarningHighChamberTemp";
    case PrintStatusFilamentWarningHighChamberTempCloseDoor: return "PrintStatusFilamentWarningHighChamberTempCloseDoor";
    case PrintStatusFilamentWarningHighChamberTempSoft: return "PrintStatusFilamentWarningHighChamberTempSoft";
    case PrintStatusFilamentWarningUnknownHighChamberTempSoft: return "PrintStatusFilamentWarningUnknownHighChamberTempSoft";
    case PrintStatusFilamentWarningRemainNotEnough: return "PrintStatusFilamentWarningRemainNotEnough";
    case PrintStatusSmartNozzleBlobNeedAuto: return "PrintStatusSmartNozzleBlobNeedAuto";
    case PrintStatusFilamentSuspectEngagementWarning: return "PrintStatusFilamentSuspectEngagementWarning";
    case PrintStatusExtSpoolNotLoadedWarning: return "PrintStatusExtSpoolNotLoadedWarning";
    case PrintStatusAmsSlotNotLoadedWarning: return "PrintStatusAmsSlotNotLoadedWarning";
    case PrintStatusRackAmsNeedManualUnloadWarning: return "PrintStatusRackAmsNeedManualUnloadWarning";
    case PrintStatusRackAmsMixExtAmsWarning: return "PrintStatusRackAmsMixExtAmsWarning";
    case PrintStatusFilamentWarningHotendLowTempResidue: return "PrintStatusFilamentWarningHotendLowTempResidue";
    case PrintStatusTpuSlipOnToolChangeWarning: return "PrintStatusTpuSlipOnToolChangeWarning";
    case PrintStatusReadingFinished: return "PrintStatusReadingFinished";
    case PrintStatusSendingCanceled: return "PrintStatusSendingCanceled";
    case PrintStatusAmsMappingSuccess: return "PrintStatusAmsMappingSuccess";
    case PrintStatusReadyToGo: return "PrintStatusReadyToGo";
    case PrintStatusNotOnTheSameLAN: return "PrintStatusNotOnTheSameLAN";
    case PrintStatusNotSupportedSendToSDCard: return "PrintStatusNotSupportedSendToSDCard";
    case PrintStatusPublicInitFailed: return "PrintStatusPublicInitFailed";
    case PrintStatusPublicUploadFiled: return "PrintStatusPublicUploadFiled";
    default: return "Unknown status";
    }
}

wxString PrePrintChecker::get_pre_state_msg(PrintDialogStatus status)
{
    switch (status) {
    case PrintStatusNoUserLogin: return _L("No login account, only printers in LAN mode are displayed");
    case PrintStatusConnectingServer: return _L("Connecting to server");
    case PrintStatusReading: return _L("Synchronizing device information");
    case PrintStatusReadingTimeout: return _L("Synchronizing device information time out");
    case PrintStatusModeNotFDM: return _L("Cannot send the print job when the printer is not at FDM mode");
    case PrintStatusInUpgrading: return _L("Cannot send the print job when the printer is updating firmware");
    case PrintStatusInSystemPrinting: return _L("The printer is executing instructions. Please restart printing after it ends");
    case PrintStatusInColdPull: return _L("Cold pull in progress. Unable to start a task.");
    case PrintStatusInPrinting: return _L("The printer is busy on other print job");
    case PrintStatusAmsOnSettingup: return _L("AMS is setting up. Please try again later.");
    case PrintStatusAmsMappingInvalid: return _L("Not all filaments used in slicing are mapped to the printer. Please check the mapping of filaments.");
    case PrintStatusAmsMappingMixInvalid: return _L("Please do not mix-use the Ext with AMS");
    case PrintStatusNozzleDataInvalid: return _L("Invalid nozzle information, please refresh or manually set nozzle information.");
    case PrintStatusLanModeNoSdcard: return _L("Storage needs to be inserted before printing via LAN.");
    case PrintStatusLanModeSDcardNotAvailable: return _L("Storage is not available or is in read-only mode.");
    case PrintStatusNoSdcard: return _L("Storage needs to be inserted before printing.");
    case PrintStatusNeedForceUpgrading: return _L("Cannot send the print job to a printer whose firmware is required to get updated.");
    case PrintStatusNeedConsistencyUpgrading: return _L("Cannot send the print job to a printer whose firmware is required to get updated.");
    case PrintStatusBlankPlate: return _L("Cannot send the print job for empty plate");
    case PrintStatusSliceResultInvalid: return _L("The slicing result of the current plate is invalid. Please re-slice and try again");
    case PrintStatusTimelapseNoSdcard: return _L("Storage needs to be inserted to record timelapse.");
    case PrintStatusMixAmsAndVtSlotWarning: return _L("You have selected both external and AMS filaments for an extruder. You will need to manually switch the external filament during printing.");
    case PrintStatusFilamentWarningHotendLowTempResidue: return _L("Low-temperature filament residue detected in Hotend. High chamber temperature printing may cause nozzle clogging. Please load filament first.");
    case PrintStatusTPUUnsupportAutoCali: 
        return _L("TPU 90A/TPU 85A are too soft. It is recommended to perform manual flow calibration on the 'Calibration' page. "
                  "If 'Dynamic Flow Calibration' is set to auto/on, the system will use the previous calibration value and skip the flow calibration process.");
    case PrintStatusTPUUnsupportCaliOn:
        return _L("TPU 90A/TPU 85A are too soft. It is recommended to perform manual flow calibration on the 'Calibration' page. "
                  "If 'Dynamic Flow Calibration' is set to auto/on, the system will use the previous calibration value and skip the flow calibration process.");
    case PrintStatusWarningKvalueNotUsed: return _L("Set dynamic flow calibration to 'OFF' to enable custom dynamic flow value.");
    case PrintStatusNotSupportedPrintAll: return _L("This printer does not support printing all plates");
    case PrintStatusNotSupportedMultiNozzleDiameterFromSdcard:
        return _L("Printing a project that uses multiple nozzle diameters directly from storage is not supported yet. "
                  "You can download the file and import it into Studio to print.");
    case PrintStatusHasUnreliableNozzleWarning: return _L("Please check if the required nozzle diameter and flow rate match the current display.");
    case PrintStatusColorQuantityExceed: return _L("The current firmware supports a maximum of %s materials. You can either reduce the number of materials to %s or fewer on the Preparation Page, or try updating the firmware. If you are still restricted after the update, please wait for subsequent firmware support.");
    }
    return wxEmptyString;
}

void PrePrintChecker::clear()
{
    printerList.clear();
    filamentList.clear();
}

void PrePrintChecker::add(PrintDialogStatus state, wxString msg, wxString tip, const wxString& wiki_url, prePrintInfoStyle style)
{
    prePrintInfo info;

    if (is_error(state)) {
        info.level = prePrintInfoLevel::Error;
    } else if (is_warning(state)) {
        info.level = prePrintInfoLevel::Warning;
    } else {
        info.level = prePrintInfoLevel::Normal;
    }

    if (is_error_printer(state)) {
        info.type = prePrintInfoType::Printer;
    } else if (is_error_filament(state)) {
        info.type = prePrintInfoType::Filament;
    } else if (is_warning_printer(state)) {
        info.type = prePrintInfoType::Printer;
    } else if (is_warning_filament(state)) {
        info.type = prePrintInfoType::Filament;
    }

    if (!msg.IsEmpty()) {
        info.msg  = msg;
        info.tips = tip;
    } else {
        info.msg  = get_pre_state_msg(state);
        info.tips = wxEmptyString;
    }

    info.wiki_url = wiki_url;
    info.m_style = style;

    switch (info.type) {
    case prePrintInfoType::Filament:
        if (std::find(filamentList.begin(), filamentList.end(), info) == filamentList.end()) {
            filamentList.push_back(info);
        }
        break;
    case prePrintInfoType::Printer:
        if (std::find(printerList.begin(), printerList.end(), info) == printerList.end()) {
            printerList.push_back(info);
        }
        break;
    default: break;
    }
}

void PrePrintChecker::add_with_link(PrintDialogStatus state, wxString msg, wxString link_label, std::function<void()> callback, prePrintInfoStyle style)
{
    prePrintInfo info;
    info.level = is_error(state) ? prePrintInfoLevel::Error :
                 is_warning(state) ? prePrintInfoLevel::Warning : prePrintInfoLevel::Normal;
    info.type  = (is_error_printer(state) || is_warning_printer(state)) ? prePrintInfoType::Printer : prePrintInfoType::Filament;
    info.msg   = msg;
    info.link_label    = link_label;
    info.link_callback = callback;
    info.m_style = style;

    auto& list = (info.type == prePrintInfoType::Printer) ? printerList : filamentList;
    if (std::find(list.begin(), list.end(), info) == list.end()) {
        list.push_back(info);
    }
}

PrinterMsgPanel::PrinterMsgPanel(wxWindow *parent, SelectMachineDialog* select_dialog)
    : wxPanel(parent), m_select_dialog(select_dialog)
{
    m_sizer = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(m_sizer);
}

void PrinterMsgPanel::Clear()
{
    m_infos.clear();
    m_not_show_again_infos.clear();
    ClearGUI();
}

void PrinterMsgPanel::ClearGUI()
{
    m_sizer->Clear(true);
    m_scale_btns.clear();
    m_ctrl_btns.clear();
}

static wxColour _GetLabelColour(const prePrintInfo& info)
{
    if (info.level == Error)
    {
        return wxColour("#D01B1B");
    }
    else if (info.level == Warning)
    {
        return wxColour("#FF6F00");
    }

    return *wxBLACK; // Default colour for normal messages
}

bool PrinterMsgPanel::UpdateInfos(const std::vector<prePrintInfo>& infos)
{
    if (m_infos == infos)
    {
        return false;
    }
    m_infos = infos;

    ClearGUI();
    for (const prePrintInfo& info : infos)
    {
        if (m_not_show_again_infos.count(info) != 0) {
            continue;
        };

        if (!info.msg.empty())
        {
            const bool use_wiki_rich_text =
                !info.testStyle(prePrintInfoStyle::BtnSwitchNozzleBlobAuto) &&
                !info.link_callback && !info.wiki_url.empty();
            Label* label = nullptr;
            wxWindow* message_widget = nullptr;
            ScalableButton* btn = CreateTypeButton(info);
            const int message_width = this->GetMinSize().GetWidth() - btn->GetSize().x - FromDIP(6);

            if (!use_wiki_rich_text) {
                label = new Label(this);
                label->SetFont(::Label::Body_13);
                label->SetForegroundColour(_GetLabelColour(info));
                message_widget = label;
            }

            if (info.testStyle(prePrintInfoStyle::BtnSwitchNozzleBlobAuto))
            {
                label->SetLabel(info.msg + " " + _L("Switch"));
                label->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
                label->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
                label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
                    auto obj_ = m_select_dialog ? m_select_dialog->get_current_machine() : nullptr;
                    if (obj_ && obj_->GetPrintOptions()) {
                        obj_->GetPrintOptions()->command_smart_nozzle_blob_detect_mode(2);
                    }
                });
            }
            else if (!info.link_callback && info.wiki_url.empty())
            {
                // plain text, no link
                label->SetLabel(info.msg);
            }
            else if (info.link_callback)
            {
                // internal callback link (e.g. "Clean up files" → navigate to device page)
                wxString link_text = info.link_label.empty() ? _L("Clean up files") : info.link_label;
                label->SetLabel(info.msg + " " + link_text);
                label->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
                label->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
                label->Bind(wxEVT_LEFT_DOWN, [info](wxMouseEvent& event) {
                    if (info.link_callback) info.link_callback();
                });
            }
            else
            {
                message_widget = new WikiWarningLabel(
                    this,
                    info.msg + _L(" Please "),
                    _L("refer to the Wiki User Guide"),
                    _L(" before continuing."),
                    info.wiki_url,
                    _GetLabelColour(info),
                    message_width);
            }

            if (label)
                label->Wrap(message_width);

            wxSizer* msg_sizer = new wxBoxSizer(wxHORIZONTAL);
            msg_sizer->Add(btn, 0, wxLEFT | wxTOP, FromDIP(2));
            msg_sizer->AddSpacer(FromDIP(2));
            msg_sizer->Add(message_widget, 0, wxLEFT | wxBOTTOM);
            msg_sizer->Layout();
            m_sizer->Add(msg_sizer, 0, wxBOTTOM, FromDIP(4));

            // some special styles
            AppendStyles(info);
        }
    }

    this->Show();
    this->Layout();

    Fit();

    return true;
}

void PrinterMsgPanel::Rescale()
{
    for (auto item : m_scale_btns) {
        item->msw_rescale();
    }

    for (auto item : m_ctrl_btns) {
        item->Rescale();
    }

    Layout();
    Fit();
}

ScalableButton* PrinterMsgPanel::CreateTypeButton(const prePrintInfo& info)
{
    ScalableButton* btn = nullptr;
    if (info.level == Error)         {
        btn = new ScalableButton(this, wxID_ANY, "dev_error");
    } else if (info.level == Warning) {
        btn = new ScalableButton(this, wxID_ANY, "dev_warning");
    } else {
        btn = new ScalableButton(this, wxID_ANY, "dev_warning");
    }

    btn->SetBackgroundColour(*wxWHITE);
    btn->SetMaxSize(wxSize(FromDIP(16), FromDIP(16)));
    btn->SetMinSize(wxSize(FromDIP(16), FromDIP(16)));
    btn->SetSize(wxSize(FromDIP(16), FromDIP(16)));
    wxGetApp().UpdateDarkUI(btn);
    m_scale_btns.push_back(btn);
    return btn;
};

static Label* s_create_btn_label(PrinterMsgPanel* panel, const wxString& btn_name)
{
    Label* btn = new Label(panel, btn_name);
    btn->SetFont(Label::Body_13);
    auto font = btn->GetFont();
    font.SetUnderlined(true);
    btn->SetFont(font);
    btn->SetBackgroundColour(*wxWHITE);
    btn->SetForegroundColour(wxColour("#00AE42"));

    btn->Bind(wxEVT_ENTER_WINDOW, [panel](auto &e) { panel->SetCursor(wxCURSOR_HAND); });
    btn->Bind(wxEVT_LEAVE_WINDOW, [panel](auto &e) { panel->SetCursor(wxCURSOR_ARROW); });

    wxGetApp().UpdateDarkUI(btn);
    return btn;
}

void PrinterMsgPanel::AppendStyles(const prePrintInfo& info)
{
    // special styles
    if (info.testStyle(prePrintInfoStyle::BtnNozzleRefresh) ||
        info.testStyle(prePrintInfoStyle::BtnConfirmNotShowAgain) ||
        info.testStyle(prePrintInfoStyle::BtnInstallFanF000) ||
        info.testStyle(prePrintInfoStyle::BtnJumpToUpgrade)) {
        wxBoxSizer* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
        if (info.testStyle(prePrintInfoStyle::BtnNozzleRefresh)){
            auto btn = s_create_btn_label(this, _L("Refresh"));
            btn->Bind(wxEVT_LEFT_DOWN, &PrinterMsgPanel::OnRefreshNozzleBtnClicked, this);
            btn_sizer->Add(btn, 0, wxLEFT, FromDIP(16));
        }

        if (info.testStyle(prePrintInfoStyle::BtnConfirmNotShowAgain)) {
            auto btn = s_create_btn_label(this, _L("Confirm"));
            btn->Bind(wxEVT_LEFT_DOWN, [this, info](auto& e) {
                this->OnNotShowAgain(info);
            });

            btn_sizer->Add(btn, 0, wxLEFT, FromDIP(16));
        }

        if (info.testStyle(prePrintInfoStyle::BtnInstallFanF000)) {
            auto btn = s_create_btn_label(this, _L("How to install"));
            btn->Bind(wxEVT_LEFT_DOWN, [this, info](auto& e) {
                wxLaunchDefaultBrowser("https://e.bambulab.com/t?c=l3T7caKGeNt3omA9");
            });

            btn_sizer->Add(btn, 0, wxLEFT, FromDIP(16));
        }

        if (info.testStyle(prePrintInfoStyle::BtnJumpToUpgrade)) {
            auto btn = s_create_btn_label(this, _L("Upgrade"));
            btn->Bind(wxEVT_LEFT_DOWN, &PrinterMsgPanel::OnUpgradeBtnClicked, this);
            btn_sizer->Add(btn, 0, wxLEFT, FromDIP(16));
        }

        m_sizer->Add(btn_sizer, 0, wxLEFT);
        m_sizer->AddSpacer(FromDIP(4));
    }

    if (info.testStyle(prePrintInfoStyle::NozzleState)) {
        NozzleStatePanel* nozzle_info = new NozzleStatePanel(this);
        nozzle_info->UpdateInfoBy(m_select_dialog->get_plater(), m_select_dialog->get_current_machine());
        // Full message width, no indent
        m_sizer->Add(nozzle_info, 0, wxEXPAND);
        m_sizer->AddSpacer(FromDIP(4));
    }
}

void PrinterMsgPanel::OnRefreshNozzleBtnClicked(wxMouseEvent& event)
{
    auto obj_ = m_select_dialog->get_current_machine();
    if (obj_) {
        obj_->GetNozzleSystem()->GetNozzleRack()->CtrlRackReadAll(true);
    }
}

void PrinterMsgPanel::OnNotShowAgain(const prePrintInfo& info)
{
    m_not_show_again_infos.insert(info);

    auto cp_infos = m_infos;
    m_infos.clear();
    UpdateInfos(cp_infos);
}

void PrinterMsgPanel::OnUpgradeBtnClicked(wxMouseEvent& event)
{
    auto obj_ = m_select_dialog ? m_select_dialog->get_current_machine() : nullptr;
    if (!obj_) {
        return;
    }

    m_select_dialog->Hide();
    if (Slic3r::GUI::wxGetApp().mainframe && Slic3r::GUI::wxGetApp().mainframe->m_monitor) {
        Slic3r::GUI::wxGetApp().mainframe->jump_to_monitor();
        Slic3r::GUI::wxGetApp().mainframe->m_monitor->jump_to_Upgrade();
    }
}

}
};
