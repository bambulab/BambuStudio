#include "PrintResultDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include <wx/clipbrd.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace pt = boost::property_tree;
typedef pt::ptree JSON;

namespace Slic3r { 
namespace GUI {

	wxDECLARE_EVENT(EVT_CLOSE_DIALOG, SimpleEvent);
	wxDEFINE_EVENT(EVT_CLOSE_DIALOG, SimpleEvent);


PrintResultDialog::PrintResultDialog(PrintSummary* s)
	: DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _L("Print Summery"), wxDefaultPosition,
	wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	//bind_handlers();
	summary = s;
	const int em = GUI::wxGetApp().em_unit();
	int min_width = WIN_WIDTH * em;
	int min_height = MIN_HEIGHT * em;
	int max_width = WIN_WIDTH * em;
	int max_height = MAX_HEIGHT * em;

	SetFont(wxGetApp().normal_font());
	wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	SetBackgroundColour(bgr_clr);

	wxScrolledWindow* panel = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	panel->SetMinSize(wxSize(-1, 400));
	int pixelsPerUnixX = 10;
	int pixelsPerUnixY = 10;
	int noUnitsX = 1000;
	int noUnitsY = 1000;
	panel->SetScrollbars(pixelsPerUnixX, pixelsPerUnixY, noUnitsX, noUnitsY);
	

	// Create GUI components and layout
	auto user_info_sizer = new wxBoxSizer(wxVERTICAL);
	panel->SetSizer(user_info_sizer);

	m_label_title = new wxStaticText(panel, wxID_ANY, _L("Print Result Summary:"), wxDefaultPosition, wxDefaultSize);

