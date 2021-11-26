#include "SelectMachine.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/PresetBundle.hpp"
//#include "libslic3r/Model.hpp"
//#include "Plater.hpp"
#include "Widgets/Label.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "BitmapCache.hpp"
#include "GUI_ObjectTable.hpp"
#include "GUI_ObjectList.hpp"

namespace Slic3r {
namespace GUI {
static const int grid_cell_border_width = 2;
static const int grid_cell_border_height = 2;
static const int grid_cell_checkbox_size = 16;
static const int min_row_count = 16;

/* ObjectGridTable related class */
// ----------------------------------------------------------------------------
// GridCellIconRenderer
// ----------------------------------------------------------------------------
void GridCellIconRenderer::Draw(wxGrid& grid,
                              wxGridCellAttr& attr,
                              wxDC& dc,
                              const wxRect& rect,
                              int row, int col,
                              bool isSelected)
{
    ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(grid.GetTable());

    wxGridCellRenderer::Draw(grid, attr, dc, rect, row, col, isSelected);
    if (table) {
        ObjectGridTable::ObjectGridCol* grid_col = table->get_grid_col(col);
        if (!grid_col || !grid_col->b_icon) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": cell (%1%, %2%) not icon type") %row %col;
            return;
        }
        ObjectGridTable::ObjectGridRow* grid_row = table->get_grid_row(row - 1);
        ConfigOption& orig_option = (*grid_row)[(ObjectGridTable::GridColType)col];
        ConfigOption& cur_option = (*grid_row)[(ObjectGridTable::GridColType)(col-1)];
        if (cur_option == orig_option) {
            //not changed
            return;
        }
        if (!table->m_icon_col_width) {
            table->m_icon_row_height = grid.GetRowSize(row);
            table->m_icon_col_width = grid.GetColSize(col);
        }
        wxBitmap& bitmap = table->get_undo_bitmap();
        int bitmap_width = bitmap.GetWidth();
        int bitmap_height = bitmap.GetHeight();
        int offset_x = (table->m_icon_col_width - bitmap_width)/2;
        int offset_y = (table->m_icon_row_height - bitmap_height)/2;
        dc.DrawBitmap(bitmap, wxPoint(rect.x + offset_x, rect.y + offset_y));

        //dc.SetPen(*wxGREEN_PEN);
        //dc.SetBrush(*wxTRANSPARENT_BRUSH);
        //dc.DrawEllipse(rect);
    }
    else {
        //should not happen
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": no table found, should not happen" << std::endl;
    }
}

wxSize GridCellIconRenderer::GetBestSize(wxGrid& WXUNUSED(grid),
                               wxGridCellAttr& attr,
                               wxDC& dc,
                               int WXUNUSED(row),
                               int WXUNUSED(col))
{
    wxSize size{32, 30};
    return size;
}

GridCellIconRenderer *GridCellIconRenderer::Clone() const
{
    return new GridCellIconRenderer();
}

// ----------------------------------------------------------------------------
// GridCellFilamentsEditor
// ----------------------------------------------------------------------------

GridCellFilamentsEditor::GridCellFilamentsEditor(const wxArrayString& choices,
                                               bool allowOthers,
                                               std::vector<wxBitmap*>* bitmaps)
    : wxGridCellChoiceEditor(choices, allowOthers), m_icons(bitmaps)
{
}

GridCellFilamentsEditor::GridCellFilamentsEditor(size_t count,
                                               const wxString choices[],
                                               bool allowOthers,
                                               std::vector<wxBitmap*>* bitmaps)
    : wxGridCellChoiceEditor(count, choices, allowOthers), m_icons(bitmaps)
{
}

wxGridCellEditor *GridCellFilamentsEditor::Clone() const
{
    GridCellFilamentsEditor *editor = new GridCellFilamentsEditor;
    editor->m_allowOthers = m_allowOthers;
    editor->m_choices = m_choices;
    editor->m_icons = m_icons;

    return editor;
}

void GridCellFilamentsEditor::Create(wxWindow* parent,
                                    wxWindowID id,
                                    wxEvtHandler* evtHandler)
{
    int style = wxTE_PROCESS_ENTER |
                wxTE_PROCESS_TAB |
                wxBORDER_NONE;

    if ( !m_allowOthers )
        style |= wxCB_READONLY;
    wxBitmapComboBox *bitmap_combo = new wxBitmapComboBox(parent, id, wxEmptyString,
                               wxDefaultPosition, wxDefaultSize,
                               m_choices,
                               style);
    if (m_icons) {
        int array_count = m_choices.GetCount();
        int icon_count = m_icons->size();
        for (int i = 0; i < array_count; i++)
        {
            wxBitmap* bitmap = (i < icon_count) ? (*m_icons)[i] : (*m_icons)[0];
            bitmap_combo->SetItemBitmap(i, *bitmap);
        }
    }
    m_control = bitmap_combo;
    wxGridCellEditor::Create(parent, id, evtHandler);
}

void GridCellFilamentsEditor::SetSize(const wxRect& rect)
{
    wxGridCellChoiceEditor::SetSize(rect);
    /*wxASSERT_MSG(m_control,
                 wxT("The wxGridCellChoiceEditor must be created first!"));

    // Use normal wxChoice size, except for extending it to fill the cell
    // width: we can't be smaller because this could make the control unusable
    // and we don't want to be taller because this looks unusual and weird.
    wxSize size = m_control->GetBestSize();
    if ( size.x < rect.width )
        size.x = rect.width;

    DoPositionEditor(size, rect);*/
}

// ----------------------------------------------------------------------------
// GridCellFilamentsRenderer
// ----------------------------------------------------------------------------
void GridCellFilamentsRenderer::Draw(wxGrid& grid,
                              wxGridCellAttr& attr,
                              wxDC& dc,
                              const wxRect& rect,
                              int row, int col,
                              bool isSelected)
{
    ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(grid.GetTable());
    wxRect text_rect = rect;

    if (table) {
        ObjectGridTable::ObjectGridCol* grid_col = table->get_grid_col(col);
        ObjectGridTable::ObjectGridRow* grid_row = table->get_grid_row(row - 1);
        ConfigOptionInt& cur_option = dynamic_cast<ConfigOptionInt&>((*grid_row)[(ObjectGridTable::GridColType)col]);

        wxBitmap* bitmap = table->get_color_bitmap((cur_option.value >= 1)?cur_option.value-1:cur_option.value);
        int bitmap_width = bitmap->GetWidth();
        int bitmap_height = bitmap->GetHeight();
        int offset_x = grid_cell_border_width;
        int offset_y = (rect.height > bitmap_height)?(rect.height - bitmap_height)/2 : grid_cell_border_height;

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(attr.GetBackgroundColour()));
        dc.DrawRectangle(rect);
        dc.DrawBitmap(*bitmap, wxPoint(rect.x + offset_x, rect.y + offset_y));
        text_rect.x += bitmap_width + grid_cell_border_width *2;
        text_rect.width -= (bitmap_width + grid_cell_border_width *2);
    }

    wxGridCellChoiceRenderer::Draw(grid, attr, dc, text_rect, row, col, isSelected);
}

wxSize GridCellFilamentsRenderer::GetBestSize(wxGrid& grid,
                               wxGridCellAttr& attr,
                               wxDC& dc,
                               int WXUNUSED(row),
                               int WXUNUSED(col))
{
    wxSize size{128, -1};
    return size;
}

GridCellFilamentsRenderer *GridCellFilamentsRenderer::Clone() const
{
    return new GridCellFilamentsRenderer();
}

