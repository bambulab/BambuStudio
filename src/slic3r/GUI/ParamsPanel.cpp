///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 3.10.0-4761b0c)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Preset.hpp"
#include "ParamsPanel.hpp"
#include "Tab.hpp"
#include "format.hpp"
#include "MainFrame.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"

namespace Slic3r {
namespace GUI {

ParamsPanel::ParamsPanel( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name )
    : wxPanel( parent, id, pos, size, style, name )
{
#ifdef __WXOSX__
    m_top_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_top_sizer->SetSizeHints(this);
    this->SetSizer(m_top_sizer);

    // Create additional panel to Fit() it from OnActivate()
    // It's needed for tooltip showing on OSX
    m_tmp_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    auto panel = m_tmp_panel;
    auto  sizer = new wxBoxSizer(wxHORIZONTAL);
    m_tmp_panel->SetSizer(sizer);
    m_tmp_panel->Layout();

    m_top_sizer->Add(m_tmp_panel, 1, wxEXPAND | wxALL, 0);
#else
    ParamsPanel*panel = this;
    m_top_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_top_sizer->SetSizeHints(panel);
    panel->SetSizer(m_top_sizer);
#endif //__WXOSX__

    m_mode_text = new wxStaticText( this, wxID_ANY, wxT("Advanced Mode"), wxDefaultPosition, wxDefaultSize, 0 );
    m_mode_text->Wrap( -1 );

    //int width, height;
    m_mode_status = new wxBitmapToggleButton( this, wxID_ANY, wxNullBitmap, wxDefaultPosition,  wxSize(4 * em_unit(this), -1), wxBORDER_NONE );
    m_toggle_on_icon = create_scaled_bitmap("toggle_on");
    m_toggle_off_icon = create_scaled_bitmap("toggle_off");
    m_mode_status->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    update_mode();
    //m_mode_status->GetSize(&width, &height);

    m_search_btn = new ScalableButton(this, wxID_ANY, "search", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
    m_search_btn->SetToolTip(format_wxstr(_L("Search in settings [%1%]"), "Ctrl+F"));
    m_search_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { wxGetApp().plater()->search(false); });
    //m_search_button = new wxBitmapButton( this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );

    m_staticline_filament = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
    m_staticline_print = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
    m_staticline_printer = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );

    m_export_to_file = new wxButton( this, wxID_ANY, wxT("Export To File"), wxDefaultPosition, wxDefaultSize, 0 );
    m_import_from_file = new wxButton( this, wxID_ANY, wxT("Import From File"), wxDefaultPosition, wxDefaultSize, 0 );

    // Initialize the page.
#ifdef __WXOSX__
    auto page_parent = m_tmp_panel;
#else
    auto page_parent = this;
#endif

    m_page_view = new wxScrolledWindow( page_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL|wxVSCROLL );
    m_page_sizer = new wxBoxSizer(wxVERTICAL);
    m_page_view->SetSizer(m_page_sizer);
    m_page_view->SetScrollbars(1, 20, 1, 2);
    //m_page_view->SetScrollRate( 5, 5 );

    this->Bind(wxEVT_TOGGLEBUTTON, &ParamsPanel::OnToggled, this);
    m_export_to_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { wxGetApp().mainframe->export_config(); });
    m_import_from_file->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { wxGetApp().mainframe->load_config_file(); });
}

void ParamsPanel::create_layout()
{
#ifdef __WINDOWS__
    this->SetDoubleBuffered(true);
    m_page_view->SetDoubleBuffered(true);
#endif //__WINDOWS__

    m_left_sizer = new wxBoxSizer( wxVERTICAL );
    m_left_sizer->SetMinSize( wxSize( 300,-1 ) );

    m_mode_sizer = new wxBoxSizer( wxHORIZONTAL );
    m_mode_sizer->Add( m_mode_text, 0, wxALIGN_CENTER|wxALL, 5 );
    m_mode_sizer->Add( m_mode_status, 0, wxALIGN_CENTER|wxALL, 5 );
    m_mode_sizer->Add( 0, 0, 1, wxEXPAND, 5 );
    m_mode_sizer->Add( m_search_btn, 0, wxALIGN_RIGHT|wxALL, 5 );
    m_left_sizer->Add( m_mode_sizer, 0, wxEXPAND, 5 );

    m_left_sizer->Add( m_staticline_print, 0, wxEXPAND | wxALL, 5 );
    //m_print_sizer = new wxBoxSizer( wxHORIZONTAL );
    //m_print_sizer->Add( m_tab_print, 1, wxEXPAND | wxALL, 5 );
    //m_left_sizer->Add( m_print_sizer, 1, wxEXPAND, 5 );
    m_left_sizer->Add( m_tab_print, 1, wxEXPAND | wxALL, 5 );

    m_left_sizer->Add( m_staticline_filament, 0, wxEXPAND | wxALL, 5 );
    //m_filament_sizer = new wxBoxSizer( wxVERTICAL );
    //m_filament_sizer->Add( m_tab_filament, 1, wxEXPAND | wxALL, 5 );
   // m_left_sizer->Add( m_filament_sizer, 1, wxEXPAND, 5 );
    m_left_sizer->Add( m_tab_filament, 1, wxEXPAND | wxALL, 5 );

    m_left_sizer->Add( m_staticline_printer, 0, wxEXPAND | wxALL, 5 );
    //m_printer_sizer = new wxBoxSizer( wxVERTICAL );
    //m_printer_sizer->Add( m_tab_printer, 1, wxEXPAND | wxALL, 5 );
    m_left_sizer->Add( m_tab_printer, 1, wxEXPAND | wxALL, 5 );

    //m_left_sizer->Add( m_printer_sizer, 1, wxEXPAND, 5 );

    m_button_sizer = new wxBoxSizer( wxHORIZONTAL );

    m_button_sizer->Add( m_export_to_file, 0, wxALL, 5 );

    m_button_sizer->Add( m_import_from_file, 0, wxALL, 5 );

    m_left_sizer->Add( m_button_sizer, 0, wxALIGN_CENTER, 5 );

    m_top_sizer->Add( m_left_sizer, 0, wxEXPAND, 5 );

    //m_right_sizer = new wxBoxSizer( wxVERTICAL );

    //m_right_sizer->Add( m_page_view, 1, wxEXPAND | wxALL, 5 );

    //m_top_sizer->Add( m_right_sizer, 1, wxEXPAND, 5 );
    m_top_sizer->Add( m_page_view, 1, wxEXPAND, 5 );

    //this->SetSizer( m_top_sizer );
    this->Layout();
}

