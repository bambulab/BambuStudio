#ifndef slic3r_DragDropPanel_hpp_
#define slic3r_DragDropPanel_hpp_

#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "Widgets/Label.hpp"

#include <wx/simplebook.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <vector>


namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(wxEVT_DRAG_DROP_COMPLETED, wxCommandEvent);

class FilamentMapManualPanel;

wxColor Hex2Color(const std::string& str);

class ColorPanel;
class DragDropPanel : public wxPanel
{
public:
    DragDropPanel(wxWindow *parent, const wxString &label, bool is_auto, bool has_title = true, bool is_sub = false);

    void AddColorBlock(const std::string &type, int filament_id, bool update_ui = true);
    void RemoveColorBlock(ColorPanel *panel, bool update_ui = true);
    void DoDragDrop(ColorPanel *panel, const std::string &type, int filament_id);
    void UpdateLabel(const wxString &label);
    void SetGridColumns(int cols);
    void SetGridColumnsForWidth(int panel_width);

    std::vector<int> GetAllFilaments() const;

    void set_is_draging(bool is_draging) { m_is_draging = is_draging; }
    bool is_draging() const { return m_is_draging; }

    std::vector<ColorPanel *> get_filament_blocks() const { return m_filament_blocks; }

private:
    wxBoxSizer *m_sizer;
    wxGridSizer *m_grid_item_sizer;
    Label       *m_title_label = nullptr;
    bool         m_is_auto;

    std::vector<ColorPanel *> m_filament_blocks;

    void NotifyDragDropCompleted();
private:
    bool m_is_draging = false;
};

///////////////   ColorPanel  start ////////////////////////
// The UI panel of drag item
class ColorPanel : public wxPanel
{
public:
    ColorPanel(DragDropPanel *parent, int filament_id, const std::string& type);

    int      GetFilamentId() const { return m_filament_id; }
    std::string GetType() const { return m_type; }

private:
    void OnLeftDown(wxMouseEvent &event);
    void OnLeftUp(wxMouseEvent &event);
    void OnPaint(wxPaintEvent &event);

    DragDropPanel *m_parent;
    std::string    m_type;
    int            m_filament_id;
};

class SeparatedDragDropPanel : public wxPanel
{
public:
    SeparatedDragDropPanel(wxWindow *parent, const wxString &label, bool use_separation = false);

    void AddColorBlock(const std::string &type, int filament_id, NozzleVolumeType volume_type = nvtStandard, bool update_ui = true);
    void RemoveColorBlock(ColorPanel *panel, bool update_ui = true);

    std::vector<int> GetAllFilaments() const;
    std::vector<int> GetHighFlowFilaments() const;
    std::vector<int> GetStandardFilaments() const;
    std::vector<int> GetTPUHighFlowFilaments() const;

    std::vector<ColorPanel *> get_filament_blocks() const;
    std::vector<ColorPanel *> get_high_flow_blocks() const;
    std::vector<ColorPanel *> get_standard_blocks() const;
    std::vector<ColorPanel *> get_tpu_blocks() const;

    void SetUseSeparation(bool use_separation);
    bool IsUseSeparation() const { return m_use_separation; }
    void ClearAllBlocks();
    void UpdateLabel(const wxString &label);
    // 把各流量类型的喷嘴数量挂到次级标题上，主标题只保留喷嘴编号。
    void UpdateVolumeTypeCounts(int standard_count, int high_flow_count, int tpu_count);
    // 列数决定 unified（非分隔）网格的列宽，列宽小于图标宽度时图标之间就没有间隙。
    void SetUnifiedGridColumns(int cols);
    void SetUnifiedGridWidth(int box_width);

private:
    void UpdateLayout();

    wxBoxSizer   *m_main_sizer;
    wxPanel      *m_content_panel;
    wxBoxSizer   *m_content_sizer;
    wxStaticText *m_label;

    DragDropPanel *m_high_flow_panel;
    DragDropPanel *m_standard_panel;
    DragDropPanel *m_tpu_panel;

    DragDropPanel *m_unified_panel;

    bool m_use_separation;
};

}} // namespace Slic3r::GUI

#endif /* slic3r_DragDropPanel_hpp_ */