// ----------------------------------------------------------------------------
// GridCellSupportRenderer
// ----------------------------------------------------------------------------
void GridCellSupportRenderer::Draw(wxGrid& grid,
                              wxGridCellAttr& attr,
                              wxDC& dc,
                              const wxRect& rect,
                              int row, int col,
                              bool isSelected)
{
    ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(grid.GetTable());
    wxRect text_rect = rect;

    if (table) {
        ObjectGridTable::ObjectGridCol* grid_col = table->get_grid_col(col);
        ObjectGridTable::ObjectGridRow* grid_row = table->get_grid_row(row - 1);
        ConfigOptionBool& cur_option = dynamic_cast<ConfigOptionBool&>((*grid_row)[(ObjectGridTable::GridColType)col]);
        wxString support_text;

        if (cur_option.value)
            support_text = L("Support Enabled");
        else
            support_text = L("Support Disabled");
        int text_width, text_height;
        grid.GetTextExtent(L("Support Disabled"), &text_width, &text_height, NULL, NULL, &Label::Body_10);

        int offset_x = grid_cell_border_width;
        int offset_y = (rect.height > text_height)?(rect.height - text_height)/2 : grid_cell_border_height;

        //dc.SetPen(*wxTRANSPARENT_PEN);
        //dc.SetBrush(wxBrush(attr.GetBackgroundColour()));
        //dc.DrawRectangle(rect);

        wxGridCellRenderer::Draw(grid, attr, dc, rect, row, col, isSelected);

        wxColour text_back_colour;
        wxColour text_fore_colour;
        if ( isSelected )
        {
            text_back_colour = grid.GetSelectionBackground();
            text_fore_colour = grid.GetSelectionForeground();
        }
        else
        {
            text_back_colour = attr.GetBackgroundColour();
            text_fore_colour = attr.GetTextColour();
        }
        dc.SetTextBackground(text_back_colour);
        dc.SetTextForeground(text_fore_colour);
        dc.DrawText(support_text, wxPoint(rect.x + offset_x, rect.y + offset_y));
        text_rect.x += text_width + grid_cell_border_width *2;
        text_rect.width -= (text_width + grid_cell_border_width *2);
        text_rect.y += offset_y;
        text_rect.height = grid_cell_checkbox_size;// + grid_cell_border_width *2;

        int flags = wxCONTROL_CELL;
        if (cur_option.value)
            flags |= wxCONTROL_CHECKED;

        wxRendererNative::Get().DrawCheckBox( &grid, dc, text_rect, flags );
    }

    //wxGridCellBoolRenderer::Draw(grid, attr, dc, text_rect, row, col, isSelected);
}

wxSize GridCellSupportRenderer::GetBestSize(wxGrid& grid,
                               wxGridCellAttr& attr,
                               wxDC& dc,
                               int WXUNUSED(row),
                               int WXUNUSED(col))
{
    int text_width, text_height, width;
    grid.GetTextExtent(L("Support Disabled"), &text_width, &text_height, NULL, NULL, &Label::Body_10);
    width = text_width + 3*grid_cell_border_width + grid_cell_checkbox_size;

    wxSize size{width, 20};

    return size;
}

GridCellSupportRenderer *GridCellSupportRenderer::Clone() const
{
    return new GridCellSupportRenderer();
}

ObjectGridTable::~ObjectGridTable()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", this %1%, row_data size %2%") %this % m_grid_data.size();
    for ( int index = 0; index < m_grid_data.size(); index++ )
    {
        if (m_grid_data[index])
            delete (m_grid_data[index]);
    }
    m_grid_data.clear();

    for ( int index = 0; index < m_col_data.size(); index++ )
    {
        if (m_col_data[index])
            delete (m_col_data[index]);
    }

    m_selected_cells.clear();
}

/* ObjectGridTable related class */
wxString ObjectGridTable::GetTypeName(int row, int col)
{
    if (row == 0)
        return wxGRID_VALUE_STRING;

    ObjectGridCol* col_object = m_col_data[col];
    ConfigOptionType option_type = col_object->type;
    wxString type_name;

    switch (option_type)
    {
        case coString:
            return wxGRID_VALUE_STRING;
        case coBool:
            return wxGRID_VALUE_BOOL;
        case coInt:
            return wxGRID_VALUE_NUMBER;
        case coFloat:
        case coPercent:
            return wxGRID_VALUE_FLOAT;
        case coEnum:
            return wxGRID_VALUE_CHOICE;
        default:
            break;
    }

    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", unknown column");
    return wxEmptyString;
}

int ObjectGridTable::GetNumberRows()
{
    return m_grid_data.size() + 1;
}

int ObjectGridTable::GetNumberCols()
{
    return m_col_data.size();
}

bool ObjectGridTable::IsEmptyCell( int row, int col )
{
    if (row == 0)
        return false;

    ObjectGridCol* col_object = m_col_data[col];
    ObjectGridRow* row_object = m_grid_data[row - 1];

    if (col_object->b_for_object && (row_object->row_type == row_volume))
        return true;

    return false;
}

bool ObjectGridTable::CanGetValueAs(int row, int col, const wxString& typeName)
{
    //row 0 always use string type for label
    if (row == 0) {
        if ( typeName == wxGRID_VALUE_STRING )
            return true;
        else
            return false;
    }

    //other rows for data
    ObjectGridCol* col_object = m_col_data[col];
    ObjectGridRow* row_object = m_grid_data[row - 1];
    ConfigOptionType option_type = col_object->type;

    if (col_object->b_icon)
        return false;
    if (col_object->b_for_object && (row_object->row_type == row_volume))
        return false;

    if ( typeName == wxGRID_VALUE_STRING )
    {
        if (option_type == coString)
            return true;
        else
            return false;
    }
    else if ( typeName == wxGRID_VALUE_BOOL )
    {
        if (option_type == coBool)
            return true;
        else
            return false;
    }
    else if ( typeName == wxGRID_VALUE_NUMBER )
    {
        if (option_type == coInt)
            return true;
        else
            return false;
    }
    else if ( typeName == wxGRID_VALUE_FLOAT  )
    {
        if ((option_type == coFloat) || (option_type == coPercent))
            return true;
        else
            return false;
    }
    else if ( typeName == wxGRID_VALUE_CHOICE )
    {
        if (option_type == coEnum)
            return true;
        else
            return false;
    }
    else
    {
        return false;
    }
}

bool ObjectGridTable::CanSetValueAs( int row, int col, const wxString& typeName )
{
    return CanGetValueAs(row, col, typeName);
}

wxString ObjectGridTable::GetValue (int row, int col)
{
    if (!m_data_valid)
        return wxString();

    //row 0 always use string type for label
    if (row == 0) {
        switch ((GridColType)col)
        {
            case col_plate_index:
                return "Plate Index";
            case col_assemble_name:
                return "Module";
            case col_name:
                return "Name";
            case col_filaments:
                return "Filaments";
            case col_layer_height:
                return "Layer height";
            case col_perimeters:
                return "Perimeter";
            case col_fill_density:
                return "Infill density(%)";
            case col_support_material:
                return "Support enable";
            case col_brim_type:
                return "Brim type";
            case col_speed_perimeter:
                return "Perimeter speed";
            default:
                return wxString();
        }
    }

    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ObjectGridCol* grid_col = m_col_data[(GridColType)col];
    if (grid_col->b_for_object && (grid_row->row_type == row_volume))
        return wxString();

    ConfigOption& option = (*grid_row)[(GridColType)col];
    if (grid_col->type == coEnum) {
        if (col == col_brim_type) {
            ConfigOptionEnum<BrimType>& option_value = dynamic_cast<ConfigOptionEnum<BrimType>&>(option);
            if (option_value.value < grid_col->choice_count)
               return grid_col->choices[option_value.value];
        }
        else if (col == col_filaments) {
            ConfigOptionInt& option_value = dynamic_cast<ConfigOptionInt&>(option);
            if ((option_value.value - 1) < grid_col->choice_count) {
               //return grid_col->choices[(option_value.value > 0)?option_value.value - 1: 0];
                int index = (option_value.value > 0) ? option_value.value - 1 : 0;
               return convert_filament_string(index, grid_col->choices[index]);
            }
        }
    }
    else if (grid_col->type == coBool) {
        ConfigOptionBool& option_value = dynamic_cast<ConfigOptionBool&>(option);
        return option_value.value?"1":"0";
    }
    else if (grid_col->type == coInt) {
        ConfigOptionInt& option_value = dynamic_cast<ConfigOptionInt&>(option);
        return wxString::Format("%d", option_value.value);
    }
    else if (grid_col->type == coFloat) {
        ConfigOptionFloat& option_value = dynamic_cast<ConfigOptionFloat&>(option);
        return wxString::Format("%.2f", option_value.value);
    }
    else if (grid_col->type == coPercent) {
        ConfigOptionPercent& option_value = dynamic_cast<ConfigOptionPercent&>(option);
        return wxString::Format("%.2f", option_value.value);
    }
    else {
        try {
            ConfigOptionString& option_value = dynamic_cast<ConfigOptionString&>(option);
            if (grid_row->row_type == row_volume)
                return GUI::from_u8(std::string("  ") + option_value.value);
            else
                return GUI::from_u8(option_value.value);
        }
        catch(...) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("row %1%, col %2%, type %3% ")%row %col %grid_col->type;
            return wxString();
        }
    }

    return wxString();
}

void ObjectGridTable::update_value_to_config(ModelConfig* config, std::string& key, ConfigOption& new_value,  ConfigOption& ori_value)
{
    if (!config->has(key))
    {
        if (ori_value != new_value)
            config->set_key_value(key, new_value.clone());
    }
    else {
        if (ori_value != new_value)
            config->set_key_value(key, new_value.clone());
        else
            config->erase(key);
    }
    config->touch();
}

