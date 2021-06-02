#include "DebugToolDialog.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <regex>
#include <wx/frame.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/dataview.h>
#include <wx/wupdlock.h>
#include <wx/debug.h>
#include <wx/msgdlg.h>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/array.hpp>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "libslic3r/AppConfig.hpp"
#include "NotificationManager.hpp"
#include "libslic3r/Time.hpp"
#include "slic3r/Utils/Http.hpp"
#include <expat.h>

typedef pt::ptree JSON;

namespace Slic3r {
namespace GUI {
    std::string DebugToolDialog::_getNewLogFilename()
    {
        std::time_t t = std::time(0);
        std::tm* now_time = std::localtime(&t);
        std::stringstream buf;
        buf << std::put_time(now_time, "log_%a_%b_%d_%H_%M_%S.txt");
        std::string log_filename = buf.str();
        return log_filename;
    }

    /* upgrade */
    void XML_StartElementHandler(void* userData, const XML_Char* name, const XML_Char** atts) {
        if (strcmp(name, "a") == 0) {
            if (strcmp(atts[0], "href") == 0) {
                DebugToolDialog* dlg = (DebugToolDialog*)userData;
                std::string firmware_value(atts[1]);

                //check valid
                if (firmware_value._Starts_with("update") && firmware_value.find("img") > 0)
                    dlg->add_firmware(atts[1]);
            }
        }
    };
    void XML_EndElementHandler(void* userData, const XML_Char* name) {
        ;
    };
    void XML_CharacterDataHandler(void* userData, const XML_Char* s, int len) {
        ;
    };

    void DebugToolDialog::add_firmware(std::string firmware)
    {
        upgrade_file_list.push_back(firmware);
    }

    DebugToolDialog::DebugToolDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("Debug Tool"), wxDefaultPosition, wxSize(900, 820), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_deviceListTimer(new wxTimer(this, TIMER_ID)),
        m_timer(new wxTimer)
    {
        selectGcodeDialog = new wxFileDialog(parent, "Open Gcode File", "", "", "Gcode files(*.gcode)|*.gcode", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

        btn_refresh_upgrade_list = new wxButton(this, wxID_ANY, _L("Refresh"), wxDefaultPosition, wxDefaultSize);
        btn_refresh_upgrade_list->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->refresh_firmware_list(true);
        });

        btn_upgrade_firmware = new wxButton(this, wxID_ANY, _L("Upgrade Firmware"), wxDefaultPosition, wxSize(180, -1));
        btn_upgrade_firmware->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            std::string firmware_name = cb_upgrade_firmware->GetValue().ToStdString();

            std::string dst_url = UPGRADE_URL + firmware_name;
            std::string version = firmware_name.substr(firmware_name.find_first_of("_v") + 1, 11);

            // send upgrade
            pt::ptree root, upgrade;
            upgrade.put<int>("sequence_id", this->m_sequence_id++);
            upgrade.put("command", "start");
            upgrade.put("url", dst_url);
            upgrade.put("module", cb_upgrade_module->GetValue().ToStdString());
            upgrade.put("version", version);
            root.put_child("upgrade", upgrade);

