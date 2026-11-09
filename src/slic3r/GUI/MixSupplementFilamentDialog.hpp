#ifndef slic3r_MixSupplementFilamentDialog_hpp_
#define slic3r_MixSupplementFilamentDialog_hpp_

#include "GUI_Utils.hpp"

#include <string>
#include <vector>

class Button;
class Label;
class ScalableButton;
class StaticBox;
class wxBoxSizer;

namespace Slic3r {
namespace GUI {

class VirtualFilamentComboBox;

// Lists the filaments that color mixing needs before any of them is added to the
// project. Rows stay virtual: the caller adds them to the project only after OK.
class MixSupplementFilamentDialog : public DPIDialog
{
public:
    struct Row
    {
        std::string preset_name;
        std::string color_hex;
        // Caller-defined id of a suggested row. Reset to -1 once the user changes
        // the row's preset or color, so the caller can tell untouched suggestions apart.
        int         tag{-1};
    };

    // max_rows: how many filaments can still be added to the project.
    // fallback_preset: preset for "New filament" when no row is left to copy from.
    // project_colors: physical filament colors already in the project; "New filament" avoids them.
    // family_has_color: whether the mixing family already has one color, which picks the message.
    MixSupplementFilamentDialog(wxWindow*                       parent,
                                const std::vector<Row>&         rows,
                                size_t                          max_rows,
                                const std::string&              fallback_preset,
                                const std::vector<std::string>& project_colors,
                                bool                            family_has_color);

    std::vector<Row> get_rows() const;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;

private:
    struct RowUi
    {
        wxBoxSizer*              sizer{nullptr};
        StaticBox*               card{nullptr};
        VirtualFilamentComboBox* combo{nullptr};
        ScalableButton*          del_btn{nullptr};
        int                      tag{-1};
    };

    void build_ui(const std::vector<Row>& rows, bool family_has_color);
    void add_row(const Row& row);
    void remove_row(StaticBox* card);
    void on_new_filament();
    std::string next_distinct_color() const;
    void update_buttons();
    void relayout();

    size_t                   m_max_rows{0};
    std::string              m_fallback_preset;
    std::vector<std::string> m_project_colors;
    std::vector<RowUi>       m_rows;

    Label*      m_message{nullptr};
    Label*      m_note{nullptr};
    wxBoxSizer* m_rows_sizer{nullptr};
    Button*     m_btn_new{nullptr};
    Button*     m_btn_ok{nullptr};
    Button*     m_btn_cancel{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif
