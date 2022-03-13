#include <algorithm>
#include <sstream>
#include "WipeTowerDialog.hpp"
#include "BitmapCache.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"

#include <wx/sizer.h>

int scale(const int val) { return val * Slic3r::GUI::wxGetApp().em_unit(); }
int ITEM_WIDTH() { return scale(6); }

static void update_ui(wxWindow* window)
{
    Slic3r::GUI::wxGetApp().UpdateDarkUI(window);
}

#ifdef _WIN32
#define style wxSP_ARROW_KEYS | wxBORDER_SIMPLE
#else 
#define style wxSP_ARROW_KEYS
#endif

// Parent dialog for purging volume adjustments - it fathers WipingPanel widget (that contains all controls) and a button to toggle simple/advanced mode:
WipingDialog::WipingDialog(wxWindow* parent, const std::vector<float>& matrix, const std::vector<float>& extruders, const std::vector<std::string>& extruder_colours)
: wxDialog(parent, wxID_ANY, _(L("Fialment Change - Purging volume adjustment")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE/* | wxRESIZE_BORDER*/)
{
    update_ui(this);

    //auto widget_button = new wxButton(this,wxID_ANY,"-",wxPoint(0,0),wxDefaultSize);
    //update_ui(widget_button);
    m_panel_wiping = new WipingPanel(this, matrix, extruders, extruder_colours, nullptr);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

	// set min sizer width according to extruders count
	const auto sizer_width = (int)((sqrt(matrix.size()) + 2.8)*ITEM_WIDTH());
	main_sizer->SetMinSize(wxSize(sizer_width, -1));

    main_sizer->Add(m_panel_wiping, 0, wxEXPAND | wxALL, 5);
    //main_sizer->Add(widget_button, 0, wxALIGN_CENTER_HORIZONTAL | wxCENTER | wxBOTTOM, 5);
    main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);
    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    update_ui(static_cast<wxButton*>(this->FindWindowById(wxID_OK, this)));
    update_ui(static_cast<wxButton*>(this->FindWindowById(wxID_CANCEL, this)));

    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });
    
    this->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {                 // if OK button is clicked..
        m_output_matrix    = m_panel_wiping->read_matrix_values();    // ..query wiping panel and save returned values
        m_output_extruders = m_panel_wiping->read_extruders_values(); // so they can be recovered later by calling get_...()
        EndModal(wxID_OK);
        },wxID_OK);

    this->Show();
}

