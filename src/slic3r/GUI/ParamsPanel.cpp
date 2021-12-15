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

#include "Widgets/Label.hpp"
#include "Widgets/SwitchButton.hpp"
#include "Widgets/Button.hpp"
#include "MarkdownTip.hpp"

namespace Slic3r {
namespace GUI {

ParamsPanel::ParamsPanel( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name )
    : wxPanel( parent, id, pos, size, style, name )
{
    // BBS: new layout
    SetBackgroundColour("white");
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

#else
    ParamsPanel*panel = this;
    m_top_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_top_sizer->SetSizeHints(panel);
    panel->SetSizer(m_top_sizer);
#endif //__WXOSX__

    // BBS: new layout
    m_mode_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition);
    m_mode_panel->SetBackgroundColour("#FFFFFF");
    m_mode_text = new Label(wxT("Advanced Mode"), m_mode_panel);
    m_mode_text->SetFont(Label::Head_12);
    m_mode_text->Wrap( -1 );

    //int width, height;
    // BBS: new layout
    m_mode_status = new SwitchButton(m_mode_panel);
    update_mode();
    //m_mode_status->GetSize(&width, &height);

    // BBS: new layout
    m_search_btn = new ScalableButton(m_mode_panel, wxID_ANY, "search", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
    m_search_btn->SetBackgroundColour("#E9E9E9");
    m_search_btn->SetToolTip(format_wxstr(_L("Search in settings [%1%]"), "Ctrl+F"));
    m_search_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { wxGetApp().plater()->search(false); });
    //m_search_button = new wxBitmapButton( this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );

    m_staticline_filament = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
    m_staticline_print = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
    m_staticline_printer = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    // BBS: new layout
    m_staticline_buttons = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    m_staticline_middle = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL);

    m_export_to_file = new Button( this, wxT("Export To File"), "");
    m_import_from_file = new Button( this, wxT("Import From File") );

    // Initialize the page.
#ifdef __WXOSX__
    auto page_parent = m_tmp_panel;
#else
    auto page_parent = this;
#endif

    // BBS: fix scroll to tip view
    class PageScrolledWindow : public wxScrolledWindow
    {
    public:
        PageScrolledWindow(wxWindow *parent)
            : wxScrolledWindow(parent,
                               wxID_ANY,
                               wxDefaultPosition,
                               wxDefaultSize,
                               wxVSCROLL | wxALWAYS_SHOW_SB) // hide hori-bar will cause hidden field mis-position
        {}
        virtual bool ShouldScrollToChildOnFocus(wxWindow *child)
        {
            return false;
        }
    };

    m_page_view = new PageScrolledWindow(page_parent);
    // BBS: center content view and add marktip at right side
    m_page_view->SetBackgroundColour("##FAFAFA");
    wxBoxSizer * page_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_page_sizer = new wxBoxSizer(wxVERTICAL);
    page_sizer->AddStretchSpacer(1.0);
    page_sizer->Add(m_page_sizer);
    page_sizer->AddStretchSpacer(1.0);
    page_sizer->Add(MarkdownTip::AttachTo(m_page_view), 0, wxEXPAND);

    m_page_view->SetSizer(page_sizer);
    m_page_view->SetScrollbars(1, 20, 1, 2);
    //m_page_view->SetScrollRate( 5, 5 );

    m_mode_status->Bind(wxEVT_TOGGLEBUTTON, &ParamsPanel::OnToggled, this);
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
    // BBS: new layout
    m_left_sizer->SetMinSize( wxSize( 300, -1 ) );

    m_mode_sizer = new wxBoxSizer( wxHORIZONTAL );
    m_mode_sizer->AddSpacer(22);
    m_mode_sizer->Add( m_mode_text, 0, wxALIGN_CENTER );
    m_mode_sizer->AddSpacer(9);
    m_mode_sizer->Add( m_mode_status, 0, wxALIGN_CENTER );
    m_mode_sizer->AddStretchSpacer(1);
    m_mode_sizer->Add( m_search_btn, 0, wxALIGN_CENTER );
    m_mode_sizer->AddSpacer(16);
    m_mode_sizer->SetMinSize(-1, 3 * em_unit(this));
    m_mode_panel->SetSizer(m_mode_sizer);
    m_left_sizer->Add( m_mode_panel, 0, wxEXPAND );