void ObjectGridTable::update_value_to_object(Model* model, ObjectGridRow* grid_row, int col)
{
    ModelObject* object = model->objects[grid_row->object_id];
    ModelVolume* volume = nullptr;
    std::string* name_ptr = nullptr;
    std::string name_value;

    if (!object) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", object_id %1%, volume_id %2%, can not find modelObject anymore!")%grid_row->object_id %grid_row->volume_id;
        return;
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", object_id %1%, volume_id %2%, col %3%, row_type %4%")%grid_row->object_id %grid_row->volume_id %col %grid_row->row_type;

    if (grid_row->row_type != row_object) {
        volume = object->volumes[grid_row->volume_id];
        if (col == col_name) {
            name_ptr = &(volume->name);
            name_value = grid_row->name.value.erase(0, 2);
        }
    }
    else {
        if (col == col_name) {
            name_ptr = &(object->name);
            name_value = grid_row->name.value;
        }
        else if (col == col_assemble_name) {
            name_ptr = &(object->module_name);
            name_value = grid_row->assemble_name.value;
        }
    }

    if ((name_ptr) && (*name_ptr != name_value)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", change name from %1% to %2%!")%name_ptr->c_str() %name_value.c_str();
        *name_ptr = name_value;
        //todo: notify object list
    }
}

void ObjectGridTable::SetValue( int row, int col, const wxString& value )
{
    if (row == 0)
        return;
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ObjectGridCol* grid_col = m_col_data[col];
    if (grid_col->type == coEnum) {
        int enum_value = 0;
        for (int i = 0; i < grid_col->choice_count; i++)
        {
            if (grid_col->choices[i] == value) {
                enum_value = i;
                break;
            }
        }
        if (col == col_brim_type) {
            ConfigOptionEnum<BrimType>& option_value = dynamic_cast<ConfigOptionEnum<BrimType>&>((*grid_row)[(GridColType)col]);
            ConfigOptionEnum<BrimType>& option_ori_value = dynamic_cast<ConfigOptionEnum<BrimType>&>((*grid_row)[(GridColType)(col + 1)]);

            option_value.value = (BrimType)enum_value;
            update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
        }
        else if (col == col_filaments) {
            ConfigOptionInt& option_value = dynamic_cast<ConfigOptionInt&>((*grid_row)[(GridColType)col]);
            ConfigOptionInt& option_ori_value = dynamic_cast<ConfigOptionInt&>((*grid_row)[(GridColType)(col + 1)]);

            option_value.value = enum_value + 1;
            update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
        }
    }
    else {
        if (grid_col->b_from_config) {
            ConfigOptionString& option_value = dynamic_cast<ConfigOptionString&>((*grid_row)[(GridColType)col]);
            ConfigOptionString& option_ori_value = dynamic_cast<ConfigOptionString&>((*grid_row)[(GridColType)(col + 1)]);

            option_value.value = into_u8(value);
            update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
        }
        else {
            ConfigOptionString& option_value = dynamic_cast<ConfigOptionString&>((*grid_row)[(GridColType)col]);

            if (grid_row->row_type == row_volume) {
                //std::string new_value = value.ToStdString();
                std::string new_value = into_u8(value);
                size_t pos = new_value.find_first_not_of(' ');
                if (pos > 0)
                    new_value.erase(0, pos);
                option_value.value = new_value;
            }
            else
                option_value.value = into_u8(value);
            update_value_to_object(m_panel->m_model, grid_row, col);
        }
    }
}

long ObjectGridTable::GetValueAsLong( int row, int col )
{
    if (!m_data_valid)
        return 0;

    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ConfigOptionInt &option_value = dynamic_cast<ConfigOptionInt &>((*grid_row)[(GridColType)col]);
    return (long)option_value.getInt();
}

bool ObjectGridTable::GetValueAsBool( int row, int col )
{
    if (!m_data_valid)
        return 0;

    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ConfigOptionBool &option_value = dynamic_cast<ConfigOptionBool &>((*grid_row)[(GridColType)col]);
    return option_value.getBool();
}

double ObjectGridTable::GetValueAsDouble( int row, int col )
{
    if (!m_data_valid)
        return 0;

    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ConfigOptionFloat &option_value = dynamic_cast<ConfigOptionFloat &>((*grid_row)[(GridColType)col]);
    return (double )option_value.getFloat();
}

void ObjectGridTable::SetValueAsLong( int row, int col, long value )
{
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ObjectGridCol* grid_col = m_col_data[col];
    ConfigOptionInt &option_value = dynamic_cast<ConfigOptionInt &>((*grid_row)[(GridColType)col]);
    ConfigOptionInt &option_ori_value = dynamic_cast<ConfigOptionInt &>((*grid_row)[(GridColType)(col+1)]);

    option_value.value = (int) value;
    update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);

    return;
}

void ObjectGridTable::SetValueAsBool( int row, int col, bool value )
{
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ObjectGridCol* grid_col = m_col_data[col];
    ConfigOptionBool &option_value = dynamic_cast<ConfigOptionBool &>((*grid_row)[(GridColType)col]);
    ConfigOptionBool &option_ori_value = dynamic_cast<ConfigOptionBool &>((*grid_row)[(GridColType)(col+1)]);

    option_value.value = (int) value;

    update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
    m_panel->m_object_grid->ForceRefresh();

    return;
}

void ObjectGridTable::SetValueAsDouble(int row, int col, double value)
{
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ObjectGridCol* grid_col = m_col_data[col];

    if (grid_col->type == coPercent) {
        if ((value > 100.f) || (value < 0.f))
            return;
    }
    ConfigOptionFloat &option_value = dynamic_cast<ConfigOptionFloat &>((*grid_row)[(GridColType)col]);
    ConfigOptionFloat &option_ori_value = dynamic_cast<ConfigOptionFloat &>((*grid_row)[(GridColType)(col+1)]);

    option_value.value = (float)value;

    update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);

    return;
}

void ObjectGridTable::release_object_configs()
{
    if (m_grid_data.size() > 0)
    {
        for (int i = 0; i < m_grid_data.size(); i ++)
        {
            delete m_grid_data[i];
        }
        m_grid_data.clear();
    }

    if (m_col_data.size() > 0)
    {
        for (int i = 0; i < m_col_data.size(); i ++)
        {
            delete m_col_data[i];
        }
        m_col_data.clear();
    }

    m_data_valid = false;

    return;
}

//convert the filament str to short and readable
wxString ObjectGridTable::convert_filament_string(int index, wxString& filament_str)
{
    wxString result_str;
    if (filament_str.find("PLA") !=  wxNOT_FOUND ) {
        //PLA
        result_str = wxString(std::to_string(index+1) + ": PLA");
    }
    else if (filament_str.find("ABS") != wxNOT_FOUND ) {
        //ABS
        result_str = wxString(std::to_string(index+1) + ": ABS");
    }
    else if (filament_str.find("PETG") != wxNOT_FOUND ) {
        //PETG
        result_str= wxString(std::to_string(index+1) + ": PETG");
    }
    else if (filament_str.find("TPU") != wxNOT_FOUND ) {
        //TPU
        result_str = wxString(std::to_string(index+1) + ": TPU");
    }
    else
        result_str = filament_str;

    return result_str;
}

static wxString brim_choices[] =
{
    "Auto brim",
    "No brim",
    "Outer brim only",
    "Inner brim only",
    "Outer and inner brim"
};