void ParamsPanel::rebuild_panels()
{
    refresh_tabs();
    free_sizers();
    create_layout();
}

void ParamsPanel::refresh_tabs()
{
    auto& tabs_list = wxGetApp().tabs_list;
    auto print_tech = wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology();
    for (auto tab : tabs_list)
        if (tab->supports_printer_technology(print_tech))
        {
            switch (tab->type())
            {
                case Preset::TYPE_PRINT:
                case Preset::TYPE_SLA_PRINT:
                    m_tab_print = tab;
                    break;

                case Preset::TYPE_FILAMENT:
                case Preset::TYPE_SLA_MATERIAL:
                    m_tab_filament = tab;
                    break;

                case Preset::TYPE_PRINTER:
                    m_tab_printer = tab;
                    break;
                default:
                    break;
            }
        }
    return;
}

void ParamsPanel::clear_page()
{
    if (m_page_sizer)
        m_page_sizer->Clear(true);
}


void ParamsPanel::OnActivate()
{
    if (m_current_tab == NULL)
    {
        //the first time
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": first time opened, set current tab to print");
        m_current_tab = m_tab_print;
    }
    Tab* cur_tab = dynamic_cast<Tab *> (m_current_tab);
    if (cur_tab)
        cur_tab->OnActivate();
}

void ParamsPanel::OnToggled(wxCommandEvent& event)
{
    bool value = m_mode_status->GetValue();
    int mode_id;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": Advanced mode toogle to %1%") % value;

    if (value)
    {
        //m_mode_status->SetBitmap(m_toggle_on_icon);
        mode_id = comExpert;
    }
    else
    {
        //m_mode_status->SetBitmap(m_toggle_off_icon);
        mode_id = comSimple;
    }

    Slic3r::GUI::wxGetApp().save_mode(mode_id);
}


void ParamsPanel::set_active_tab(wxPanel* tab)
{
    Tab* cur_tab = dynamic_cast<Tab *> (tab);

    m_current_tab = tab;
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": set current to %1%, type=%2%") % cur_tab % cur_tab?cur_tab->type():-1;
}

bool ParamsPanel::is_active_and_shown_tab(wxPanel* tab)
{
    if (m_current_tab == tab)
        return true;
    else
        return false;
}

void ParamsPanel::update_mode()
{
    int app_mode = Slic3r::GUI::wxGetApp().get_mode();

    if (app_mode == comExpert)
    {
        m_mode_status->SetValue(true);
        m_mode_status->SetBitmap(m_toggle_on_icon);
    }
    else
    {
        m_mode_status->SetValue(false);
        m_mode_status->SetBitmap(m_toggle_off_icon);
    }
}

void ParamsPanel::free_sizers()
{
    if (m_top_sizer)
    {
        m_top_sizer->Clear(false);
        //m_top_sizer = nullptr;
    }

    m_left_sizer = nullptr;
    m_right_sizer = nullptr;
    m_mode_sizer = nullptr;
    m_print_sizer = nullptr;
    m_filament_sizer = nullptr;
    m_printer_sizer = nullptr;
    m_button_sizer = nullptr;
}

void ParamsPanel::delete_subwindows()
{
    if (m_mode_text)
    {
        delete m_mode_text;
        m_mode_text = nullptr;
    }

    if (m_mode_status)
    {
        delete m_mode_status;
        m_mode_status = nullptr;
    }

    if (m_search_btn)
    {
        delete m_search_btn;
        m_search_btn = nullptr;
    }

    if (m_staticline_print)
    {
        delete m_staticline_print;
        m_staticline_print = nullptr;
    }

    if (m_staticline_filament)
    {
        delete m_staticline_filament;
        m_staticline_filament = nullptr;
    }

    if (m_staticline_printer)
    {
        delete m_staticline_printer;
        m_staticline_printer = nullptr;
    }

    if (m_export_to_file)
    {
        delete m_export_to_file;
        m_export_to_file = nullptr;
    }

    if (m_import_from_file)
    {
        delete m_import_from_file;
        m_import_from_file = nullptr;
    }

    if (m_page_view)
    {
        delete m_page_view;
        m_page_view = nullptr;
    }
}

ParamsPanel::~ParamsPanel()
{
    free_sizers();
    delete m_top_sizer;

    delete_subwindows();
}

} // GUI
} // Slic3r
