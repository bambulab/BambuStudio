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
#include <functional>
#include <string>

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(wxEVT_DRAG_DROP_COMPLETED, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_FILAMENT_DRAG_HOVER, wxCommandEvent);

class FilamentMapManualPanel;

wxColor Hex2Color(const std::string& str);

class ColorPanel;
class DragDropPanel : public wxPanel
{
public:
    DragDropPanel(wxWindow *parent, const wxString &label, bool is_auto, bool has_title = true,
                  bool is_sub = false, bool use_fixed_grid_item_size = false);

    void AddColorBlock(const std::string &type, int filament_id, bool update_ui = true);
    void RemoveColorBlock(ColorPanel *panel, bool update_ui = true);
    void DoDragDrop(ColorPanel *panel, const std::string &type, int filament_id);
    void UpdateLabel(const wxString &label);
    void SetGridColumns(int cols);
    void SetGridColumnsForWidth(int panel_width);
    void SetGridHorizontalPadding(int padding, int gap);
    void SetInteractionEnabled(bool enabled);
    bool IsInteractionEnabled() const { return m_interaction_enabled; }

    std::vector<int> GetAllFilaments() const;

    void set_is_draging(bool is_draging) { m_is_draging = is_draging; }
    bool is_draging() const { return m_is_draging; }

    std::vector<ColorPanel *> get_filament_blocks() const { return m_filament_blocks; }

    // Multi-diameter restriction: predicate deciding whether this panel (an extruder/nozzle of a fixed
    // working diameter) accepts a given filament (1-based id). Empty predicate = accept everything.
    void set_accept_filament_predicate(std::function<bool(int)> pred) { m_accept_filament = std::move(pred); }
    bool accepts_filament(int filament_id) const {
        return m_interaction_enabled && (!m_accept_filament || m_accept_filament(filament_id));
    }

private:
    wxBoxSizer *m_sizer;
    wxGridSizer *m_grid_item_sizer;
    wxSizerItem *m_grid_sizer_item{nullptr};
    Label       *m_title_label = nullptr;
    bool         m_is_auto;
    bool         m_use_fixed_grid_item_size{false};
    bool         m_interaction_enabled{true};
    std::function<bool(int)> m_accept_filament;

    std::vector<ColorPanel *> m_filament_blocks;

    void NotifyDragDropCompleted();
    void BindInteractionHoverEvents(wxWindow *window);
    void OnInteractionHoverEnter(wxMouseEvent &event);
    void OnInteractionHoverLeave(wxMouseEvent &event);
private:
    bool m_is_draging = false;
};

///////////////   ColorPanel  start ////////////////////////
// The UI panel of drag item
class ColorPanel : public wxPanel
{
public:
    ColorPanel(DragDropPanel *parent, int filament_id, const std::string& type, const wxSize &size);

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
    struct DiameterVolumeGroup
    {
        std::string      diameter;
        NozzleVolumeType volume_type;
        wxString         label;
    };

    SeparatedDragDropPanel(wxWindow *parent, const wxString &label, bool use_separation = false,
                           bool show_unified_title = false, bool expand_drop_target = false,
                           bool use_fixed_grid_item_size = false);

    void AddColorBlock(const std::string &type, int filament_id, NozzleVolumeType volume_type = nvtStandard,
                       bool update_ui = true, const std::string &diameter = {});
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
    void UpdateUnifiedLabel(const wxString &label);
    // 把各流量类型的喷嘴数量挂到次级标题上，主标题只保留喷嘴编号。
    void UpdateVolumeTypeCounts(int standard_count, int high_flow_count, int tpu_count);
    // 列数决定 unified（非分隔）网格的列宽，列宽小于图标宽度时图标之间就没有间隙。
    void SetUnifiedGridColumns(int cols);
    void SetUnifiedGridWidth(int box_width);
    void SetUnifiedGridHorizontalPadding(int padding, int gap);
    void SetDiameterVolumeGroups(const std::vector<DiameterVolumeGroup> &groups, bool use_white_cards = true);
    void SetEnabledDiameterVolumeGroup(const std::string &diameter, NozzleVolumeType volume_type);
    bool IsUseDiameterVolumeGroups() const { return !m_diameter_volume_panels.empty(); }
    void UpdateDiameterVolumeLayout();

    // Multi-diameter restriction: propagate the accept predicate to every sub-panel (this panel is a
    // single extruder split by volume type, so all sub-panels share the same working diameter).
    void set_accept_filament_predicate(std::function<bool(int)> pred);
    void set_diameter_group_accept_filament_predicate(
        std::function<bool(int, const std::string &)> pred);
    bool accepts_filament(int filament_id) const {
        if (m_diameter_volume_panels.empty())
            return !m_accept_filament || m_accept_filament(filament_id);
        return m_diameter_volume_panels.size() == 1 &&
               m_diameter_volume_panels.front().panel->accepts_filament(filament_id);
    }

private:
    void UpdateLayout();
    DragDropPanel *ResolveSeparatedTargetPanel(NozzleVolumeType volume_type = nvtStandard) const;

    wxBoxSizer   *m_main_sizer;
    wxPanel      *m_content_panel;
    wxBoxSizer   *m_content_sizer;
    wxStaticText *m_label;

    DragDropPanel *m_high_flow_panel;
    DragDropPanel *m_standard_panel;
    DragDropPanel *m_tpu_panel;

    DragDropPanel *m_unified_panel;

    bool m_use_separation;
    bool m_use_fixed_grid_item_size{false};
    bool m_has_volume_type_counts{false};
    int  m_std_count{0};
    int  m_hf_count{0};
    int  m_tpu_count{0};
    std::vector<wxWindow *> m_separators;
    std::function<bool(int)> m_accept_filament;

    struct DiameterVolumePanel
    {
        std::string      diameter;
        NozzleVolumeType volume_type;
        DragDropPanel   *panel;
    };
    wxPanel                       *m_diameter_volume_container{nullptr};
    wxFlexGridSizer                *m_diameter_volume_sizer{nullptr};
    std::vector<DiameterVolumePanel> m_diameter_volume_panels;
};

}} // namespace Slic3r::GUI

#endif /* slic3r_DragDropPanel_hpp_ */
