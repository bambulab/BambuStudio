#ifndef slic3r_GUI_ObjectTable_hpp_
#define slic3r_GUI_ObjectTable_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/grid.h>
#include <wx/renderer.h>
#include <wx/gdicmn.h>
#include <wx/valnum.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/popupwin.h>

#include "Plater.hpp"
#include "libslic3r/Model.hpp"
//#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "OptionsGroup.hpp"
#include "GUI_Factories.hpp"
#include "GUI_ObjectTableSettings.hpp"

namespace Slic3r {


namespace GUI {

class ObjectTablePanel;

class GridCellIconRenderer : public wxGridCellRenderer
{
public:
    virtual void Draw(wxGrid& grid,
                      wxGridCellAttr& attr,
                      wxDC& dc,
                      const wxRect& rect,
                      int row, int col,
                      bool isSelected) wxOVERRIDE;
    
    virtual wxSize GetBestSize(wxGrid& WXUNUSED(grid),
                               wxGridCellAttr& attr,
                               wxDC& dc,
                               int WXUNUSED(row),
                               int WXUNUSED(col)) wxOVERRIDE;

    virtual GridCellIconRenderer *Clone() const wxOVERRIDE;
};

// the editor for string data allowing to choose from the list of strings
class  GridCellFilamentsEditor : public wxGridCellChoiceEditor
{
public:
    // if !allowOthers, user can't type a string not in choices array
    GridCellFilamentsEditor(size_t count = 0,
                           const wxString choices[] = NULL,
                           bool allowOthers = false,
                           std::vector<wxBitmap*>* bitmaps = NULL);
    GridCellFilamentsEditor(const wxArrayString& choices,
                           bool allowOthers = false,
                           std::vector<wxBitmap*>* bitmaps = NULL);

    virtual void Create(wxWindow* parent,
                        wxWindowID id,
                        wxEvtHandler* evtHandler) wxOVERRIDE;
    virtual void SetSize(const wxRect& rect) wxOVERRIDE;

    virtual wxGridCellEditor *Clone() const wxOVERRIDE;


protected:
    wxBitmapComboBox *Combo() const { return (wxBitmapComboBox *)m_control; }

    std::vector<wxBitmap*>* m_icons;

    wxDECLARE_NO_COPY_CLASS(GridCellFilamentsEditor);
};


class GridCellFilamentsRenderer : public wxGridCellChoiceRenderer
{
public:
    virtual void Draw(wxGrid& grid,
                      wxGridCellAttr& attr,
                      wxDC& dc,
                      const wxRect& rect,
                      int row, int col,
                      bool isSelected) wxOVERRIDE;

    virtual wxSize GetBestSize(wxGrid& WXUNUSED(grid),
                               wxGridCellAttr& attr,
                               wxDC& dc,
                               int WXUNUSED(row),
                               int WXUNUSED(col)) wxOVERRIDE;

    virtual GridCellFilamentsRenderer *Clone() const wxOVERRIDE;
};

class GridCellSupportRenderer : public wxGridCellBoolRenderer
{
public:
    virtual void Draw(wxGrid& grid,
                      wxGridCellAttr& attr,
                      wxDC& dc,
                      const wxRect& rect,
                      int row, int col,
                      bool isSelected) wxOVERRIDE;

    virtual wxSize GetBestSize(wxGrid& WXUNUSED(grid),
                               wxGridCellAttr& attr,
                               wxDC& dc,
                               int WXUNUSED(row),
                               int WXUNUSED(col)) wxOVERRIDE;

    virtual GridCellSupportRenderer *Clone() const wxOVERRIDE;
};

//ObjectGrid for the param setting table
class ObjectGrid : public wxGrid
{
public:    
    ObjectGrid(wxWindow *parent,
        wxWindowID id,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxWANTS_CHARS,
        const wxString& name = wxASCII_STR(wxGridNameStr))
        :wxGrid(parent, id, pos, size, style, name)
    {
    }

    ~ObjectGrid() {}