	wxStaticText* m_label_1 = new wxStaticText(panel, wxID_ANY, _L("1. Printing Result?"), wxDefaultPosition, wxDefaultSize);
	m_radio_result1 = new wxRadioButton(panel, wxID_ANY, _L("Printing Finished, Model is fine"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_radio_result2 = new wxRadioButton(panel, wxID_ANY, _L("Printing Finished, Model is failed"), wxDefaultPosition, wxDefaultSize);
	m_radio_result3 = new wxRadioButton(panel, wxID_ANY, _L("Printing Abort, just for testing"), wxDefaultPosition, wxDefaultSize);
	m_radio_result4 = new wxRadioButton(panel, wxID_ANY, _L("Printing Abort, Model is failed"), wxDefaultPosition, wxDefaultSize);

	
	wxStaticText* m_label_ams = new wxStaticText(panel, wxID_ANY, _L("2. Print with?"), wxDefaultPosition, wxDefaultSize);
	m_radio_single = new wxRadioButton(panel, wxID_ANY, _L("Single meterial"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_radio_ams = new wxRadioButton(panel, wxID_ANY, _L("AMS, multi meterials"), wxDefaultPosition, wxDefaultSize);
	m_radio_other = new wxRadioButton(panel, wxID_ANY, _L("Other verifications"), wxDefaultPosition, wxDefaultSize);
	m_btn_submit = new wxButton(panel, wxID_ANY, _L("Submit"), wxDefaultPosition, wxDefaultSize);
	m_btn_open_link = new wxButton(panel, wxID_ANY, _L("Open Link"), wxDefaultPosition, wxDefaultSize);
	

	user_info_sizer->Add(m_label_title, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);

	user_info_sizer->Add(-1, 10);
	user_info_sizer->Add(m_label_1, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
	user_info_sizer->Add(m_radio_result1, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
	user_info_sizer->Add(m_radio_result2, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
	user_info_sizer->Add(m_radio_result3, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
	user_info_sizer->Add(m_radio_result4, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);

	user_info_sizer->Add(-1, 10);
	user_info_sizer->Add(m_label_ams, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
	user_info_sizer->Add(m_radio_single, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
	user_info_sizer->Add(m_radio_ams, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
	user_info_sizer->Add(m_radio_other, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);

	user_info_sizer->Add(-1, 10);

	m_btn_open_link->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
			/* submit summary to cloud */
			wxString url = wxString("https://wenjuan.feishu.cn/m?t=sTOWz7rzrwvi-edku");
			wxLaunchDefaultBrowser(url);
		});

	m_btn_submit->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
			this->submit();
		});

	auto* topsizer = new wxBoxSizer(wxVERTICAL);
	topsizer->Add(panel, 1, wxALL | wxALIGN_LEFT | wxGROW, 5, NULL);
	user_info_sizer->Add(-1, 10);
	auto h_sizer = new wxBoxSizer(wxHORIZONTAL);
	h_sizer->Add(m_btn_submit, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 20);
	h_sizer->Add(m_btn_open_link, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 20);
	user_info_sizer->Add(h_sizer);
	topsizer->Add(-1, 5);

	SetSizerAndFit(topsizer);
	Refresh();

	const auto size = GetSize();
	SetSize(std::max(size.GetWidth(), static_cast<int>(min_width)), std::max(size.GetHeight(), static_cast<int>(min_height)));
	Layout();
	Center();

	Bind(EVT_CLOSE_DIALOG, &PrintResultDialog::on_close, this);
}

void PrintResultDialog::submit()
{
	try {
		pt::ptree root, info;
		pt::ptree reason_array, ams_reason_array;
		pt::ptree reason_item[FAIL_REASON_NUM];
		pt::ptree ams_reason_items[AMS_FAIL_REASON_NUM];
		bool ams_on = m_radio_ams->GetValue();
		std::string action_str;	//finished, stopped
		std::string result_str; // success, fail

		if (m_radio_result1->GetValue()) {
			action_str = "finished";
			result_str = "success";
		}
		else if (m_radio_result2->GetValue()) {
			action_str = "finished";
			result_str = "fail";
		}
		else if (m_radio_result3->GetValue()) {
			action_str = "stopped";
			result_str = "success";
		}
		else if (m_radio_result4->GetValue()) {
			action_str = "stopped";
			result_str = "fail";
		}

		root.put("account", summary->username);
		root.put("user_id", summary->user_id);
		root.put("dev_ip", summary->device_ip);
		root.put("host_ip", summary->host_ip);
		root.put("type", "print");
		root.put("dev_id", summary->device_id);

		info.put("account", summary->username);
		info.put("user_id", summary->user_id);
		info.put("dev_ip", summary->device_ip);
		info.put("host_ip", summary->host_ip);
		info.put("type", "print");
		info.put("dev_id", summary->device_id);
		info.put("model_name", summary->print_filename);
		info.put("model_url", summary->print_filename);
		info.put("start_time", summary->start_time);
		info.put<int>("start", summary->start);
		info.put<int>("duration", summary->duration);
		info.put<bool>("colorful", ams_on);
		info.put("hardware", "");
		info.put("firmware", summary->device_version);
		info.put("software", summary->slicer_version);
		info.put("action", action_str);
		info.put("result", result_str);
		if (!reason_array.empty()) {
			info.add_child("body", reason_array);
		}
		if (!ams_reason_array.empty()) {
			info.add_child("ams", ams_reason_array);
		}
		std::stringstream info_oss;
		pt::write_json(info_oss, info);
		std::string info_json_str = info_oss.str();
		root.put("detail", info_json_str);

		AccountManager* manager = wxGetApp().getAccountManager();

		std::stringstream oss;
		pt::write_json(oss, root);
		std::string json_str = oss.str();

		manager->submit_print_result(summary->device_id, json_str,
			[this](int result, std::string info) {
				if (result == 0) {
					wxQueueEvent(this, new SimpleEvent(EVT_CLOSE_DIALOG));
				}
				else {
					wxMessageBox(info);
				}
			});
	}
	catch (std::exception& e) {
		wxMessageBox(e.what());
	}
}

void PrintResultDialog::on_close(SimpleEvent& evt)
{
	this->EndModal(wxID_OK);
}

void PrintResultDialog::fit_no_shrink()
{
	// Ensure content fits into window and window is not shrinked
	const auto old_size = GetSize();
	Layout();
	Fit();
	const auto new_size = GetSize();
	const auto new_width = std::max(old_size.GetWidth(), new_size.GetWidth());
	const auto new_height = std::max(old_size.GetHeight(), new_size.GetHeight());
}

void PrintResultDialog::on_dpi_changed(const wxRect& suggested_rect)
{
	Fit();
	Refresh();
}

} // namespace GUI
} // namespace Slic3r