            std::stringstream oss;
            pt::write_json(oss, root);
            std::string json_str = oss.str();
            json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
            this->publish_json(json_str);
            wxMessageBox("Start Upgrading (About 2 Minutes)...");
            });

        btn_run_gcode = new wxButton(this, wxID_ANY, _L("Run Gcode"), wxDefaultPosition, wxSize(180, -1));
        btn_run_gcode->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            std::string filepath = txt_gcode_filename->GetValue().ToStdString();
            int name_start = filepath.find_last_of("\\");
            if (name_start <= 0) return;

            std::string filename = filepath.substr(name_start + 1, filepath.size());
            std::string dstname = "/data/" + filename;
            std::string device = cb_device_list->GetValue().ToStdString();
            std::string ip_str = device.substr(0, device.find_first_of("("));

            int result = this->curl_upload(filepath, filename, ip_str);
            if (result < 0) {
                wxMessageBox("Push Gcode Failed!");
            }
            pt::ptree root, print;
            print.put("sequence_id", m_sequence_id++);
            print.put("command", "gcode_file");
            print.put<std::string>("param", dstname);
            root.put_child("print", print);
            std::stringstream oss;
            pt::write_json(oss, root);
            std::string json_str = oss.str();
            json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
            this->publish_json(json_str);
            wxMessageBox("Run Gcode ...");
            });

        btn_select_gcode_file = new wxButton(this, wxID_ANY, _L("Select File"), wxDefaultPosition, wxDefaultSize);
        btn_select_gcode_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            if (selectGcodeDialog->ShowModal() == wxID_CANCEL) return;

            txt_gcode_filename->SetValue(selectGcodeDialog->GetPath());
            txt_gcode_filename->SetFocus();
            this->SetFocus();
            });


        btn_set_x_pos_0_1 = new wxButton(this, wxID_ANY, _L("X+0.1"), wxDefaultPosition, wxDefaultSize);
        btn_set_x_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 X0.1 F3000 \n");
            });
        btn_set_x_pos_1_0 = new wxButton(this, wxID_ANY, _L("X+1.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_x_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 X1.0 F3000 \n");
            });
        btn_set_x_pos_10_0 = new wxButton(this, wxID_ANY, _L("X+10.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_x_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 X10.0 F3000 \n");
            });
        btn_set_x_neg_0_1 = new wxButton(this, wxID_ANY, _L("X-0.1"), wxDefaultPosition, wxDefaultSize);
        btn_set_x_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 X-0.1 F3000 \n");
            });
        btn_set_x_neg_1_0 = new wxButton(this, wxID_ANY, _L("X-1.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_x_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 X-1.0 F3000 \n");
            });
        btn_set_x_neg_10_0 = new wxButton(this, wxID_ANY, _L("X-10.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_x_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 X-10.0 F3000 \n");
            });
        btn_set_y_pos_0_1 = new wxButton(this, wxID_ANY, _L("Y+0.1"), wxDefaultPosition, wxDefaultSize);
        btn_set_y_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Y0.1 F3000 \n");
            });
        btn_set_y_pos_1_0 = new wxButton(this, wxID_ANY, _L("Y+1.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_y_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Y1.0 F3000 \n");
            });
        btn_set_y_pos_10_0 = new wxButton(this, wxID_ANY, _L("Y+10.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_y_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Y10.0 F3000 \n");
            });
        btn_set_y_neg_0_1 = new wxButton(this, wxID_ANY, _L("Y-0.1"), wxDefaultPosition, wxDefaultSize);
        btn_set_y_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Y-0.1 F3000 \n");
            });
        btn_set_y_neg_1_0 = new wxButton(this, wxID_ANY, _L("Y-1.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_y_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Y-1.0 F3000 \n");
            });
        btn_set_y_neg_10_0 = new wxButton(this, wxID_ANY, _L("Y-10.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_y_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Y-10.0 F3000 \n");
            });
        btn_set_z_pos_0_1 = new wxButton(this, wxID_ANY, _L("Z+0.1"), wxDefaultPosition, wxDefaultSize);
        btn_set_z_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Z0.1 F900 \n");
            });
        btn_set_z_pos_1_0 = new wxButton(this, wxID_ANY, _L("Z+1.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_z_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Z1.0 F900 \n");
            });
        btn_set_z_pos_10_0 = new wxButton(this, wxID_ANY, _L("Z+10.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_z_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Z10.0 F900 \n");
            });
        btn_set_z_neg_0_1 = new wxButton(this, wxID_ANY, _L("Z-0.1"), wxDefaultPosition, wxDefaultSize);
        btn_set_z_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Z-0.1 F900 \n");
            });
        btn_set_z_neg_1_0 = new wxButton(this, wxID_ANY, _L("Z-1.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_z_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Z-1.0 F900 \n");
            });
        btn_set_z_neg_10_0 = new wxButton(this, wxID_ANY, _L("Z-10.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_z_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G91 \nG0 Z-10.0 F900 \n");
            });
        btn_set_e_pos_0_1 = new wxButton(this, wxID_ANY, _L("E+0.1"), wxDefaultPosition, wxDefaultSize);
        btn_set_e_pos_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("M83 \nG0 E0.1 F300 \n");
            });
        btn_set_e_pos_1_0 = new wxButton(this, wxID_ANY, _L("E+1.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_e_pos_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("M83 \nG0 E1.0 F300 \n");
            });
        btn_set_e_pos_10_0 = new wxButton(this, wxID_ANY, _L("E+10.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_e_pos_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("M83 \nG0 E10.0 F300 \n");
            });
        btn_set_e_neg_0_1 = new wxButton(this, wxID_ANY, _L("E-0.1"), wxDefaultPosition, wxDefaultSize);
        btn_set_e_neg_0_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("M83 \nG0 E-0.1 F300 \n");
            });
        btn_set_e_neg_1_0 = new wxButton(this, wxID_ANY, _L("E-1.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_e_neg_1_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("M83 \nG0 E-1.0 F300 \n");
            });
        btn_set_e_neg_10_0 = new wxButton(this, wxID_ANY, _L("E-10.0"), wxDefaultPosition, wxDefaultSize);
        btn_set_e_neg_10_0->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("M83 \nG0 E-10.0 F300 \n");
            });
        btn_return_home = new wxButton(this, wxID_ANY, _L("Return Home:G28"));
        btn_return_home->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G28 \n");
            });
        btn_auto_leveling = new wxButton(this, wxID_ANY, _L("Auto Leveling:G29"));
        btn_auto_leveling->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G29 \n");
            });
        btn_xyz_abs_mode = new wxButton(this, wxID_ANY, _L("XYZ - abs mode:G90"));
        btn_xyz_abs_mode->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("G90 \n");
            });

        btn_fan_on = new wxButton(this, wxID_ANY, _L("Cooling Fan ON"));
        btn_fan_on->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("M106 S255 \n");
            });
        btn_fan_off = new wxButton(this, wxID_ANY, _L("Cooling Fan OFF"));
        btn_fan_off->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode("M106 S0 \n");
            });

        btn_set_hot_bed_temp = new wxButton(this, wxID_ANY, _L("Set Bed Temp"));
        btn_set_hot_bed_temp->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            //TODO check value
            std::string gcode_str = "M140 S" + txt_set_hot_bed_temp->GetValue().ToStdString() + " \n";
            this->publishGcode(gcode_str);
            });
        btn_set_hot_end_temp = new wxButton(this, wxID_ANY, _L("Set Hot End Temp"), wxDefaultPosition, wxDefaultSize);
        btn_set_hot_end_temp->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            //TODO check value
            std::string gcode_str = "M104 S" + txt_set_hot_end_temp->GetValue().ToStdString() + " \n";
            this->publishGcode(gcode_str);
            });

        btn_cancel_print = new wxButton(this, wxID_ANY, _L("Cancel Print"), wxDefaultPosition, wxDefaultSize);
        btn_cancel_print->Disable();
        btn_clear_output_string = new wxButton(this, wxID_ANY, _L("Clear"), wxDefaultPosition, wxDefaultSize);
        btn_clear_output_string->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            wxString content = txt_string_info->GetValue();
            txt_string_info->Clear();
            });
        btn_save_file = new wxButton(this, wxID_ANY, _L("New Save File"), wxDefaultPosition, wxDefaultSize);
        btn_save_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            wxString content = txt_string_info->GetValue();
            txt_string_info->Clear();
            });

        btn_start_temp_push = new wxButton(this, wxID_ANY, _L("Start Temp Push"), wxDefaultPosition, wxDefaultSize);
        btn_start_temp_push->Disable();
        btn_stop_temp_push = new wxButton(this, wxID_ANY, _L("Stop Temp Push"), wxDefaultPosition, wxDefaultSize);
        btn_stop_temp_push->Disable();
        btn_get_curr_temp = new wxButton(this, wxID_ANY, _L("Get Now Temp"), wxDefaultPosition, wxDefaultSize);
        btn_get_curr_temp->Disable();
        btn_get_curr_pos = new wxButton(this, wxID_ANY, _L("Get Now Pos"), wxDefaultPosition, wxDefaultSize);
        btn_get_curr_pos->Disable();

        btn_send_gcode_1 = new wxButton(this, wxID_ANY, _L("Send Custom\nGcode 1"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
        btn_send_gcode_1->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            std::string gcode1 = txt_custom_gcode1->GetValue().ToStdString() + "\n";
            this->publishGcode(txt_custom_gcode1->GetValue().ToStdString());
            });
        btn_send_gcode_2 = new wxButton(this, wxID_ANY, _L("Send Custom\nGcode 2"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
        btn_send_gcode_2->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode(txt_custom_gcode2->GetValue().ToStdString());
            });
        btn_send_gcode_3 = new wxButton(this, wxID_ANY, _L("Send Custom\nGcode 3"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
        btn_send_gcode_3->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode(txt_custom_gcode3->GetValue().ToStdString());
            });
        btn_send_gcode_4 = new wxButton(this, wxID_ANY, _L("Send Custom\nGcode 4"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
        btn_send_gcode_4->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode(txt_custom_gcode4->GetValue().ToStdString());
            });
        btn_send_gcode_5 = new wxButton(this, wxID_ANY, _L("Send Custom\nGcode 5"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
        btn_send_gcode_5->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode(txt_custom_gcode5->GetValue().ToStdString());
            });
        btn_send_gcode_6 = new wxButton(this, wxID_ANY, _L("Send Custom\nGcode 6"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
        btn_send_gcode_6->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode(txt_custom_gcode6->GetValue().ToStdString());
            });
        btn_send_gcode_7 = new wxButton(this, wxID_ANY, _L("Send Custom\nGcode 7"), wxDefaultPosition, wxSize(BTN_SEND_WIDTH, TXT_GCODE_HEIGHT));
        btn_send_gcode_7->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
            this->publishGcode(txt_custom_gcode7->GetValue().ToStdString());
            });

        txt_gcode_filename = new wxTextCtrl(this, wxID_ANY, _L(""), wxDefaultPosition, wxDefaultSize);
        txt_set_hot_bed_temp = new wxTextCtrl(this, wxID_ANY, _L("60"), wxDefaultPosition, wxDefaultSize);
        txt_set_hot_end_temp = new wxTextCtrl(this, wxID_ANY, _L("200"), wxDefaultPosition, wxDefaultSize);
        txt_string_info = new wxTextCtrl(this, wxID_ANY, _L(""), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);

        txt_custom_gcode1 = new wxTextCtrl(this, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
        txt_custom_gcode2 = new wxTextCtrl(this, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
        txt_custom_gcode3 = new wxTextCtrl(this, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
        txt_custom_gcode4 = new wxTextCtrl(this, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
        txt_custom_gcode5 = new wxTextCtrl(this, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
        txt_custom_gcode6 = new wxTextCtrl(this, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);
        txt_custom_gcode7 = new wxTextCtrl(this, wxID_ANY, _L(""), wxDefaultPosition, wxSize(300, TXT_GCODE_HEIGHT), wxTE_MULTILINE);

        label_pos_x = new wxStaticText(this, wxID_ANY, _L("Pos X: "), wxDefaultPosition, wxDefaultSize);
        label_pos_x_val = new wxStaticText(this, wxID_ANY, _L("0.00"), wxDefaultPosition, wxDefaultSize);
        label_pos_y = new wxStaticText(this, wxID_ANY, _L("Pos Y: "), wxDefaultPosition, wxDefaultSize);
        label_pos_y_val = new wxStaticText(this, wxID_ANY, _L("0.00"), wxDefaultPosition, wxDefaultSize);
        label_pos_z = new wxStaticText(this, wxID_ANY, _L("Pos Z: "), wxDefaultPosition, wxDefaultSize);
        label_pos_z_val = new wxStaticText(this, wxID_ANY, _L("0.00"), wxDefaultPosition, wxDefaultSize);
        label_pos_e = new wxStaticText(this, wxID_ANY, _L("Pos E: "), wxDefaultPosition, wxDefaultSize);
        label_pos_e_val = new wxStaticText(this, wxID_ANY, _L("0.00"), wxDefaultPosition, wxDefaultSize);
        label_hot_end_temp = new wxStaticText(this, wxID_ANY, _L("Nozzle Temp: "), wxDefaultPosition, wxDefaultSize);
        label_hot_end_temp_val = new wxStaticText(this, wxID_ANY, _L("0/0"), wxDefaultPosition, wxDefaultSize);
        label_bed_end_temp = new wxStaticText(this, wxID_ANY, _L("Bed Temp: "), wxDefaultPosition, wxDefaultSize);
        label_bed_end_temp_val = new wxStaticText(this, wxID_ANY, _L("0/0"), wxDefaultPosition, wxDefaultSize);
        label_print_progress = new wxStaticText(this, wxID_ANY, _L("Print Progress: "), wxDefaultPosition, wxDefaultSize);
        label_print_progress_val = new wxStaticText(this, wxID_ANY, _L("0%"), wxDefaultPosition, wxDefaultSize);

        // Init custom_gcode
        pt::ptree custom_gocde_root;
        try {
            pt::read_json("CustomGcode.json", custom_gocde_root);
            std::string gcode1 = custom_gocde_root.get<std::string>("custom_gcode_1");
            txt_custom_gcode1->SetValue(wxString(gcode1));
            std::string gcode2 = custom_gocde_root.get<std::string>("custom_gcode_2");
            txt_custom_gcode2->SetValue(wxString(gcode2));
            std::string gcode3 = custom_gocde_root.get<std::string>("custom_gcode_3");
            txt_custom_gcode3->SetValue(wxString(gcode3));
            std::string gcode4 = custom_gocde_root.get<std::string>("custom_gcode_4");
            txt_custom_gcode4->SetValue(wxString(gcode4));
            std::string gcode5 = custom_gocde_root.get<std::string>("custom_gcode_5");
            txt_custom_gcode5->SetValue(wxString(gcode5));
            std::string gcode6 = custom_gocde_root.get<std::string>("custom_gcode_6");
            txt_custom_gcode6->SetValue(wxString(gcode6));
            std::string gcode7 = custom_gocde_root.get<std::string>("custom_gcode_7");
            txt_custom_gcode7->SetValue(wxString(gcode7));
        }
        catch (std::exception const& e) {
            ;
        }

        label_upgrade_filename = new wxStaticText(this, wxID_ANY, _L("File Path:"), wxDefaultPosition, wxDefaultSize);
        label_gcode_filename = new wxStaticText(this, wxID_ANY, _L("File Path:"), wxDefaultPosition, wxDefaultSize);
        label_output_string = new wxStaticText(this, wxID_ANY, _L("MC String Info:"), wxDefaultPosition, wxDefaultSize);
        label_device_list = new wxStaticText(this, wxID_ANY, _L("Device List:"), wxDefaultPosition, wxDefaultSize);
        //wxStaticText *label_connect_status = new wxStaticText(this, wxID_ANY, _L("offline"), wxDefaultPosition, wxDefaultSize);

        wxArrayString module_items;
        module_items.Add(_L("AP"));
        module_items.Add(_L("M4"));
        module_items.Add(_L("M0"));
        cb_upgrade_module = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, module_items);
        cb_upgrade_module->Select(0);
        cb_upgrade_firmware = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
        cb_upgrade_firmware->SetEditable(false);
        cb_device_list = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
        cb_device_list->SetEditable(false);
        wxButton* btn_refresh_device_list = new wxButton(this, wxID_ANY, _L("Refresh"), wxDefaultPosition, wxDefaultSize);
        btn_refresh_device_list->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            this->refresh_device_list();
        });

        cb_upgrade_module->Select(0);

        // Layout Sizer
        auto* top_sizer = new wxBoxSizer(wxVERTICAL);
        auto* conn_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* conn_device_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* upgrade_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* run_gcode_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* control_sizer = new wxBoxSizer(wxVERTICAL);
        auto* pos_btns_sizer = new wxGridSizer(9, 3, 5, 5);
        auto* temp_btns_sizer = new wxGridSizer(5, 2, 5, 5);
        auto* custom_gcode_sizer = new wxFlexGridSizer(7, 2, 0, 5);
        auto* ctrl_custom_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* output_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* output_content_sizer = new wxBoxSizer(wxVERTICAL);
        auto* output_btns_sizer = new wxBoxSizer(wxVERTICAL);

        conn_device_sizer->Add(label_device_list, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, SPACING);
        conn_device_sizer->Add(cb_device_list, 1, wxALL | wxEXPAND, SPACING);
        conn_device_sizer->Add(btn_refresh_device_list, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, SPACING);

        upgrade_sizer->Add(btn_upgrade_firmware, 0, wxLEFT, SPACING);
        upgrade_sizer->Add(label_upgrade_filename, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, SPACING);
        upgrade_sizer->Add(cb_upgrade_firmware, 1, wxLEFT | wxRIGHT | wxEXPAND);
        upgrade_sizer->Add(cb_upgrade_module, 0);
        upgrade_sizer->Add(btn_refresh_upgrade_list, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);

        run_gcode_sizer->Add(btn_run_gcode, 0, wxLEFT, SPACING);
        run_gcode_sizer->Add(label_gcode_filename, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, SPACING);
        run_gcode_sizer->Add(txt_gcode_filename, 1, wxLEFT | wxRIGHT | wxEXPAND, SPACING);
        run_gcode_sizer->Add(btn_select_gcode_file, 0, wxALIGN_CENTRE_HORIZONTAL | wxLEFT, SPACING);
        run_gcode_sizer->Add(btn_cancel_print, 0, wxALIGN_RIGHT| wxRIGHT, SPACING);

        pos_btns_sizer->Add(btn_set_x_pos_0_1);
        pos_btns_sizer->Add(btn_set_x_pos_1_0);
        pos_btns_sizer->Add(btn_set_x_pos_10_0);
        pos_btns_sizer->Add(btn_set_x_neg_0_1);
        pos_btns_sizer->Add(btn_set_x_neg_1_0);
        pos_btns_sizer->Add(btn_set_x_neg_10_0);
        pos_btns_sizer->Add(btn_set_y_pos_0_1);
        pos_btns_sizer->Add(btn_set_y_pos_1_0);
        pos_btns_sizer->Add(btn_set_y_pos_10_0);
        pos_btns_sizer->Add(btn_set_y_neg_0_1);
        pos_btns_sizer->Add(btn_set_y_neg_1_0);
        pos_btns_sizer->Add(btn_set_y_neg_10_0);

        pos_btns_sizer->Add(btn_set_z_pos_0_1);
        pos_btns_sizer->Add(btn_set_z_pos_1_0);
        pos_btns_sizer->Add(btn_set_z_pos_10_0);
        pos_btns_sizer->Add(btn_set_z_neg_0_1);
        pos_btns_sizer->Add(btn_set_z_neg_1_0);
        pos_btns_sizer->Add(btn_set_z_neg_10_0);
        pos_btns_sizer->Add(btn_set_e_pos_0_1);
        pos_btns_sizer->Add(btn_set_e_pos_1_0);
        pos_btns_sizer->Add(btn_set_e_pos_10_0);
        pos_btns_sizer->Add(btn_set_e_neg_0_1);
        pos_btns_sizer->Add(btn_set_e_neg_1_0);
        pos_btns_sizer->Add(btn_set_e_neg_10_0);

        temp_btns_sizer->Add(btn_set_hot_bed_temp, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(txt_set_hot_bed_temp, 0, wxLEFT | wxALIGN_LEFT);
        temp_btns_sizer->Add(btn_set_hot_end_temp, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(txt_set_hot_end_temp, 0, wxLEFT | wxALIGN_LEFT);
        temp_btns_sizer->Add(btn_fan_on, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(btn_fan_off, 0, wxLEFT | wxALIGN_LEFT);
        temp_btns_sizer->Add(btn_auto_leveling, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(btn_xyz_abs_mode, 0, wxLEFT | wxALIGN_LEFT);
        temp_btns_sizer->Add(btn_return_home, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(NULL, 0, wxLEFT | wxALIGN_LEFT);
        temp_btns_sizer->Add(label_pos_x, 0, wxRIGHT | wxALIGN_RIGHT); 
        temp_btns_sizer->Add(label_pos_x_val, 0, wxLEFT | wxALIGN_LEFT);
        temp_btns_sizer->Add(label_pos_y, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(label_pos_y_val, 0, wxLEFT | wxALIGN_LEFT); 
        temp_btns_sizer->Add(label_pos_z, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(label_pos_z_val, 0, wxLEFT | wxALIGN_LEFT);
        temp_btns_sizer->Add(label_pos_e, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(label_pos_e_val, 0, wxLEFT | wxALIGN_LEFT); 
        temp_btns_sizer->Add(label_hot_end_temp, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(label_hot_end_temp_val, 0, wxLEFT | wxALIGN_LEFT); 
        temp_btns_sizer->Add(label_bed_end_temp, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(label_bed_end_temp_val, 0, wxLEFT | wxALIGN_LEFT); 
        temp_btns_sizer->Add(label_print_progress, 0, wxRIGHT | wxALIGN_RIGHT);
        temp_btns_sizer->Add(label_print_progress_val, 0, wxLEFT | wxALIGN_LEFT);

        control_sizer->Add(pos_btns_sizer, 0, wxTOP | wxALIGN_CENTER_VERTICAL | wxALIGN_CENTER_HORIZONTAL, SPACING);
        control_sizer->Add(temp_btns_sizer, 1, wxTOP | wxLEFT);

        custom_gcode_sizer->AddGrowableCol(1, 1);
        custom_gcode_sizer->Add(btn_send_gcode_1, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
        custom_gcode_sizer->Add(txt_custom_gcode1, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
        custom_gcode_sizer->Add(btn_send_gcode_2, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
        custom_gcode_sizer->Add(txt_custom_gcode2, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
        custom_gcode_sizer->Add(btn_send_gcode_3, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
        custom_gcode_sizer->Add(txt_custom_gcode3, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
        custom_gcode_sizer->Add(btn_send_gcode_4, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
        custom_gcode_sizer->Add(txt_custom_gcode4, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
        custom_gcode_sizer->Add(btn_send_gcode_5, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
        custom_gcode_sizer->Add(txt_custom_gcode5, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
        custom_gcode_sizer->Add(btn_send_gcode_6, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
        custom_gcode_sizer->Add(txt_custom_gcode6, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);
        custom_gcode_sizer->Add(btn_send_gcode_7, 0, wxRIGHT | wxALIGN_RIGHT, SPACING);
        custom_gcode_sizer->Add(txt_custom_gcode7, 1, wxLEFT | wxALIGN_LEFT | wxEXPAND, SPACING);

        output_content_sizer->Add(label_output_string, 0);
        output_content_sizer->Add(txt_string_info, 1, wxALL | wxEXPAND, SPACING);
        output_btns_sizer->Add(-1, 20);
        output_btns_sizer->Add(btn_start_temp_push, 0, wxLEFT | wxRIGHT | wxEXPAND);
        output_btns_sizer->Add(btn_stop_temp_push, 0, wxLEFT | wxRIGHT | wxEXPAND);
        output_btns_sizer->Add(btn_get_curr_temp, 0, wxLEFT | wxRIGHT | wxEXPAND);
        output_btns_sizer->Add(btn_get_curr_pos, 0, wxLEFT | wxRIGHT | wxEXPAND);
        output_btns_sizer->Add(-1, 25);
        output_btns_sizer->Add(btn_clear_output_string, 0, wxLEFT | wxRIGHT | wxEXPAND);
        output_btns_sizer->Add(btn_save_file, 0, wxLEFT | wxRIGHT | wxEXPAND);

        output_sizer->Add(output_content_sizer, 1, wxALL | wxEXPAND);
        output_sizer->Add(output_btns_sizer, 0, wxTOP | wxALL | wxEXPAND, SPACING);

        ctrl_custom_sizer->Add(control_sizer, 0, wxLEFT | wxALIGN_LEFT, 0);
        ctrl_custom_sizer->Add(custom_gcode_sizer, 1, wxLEFT | wxRIGHT | wxEXPAND, 5);

        top_sizer->Add(-1, 8);
        top_sizer->Add(conn_device_sizer, 0, wxLEFT | wxRIGHT | wxEXPAND, 0);
        top_sizer->Add(-1, 8);
        top_sizer->Add(upgrade_sizer, 0, wxLEFT | wxRIGHT | wxEXPAND, 0);
        top_sizer->Add(-1, 8);
        top_sizer->Add(run_gcode_sizer, 0, wxLEFT | wxRIGHT | wxEXPAND, 0);
        top_sizer->Add(-1, 20);
        top_sizer->Add(ctrl_custom_sizer, 0, wxLEFT | wxRIGHT | wxEXPAND, 0);
        top_sizer->Add(-1, 8);
        top_sizer->Add(output_sizer, 1, wxALL | wxEXPAND, 0);

        SetSizer(top_sizer);

        Bind(wxEVT_TIMER, &DebugToolDialog::on_timer, this);
}


bool DebugToolDialog::Show(bool show)
{
    bool result = DPIDialog::Show(show);
    if (show) {
        m_timer->Stop();
        m_timer->SetOwner(this);
        m_timer->Start(10000);
    }
    else {
        m_timer->Stop();
    }

    return result;
}


void DebugToolDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();
    msw_buttons_rescale(this, em, { wxID_DELETE, wxID_CANCEL, btn_run_gcode->GetId() });

    SetMinSize(wxSize(HEIGHT * em, WIDTH * em));

    Fit();
    Refresh();
}

int DebugToolDialog::publish_json(std::string json_str)
{
    /* Check device is online */
    Slic3r::DeviceManager *manager = Slic3r::GUI::wxGetApp().getDeviceManager();
    try
    {
        std::string content = cb_device_list->GetValue().ToStdString();
        size_t start = content.find_last_of("(") + 1;
        std::string device_id = content.substr(start, content.find_last_of(")") - start);
        if (device_id.compare("") == 0) {
            wxMessageBox("Please select a device!");
            return -1;
        }
        if (!manager->is_online(device_id)) {
            wxMessageBox("Device is Offline!");
            return -1;
        }
        return publish_json_to_device(device_id, json_str);
    }
    catch (std::exception& e) {
        return -1;
    }
}

int DebugToolDialog::publish_json_to_device(std::string dev_id, std::string json_str)
{
    Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
    handle_request_print_msg(json_str);
    return backend->publish_json_to_device(dev_id, json_str);
}

int DebugToolDialog::handle_request_print_msg(std::string json_str)
{
    try {
        this->append_output_string_info("send msg = " + json_str + " ok!\n");
    }
    catch (std::exception& e) {
        ;
    }

    return 0;
}

int DebugToolDialog::handle_report_print_msg(std::string json_str)
{
    try {
        std::stringstream ss(json_str);
        pt::ptree root;
        pt::read_json(ss, root);
        if (root.get_child_optional("print") != boost::none) {
            pt::ptree print = root.get_child("print");
            /* Update labels */
            boost::optional<std::string> command = print.get_optional<std::string>("command");
            if (command.has_value() &&  command.value_or("").compare("push_status") == 0) {
                boost::optional<std::string> sequence_id = print.get_optional<std::string>("sequence_id");
                boost::optional<std::string> nozzle_temp = print.get_optional<std::string>("nozzle_temp");
                boost::optional<std::string> nozzle_temp_target = print.get_optional<std::string>("nozzle_target_temp");
                boost::optional<std::string> bed_temp = print.get_optional<std::string>("bed_temp");
                boost::optional<std::string> bed_temp_target = print.get_optional<std::string>("bed_target_temp");

                if (nozzle_temp.has_value() && nozzle_temp_target.has_value()) {
                    label_hot_end_temp_val->SetLabelText(nozzle_temp.value() + "/" + nozzle_temp_target.value());
                }

                if (bed_temp.has_value() && bed_temp_target.has_value()) {
                    label_bed_end_temp_val->SetLabelText(bed_temp.value() + "/" + bed_temp_target.value());
                }

                boost::optional<std::string> pos_x = print.get_optional<std::string>("pos_x");
                boost::optional<std::string> pos_y = print.get_optional<std::string>("pos_y");
                boost::optional<std::string> pos_z = print.get_optional<std::string>("pos_z");
                boost::optional<std::string> pos_e = print.get_optional<std::string>("pos_e");

                if (pos_x.has_value()) label_pos_x_val->SetLabelText(pos_x.value());
                if (pos_y.has_value()) label_pos_y_val->SetLabelText(pos_y.value());
                if (pos_z.has_value()) label_pos_z_val->SetLabelText(pos_z.value());
                if (pos_e.has_value()) label_pos_e_val->SetLabelText(pos_e.value());

                boost::optional<std::string> progress = print.get_optional<std::string>("progress");
                if (progress.has_value()) {
                    label_print_progress_val->SetLabelText(progress.value());
                }

                return 0;
            }
            else if (command.has_value() && command.value_or("").compare("gcode_line") == 0) {
                boost::optional<std::string> sequence_id = print.get_optional<std::string>("sequence_id");
                //TODO
            }
            else if (command.has_value() && command.value_or("").compare("gcode_file") == 0) {
                boost::optional<std::string> sequence_id = print.get_optional<std::string>("sequence_id");
                //TODO
            }
        }
        this->append_output_string_info("received ack = " + json_str + " ok!\n");
    }
    catch (std::exception& e) {
        this->append_output_string_info("received report print msg json error.\n");
    }

    return 0;
}

int DebugToolDialog::handle_alive_msg(std::string dev_id)
{
    std::string content = cb_device_list->GetValue().ToStdString();
    size_t start = content.find_last_of("(") + 1;
    std::string device_id = content.substr(start, content.find_last_of(")") - start);
    if (device_id.compare(dev_id) == 0) {
        this->append_output_string_info("dev_id=" + dev_id + " is alive!\n");
    }
    return 0;
}

void DebugToolDialog::refresh_device_list()
{
    Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
    wxArrayString new_items = manager->get_connected_devicelist();
    cb_device_list->Set(new_items);
}

void DebugToolDialog::refresh_firmware_list(bool show_error)
{
    cb_upgrade_firmware->Clear();
    upgrade_file_list.clear();
    Http http = Http::get(UPGRADE_URL);
    http.on_complete([&](std::string body, unsigned) {
        this->append_output_string_info(body);
        XML_Parser parser = XML_ParserCreate(nullptr);
        XML_SetUserData(parser, this);
        XML_SetElementHandler(parser, XML_StartElementHandler, XML_EndElementHandler);
        XML_SetCharacterDataHandler(parser, XML_CharacterDataHandler);
        XML_Parse(parser, body.c_str(), body.size(), 1);
        XML_ParserFree(parser);
        cb_upgrade_firmware->Set(upgrade_file_list);
        cb_upgrade_firmware->Select(0);

        }).on_error([&](std::string body, std::string error, unsigned status) {
            if (show_error)
                wxMessageBox("Get Upgrade List Failed! error " + error);
            }).perform();
}

int DebugToolDialog::append_output_string_info(std::string line)
{
    std::time_t t = std::time(0);
    std::tm* now_time = std::localtime(&t);
    std::stringstream buf;
    buf << std::put_time(now_time, "%a %b %d %H:%M:%S");
    std::string info = buf.str() + ":" + line;
 
    try {

        //if (logFile)
        //    fwrite(line.c_str(), line.size(), 1, logFile);
        log_mutex.lock();
        log_lines.push_back(info);
        log_mutex.unlock();

        // display
        txt_string_info->AppendText(wxString(info));
    }
    catch (std::exception& e) {
        ;
    }
    return 0;
}

int DebugToolDialog::curl_upload(std::string srcFile, std::string dstFile, std::string ip)
{
    std::string cmd = CURL_FILE + " -k " + "sftp://" + ip + ":/userdata/" + dstFile + " --user \"root:root\" " +
        " -T \"" + srcFile + "\" --ftp-create-dirs";
    std::string out;
    int result = callSystem(cmd, out);
    return 0;
}

int DebugToolDialog::publishGcode(std::string gcode)
{
    pt::ptree root, print;
    print.put("command", "gcode_line");
    print.put("param", gcode);
    print.put("sequence_id", this->m_sequence_id++);
    root.put_child("print", print);
    std::stringstream oss;
    pt::write_json(oss, root);
    std::string json_str = oss.str();

    return this->publish_json(json_str);
}

int DebugToolDialog::callSystem(std::string cmd, std::string& output)
{
    FILE* f;
    char out[2048] = { 0 };
    f = _popen(cmd.c_str(), "r");
    if (f == NULL) {
        return -1;
    }
    fgets(out, 2048, f);

    output = std::string(out);
    _pclose(f);

    return 0;
}

int DebugToolDialog::handle_offline_event(std::string dev_id)
{
    std::string content = cb_device_list->GetValue().ToStdString();
    size_t start = content.find_last_of("(") + 1;
    std::string device_id = content.substr(start, content.find_last_of(")") - start);

    if (device_id.compare(device_id) == 0) {
        wxMessageBox(_L("Current Device " + device_id + " is offline!"));
    }
    return 0;
}

void DebugToolDialog::on_timer(wxTimerEvent& event)
{
    //auto save custom_gcode
    pt::ptree custom_gcode_root;
    custom_gcode_root.put("custom_gcode_1", txt_custom_gcode1->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_2", txt_custom_gcode2->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_3", txt_custom_gcode3->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_4", txt_custom_gcode4->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_5", txt_custom_gcode5->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_6", txt_custom_gcode6->GetValue().ToStdString());
    custom_gcode_root.put("custom_gcode_7", txt_custom_gcode7->GetValue().ToStdString());
    pt::write_json("CustomGcode.json", custom_gcode_root);
}

}
}