void ObjectGridTable::init_cols()
{
    const float font_size = 1.5f * wxGetApp().em_unit();

    //first column for plate_index
    ObjectGridCol* col = new ObjectGridCol(coString, "plate_index", L(" "), true, false, false, false, wxALIGN_RIGHT); //bool only_object, bool icon, bool edit, bool config
    m_col_data.push_back(col);

    //second column for module name
    col = new ObjectGridCol(coString, "assemble_name", L(" "), true, false, true, false, wxALIGN_RIGHT);
    m_col_data.push_back(col);

    //preview icon for object/volume
    col = new ObjectGridCol(coString, "assemble_name_reset", L(" "), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //3th column: for object/volume name
    col = new ObjectGridCol(coString, "name", L(" "), false, false, true, false, wxALIGN_RIGHT);
    m_col_data.push_back(col);

    //reset icon for name
    col = new ObjectGridCol(coString, "name_reset", L(" "), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //object/volume extruder_id
    col = new ObjectGridCol(coEnum, "extruder", L(" "), false, false, true, true, wxALIGN_LEFT);
    //the spec now guarantees vectors store their elements contiguously
    col->choices = &m_panel->m_filaments_name[0];
    col->choice_count = m_panel->m_filaments_count;
    m_col_data.push_back(col);

    //reset icon for extruder_id
    col = new ObjectGridCol(coEnum, "extruder_reset", L(" "), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //object layer height
    col = new ObjectGridCol(coFloat, "layer_height", L("Quality"), true, false, true, true, wxALIGN_RIGHT);
    m_col_data.push_back(col);

    //reset icon for extruder_id
    col = new ObjectGridCol(coFloat, "layer_height_reset", L("Quality"), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //object/volume perimeters
    col = new ObjectGridCol(coInt, "perimeters", L("Shell"), false, false, true, true, wxALIGN_RIGHT);
    m_col_data.push_back(col);

    //reset icon for perimeters
    col = new ObjectGridCol(coInt, "perimeters_reset", L("Shell"), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //object/volume fill density
    col = new ObjectGridCol(coPercent, "fill_density", L("Infill"), false, false, true, true, wxALIGN_RIGHT);
    m_col_data.push_back(col);

    //reset icon for fill density
    col = new ObjectGridCol(coPercent, "fill_density_reset", L("Infill"), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //support material
    col = new ObjectGridCol(coBool, "support_material", L("Support material"), true, false, true, true, wxALIGN_RIGHT);
    m_col_data.push_back(col);

    //reset icon for support material
    col = new ObjectGridCol(coBool, "support_material_reset", L("Support material"), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //Bed Adhesion
    col = new ObjectGridCol(coEnum, "brim_type", L("Bed adhension"), true, false, true, true, wxALIGN_RIGHT);
    col->size = font_size*20; //20 char for the longest selection
    col->choices = brim_choices;
    col->choice_count = WXSIZEOF(brim_choices);
    m_col_data.push_back(col);

    //reset icon for Bed Adhesion
    col = new ObjectGridCol(coEnum, "brim_type_reset", L("Bed adhension"), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //object/volume speed
    col = new ObjectGridCol(coFloat, "perimeter_speed", L("Speed"), false, false, true, true, wxALIGN_RIGHT);
    m_col_data.push_back(col);

    //reset icon for speed
    col = new ObjectGridCol(coFloat, "perimeter_speed_reset", L("Speed"), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    return;
}

void ObjectGridTable::construct_object_configs ()
{
    //release first
    release_object_configs();

    //init cols
    init_cols();

    if (!m_panel->m_model)
    {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "found invalid m_model, should not happen" << std::endl;
        return;
    }
    int object_count = m_panel->m_model->objects.size();
    PartPlateList& partplate_list = m_panel->m_plater->get_partplate_list();
    DynamicPrintConfig&  global_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const DynamicPrintConfig* plater_config = m_panel->m_plater->config();
    const DynamicPrintConfig&  filament_config = *plater_config;

    for (int i = 0; i < object_count; i++)
    {
        ModelObject* object = m_panel->m_model->objects[i];
        ObjectGridRow* object_grid = new ObjectGridRow(i, 0, row_object);
        int plate_index;

        object_grid->config = &(object->config);
        object_grid->name.value = object->name;
        object_grid->ori_name = object_grid->name;
        plate_index = partplate_list.find_instance_belongs(i, 0);
        if (plate_index == -1)
            object_grid->plate_index.value = std::string("Outside");
        else
            object_grid->plate_index.value = std::string("Plate ") + std::to_string(plate_index+1);
        object_grid->assemble_name.value = object->module_name;
        object_grid->ori_assemble_name = object_grid->assemble_name;
        auto extruder_id_ptr = get_object_config_value<ConfigOptionInt>(filament_config, object_grid->config, m_col_data[col_filaments]->key);
        if (extruder_id_ptr)
            object_grid->filaments = *extruder_id_ptr;
        else
            object_grid->filaments.value = 0;
        extruder_id_ptr = filament_config.option<ConfigOptionInt>(m_col_data[col_filaments]->key);
        if (extruder_id_ptr)
            object_grid->ori_filaments = *extruder_id_ptr;
        else
            object_grid->ori_filaments.value = 0;
        object_grid->layer_height = *(get_object_config_value<ConfigOptionFloat>(global_config, object_grid->config, m_col_data[col_layer_height]->key));
        object_grid->ori_layer_height = *(global_config.option<ConfigOptionFloat>(m_col_data[col_layer_height]->key));
        object_grid->perimeters = *(get_object_config_value<ConfigOptionInt>(global_config, object_grid->config, m_col_data[col_perimeters]->key));
        object_grid->ori_perimeters = *(global_config.option<ConfigOptionInt>(m_col_data[col_perimeters]->key));
        object_grid->fill_density = *(get_object_config_value<ConfigOptionPercent>(global_config, object_grid->config, m_col_data[col_fill_density]->key));
        object_grid->ori_fill_density = *(global_config.option<ConfigOptionPercent>(m_col_data[col_fill_density]->key));
        object_grid->support_material = *(get_object_config_value<ConfigOptionBool>(global_config, object_grid->config, m_col_data[col_support_material]->key));
        object_grid->ori_support_material = *(global_config.option<ConfigOptionBool>(m_col_data[col_support_material]->key));
        object_grid->brim_type = *(get_object_config_value<ConfigOptionEnum<BrimType>>(global_config, object_grid->config, m_col_data[col_brim_type]->key));
        object_grid->ori_brim_type = *(global_config.option<ConfigOptionEnum<BrimType>>(m_col_data[col_brim_type]->key));
        object_grid->speed_perimeter = *(get_object_config_value<ConfigOptionFloat>(global_config, object_grid->config, m_col_data[col_speed_perimeter]->key));
        object_grid->ori_speed_perimeter = *(global_config.option<ConfigOptionFloat>(m_col_data[col_speed_perimeter]->key));
        m_grid_data.push_back(object_grid);

        int volume_count = object->volumes.size();
        if (volume_count <= 1)
            continue;

        for (int j = 0; j < volume_count; j++)
        {
            ModelVolume* volume = object->volumes[j];
            ObjectGridRow* volume_grid = new ObjectGridRow(i, j, row_volume);
            volume_grid->config = &(volume->config);
            volume_grid->name.value = volume->name;
            size_t pos = volume_grid->name.value.find_first_not_of(' ');
            if (pos > 0)
                volume_grid->name.value.erase(0, pos);
            volume_grid->ori_name = volume_grid->name;
            plate_index = partplate_list.find_instance_belongs(i, 0);
            if (plate_index == -1)
                volume_grid->plate_index.value = std::string("Outside");
            else
                volume_grid->plate_index.value = std::string("Plate ") + std::to_string(plate_index+1);
            volume_grid->assemble_name.value = object->module_name;
            volume_grid->ori_assemble_name = volume_grid->assemble_name;
            auto extruder_id_ptr = get_volume_config_value<ConfigOptionInt>(filament_config, object_grid->config, volume_grid->config, m_col_data[col_filaments]->key);
            if (extruder_id_ptr)
                volume_grid->filaments = *extruder_id_ptr;
            else
                volume_grid->filaments.value = 0;
            volume_grid->ori_filaments = object_grid->filaments;
            volume_grid->layer_height = *(get_volume_config_value<ConfigOptionFloat>(global_config, object_grid->config, volume_grid->config, m_col_data[col_layer_height]->key));
            volume_grid->ori_layer_height = object_grid->layer_height;
            volume_grid->perimeters = *(get_volume_config_value<ConfigOptionInt>(global_config, object_grid->config, volume_grid->config, m_col_data[col_perimeters]->key));
            volume_grid->ori_perimeters = object_grid->perimeters;
            volume_grid->fill_density = *(get_volume_config_value<ConfigOptionPercent>(global_config, object_grid->config, volume_grid->config, m_col_data[col_fill_density]->key));
            volume_grid->ori_fill_density = object_grid->fill_density;
            volume_grid->support_material = *(get_volume_config_value<ConfigOptionBool>(global_config, object_grid->config, volume_grid->config, m_col_data[col_support_material]->key));
            volume_grid->ori_support_material = object_grid->support_material;
            volume_grid->brim_type = *(get_volume_config_value<ConfigOptionEnum<BrimType>>(global_config, object_grid->config, volume_grid->config, m_col_data[col_brim_type]->key));
            volume_grid->ori_brim_type = object_grid->brim_type;
            volume_grid->speed_perimeter = *(get_volume_config_value<ConfigOptionFloat>(global_config, object_grid->config, volume_grid->config, m_col_data[col_speed_perimeter]->key));
            volume_grid->ori_speed_perimeter = object_grid->speed_perimeter;
            m_grid_data.push_back(volume_grid);
        }
    }

    m_data_valid = true;

    return;
}

void ObjectGridTable::SetSelection(int object_id, int volume_id)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", set selection to object %1% part %2%") %object_id %volume_id;
    //invalid object, skip
    if ((object_id == -1)&&(volume_id == -1))
        return;

    for (int index = 0; index <  m_grid_data.size(); index++)
    {
        ObjectGridRow* row = m_grid_data[index];
        if (row->object_id == object_id) {
            if ((volume_id == -1) || (volume_id == row->volume_id)) {
                m_panel->m_object_grid->SelectRow(index+1);
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", found row %1%") %index;
                break;
            }
        }
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", can not find row");
}

void ObjectGridTable::reload_object_data(ObjectGridRow* grid_row, const std::string& category, DynamicPrintConfig&  global_config)
{
    if (category == L("Quality")) {
        grid_row->layer_height = *(get_object_config_value<ConfigOptionFloat>(global_config, grid_row->config, m_col_data[col_layer_height]->key));
        grid_row->ori_layer_height = *(global_config.option<ConfigOptionFloat>(m_col_data[col_layer_height]->key));
    }
    else if (category == L("Shell")) {
        grid_row->perimeters = *(get_object_config_value<ConfigOptionInt>(global_config, grid_row->config, m_col_data[col_perimeters]->key));
        grid_row->ori_perimeters = *(global_config.option<ConfigOptionInt>(m_col_data[col_perimeters]->key));
    }
    else if (category == L("Infill")) {
        grid_row->fill_density = *(get_object_config_value<ConfigOptionPercent>(global_config, grid_row->config, m_col_data[col_fill_density]->key));
        grid_row->ori_fill_density = *(global_config.option<ConfigOptionPercent>(m_col_data[col_fill_density]->key));
    }
    else if (category == L("Support material")) {
        grid_row->support_material = *(get_object_config_value<ConfigOptionBool>(global_config, grid_row->config, m_col_data[col_support_material]->key));
        grid_row->ori_support_material = *(global_config.option<ConfigOptionBool>(m_col_data[col_support_material]->key));
    }
    else if (category == L("Bed adhension")) {
        grid_row->brim_type = *(get_object_config_value<ConfigOptionEnum<BrimType>>(global_config, grid_row->config, m_col_data[col_brim_type]->key));
        grid_row->ori_brim_type = *(global_config.option<ConfigOptionEnum<BrimType>>(m_col_data[col_brim_type]->key));
    }
    else if (category == L("Speed")) {
        grid_row->speed_perimeter = *(get_object_config_value<ConfigOptionFloat>(global_config, grid_row->config, m_col_data[col_speed_perimeter]->key));
        grid_row->ori_speed_perimeter = *(global_config.option<ConfigOptionFloat>(m_col_data[col_speed_perimeter]->key));
    }
}

void ObjectGridTable::reload_part_data(ObjectGridRow* volume_row, ObjectGridRow* object_row, const std::string& category, DynamicPrintConfig&  global_config)
{
    if (category == L("Quality")) {
         volume_row->layer_height = *(get_volume_config_value<ConfigOptionFloat>(global_config, object_row->config, volume_row->config, m_col_data[col_layer_height]->key));
         volume_row->ori_layer_height = object_row->layer_height;
    }
    else if (category == L("Shell")) {
        volume_row->perimeters = *(get_volume_config_value<ConfigOptionInt>(global_config, object_row->config, volume_row->config, m_col_data[col_perimeters]->key));
        volume_row->ori_perimeters = object_row->perimeters;
    }
    else if (category == L("Infill")) {
        volume_row->fill_density = *(get_volume_config_value<ConfigOptionPercent>(global_config, object_row->config, volume_row->config, m_col_data[col_fill_density]->key));
        volume_row->ori_fill_density = object_row->fill_density;
    }
    else if (category == L("Support material")) {
        volume_row->support_material = *(get_volume_config_value<ConfigOptionBool>(global_config, object_row->config, volume_row->config, m_col_data[col_support_material]->key));
        volume_row->ori_support_material = object_row->support_material;
    }
    else if (category == L("Bed adhension")) {
        volume_row->brim_type = *(get_volume_config_value<ConfigOptionEnum<BrimType>>(global_config, object_row->config, volume_row->config, m_col_data[col_brim_type]->key));
        volume_row->ori_brim_type = object_row->brim_type;
    }
    else if (category == L("Speed")) {
        volume_row->speed_perimeter = *(get_volume_config_value<ConfigOptionFloat>(global_config, object_row->config, volume_row->config, m_col_data[col_speed_perimeter]->key));
        volume_row->ori_speed_perimeter = object_row->speed_perimeter;
    }
}

void ObjectGridTable::reload_cell_data(int row, const std::string& category)
{
    if (row == 0)
        return;
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    DynamicPrintConfig&  global_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;

    if (grid_row->row_type == row_object) {
        reload_object_data(grid_row, category, global_config);

        int next_row = row + 1;
        while ((next_row - 1) < m_grid_data.size())
        {
            ObjectGridRow* part_row = m_grid_data[next_row - 1];
            if (part_row->row_type == row_volume) {
                reload_part_data(part_row, grid_row, category, global_config);
                next_row++;
            }
            else
                break;
        }
    }
    else {
        int next_row = row - 1;
        ObjectGridRow* object_row = m_grid_data[next_row - 1];
        while (object_row->row_type == row_volume)
        {
            next_row --;
            object_row = m_grid_data[next_row - 1];
        }
        reload_part_data(grid_row, object_row, category, global_config);
    }
    m_panel->m_object_grid->ForceRefresh();

    ObjectVolumeID object_volume_id;
    object_volume_id.object = m_panel->m_model->objects[grid_row->object_id];
    object_volume_id.volume = (grid_row->row_type == row_object)?nullptr:object_volume_id.object->volumes[grid_row->volume_id];
    wxGetApp().obj_list()->object_config_options_changed(object_volume_id);
}

void ObjectGridTable::update_row_properties()
{
    ObjectGrid* grid_table = m_panel->m_object_grid;
    //col 0 no need to update, always uneditable
    for (int col = 1; col < col_speed_perimeter_reset; col++)
    {
        ObjectGridTable::ObjectGridCol* grid_col = get_grid_col(col);
        grid_table->SetColSize(col, grid_col->size);

        //row 0 no need to update, always for headers
        for (int row = 1; row < get_row_count(); row++)
        {
            ObjectGridTable::ObjectGridRow* grid_row = get_grid_row(row-1);

            if ((!grid_col->b_icon) && (grid_col->b_for_object)) {
                if (grid_row->row_type == ObjectGridTable::row_volume) {
                    grid_table->SetReadOnly(row, col);
                    //FIXME: recycle the old editor and renders
                    grid_table->SetCellEditor(row, col, new wxGridCellAutoWrapStringEditor());
                    grid_table->SetCellRenderer(row, col, new wxGridCellAutoWrapStringRenderer());
                }
                else {
                    grid_table->SetReadOnly(row, col, false);
                    switch (grid_col->type)
                    {
                        case coString:
                            grid_table->SetCellEditor(row, col, new wxGridCellAutoWrapStringEditor());
                            grid_table->SetCellRenderer(row, col, new wxGridCellAutoWrapStringRenderer());
                            break;
                        case coBool:
                            grid_table->SetCellEditor(row, col, new wxGridCellBoolEditor());
                            grid_table->SetCellRenderer(row, col, new GridCellSupportRenderer());
                            //grid_table->SetCellRenderer(row, col, new wxGridCellBoolRenderer());
                            break;
                        case coInt:
                            grid_table->SetCellEditor(row, col, new wxGridCellNumberEditor());
                            grid_table->SetCellRenderer(row, col, new  wxGridCellNumberRenderer());
                            break;
                        case coEnum:
                            if (col == ObjectGridTable::col_filaments) {
                                GridCellFilamentsEditor *filament_editor = new GridCellFilamentsEditor(grid_col->choice_count, grid_col->choices, false, &m_panel->m_color_bitmaps);
                                grid_table->SetCellEditor(row, col, filament_editor);
                                grid_table->SetCellRenderer(row, col, new GridCellFilamentsRenderer());
                            }
                            else
                                grid_table->SetCellEditor(row, col, new wxGridCellChoiceEditor(grid_col->choice_count, grid_col->choices));
                            break;
                        case coFloat:
                            grid_table->SetCellEditor(row, col, new wxGridCellFloatEditor(6,2));
                            grid_table->SetCellRenderer(row, col, new wxGridCellFloatRenderer(6,2));
                            break;
                        case coPercent:
                        {
                            /*wxGridCellFloatEditor *float_editor = new wxGridCellFloatEditor(6,2);
                            wxFloatingPointValidator<float> *float_validator = new wxFloatingPointValidator<float>(3, nullptr, wxNUM_VAL_ZERO_AS_BLANK);
                            float_validator->SetRange(0.f, 100.f);
                            float_editor->SetValidator(*float_validator);

                            if (rows < 3)
                                m_object_grid->SetCellEditor(row, col, float_editor);
                            else*/
                                grid_table->SetCellEditor(row, col, new wxGridCellFloatEditor(6,2));
                            grid_table->SetCellRenderer(row, col, new wxGridCellFloatRenderer(6,2));
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }
    }

    return;
}

void ObjectGridTable::sort_by_default()
{
    compare_row_func sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
        std::string row1_plate = row1->plate_index.value;
        std::string row2_plate = row2->plate_index.value;
        //set Outside as the same start char 'P'
        row1_plate[0] = 'P';
        row2_plate[0] = 'P';
        return (row1_plate < row2_plate);
    };
    sort_row_data(sort_func);
}

void ObjectGridTable::sort_row_data(compare_row_func sort_func)
{
    int size = m_grid_data.size();
    if (!size)
        return;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" enter, this %1%, row_data size %2%") %this % m_grid_data.size();
    std::list<ObjectGridRow*> new_grid_rows;
    for ( auto it = m_grid_data.begin(); it != m_grid_data.end(); it++ )
    {
        if ((*it)->row_type == row_object)
           new_grid_rows.push_back(*it);
    }
    new_grid_rows.sort(sort_func);
    //std::sort(new_grid_rows.begin(), new_grid_rows.end(), sort_func);
    auto it = new_grid_rows.begin();
    while( it != new_grid_rows.end() )
    {
        if ((*it)->row_type != row_object) {
            ++it;
            continue;
        }
        auto origin_it = find(m_grid_data.begin(), m_grid_data.end(), *it);
        //move it to next for insert
        ++it;
        if (origin_it == m_grid_data.end()) //should not happen, finished
            break;
        ++origin_it;
        while (origin_it != m_grid_data.end() && ((*origin_it)->row_type != row_object))
        {
            new_grid_rows.insert(it, *origin_it);
            ++origin_it;
        }
    }
    m_grid_data.clear();
    m_grid_data.resize(size);
    std::copy(new_grid_rows.begin(), new_grid_rows.end(), m_grid_data.begin());
    new_grid_rows.clear();

    update_row_properties();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" finished, this %1%, row_data size %2%") %this % m_grid_data.size();
}

void ObjectGridTable::OnCellLeftClick(int row, int col)
{
    if (row == 0) {
        //handle the sort logic
        if (col == col_name) {
            auto sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
                return (row1->name.value.compare(row2->name.value) < 0);
            };
            //sort_by_name();
            sort_row_data(sort_func);
        }
        else if (col == col_assemble_name) {
            compare_row_func sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
                //wxString string1 = GUI::from_u8(row1->assemble_name.value);
                //wxString string2 = GUI::from_u8(row2->assemble_name.value);
                //return (string1.compare(string2) <= 0);
                return (row1->assemble_name.value.compare(row2->assemble_name.value) < 0);
            };
            sort_row_data(sort_func);
        }
        else if (col == col_plate_index) {
            compare_row_func sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
                std::string row1_plate = row1->plate_index.value;
                std::string row2_plate = row2->plate_index.value;
                //set Outside as the same start char 'P'
                row1_plate[0] = 'P';
                row2_plate[0] = 'P';
                return (row1_plate < row2_plate);
            };
            sort_row_data(sort_func);
        }
    }
    else if (col >= col_assemble_name) {
        ObjectGridRow* grid_row = m_grid_data[row - 1];
        ObjectGridCol* grid_col = m_col_data[col];
        ObjectGridCol* grid_col_2 = m_col_data[col - 1];

        if (grid_col->b_icon) {
            ConfigOption& orig_option = (*grid_row)[(GridColType)col];
            ConfigOption& cur_option = (*grid_row)[(GridColType)(col-1)];
            //reset the value to original one
            if (cur_option != orig_option) {
                cur_option.set(&orig_option);
                update_value_to_config(grid_row->config, grid_col_2->key, cur_option, orig_option);
                m_panel->m_object_grid->ForceRefresh();

                ObjectVolumeID object_volume_id;
                object_volume_id.object = m_panel->m_model->objects[grid_row->object_id];
                object_volume_id.volume = (grid_row->row_type == row_object)?nullptr:object_volume_id.object->volumes[grid_row->volume_id];
                wxGetApp().obj_list()->object_config_options_changed(object_volume_id);
            }
        }
    }
}

void ObjectGridTable::OnSelectCell(int row, int col)
{
    m_selected_cells.clear();
    m_panel->m_side_window->Freeze();
    if (row == 0) {
        m_panel->m_object_settings->UpdateAndShow(row, false, false, false, nullptr, nullptr, std::string());
    }
    else {
        ObjectGridRow* grid_row = m_grid_data[row - 1];
        ObjectGridCol* grid_col = m_col_data[col];
        bool is_object = (grid_row->row_type == row_object);
        ModelObject* object = m_panel->m_model->objects[grid_row->object_id];

        m_panel->m_object_settings->get_og()->set_name(GUI::from_u8(grid_row->name.value));
        m_panel->m_page_text->SetLabel(GUI::from_u8(grid_row->name.value));
        m_panel->m_object_settings->UpdateAndShow(row, true, is_object, false, object, grid_row->config, grid_col->category);

        wxGridCellCoordsArray cell_array = m_panel->m_object_grid->GetSelectedCells();
        std::vector<ObjectVolumeID> object_volume_ids;
        int count = cell_array.size();
        if (count == 0) {
            ObjectVolumeID object_volume_id;
            object_volume_id.object = object;
            object_volume_id.volume = (grid_row->row_type == row_object)?nullptr:object->volumes[grid_row->volume_id];
            object_volume_ids.push_back(object_volume_id);

            wxGridCellCoords cell_coord(row, col);
            m_selected_cells.push_back(cell_coord);
        }
        else {
            while (count > 0) {
                int cel_row = cell_array[count-1].GetRow();
                int cel_col = cell_array[count-1].GetCol();
                ObjectGridRow* grid_data = m_grid_data[cel_row - 1];
                object = m_panel->m_model->objects[grid_data->object_id];
                ObjectVolumeID object_volume_id;
                object_volume_id.object = object;
                object_volume_id.volume = (grid_data->row_type == row_object)?nullptr:object->volumes[grid_data->volume_id];
                object_volume_ids.push_back(object_volume_id);
                count --;

                wxGridCellCoords cell_coord(cel_row, cel_col);
                m_selected_cells.push_back(cell_coord);
            }
        }

        wxGetApp().obj_list()->select_items(object_volume_ids);
    }
    m_panel->m_side_window->Layout();
    m_panel->m_side_window->Thaw();
}

void ObjectGridTable::OnRangeSelected(int row, int col, int row_count, int col_count)
{
}

void ObjectGridTable::OnCellValueChanged(int row, int col)
{
    if (row == 0) {
        //skip it
    }
    else if (col <= (int)col_filaments_reset) {
        //skip it
    }
    else {
        ObjectGridRow* grid_row = m_grid_data[row - 1];
        ObjectGridCol* grid_col = m_col_data[col];
        bool is_object = (grid_row->row_type == row_object);
        ModelObject* object = m_panel->m_model->objects[grid_row->object_id];

        m_panel->m_side_window->Freeze();

        m_panel->m_object_settings->ValueChanged(row, is_object, object, grid_row->config, grid_col->category, grid_col->key);

        m_panel->m_side_window->Thaw();
        //update volume cell
        /*if (is_object) {
            int next_row = row + 1;
            DynamicPrintConfig&  global_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
            while ((next_row - 1) < m_grid_data.size())
            {
                ObjectGridRow* part_row = m_grid_data[next_row - 1];
                if (part_row->row_type == row_volume) {
                    reload_part_data(part_row, grid_row, grid_col->category, global_config);
                    next_row++;
                }
                else
                    break;
            }
        }*/
        ObjectVolumeID object_volume_id;
        object_volume_id.object = object;
        object_volume_id.volume = is_object?nullptr:object->volumes[grid_row->volume_id];
        wxGetApp().obj_list()->object_config_options_changed(object_volume_id);
    }
}

wxBitmap& ObjectGridTable::get_undo_bitmap(bool selected)
{
    return m_panel->m_undo_bitmap;
}

wxBitmap* ObjectGridTable::get_color_bitmap(int color_index)
{
    if (color_index < m_panel->m_color_bitmaps.size())
        return m_panel->m_color_bitmaps[color_index];
    else
        return m_panel->m_color_bitmaps[0];
}


wxIMPLEMENT_CLASS(ObjectTablePanel, wxPanel);
/* ObjectTabelPanel related class */
wxBEGIN_EVENT_TABLE( ObjectTablePanel, wxPanel )
    //EVT_GRID_LABEL_LEFT_CLICK( ObjectTablePanel::OnLabelLeftClick )
    EVT_GRID_CELL_LEFT_CLICK( ObjectTablePanel::OnCellLeftClick )
    //EVT_GRID_ROW_SIZE( ObjectTablePanel::OnRowSize )
    //EVT_GRID_COL_SIZE( ObjectTablePanel::OnColSize )
    //EVT_GRID_COL_AUTO_SIZE( ObjectTablePanel::OnColAutoSize )
    EVT_GRID_SELECT_CELL( ObjectTablePanel::OnSelectCell )
    //EVT_GRID_RANGE_SELECTING( ObjectTablePanel::OnRangeSelecting )
    EVT_GRID_RANGE_SELECTED( ObjectTablePanel::OnRangeSelected )
    //EVT_GRID_CELL_CHANGING( GridFrame::OnCellValueChanging )
    EVT_GRID_CELL_CHANGED( ObjectTablePanel::OnCellValueChanged )
    //EVT_GRID_CELL_BEGIN_DRAG( GridFrame::OnCellBeginDrag )
wxEND_EVENT_TABLE()


ObjectTablePanel::ObjectTablePanel( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name, Plater* platerObj, Model *modelObj )
                    : wxPanel( parent, id, pos, size, style, name ), m_model(modelObj), m_plater(platerObj), m_float_validator(2, nullptr, wxNUM_VAL_ZERO_AS_BLANK)
{
    //m_bg_colour = wxColour(0xfa, 0xfa, 0xfa);
    m_float_validator.SetRange(0, 100);
    m_bg_colour = wxColour(0xff, 0xff, 0xff);
    //m_hover_colour = wxColour(61, 70, 72);
    this->SetBackgroundColour(m_bg_colour);

    //m_search_line = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);

    init_bitmap();

    init_filaments_and_colors();

	m_top_sizer = new wxBoxSizer(wxHORIZONTAL);
    //m_top_sizer->Add(m_search_line, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 10);

	m_object_grid = new ObjectGrid(this, wxID_ANY);
    m_object_grid_table = new ObjectGridTable(this);
    this->load_data();
    //m_object_grid_table->SetAttrProvider(new MyGridCellAttrProvider);
    //m_object_grid->AssignTable(m_object_grid_table);

    //set sizers
	m_top_sizer->Add(m_object_grid, 0, wxEXPAND);

    m_side_window = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_page_sizer = new wxBoxSizer(wxVERTICAL);
    m_side_window->SetBackgroundColour(wxColour(0xff, 0xff, 0xff));
    m_side_window->SetSizer(m_page_sizer);
    m_side_window->SetScrollbars(1, 20, 1, 2);
    //m_side_window->SetSize(wxSize(128, 512));
    m_page_text = new wxStaticText(m_side_window, wxID_ANY, wxString("Per Object Setting"), wxDefaultPosition, wxSize(128, 32), wxALIGN_CENTRE_HORIZONTAL);
    m_page_text->SetFont(Label::Head_18);
    m_page_sizer->Add(m_page_text, 0, wxEXPAND | wxTOP | wxALIGN_CENTER_HORIZONTAL, 5);
    //create object settings
    m_object_settings = new ObjectTableSettings(m_side_window, m_object_grid_table);
    m_object_settings->Hide();
    m_page_sizer->Add(m_object_settings->get_sizer(), 0, wxEXPAND | wxTOP, 5);

    m_top_sizer->Add(m_side_window, 1, wxEXPAND);

    //wxBoxSizer * page_sizer = new wxBoxSizer(wxHORIZONTAL);

	this->SetSizer(m_top_sizer);
	this->Layout();
}

int ObjectTablePanel::init_bitmap()
{
    m_undo_bitmap = create_scaled_bitmap("undo", nullptr, 24, false, true);
    m_color_bitmaps = get_extruder_color_icons();
    return 0;
}

int ObjectTablePanel::init_filaments_and_colors()
{
    //DynamicPrintConfig&  global_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const DynamicPrintConfig* global_config = m_plater->config();
    const std::vector<std::string> filament_presets = wxGetApp().preset_bundle->filament_presets;
    m_filaments_count = filament_presets.size();
    if (m_filaments_count <= 0) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", can not get filaments, count: %1%, set to default") %m_filaments_count;
        set_default_filaments_and_colors();
        return -1;
    }

    const ConfigOptionStrings* extruders_opt = dynamic_cast<const ConfigOptionStrings*>(global_config->option("extruder_colour"));
    if (extruders_opt == nullptr) {
        set_default_filaments_and_colors();
        return -1;
    }
    m_filaments_colors.resize(m_filaments_count);
    m_filaments_name.resize(m_filaments_count);
    unsigned int color_count = extruders_opt->values.size();
    if (color_count != m_filaments_count) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", invalid color count:%1%, extruder count: %2%") %color_count %m_filaments_count;
    }

    unsigned int i = 0;
    unsigned char rgb[3];
    while (i < m_filaments_count) {
        const std::string& txt_color = global_config->opt_string("extruder_colour", i);
        if (i < color_count) {
            if (Slic3r::GUI::BitmapCache::parse_color(txt_color, rgb))
            {
                m_filaments_colors[i] = wxColour(rgb[0], rgb[1], rgb[2]);
            }
            else
            {
                m_filaments_colors[i] = *wxGREEN;
            }
        }
        else {
            m_filaments_colors[i] = *wxGREEN;
        }

        //parse the filaments
        m_filaments_name[i] = wxString(std::to_string(i+1) + ": " + filament_presets[i]);

        i++;
    }

    return 0;
}