    m_left_sizer->Add( m_staticline_print, 0, wxEXPAND );
    //m_print_sizer = new wxBoxSizer( wxHORIZONTAL );
    //m_print_sizer->Add( m_tab_print, 1, wxEXPAND | wxALL, 5 );
    //m_left_sizer->Add( m_print_sizer, 1, wxEXPAND, 5 );
    m_left_sizer->Add( m_tab_print, 0, wxEXPAND );

    m_left_sizer->Add( m_staticline_filament, 0, wxEXPAND );
    //m_filament_sizer = new wxBoxSizer( wxVERTICAL );
    //m_filament_sizer->Add( m_tab_filament, 1, wxEXPAND | wxALL, 5 );
   // m_left_sizer->Add( m_filament_sizer, 1, wxEXPAND, 5 );
    m_left_sizer->Add( m_tab_filament, 0, wxEXPAND );

    m_left_sizer->Add( m_staticline_printer, 0, wxEXPAND );
    //m_printer_sizer = new wxBoxSizer( wxVERTICAL );
    //m_printer_sizer->Add( m_tab_printer, 1, wxEXPAND | wxALL, 5 );
    m_left_sizer->Add( m_tab_printer, 0, wxEXPAND );

    //m_left_sizer->Add( m_printer_sizer, 1, wxEXPAND, 1 );

    m_button_sizer = new wxBoxSizer( wxHORIZONTAL );

    m_button_sizer->Add( m_export_to_file, 0, wxALL, 5 );

    m_button_sizer->Add( m_import_from_file, 0, wxALL, 5 );

    m_left_sizer->Add( m_staticline_buttons, 0, wxEXPAND );
    m_left_sizer->Add( m_button_sizer, 0, wxALIGN_CENTER, 5 );

    m_top_sizer->Add(m_left_sizer, 0, wxEXPAND);
    m_top_sizer->Add(m_staticline_middle, 0, wxEXPAND, 0);

    //m_right_sizer = new wxBoxSizer( wxVERTICAL );

    //m_right_sizer->Add( m_page_view, 1, wxEXPAND | wxALL, 5 );

    //m_top_sizer->Add( m_right_sizer, 1, wxEXPAND, 5 );
    // BBS: new layout
#ifdef __WXOSX__
    m_top_sizer->Add(m_tmp_panel, 1, wxEXPAND | wxALL, 0);
    m_tmp_panel->GetSizer()->Add( m_page_view, 1, wxEXPAND );
#else
    m_top_sizer->Add( m_page_view, 1, wxEXPAND );
#endif

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
        // BBS: open/close tab
        //m_current_tab = m_tab_print;
        set_active_tab(m_tab_print);
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

    // BBS: open/close tab
    for (auto t : { m_tab_print , m_tab_filament, m_tab_printer }) {
        dynamic_cast<Tab*> (t)->set_expanded(tab == t);
        m_left_sizer->GetItem(t)->SetProportion(tab == t ? 1 : 0);
    }
    m_left_sizer->Layout();
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

    //BBS: disable the mode tab and return directly when enable develop mode
    if (app_mode == comDevelop)
    {
        m_mode_status->Disable();
        return;
    }
    if (!m_mode_status->IsEnabled())
        m_mode_status->Enable();

    if (app_mode == comExpert)
    {
        m_mode_status->SetValue(true);
    }
    else
    {
        m_mode_status->SetValue(false);
    }
}

void ParamsPanel::msw_rescale()
{
    m_mode_sizer->SetMinSize(-1, 3 * em_unit(this));
    ((Button*)m_export_to_file)->Rescale();
    ((Button*)m_import_from_file)->Rescale();
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

    // BBS: new layout
    if (m_staticline_buttons)
    {
        delete m_staticline_buttons;
        m_staticline_buttons = nullptr;
    }

    if (m_staticline_middle)
    {
        delete m_staticline_middle;
        m_staticline_middle = nullptr;
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
#if 0
    free_sizers();
    delete m_top_sizer;

    delete_subwindows();
#endif
    // BBS: fix double destruct of OG_CustomCtrl
    Tab* cur_tab = dynamic_cast<Tab*> (m_current_tab);
    if (cur_tab)
        cur_tab->clear_pages();

    MarkdownTip::AttachTo(NULL);
}

} // GUI
} // Slic3r