    /*virtual wxPen GetColGridLinePen(int col)
    {
        if (col % 2 == 1)
            return wxPen(*wxBLUE, 2, wxPENSTYLE_SOLID);
        else
            return *wxTRANSPARENT_PEN;
    }

    virtual wxPen GetRowGridLinePen(int row)
    {
        return wxNullPen;
    }*/

};

class ObjectGridTable : public wxGridTableBase
{
public:
    enum GridRowType
    {
        row_object = 0,
        row_volume = 1
    };
    enum GridColType
    {
        col_plate_index = 0,
        col_assemble_name = 1,
        col_assemble_name_reset = 2,
        col_name = 3,
        col_name_reset = 4,
        col_filaments = 5,
        col_filaments_reset = 6,
        col_layer_height = 7,
        col_layer_height_reset = 8,
        col_perimeters = 9,
        col_perimeters_reset = 10,
        col_fill_density = 11,
        col_fill_density_reset = 12,
        col_support_material = 13,
        col_support_material_reset = 14,
        col_brim_type = 15,
        col_brim_type_reset = 16,
        col_speed_perimeter = 17,
        col_speed_perimeter_reset = 18,
        col_max
    };

    struct ObjectGridRow
    {
        int                         object_id;
        int                         volume_id;
        GridRowType                 row_type;
        ConfigOptionString          plate_index;
        ConfigOptionString          assemble_name;
        ConfigOptionString          ori_assemble_name;
        ConfigOptionString          name;
        ConfigOptionString          ori_name;
        ConfigOptionInt             filaments;
        ConfigOptionInt             ori_filaments;
        ConfigOptionFloat           layer_height;
        ConfigOptionFloat           ori_layer_height;
        ConfigOptionInt             perimeters;
        ConfigOptionInt             ori_perimeters;
        ConfigOptionPercent         fill_density;
        ConfigOptionPercent         ori_fill_density;
        ConfigOptionBool            support_material;
        ConfigOptionBool            ori_support_material;
        ConfigOptionEnum<BrimType>  brim_type;
        ConfigOptionEnum<BrimType>  ori_brim_type;
        ConfigOptionFloat           speed_perimeter;
        ConfigOptionFloat           ori_speed_perimeter;

        ModelConfig*                config;

        ObjectGridRow(int obj_id, int vol_id, GridRowType type)
            : object_id(obj_id), volume_id(vol_id), row_type(type)
        {
            config = nullptr;
        }

        ConfigOption& operator[](GridColType idx)
        {
            switch(idx)
            {
                case col_plate_index:
                    return plate_index;
                case col_assemble_name:
                    return assemble_name;
                case col_assemble_name_reset:
                    return ori_assemble_name;
                case col_name:
                    return name;
                case col_name_reset:
                    return ori_name;
                case col_filaments:
                    return filaments;
                case col_filaments_reset:
                    return ori_filaments;
                case col_layer_height:
                    return layer_height;
                case col_layer_height_reset:
                    return ori_layer_height;
                case col_perimeters:
                    return perimeters;
                case col_perimeters_reset:
                    return ori_perimeters;
                case col_fill_density:
                    return fill_density;
                case col_fill_density_reset:
                    return ori_fill_density;
                case col_support_material:
                    return support_material;
                case col_support_material_reset:
                    return ori_support_material;
                case col_brim_type:
                    return brim_type;
                case col_brim_type_reset:
                    return ori_brim_type;
                case col_speed_perimeter:
                    return speed_perimeter;
                case col_speed_perimeter_reset:
                    return ori_speed_perimeter;
                default:
                    break;
            }
            return name;
        }
    };
    typedef std::function<bool(ObjectGridRow* row1, ObjectGridRow* row2)> compare_row_func;

    struct ObjectGridCol
    {
        int                  size;
        ConfigOptionType     type;
        std::string          key;
        std::string          category;
        bool                 b_for_object;
        bool                 b_icon;
        bool                 b_editable;
        bool                 b_from_config;
        wxString             *choices;
        int                  choice_count;
        int                  horizontal_align;

