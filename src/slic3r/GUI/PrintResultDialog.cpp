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

	wxStaticText* m_label_reasons = new wxStaticText(panel, wxID_ANY, _L("3. What are your problems? Please select reasons."), wxDefaultPosition, wxDefaultSize);
	user_info_sizer->Add(m_label_reasons, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);

	std::string title[FAIL_REASON_NUM] = {
		"The hot bed not sticky(record special material)",
		"The hot bed too sticky(record special material)",
		"The first layer is not flat, warped or the nozzle is too far away, lead to printing failed?",
		"XY lost step, lead to printing failed?",
		"Hot end plug",
		"The hot end falls off",
		"Extrusion shortage, lead to printing failed?",
		"The thread slips",
		"Printing on a staggered level, lead to printing failed?",
		"Break material",
		"Slicer crash",
		"ams-refueling stuck or failed, lead to printing failed?",
		"other"
	};

	std::string comment[FAIL_REASON_NUM] = {
		"PLA+PC",
		"PLA+PC",
		"yes",
		"yes",
		"",
		"",
		"yes",
		"",
		"yes",
		"",
		"",
		"yes",
		""
	};

	for (int i = 0; i < FAIL_REASON_NUM; i++) {
		m_cb_fail_reason[i] = new wxCheckBox(panel, wxID_ANY, _L(title[i]), wxDefaultPosition, wxDefaultSize);
		txt_fail_reason[i] = new wxTextCtrl(panel, wxID_ANY, _L(comment[i]), wxDefaultPosition, wxSize(250, -1));
	}

	for (int i = 0; i < FAIL_REASON_NUM; i++)
	{
		user_info_sizer->Add(-1, 4);
		user_info_sizer->Add(m_cb_fail_reason[i], 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
		user_info_sizer->Add(txt_fail_reason[i], 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 30);
	}
	
	wxString ams_reason[AMS_FAIL_REASON_NUM] = {
		"AMS five-way part block",
		"AMS cutting material failed",
		"AMS meterial expand lead to block",
		"AMS understrength with ams",
		"AMS structure is loose",
		"other"
	};

	wxString ams_comment[AMS_FAIL_REASON_NUM] = {
		"",
		"1",
		"1",
		"",
		"",
		""
	};

	for (int i = 0; i < AMS_FAIL_REASON_NUM; i++) {
		m_ams_fail_reason[i] = new wxCheckBox(panel, wxID_ANY, _L(ams_reason[i]), wxDefaultPosition, wxDefaultSize);
		txt_ams_fail_reason[i] = new wxTextCtrl(panel, wxID_ANY, _L(ams_comment[i]), wxDefaultPosition, wxSize(250, -1));
		
	}

	user_info_sizer->Add(-1, 8);
	wxStaticText* m_label_ams_fail_reason = new wxStaticText(panel, wxID_ANY, _L("4. Any Issues met with AMS?"), wxDefaultPosition, wxDefaultSize);
	user_info_sizer->Add(m_label_ams_fail_reason, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);

	for (int i = 0; i < AMS_FAIL_REASON_NUM; i++) {
		user_info_sizer->Add(-1, 4);
		user_info_sizer->Add(m_ams_fail_reason[i], 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
		user_info_sizer->Add(txt_ams_fail_reason[i], 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 30);
	}

	m_btn_submit->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
			/* submit summary to cloud */
			this->submit();
		});

	auto* topsizer = new wxBoxSizer(wxVERTICAL);
	topsizer->Add(panel, 1, wxALL | wxALIGN_LEFT | wxGROW, 5, NULL);
	user_info_sizer->Add(-1, 10);
	user_info_sizer->Add(m_btn_submit, 0, wxBOTTOM | wxALIGN_LEFT, 20);
	topsizer->Add(-1, 5);

	SetSizerAndFit(topsizer);
	Refresh();

	const auto size = GetSize();
	SetSize(std::max(size.GetWidth(), static_cast<int>(min_width)), std::max(size.GetHeight(), static_cast<int>(min_height)));
	Layout();
	Center();
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

		for (int i = 0; i < FAIL_REASON_NUM; i++) {
			if (m_cb_fail_reason[i]->IsChecked()) {
				reason_item[i].put("reason", m_cb_fail_reason[i]->GetLabelText().ToStdString());
				reason_item[i].put("comments", txt_fail_reason[i]->GetValue().ToStdString());
				reason_array.push_back(std::make_pair("", reason_item[i]));
			}
		}

		for (int i = 0; i < AMS_FAIL_REASON_NUM; i++) {
			if (m_ams_fail_reason[i]->IsChecked()) {
				ams_reason_items[i].put("reason", m_ams_fail_reason[i]->GetLabelText().ToStdString());
				ams_reason_items[i].put("comments", txt_ams_fail_reason[i]->GetValue().ToStdString());
				ams_reason_array.push_back(std::make_pair("", ams_reason_items[i]));
			}
		}

		root.put("account", summary->username);
		root.put("user_id", summary->user_id);
		root.put("dev_ip", summary->device_ip);
		root.put("host_ip", summary->host_ip);
		root.put("type", "print");

		info.put("account", summary->username);
		info.put("user_id", summary->user_id);
		info.put("dev_ip", summary->device_ip);
		info.put("host_ip", summary->host_ip);
		info.put("type", "print");
		info.put("model_name", summary->print_filename);
		info.put("model_url", summary->print_filename);
		info.put("start_time", summary->start_time);
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
					this->EndModal(wxID_OK);
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