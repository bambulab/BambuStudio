#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Factories.hpp"
#include "GUI_ObjectTableSettings.hpp"
#include "GUI_ObjectTable.hpp"

#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "Plater.hpp"

#include <boost/algorithm/string.hpp>

#include "I18N.hpp"
#include "ConfigManipulation.hpp"

#include <wx/wupdlock.h>

namespace Slic3r
{
namespace GUI
{

OTG_Settings::OTG_Settings(wxWindow* parent, const bool staticbox) :
    m_parent(parent)
{
    wxString title = staticbox ? " " : ""; // temporary workaround - #ys_FIXME
    m_og = std::make_shared<ConfigOptionsGroup>(parent, title, (DynamicPrintConfig*)nullptr, true);
}

bool OTG_Settings::IsShown()
{
    return m_og->sizer->IsEmpty() ? false : m_og->sizer->IsShown(size_t(0));
}

void OTG_Settings::Show(const bool show)
{
    m_og->Show(show);
}

void OTG_Settings::Hide()
{
    Show(false);
}

void OTG_Settings::UpdateAndShow(const bool show)
{
    Show(show);
//    m_parent->Layout();
}

wxSizer* OTG_Settings::get_sizer()
{
    return m_og->sizer;
}



ObjectTableSettings::ObjectTableSettings(wxWindow* parent, ObjectGridTable* table) :
    OTG_Settings(parent, true), m_table(table)
{
    m_og->activate();
    //m_og->set_name(_(L("Per-Object Settings")));    

    m_settings_list_sizer = new wxBoxSizer(wxVERTICAL);
    m_og->sizer->Add(m_settings_list_sizer, 1, wxEXPAND | wxLEFT, 5);

    m_bmp_reset = ScalableBitmap(parent, "undo");
    m_bmp_reset_focus = ScalableBitmap(parent, "undo");
    //TODO, adjust later
    m_bmp_reset_disable = ScalableBitmap(parent, "dot_white");
}

bool ObjectTableSettings::update_settings_list(bool is_object, bool is_multiple_selection, ModelObject* object, ModelConfig* config, const std::string& category)
{
    m_settings_list_sizer->Clear(true);
    m_og_settings.resize(0);
    Show(true);

    if (!config || is_multiple_selection || !object)
        return false;

    const auto printer_technology   = wxGetApp().plater()->printer_technology();

    // update config values according to configuration hierarchy
    m_current_config   = printer_technology == ptFFF ?
                                        wxGetApp().preset_bundle->prints.get_edited_preset().config :
                                        wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;

    //ConfigManipulation config_manipulation(load_config, toggle_field, nullptr, config);

    if (!is_object)
    {
        m_current_config.apply(object->config.get(), true);
    }

    m_origin_config = m_current_config;
    m_current_config.apply(config->get(), true);

    //SettingsFactory::Bundle cat_options = SettingsFactory::get_bundle(&config->get(), is_object);
    std::map<std::string, std::vector<SimpleSettingData>> cat_options;
    std::vector<SimpleSettingData> category_settings = SettingsFactory::get_visible_options(category, !is_object);
    if (category_settings.size() == 0) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "can not find settings for category " <<category <<", should not happen!!!" << std::endl;
        return false;
    }
    cat_options.emplace(category, category_settings);
    std::vector<std::string> categories;
    categories.reserve(cat_options.size());

    auto extra_column = [this, is_object, object, config, category](wxWindow* parent, const Line& line)
    {
        auto opt_key = (line.get_options())[0].opt_id;  //we assume that we have one option per line

        auto btn = new ScalableButton(parent, wxID_ANY, m_bmp_reset);
        btn->SetToolTip(_(L("Reset parameter")));

        btn->SetBitmapFocus(m_bmp_reset_focus.bmp());
        btn->SetBitmapHover(m_bmp_reset_focus.bmp());
        btn->SetBitmapDisabled(m_bmp_reset_disable.bmp());

        btn->Bind(wxEVT_BUTTON, [btn, opt_key, this, is_object, object, config, category](wxEvent &event) {
            //wxGetApp().plater()->take_snapshot(from_u8((boost::format(_utf8(L("Reset Option %s"))) % opt_key).str()));
            config->erase(opt_key);
            //btn->Hide();
            wxGetApp().obj_list()->changed_object();
            /*wxTheApp->CallAfter([this, is_object, object, config, category]() {
                wxWindowUpdateLocker noUpdates(m_parent);
                update_settings_list(is_object, false, object, config, category); 
            });*/
            this->m_parent->Freeze();
            /* Check overriden options list after deleting.
             * Some options couldn't be deleted because of another one.
             * Like, we couldn't delete fill pattern, if fill density is set to 100%
             */
            m_current_config = m_origin_config;
            m_current_config.apply(config->get(), true);
            update_config_values(is_object, object, config, category);
            this->m_parent->Thaw();
        });
        (const_cast<Line&>(line)).extra_widget_win = btn;
        return btn;
    };