        ObjectGridCol(ConfigOptionType option_type, std::string key_str, std::string cat, bool only_object, bool icon, bool edit, bool config, int ho_align)
            : type(option_type), key(key_str), category(cat), b_for_object(only_object), b_icon(icon), b_editable(edit), b_from_config(config), horizontal_align(ho_align)
        {
            if (b_icon)
                size = 32;
            else
                size = -1;
            choices = nullptr;
            choice_count = 0;
        }

        ~ObjectGridCol()
        {
            choices = nullptr;
        }
    };
    ObjectGridTable(ObjectTablePanel* panel): m_panel(panel) { }
    ~ObjectGridTable();

    void release_object_configs();
    wxString convert_filament_string(int index, wxString& filament_str);

    virtual int GetNumberRows() wxOVERRIDE;
    virtual int GetNumberCols() wxOVERRIDE;
    virtual bool IsEmptyCell( int row, int col ) wxOVERRIDE;
    

    //virtual wxString GetColLabelValue( int col ) wxOVERRIDE;

    virtual wxString GetTypeName( int row, int col ) wxOVERRIDE;
    virtual bool CanGetValueAs( int row, int col, const wxString& typeName ) wxOVERRIDE;
    virtual bool CanSetValueAs( int row, int col, const wxString& typeName ) wxOVERRIDE;

    virtual wxString GetValue( int row, int col ) wxOVERRIDE;
    virtual void SetValue( int row, int col, const wxString& value ) wxOVERRIDE;

    virtual long GetValueAsLong( int row, int col ) wxOVERRIDE;
    virtual bool GetValueAsBool( int row, int col ) wxOVERRIDE;
    virtual double GetValueAsDouble (int row, int col) wxOVERRIDE;

    virtual void SetValueAsLong( int row, int col, long value ) wxOVERRIDE;
    virtual void SetValueAsBool( int row, int col, bool value ) wxOVERRIDE;
    virtual void SetValueAsDouble (int row, int col, double value) wxOVERRIDE;

    template<typename TYPE> const TYPE* get_object_config_value(const DynamicPrintConfig& global_config, ModelConfig* obj_config, std::string& config_option)
    {
        if (obj_config->has(config_option))
            return static_cast<const TYPE*>(obj_config->option(config_option));
        else {
            const TYPE* ptr = global_config.option<TYPE>(config_option);
            //todo: how to deal with nullptr
            return ptr;
        }
    }

    template<typename TYPE> const TYPE* get_volume_config_value(const DynamicPrintConfig& global_config, ModelConfig* obj_config, ModelConfig* volume_config, std::string& config_option)
    {
        if (volume_config->has(config_option))
            return static_cast<const TYPE*>(volume_config->option(config_option));
        else if (obj_config->has(config_option))
            return static_cast<const TYPE*>(obj_config->option(config_option));
        else {
            const TYPE* ptr = global_config.option<TYPE>(config_option);
            //todo: how to deal with nullptr
            return ptr;
        }
    }

    int get_row_count() { return m_grid_data.size() + 1; }
    int get_col_count() { return m_col_data.size(); }
    ObjectGridCol* get_grid_col(int col) { return m_col_data[col]; }
    ObjectGridRow* get_grid_row(int row) { return m_grid_data[row]; }
    void construct_object_configs();
    void update_value_to_config(ModelConfig* config, std::string& key, ConfigOption& new_value,  ConfigOption& ori_value);
    void update_value_to_object(Model* model, ObjectGridRow* grid_row, int col);
    wxBitmap& get_undo_bitmap(bool selected = false);
    wxBitmap* get_color_bitmap(int color_index);
    void OnCellLeftClick(int row, int col);
    void OnSelectCell(int row, int col);
    void OnRangeSelected(int row, int col, int row_count, int col_count);
    //void OnRangeSelecting( wxGridRangeSelectEvent& );
    //void OnCellValueChanging( wxGridEvent& );
    void OnCellValueChanged(int row, int col);
    //set the selection by object id and volume id
    void SetSelection(int object_id, int volume_id);
    //sort the table row datas by default
    void sort_by_default();

    //reload data caused by settings in the side window
    void reload_object_data(ObjectGridRow* grid_row, const std::string& category, DynamicPrintConfig&  global_config);
    void reload_part_data(ObjectGridRow* volume_row, ObjectGridRow* object_row, const std::string& category, DynamicPrintConfig&  global_config);
    void reload_cell_data(int row, const std::string& category);