// This function allows to "play" with sizers parameters (like align or border)
void WipingPanel::format_sizer(wxSizer* sizer, wxPanel* page, wxGridSizer* grid_sizer, const wxString& info, const wxString& table_title, int table_lshift/*=0*/)
{
    wxSize text_size = GetTextExtent(info);
    auto info_str = new wxStaticText(page, wxID_ANY, info ,wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    info_str->Wrap(int(0.6*text_size.x));
	sizer->Add( info_str, 0, wxEXPAND);
	auto table_sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(table_sizer, 0, wxALIGN_CENTER | wxCENTER, table_lshift);
	table_sizer->Add(new wxStaticText(page, wxID_ANY, table_title), 0, wxALIGN_CENTER | wxTOP, 50);
	table_sizer->Add(grid_sizer, 0, wxALIGN_CENTER | wxTOP, 10);
}

// This panel contains all control widgets for both simple and advanced mode (these reside in separate sizers)
WipingPanel::WipingPanel(wxWindow* parent, const std::vector<float>& matrix, const std::vector<float>& extruders, const std::vector<std::string>& extruder_colours, wxButton* widget_button)
: wxPanel(parent,wxID_ANY, wxDefaultPosition, wxDefaultSize/*,wxBORDER_RAISED*/)
{
    // BBS: toggle button is removed
    //m_widget_button = widget_button;    // pointer to the button in parent dialog
    //m_widget_button->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){ toggle_advanced(true); });

    m_number_of_extruders = (int)(sqrt(matrix.size())+0.001);

    for (const std::string& color : extruder_colours) {
        unsigned char rgb[3];
        Slic3r::GUI::BitmapCache::parse_color(color, rgb);
        m_colours.push_back(wxColor(rgb[0], rgb[1], rgb[2]));
    }

	// Create two switched panels with their own sizers
    m_sizer_simple          = new wxBoxSizer(wxVERTICAL);
    m_sizer_advanced        = new wxBoxSizer(wxVERTICAL);
	m_page_simple			= new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_page_advanced			= new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_page_simple->SetSizer(m_sizer_simple);
	m_page_advanced->SetSizer(m_sizer_advanced);

    update_ui(m_page_simple);
    update_ui(m_page_advanced);

    auto gridsizer_simple   = new wxGridSizer(3, 5, 10);
    m_gridsizer_advanced = new wxGridSizer(m_number_of_extruders+1, 5, 1);

	// First create controls for advanced mode and assign them to m_page_advanced:
	for (unsigned int i = 0; i < m_number_of_extruders; ++i) {
		edit_boxes.push_back(std::vector<wxTextCtrl*>(0));

		for (unsigned int j = 0; j < m_number_of_extruders; ++j) {
#ifdef _WIN32
            wxTextCtrl* text = new wxTextCtrl(m_page_advanced, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(ITEM_WIDTH(), -1), wxBORDER_SIMPLE);
            update_ui(text);
            edit_boxes.back().push_back(text);
#else
			edit_boxes.back().push_back(new wxTextCtrl(m_page_advanced, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(ITEM_WIDTH(), -1)));
#endif
			if (i == j)
				edit_boxes[i][j]->Disable();
			else
				edit_boxes[i][j]->SetValue(wxString("") << int(matrix[m_number_of_extruders*j + i]));
		}
	}

    const int clr_icon_side = edit_boxes.front().front()->GetSize().y;
    const auto icon_size = wxSize(clr_icon_side, clr_icon_side);

	m_gridsizer_advanced->Add(new wxStaticText(m_page_advanced, wxID_ANY, wxString("")));
	for (unsigned int i = 0; i < m_number_of_extruders; ++i) {
        auto hsizer = new wxBoxSizer(wxHORIZONTAL);
        hsizer->AddSpacer(20);
        hsizer->Add(new wxStaticText(m_page_advanced, wxID_ANY, wxString("") << i + 1), 0, wxALIGN_CENTER);
        wxWindow* w = new wxWindow(m_page_advanced, wxID_ANY, wxDefaultPosition, icon_size, wxBORDER_SIMPLE);
        w->SetCanFocus(false);
        w->SetBackgroundColour(m_colours[i]);
        hsizer->AddStretchSpacer();
        hsizer->Add(w);
		m_gridsizer_advanced->Add(hsizer, 1, wxEXPAND);
    }
	for (unsigned int i = 0; i < m_number_of_extruders; ++i) {
        auto hsizer = new wxBoxSizer(wxHORIZONTAL);
        wxWindow* w = new wxWindow(m_page_advanced, wxID_ANY, wxDefaultPosition, icon_size, wxBORDER_SIMPLE);
        w->SetCanFocus(false);
        w->SetBackgroundColour(m_colours[i]);
        hsizer->AddSpacer(20);
        hsizer->Add(new wxStaticText(m_page_advanced, wxID_ANY, wxString("") << i + 1), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
        hsizer->AddStretchSpacer();
        hsizer->Add(w);
        m_gridsizer_advanced->Add(hsizer, 1, wxEXPAND);

    for (unsigned int j = 0; j < m_number_of_extruders; ++j)
        m_gridsizer_advanced->Add(edit_boxes[j][i], 0);
    }

	// collect and format sizer
	format_sizer(m_sizer_advanced, m_page_advanced, m_gridsizer_advanced,
		_(L("Here you can adjust required purging volume (mm³) for any given pair of filaments.")),
		_(L("Fiament changed to")));

	// Hide preview page before new page creating 
	// It allows to do that from a beginning of the main panel
	m_page_advanced->Hide(); 

	// Now the same for simple mode:
	gridsizer_simple->Add(new wxStaticText(m_page_simple, wxID_ANY, wxString("")), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
	gridsizer_simple->Add(new wxStaticText(m_page_simple, wxID_ANY, wxString(_(L("unloaded")))), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
    gridsizer_simple->Add(new wxStaticText(m_page_simple,wxID_ANY,wxString(_(L("loaded")))), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);

    auto add_spin_ctrl = [this](std::vector<wxSpinCtrl*>& vec, float initial)
    {
        wxSpinCtrl* spin_ctrl = new wxSpinCtrl(m_page_simple, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(ITEM_WIDTH(), -1), style | wxALIGN_RIGHT, 0, 300, (int)initial);
        update_ui(spin_ctrl);
        vec.push_back(spin_ctrl);

#ifdef __WXOSX__
        // On OSX / Cocoa, wxSpinCtrl::GetValue() doesn't return the new value
        // when it was changed from the text control, so the on_change callback
        // gets the old one, and on_kill_focus resets the control to the old value.
        // As a workaround, we get the new value from $event->GetString and store
        // here temporarily so that we can return it from get_value()
        spin_ctrl->Bind(wxEVT_TEXT, ([spin_ctrl](wxCommandEvent e)
        {
            long value;
            const bool parsed = e.GetString().ToLong(&value);
            int tmp_value = parsed && value >= INT_MIN && value <= INT_MAX ? (int)value : INT_MIN;

            // Forcibly set the input value for SpinControl, since the value 
            // inserted from the keyboard or clipboard is not updated under OSX
            if (tmp_value != INT_MIN) {
                spin_ctrl->SetValue(tmp_value);

                // But in SetValue() is executed m_text_ctrl->SelectAll(), so
                // discard this selection and set insertion point to the end of string
                spin_ctrl->GetText()->SetInsertionPointEnd();
            }
        }), spin_ctrl->GetId());
#endif
    };

	for (unsigned int i=0;i<m_number_of_extruders;++i) {
        add_spin_ctrl(m_old, extruders[2 * i]);
        add_spin_ctrl(m_new, extruders[2 * i+1]);

        auto hsizer = new wxBoxSizer(wxHORIZONTAL);
        wxWindow* w = new wxWindow(m_page_simple, wxID_ANY, wxDefaultPosition, icon_size, wxBORDER_SIMPLE);
        w->SetCanFocus(false);
        w->SetBackgroundColour(m_colours[i]);
        hsizer->Add(w, wxALIGN_CENTER_VERTICAL);
        hsizer->AddSpacer(10);
        hsizer->Add(new wxStaticText(m_page_simple, wxID_ANY, wxString(_(L("Filament #"))) << i + 1 << ": "), 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);

        gridsizer_simple->Add(hsizer, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
        gridsizer_simple->Add(m_old.back(),0);
        gridsizer_simple->Add(m_new.back(),0);
	}

	// collect and format sizer
	format_sizer(m_sizer_simple, m_page_simple, gridsizer_simple,
		_(L("Total purging volume is calculated by summing two values below, depending on which filaments are loaded/unloaded.")),
		_(L("Volume to purge (mm³) when the filament is being")), 50);

	m_sizer = new wxBoxSizer(wxVERTICAL);
	m_sizer->Add(m_page_simple, 0, wxEXPAND | wxALL, 25);
	m_sizer->Add(m_page_advanced, 0, wxEXPAND | wxALL, 25);

	m_sizer->SetSizeHints(this);
	SetSizer(m_sizer);

    toggle_advanced(); // to show/hide what is appropriate
    
    m_page_advanced->Bind(wxEVT_PAINT,[this](wxPaintEvent&) {
                                              wxPaintDC dc(m_page_advanced);
                                              int y_pos = 0.5 * (edit_boxes[0][0]->GetPosition().y + edit_boxes[0][edit_boxes.size()-1]->GetPosition().y + edit_boxes[0][edit_boxes.size()-1]->GetSize().y);
                                              wxString label = _(L("From"));
                                              int text_width = 0;
                                              int text_height = 0;
                                              dc.GetTextExtent(label,&text_width,&text_height);
                                              int xpos = m_gridsizer_advanced->GetPosition().x;
                                              dc.DrawRotatedText(label,xpos-text_height,y_pos + text_width/2.f,90);
    });
}




// Reads values from the (advanced) wiping matrix:
std::vector<float> WipingPanel::read_matrix_values() {
    if (!m_advanced)
        fill_in_matrix();
    std::vector<float> output;
    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        for (unsigned int j=0;j<m_number_of_extruders;++j) {
            double val = 0.;
            edit_boxes[j][i]->GetValue().ToDouble(&val);
            output.push_back((float)val);
        }
    }
    return output;
}

// Reads values from simple mode to save them for next time:
std::vector<float> WipingPanel::read_extruders_values() {
    std::vector<float> output;
    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        output.push_back(m_old[i]->GetValue());
        output.push_back(m_new[i]->GetValue());
    }
    return output;
}

// This updates the "advanced" matrix based on values from "simple" mode
void WipingPanel::fill_in_matrix() {
    for (unsigned i=0;i<m_number_of_extruders;++i) {
        for (unsigned j=0;j<m_number_of_extruders;++j) {
            if (i==j) continue;
                edit_boxes[j][i]->SetValue(wxString("")<< (m_old[i]->GetValue() + m_new[j]->GetValue()));
        }
    }
}



// Function to check if simple and advanced settings are matching
bool WipingPanel::advanced_matches_simple() {
    for (unsigned i=0;i<m_number_of_extruders;++i) {
        for (unsigned j=0;j<m_number_of_extruders;++j) {
            if (i==j) continue;
            if (edit_boxes[j][i]->GetValue() != (wxString("")<< (m_old[i]->GetValue() + m_new[j]->GetValue())))
                return false;
        }
    }
    return true;
}


// Switches the dialog from simple to advanced mode and vice versa
void WipingPanel::toggle_advanced(bool user_action) {
    if (m_advanced && !advanced_matches_simple() && user_action) {
//        if (wxMessageDialog(this,wxString(_(L("Switching to simple settings will discard changes done in the advanced mode!\n\nDo you want to proceed?"))),
        if (Slic3r::GUI::MessageDialog(this, _L("Switching to simple settings will discard changes done in the advanced mode!\n\nDo you want to proceed?"),
                            _L("Warning"),wxYES_NO|wxICON_EXCLAMATION).ShowModal() != wxID_YES)
            return;
    }
    if (user_action)
        m_advanced = !m_advanced;                // user demands a change -> toggle
    else {
        // BBS: show advanced mode by default
        //m_advanced = !advanced_matches_simple(); // if called from constructor, show what is appropriate
        m_advanced = true;
    }

    (m_advanced ? m_page_advanced : m_page_simple)->Show();
	(!m_advanced ? m_page_advanced : m_page_simple)->Hide();

    //m_widget_button->SetLabel(m_advanced ? _(L("Show simplified settings")) : _(L("Show advanced settings")));
    if (m_advanced)
        if (user_action) fill_in_matrix();  // otherwise keep values loaded from config

   m_sizer->Layout();
   Refresh();
}