    for (auto& cat : cat_options)
    {
        categories.push_back(cat.first);

        auto optgroup = std::make_shared<ConfigOptionsGroup>(m_og->ctrl_parent(), _(cat.first), &m_current_config, false, extra_column);
        optgroup->label_width = 15;
        optgroup->sidetext_width = 5;

        optgroup->m_on_change = [this, optgroup, is_object, object, config, category](const t_config_option_key& opt_id, const boost::any& value) {
                                    this->m_parent->Freeze();
                                    this->update_config_values(is_object, object, config, category);
                                    wxGetApp().obj_list()->changed_object();
                                    this->m_parent->Thaw();
                                    //update_extra_column_visible_status(optgroup.get(), cat.second, config);
                                };

        // call back for rescaling of the extracolumn control
        optgroup->rescale_extra_column_item = [this](wxWindow* win) {
            auto *ctrl = dynamic_cast<ScalableButton*>(win);
            if (ctrl == nullptr)
                return;
            ctrl->SetBitmap_(m_bmp_reset);
            ctrl->SetBitmapFocus(m_bmp_reset_focus.bmp()); 
            ctrl->SetBitmapHover(m_bmp_reset_focus.bmp());
            ctrl->SetBitmapDisabled(m_bmp_reset_disable.bmp());
        };

        const bool is_extruders_cat = cat.first == "Extruders";
        for (auto& opt : cat.second)
        {
            Option option = optgroup->get_option(opt.name);
            option.opt.width = 12;
            if (is_extruders_cat)
                option.opt.max = wxGetApp().extruders_edited_cnt();
            optgroup->append_single_option_line(option);

            if (!opt.label.empty()) {
                auto line = optgroup->get_line(opt.name);
                if (line)
                    line->label = GUI::from_u8(opt.label);
            }
        }
        optgroup->activate();
        for (auto& opt : cat.second)
            optgroup->get_field(opt.name)->m_on_change = [optgroup](const std::string& opt_id, const boost::any& value) {
                // first of all take a snapshot and then change value in configuration
                wxGetApp().plater()->take_snapshot(from_u8((boost::format(_utf8(L("Change Option %s"))) % opt_id).str()));
                optgroup->on_change_OG(opt_id, value);
            };

        optgroup->reload_config();
        update_extra_column_visible_status(optgroup.get(), cat.second, config);
        /*for (auto& opt : cat.second)
        {
            auto line = optgroup->get_line(opt);
            if (line) {
                if (config->has(opt))
                    line->extra_widget_win->Show(true);
                else
                    line->extra_widget_win->Hide();
            }
        }*/

        m_settings_list_sizer->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 0);
        m_og_settings.push_back(optgroup);

        auto toggle_field = [this, optgroup](const t_config_option_key & opt_key, bool toggle, int opt_index)
        {
            Field* field = optgroup->get_fieldc(opt_key, opt_index);;
            if (field)
                field->toggle(toggle);
        };
        ConfigManipulation config_manipulation(nullptr, toggle_field, nullptr, &m_current_config);

        printer_technology == ptFFF  ?  config_manipulation.toggle_print_fff_options(&m_current_config) :
                                        config_manipulation.toggle_print_sla_options(&m_current_config) ;
    }

    //if (!categories.empty()) {
    //    update_config_values(is_object, object, config, category);
    //}

    return true;
}