void ObjectTablePanel::load_data()
{
    int rows, cols;

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", enter");
    m_object_grid_table->construct_object_configs();
    m_object_grid->AssignTable(m_object_grid_table);

    rows = m_object_grid_table->get_row_count();
    cols = m_object_grid_table->get_col_count();

    //construct tables
    //m_object_grid->CreateGrid(rows, cols, wxGridSelectCells);
    m_object_grid->HideColLabels();
    m_object_grid->HideRowLabels();
    m_object_grid->EnableGridLines (true);

    /*set the first row as label*/
    //format
    wxGridCellAttr *attr;
    attr = new wxGridCellAttr;
    attr->SetBackgroundColour(wxColour(191, 191, 255));
    attr->SetTextColour(*wxBLACK);
    attr->SetAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
    attr->SetReadOnly(true);
    m_object_grid->SetRowAttr (0, attr);
    //merges
    m_object_grid->SetCellSize(0, ObjectGridTable::col_assemble_name, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_name, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_filaments, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_layer_height, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_perimeters, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_fill_density, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_support_material, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_brim_type, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_speed_perimeter, 1, 2);

    //m_object_grid->SetSelectionForeground(wxColour(255, 0, 0));
    //m_object_grid->SetSelectionBackground(wxColour(0, 255, 0));
    //wxGridCellAutoWrapStringEditor* string_editor = new wxGridCellAutoWrapStringEditor();
    //wxGridCellBoolEditor* bool_editor = new wxGridCellBoolEditor();
    //wxGridCellFloatEditor* float_editor = new wxGridCellFloatEditor();
    //wxGridCellNumberEditor* number_editor = new wxGridCellNumberEditor();
    //wxGridCellChoiceEditor* choice_editor = new wxGridCellChoiceEditor();
    //wxGridCellEnumEditor* enum_editor = new wxGridCellEnumEditor();

    for (int col = 0; col < cols; col++)
    {
        ObjectGridTable::ObjectGridCol* grid_col = m_object_grid_table->get_grid_col(col);
        m_object_grid->SetColSize(col, grid_col->size);

        for (int row = 1; row < rows; row++)
        {
            ObjectGridTable::ObjectGridRow* grid_row = m_object_grid_table->get_grid_row(row-1);

            m_object_grid->SetCellAlignment(row, col, grid_col->horizontal_align, wxALIGN_CENTRE );
            m_object_grid->SetCellOverflow(row, col, false);
            m_object_grid->SetCellBackgroundColour (row, col, *wxLIGHT_GREY);
            //set the render and editor
            if (grid_col->b_icon) {
                m_object_grid->SetCellRenderer(row, col, new GridCellIconRenderer());
                m_object_grid->SetReadOnly(row, col);
            }
            else if (grid_col->b_for_object && (grid_row->row_type == ObjectGridTable::row_volume)) {
                m_object_grid->SetReadOnly(row, col);
                m_object_grid->SetCellEditor(row, col, new wxGridCellAutoWrapStringEditor());
                m_object_grid->SetCellRenderer(row, col, new wxGridCellAutoWrapStringRenderer());
            }
            else {
                if (!grid_col->b_editable)
                    m_object_grid->SetReadOnly(row, col);
                //set editor
                switch (grid_col->type)
                {
                    case coString:
                        m_object_grid->SetCellEditor(row, col, new wxGridCellAutoWrapStringEditor());
                        m_object_grid->SetCellRenderer(row, col, new wxGridCellAutoWrapStringRenderer());
                        break;
                    case coBool:
                        m_object_grid->SetCellEditor(row, col, new wxGridCellBoolEditor());
                        m_object_grid->SetCellRenderer(row, col, new GridCellSupportRenderer());
                        break;
                    case coInt:
                        m_object_grid->SetCellEditor(row, col, new wxGridCellNumberEditor());
                        m_object_grid->SetCellRenderer(row, col, new  wxGridCellNumberRenderer());
                        break;
                    case coEnum:
                        if (col == ObjectGridTable::col_filaments) {
                            GridCellFilamentsEditor *filament_editor = new GridCellFilamentsEditor(grid_col->choice_count, grid_col->choices, false, &m_color_bitmaps);
                            m_object_grid->SetCellEditor(row, col, filament_editor);
                            //m_object_grid->SetCellEditor(row, col, new wxGridCellChoiceEditor(grid_col->choice_count, grid_col->choices));
                            m_object_grid->SetCellRenderer(row, col, new GridCellFilamentsRenderer());
                        }
                        else
                            m_object_grid->SetCellEditor(row, col, new wxGridCellChoiceEditor(grid_col->choice_count, grid_col->choices));
                        break;
                    case coFloat:
                        m_object_grid->SetCellEditor(row, col, new wxGridCellFloatEditor(6,2));
                        m_object_grid->SetCellRenderer(row, col, new wxGridCellFloatRenderer(6,2));
                        break;
                    case coPercent:
                    {
                        /*wxGridCellFloatEditor *float_editor = new wxGridCellFloatEditor(6,2);
                        wxFloatingPointValidator<float> *float_validator = new wxFloatingPointValidator<float>(3, nullptr, wxNUM_VAL_ZERO_AS_BLANK);
                        float_validator->SetRange(0.f, 100.f);
                        float_editor->SetValidator(*float_validator);

                        if (rows < 3)
                            m_object_grid->SetCellEditor(row, col, float_editor);
                        else*/
                            m_object_grid->SetCellEditor(row, col, new wxGridCellFloatEditor(6,2));
                        m_object_grid->SetCellRenderer(row, col, new wxGridCellFloatRenderer(6,2));
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }
    m_object_grid->Fit();
    for (int i = 0; i < ObjectGridTable::col_max; i++)
    {
        ObjectGridTable::ObjectGridCol* grid_col = m_object_grid_table->get_grid_col(i);
        if (grid_col->b_icon) {
            int fit_size1 = m_object_grid->GetColSize(i);
            grid_col->size = 32;
            m_object_grid->SetColSize(i, grid_col->size);

            //adjust the left col
            int delta = grid_col->size - fit_size1;
            grid_col = m_object_grid_table->get_grid_col(i - 1);
            int fit_size2 = m_object_grid->GetColSize(i-1);
            grid_col->size = fit_size2 - delta;
            m_object_grid->SetColSize(i-1, grid_col->size);
        }
    }

    //set col size after fit
    ObjectGridTable::ObjectGridCol* grid_col = m_object_grid_table->get_grid_col(ObjectGridTable::col_brim_type);
    grid_col->size = m_object_grid->GetTextExtent(grid_col->choices[4]).x + 30;
    m_object_grid->SetColSize(ObjectGridTable::col_brim_type, grid_col->size);

    grid_col = m_object_grid_table->get_grid_col(ObjectGridTable::col_filaments);
    grid_col->size = 128;
    m_object_grid->SetColSize(ObjectGridTable::col_filaments, grid_col->size);
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", finished, got %1% rows, %2% cols") %m_object_grid_table->GetNumberRows() %m_object_grid_table->GetNumberCols() ;
}

void ObjectTablePanel::SetSelection(int object_id, int volume_id)
{
    m_object_grid_table->SetSelection(object_id, volume_id);
}

ObjectTablePanel::~ObjectTablePanel()
{
    //do it in the grid
    /*if (m_object_grid_table) {
        m_object_grid_table->release_object_configs();
        delete m_object_grid_table;
        m_object_grid_table = nullptr;
    }*/
    if (m_top_sizer)
        m_top_sizer->Clear(true);

    m_filaments_name.clear();
    m_filaments_colors.clear();
}

void ObjectTablePanel::OnCellLeftClick( wxGridEvent& ev )
{
    int row = ev.GetRow();
    int col = ev.GetCol();

    m_object_grid_table->OnCellLeftClick(row, col);

    ev.Skip();
}

void ObjectTablePanel::OnSelectCell( wxGridEvent& ev )
{
    int row = ev.GetRow();
    int col = ev.GetCol();

    if ((row == m_cur_row) && (col == m_cur_col))
    {
        //the same cel selected
        return;
    }

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format("cell (%1%, %2%) selected") %row %col;
    m_object_grid_table->OnSelectCell(row, col);

    m_cur_row = row;
    m_cur_col = col;

    ev.Skip();
}

void ObjectTablePanel::OnCellValueChanged( wxGridEvent& ev )
{
    int row = ev.GetRow();
    int col = ev.GetCol();

    m_object_grid_table->OnCellValueChanged(row, col);

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format("cell (%1%, %2%) changed from %3% to %4%") %row %col %ev.GetString() % m_object_grid->GetCellValue(row, col);

    ev.Skip();
}


void ObjectTablePanel::OnRangeSelected( wxGridRangeSelectEvent& ev )
{
    int left_col = ev.GetLeftCol();
    int right_col = ev.GetRightCol();
    int top_row = ev.GetTopRow();
    int bottom_row = ev.GetBottomRow();

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format("cell from (%1%, %2%) to (%3%, %4%) selected") %top_row %left_col %bottom_row %right_col;

    ev.Skip();
}

wxSize ObjectTablePanel::get_init_size()
{
    wxSize size;
    int width = 0, height, row_count, row_height;

    row_count = m_object_grid_table->GetNumberRows();
    if (row_count > 1)
        row_height = m_object_grid->GetRowSize(1);
    else
        row_height = m_object_grid->GetRowSize(0);

    if (row_count < (min_row_count - 2))
        row_count = min_row_count;
    else
        row_count = row_count + 2;
    height =  row_height * row_count;

    size.Set(width, height);

    return size;
}

ObjectTableDialog::ObjectTableDialog(wxWindow* parent, Plater* platerObj, Model *modelObj)
    : GUI::DPIDialog(parent, wxID_ANY, _L("Object/Part Setting"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    m_model(modelObj), m_plater(platerObj)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    m_bg_colour = wxColour(0, 0, 0);
    //this->SetBackgroundColour(m_bg_colour);

    //m_top_sizer = new wxBoxSizer( wxVERTICAL );

    //m_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    this->SetBackgroundColour(m_bg_colour);

    //m_static_title = new wxStaticText( m_panel, wxID_ANY, wxT("Totally Objects, Parts"), wxDefaultPosition, wxDefaultSize, 0 );
    //m_static_title->SetFont(Label::Head_12);
    //m_static_title->Wrap( -1 );
    //m_top_sizer->Add(m_static_title, 0, wxALL, 5);

    //wxBoxSizer* bSizer1;
	//bSizer1 = new wxBoxSizer( wxVERTICAL );

	//this->SetSizer( bSizer1 );
	//this->Layout();

    // And also actually enable them.
    //m_panel->SetScrollRate(10, 10);

    m_obj_panel = new ObjectTablePanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL|wxBORDER_NONE, wxString("Tabel Panel"), m_plater, m_model);
    //m_top_sizer->Add(m_obj_panel, 1, wxALL | wxEXPAND, 5);

    //m_panel->SetSize(wxSize(1024, 512));
    wxSize panel_size = m_obj_panel->get_init_size();
    this->SetSize(wxSize(1880, panel_size.GetHeight()));
    //m_top_sizer->SetSizeHints(this);
    //this->SetSizer(m_top_sizer);
    //SetClientSize(m_panel->GetSize());

    //this->Layout();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", created, this %1%, m_obj_panel %2%") %this % m_obj_panel;
}

ObjectTableDialog::~ObjectTableDialog()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", this %1%, m_obj_panel %2%") %this % m_obj_panel;
    if (m_obj_panel) {
        delete m_obj_panel;
        m_obj_panel = nullptr;
    }
}

void ObjectTableDialog::Popup(int obj_idx, int vol_idx, wxPoint position /*= wxDefaultPosition*/)
{
    m_obj_panel->sort_by_default();
    m_obj_panel->SetSelection(obj_idx, vol_idx);

    this->SetPosition(position);
    this->ShowModal();
}

void ObjectTableDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    const wxSize& size = wxSize(40 * em, 30 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void ObjectTableDialog::on_sys_color_changed()
{

    Refresh();
}

} // namespace GUI
} // namespace Slic3r