    int m_icon_col_width{ 0 };
    int m_icon_row_height{ 0 };
private:
    ObjectTablePanel* m_panel{nullptr};
    std::vector<ObjectGridRow*> m_grid_data;
    std::vector<ObjectGridCol*> m_col_data;
    bool m_data_valid{false};

    std::list<wxGridCellCoords> m_selected_cells;

    void init_cols();
    //generic function for sort row datas
    void sort_row_data(compare_row_func sort_func);
    //update the row properties for the data has been sorted
    void update_row_properties();
};


//the main panel
class ObjectTablePanel : public wxPanel
{
    void OnCellLeftClick( wxGridEvent& );
    //void OnRowSize( wxGridSizeEvent& );
    //void OnColSize( wxGridSizeEvent& );
    void OnSelectCell( wxGridEvent& );
    void OnRangeSelected( wxGridRangeSelectEvent& );
    //void OnRangeSelecting( wxGridRangeSelectEvent& );
    //void OnCellValueChanging( wxGridEvent& );
    void OnCellValueChanged( wxGridEvent& );
    //void OnCellBeginDrag( wxGridEvent& );
public:
    ObjectTablePanel( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name, Plater* platerObj, Model *modelObj );
    ~ObjectTablePanel();

    void load_data();
    void SetSelection(int object_id, int volume_id);
    void sort_by_default() { m_object_grid_table->sort_by_default(); }
    wxSize get_init_size();

    //set ObjectGridTable as friend
    friend class     ObjectGridTable;

    std::vector<wxString> m_filaments_name;
    std::vector<wxColour> m_filaments_colors;
    int m_filaments_count{ 1 };
    void set_default_filaments_and_colors()
    {
        m_filaments_count = 1;
        m_filaments_colors.push_back(*wxGREEN);
        m_filaments_name.push_back("Generic PLA");
    }

private:
    wxColour            m_bg_colour;
    wxColour            m_hover_colour;
    wxBoxSizer*         m_top_sizer{nullptr};
    wxBoxSizer*         m_page_sizer{nullptr};
    wxTextCtrl*         m_search_line{ nullptr };
    ObjectGrid*         m_object_grid{nullptr};
    ObjectGridTable*    m_object_grid_table{nullptr};
    wxStaticText*       m_page_text{nullptr};
    wxScrolledWindow*   m_side_window{nullptr};
    ObjectTableSettings* m_object_settings{ nullptr };
    Model*              m_model{nullptr};
    ModelConfig*        m_config {nullptr};
    Plater*             m_plater{nullptr};

    int                 m_cur_row { -1 };
    int                 m_cur_col { -1 };

    int init_bitmap();
    int init_filaments_and_colors();

    wxFloatingPointValidator<float> m_float_validator;
    wxBitmap           m_undo_bitmap;
    std::vector<wxBitmap*> m_color_bitmaps; 
private:
    wxDECLARE_ABSTRACT_CLASS(ObjectTablePanel);
    wxDECLARE_EVENT_TABLE();
};

class ObjectTableDialog : public GUI::DPIDialog
{
    const int POPUP_WIDTH   = 512;
    const int POPUP_HEIGHT  = 1024;
    wxColour m_bg_colour;

    //wxPanel*             m_panel{ nullptr };
    wxBoxSizer*          m_top_sizer{ nullptr };
    wxStaticText*        m_static_title{ nullptr };
    //wxTimer*             m_refresh_timer;
    ObjectTablePanel*    m_obj_panel{ nullptr };
    Model*               m_model{ nullptr };
    Plater*              m_plater{ nullptr };


public:
    ObjectTableDialog(wxWindow* parent, Plater* platerObj, Model *modelObj, wxSize maxSize);
    ~ObjectTableDialog();
    void Popup(int obj_idx = -1, int vol_idx = -1, wxPoint position = wxDefaultPosition);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;
};

}
}
#endif //slic3r_GUI_ObjectTable_hpp_