bool ObjectTableSettings::add_missed_options(ModelConfig* config_to, const DynamicPrintConfig& config_from)
{
    bool is_added = false;
    if (wxGetApp().plater()->printer_technology() == ptFFF)
    {
        if (config_to->has("fill_density") && !config_to->has("fill_pattern"))
        {
            if (config_from.option<ConfigOptionPercent>("fill_density")->value == 100) {
                config_to->set_key_value("fill_pattern", config_from.option("fill_pattern")->clone());
                is_added = true;
            }
        }
    }

    return is_added;
}

void ObjectTableSettings::update_extra_column_visible_status(ConfigOptionsGroup* option_group, const std::vector<SimpleSettingData>& option_keys, ModelConfig* config)
{
    for (auto& opt : option_keys)
    {
        auto line = option_group->get_line(opt.name);
        Field* field = option_group->get_fieldc(opt.name, -1);
        wxWindow *reset_window = field?field->getWindow():nullptr;
        if (line) {
            if ((config->has(opt.name)) && reset_window&&reset_window->IsEnabled())
                line->extra_widget_win->Enable();
            else
                line->extra_widget_win->Disable();
        }
    }
    wxGridSizer* grid_sizer = option_group->get_grid_sizer();
    grid_sizer->Layout();
}

void ObjectTableSettings::update_config_values(bool is_object, ModelObject* object, ModelConfig* config, const std::string& category)
{
    const auto printer_technology   = wxGetApp().plater()->printer_technology();

    if (!object || !config)
        return;

    // update config values according to configuration hierarchy
    DynamicPrintConfig  &main_config   = m_current_config;


    auto toggle_field = [this](const t_config_option_key & opt_key, bool toggle, int opt_index)
    {
        Field* field = nullptr;
        for (auto og : m_og_settings) {
            field = og->get_fieldc(opt_key, opt_index);
            if (field != nullptr)
                break;
        }
        if (field)
            field->toggle(toggle);
    };

    ConfigManipulation config_manipulation(nullptr, toggle_field, nullptr, &m_current_config);

    printer_technology == ptFFF  ?  config_manipulation.update_print_fff_config(&main_config) :
                                    config_manipulation.update_print_sla_config(&main_config) ;

    printer_technology == ptFFF  ?  config_manipulation.toggle_print_fff_options(&main_config) :
                                    config_manipulation.toggle_print_sla_options(&main_config) ;

    t_config_option_keys diff_keys;
    for (const t_config_option_key &opt_key : main_config.keys()) {
        const ConfigOption *this_opt  = main_config.option(opt_key);
        const ConfigOption *other_opt = m_origin_config.option(opt_key);
        if (this_opt != nullptr && (other_opt == nullptr || *this_opt != *other_opt))
            diff_keys.emplace_back(opt_key);
    }

    // load checked values from main_config to config
    config->reset();
    config->apply_only(main_config, diff_keys, true);
    // Initialize UI components with the config values.
    std::vector<SimpleSettingData> category_settings = SettingsFactory::get_visible_options(category, !is_object);
    for (auto og : m_og_settings)
    {
        og->reload_config();
        update_extra_column_visible_status(og.get(), category_settings, config);
    }

    //update the table and volume settings
    m_table->reload_cell_data(m_current_row, category);
}

void ObjectTableSettings::UpdateAndShow(int row, const bool show, bool is_object, bool is_multiple_selection, ModelObject* object, ModelConfig* config, const std::string& category)
{
    m_current_row = row;
    m_current_category = category;
    //OTG_Settings::UpdateAndShow(show ? update_settings_list(is_object, is_multiple_selection, object, config, category) : false);
    if (show) {
        update_settings_list(is_object, is_multiple_selection, object, config, category);
    }
    else
        OTG_Settings::UpdateAndShow(false);
}

void ObjectTableSettings::ValueChanged(int row, bool is_object,  ModelObject* object, ModelConfig* config, const std::string& category, const std::string& key)
{
    if ((row != m_current_row) || (category != m_current_category))
        return;

    ConfigOption *my_opt = m_current_config.option(key, true);
    if (config->has(key)) {
        my_opt->set(config->option(key));
    }
    else {
        my_opt->set(m_origin_config.option(key));
    }
    update_config_values(is_object, object, config, category);
}


void ObjectTableSettings::msw_rescale()
{
    for (auto group : m_og_settings)
        group->msw_rescale();
}

} //namespace GUI
} //namespace Slic3r 

