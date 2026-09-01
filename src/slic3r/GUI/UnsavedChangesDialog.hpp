#pragma once

#include <string>
#include <wx/dataview.h>
#include <map>
#include <vector>
#include <wx/string.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include "libslic3r/CommonDefs.hpp"

class ScalableButton;
class wxStaticText;
class wxStaticBitmap;
class wxBoxSizer;
class wxSimplebook;

namespace Slic3r ::GUI {

class TextTabbar;

// ----------------------------------------------------------------------------
//                  ModelNode: a node inside DiffModel
// ----------------------------------------------------------------------------

class ModelNode;
class PresetComboBox;
class MainFrame;
using ModelNodePtrArray = std::vector<std::unique_ptr<ModelNode>>;

// On all of 3 different platforms Bitmap+Text icon column looks different
// because of Markup text is missed or not implemented.
// As a temporary workaround, we will use:
// MSW - DataViewBitmapText (our custom renderer wxBitmap + wxString, supported Markup text)
// OSX - -//-, but Markup text is not implemented right now
// GTK - wxDataViewIconText (wxWidgets for GTK renderer wxIcon + wxString, supported Markup text)
class ModelNode
{
public:
    /// \brief Row kind, used to pick the per-row border style (category/group = solid, option = dashed).
    enum class NodeKind { Category, Group, Option };

private:
    wxWindow*           m_parent_win{ nullptr };

    ModelNode*          m_parent;
    ModelNodePtrArray   m_children;
    wxBitmap            m_empty_bmp;

    std::string         m_icon_name;
    // saved values for colors if they exist
    wxString            m_old_color;
    wxString            m_new_color;

#ifdef __linux__
    wxIcon              get_bitmap(const wxString& color);
#else
    wxBitmap            get_bitmap(const wxString& color);
#endif //__linux__

public:

    bool        m_toggle {true};
#ifdef __linux__
    wxIcon      m_icon;
    wxIcon      m_old_color_bmp;
    wxIcon      m_new_color_bmp;
#else
    wxBitmap    m_icon;
    wxBitmap    m_old_color_bmp;
    wxBitmap    m_new_color_bmp;
#endif //__linux__
    wxString    m_text;
    wxString    m_old_value;
    wxString    m_new_value;

    // TODO/FIXME:
    // the GTK version of wxDVC (in particular wxDataViewCtrlInternal::ItemAdded)
    // needs to know in advance if a node is or _will be_ a container.
    // Thus implementing:
    //   bool IsContainer() const
    //    { return m_children.size()>0; }
    // doesn't work with wxGTK when DiffModel::AddToClassical is called
    // AND the classical node was removed (a new node temporary without children
    // would be added to the control)
    bool                m_container {true};

    // Row kind for per-row border styling; defaults to Option and is set explicitly in the
    // category/group constructors.
    NodeKind m_kind{NodeKind::Option};

    // category node (no parent node; the categories are the tree's roots)
    ModelNode(wxWindow *parent_win, const wxString &text, const std::string &icon_name);

    // group node
    ModelNode(ModelNode* parent, const wxString& text);

    /**
     * \brief Construct an general node
     *
     * \param parent        Parent node this row is appended under.
     * \param text          Localized option label shown in the option column.
     * \param old_value     Preset A value; a leading '#' is treated as a color swatch.
     * \param new_value     Preset B value; a leading '#' is treated as a color swatch.
     * \param is_container  True for an expandable parent row that aggregates per-variant children.
     * \param icon_name     Icon name for the row (e.g. the multi-value marker on a container).
     */
    ModelNode(ModelNode *parent, const wxString &text, const wxString &old_value, const wxString &new_value, bool is_container = false, const std::string &icon_name = "empty");

    bool                IsContainer() const         { return m_container; }
    NodeKind            kind() const { return m_kind; }
    bool                IsToggled() const           { return m_toggle; }
    void                Toggle(bool toggle = true)  { m_toggle = toggle; }
    bool                IsRoot() const { return m_parent == nullptr; }
    const wxString&     text() const                { return m_text; }

    ModelNode*          GetParent()                 { return m_parent; }
    ModelNodePtrArray&  GetChildren()               { return m_children; }
    ModelNode*          GetNthChild(unsigned int n) { return m_children[n].get(); }
    unsigned int        GetChildCount() const       { return m_children.size(); }

    void Append(std::unique_ptr<ModelNode> child)   { m_children.emplace_back(std::move(child)); }

    void UpdateEnabling();
    void UpdateIcons();
};


// ----------------------------------------------------------------------------
//                  DiffModel
// ----------------------------------------------------------------------------

class DiffModel : public wxDataViewModel
{
    wxWindow*               m_parent_win { nullptr };
    ModelNodePtrArray       m_preset_nodes;
    Preset::Type            m_type{Preset::TYPE_INVALID};

    wxDataViewCtrl*         m_ctrl{ nullptr };

    /**
     * \brief Get the Category with name \p category_name, create if needed
     *
     * \param category_name
     * \param category_icon_name
     * \return ModelNode*
     */
    ModelNode *GetCategory(wxString category_name, const std::string &category_icon_name);

    /**
     * \brief Get the Group in \p category with group_name, create if needed
     *
     * \param category_node
     * \param group_name
     * \return ModelNode*
     */
    ModelNode *GetGroup(ModelNode *category_node, wxString group_name);

    ModelNode *AddOption(ModelNode *group_node, wxString option_name, wxString old_value, wxString new_value, std::string icon = "");

public:
    enum {
        colToggle,
        colIconText,
        colOldValue,
        colNewValue,
        colMax
    };

    DiffModel(wxWindow* parent);
    ~DiffModel(){};

    void            SetAssociatedControl(wxDataViewCtrl* ctrl) { m_ctrl = ctrl; }

    wxDataViewItem AddPreset(Preset::Type type);
    wxDataViewItem AddOption(wxString           category_name,
                             const std::string &category_icon_name,
                             wxString           group_name,
                             wxString           option_name,
                             wxString           old_value,
                             wxString           new_value,
                             const std::string &option_icon);

    /**
     * \brief Add an expandable option row whose collapsed value aggregates every extruder variant,
     *        with one child row per variant.
     *
     * \param category_name       Localized category (top-level tree node) the option is grouped under.
     * \param group_name          Localized group (sub-node) the option is grouped under.
     * \param option_name         Localized option label shown on the parent row.
     * \param category_icon_name  Icon name for the category node.
     * \param variant_labels      Per-variant row labels, in variant order.
     * \param variant_old_values  Per-variant Preset A values, parallel to variant_labels; use "N/A" where a variant is absent.
     * \param variant_new_values  Per-variant Preset B values, parallel to variant_labels; use "N/A" where a variant is absent.
     * \return The parent (option) item.
     */
    wxDataViewItem AddVariantOption(wxString                     category_name,
                                    wxString                     group_name,
                                    wxString                     option_name,
                                    wxString                     old_value,
                                    wxString                     new_value,
                                    const std::string            category_icon_name,
                                    const std::vector<wxString> &variant_labels,
                                    const std::vector<wxString> &variant_old_values,
                                    const std::vector<wxString> &variant_new_values);

    void            UpdateItemEnabling(wxDataViewItem item);
    bool            IsEnabledItem(const wxDataViewItem& item);

    unsigned int    GetColumnCount() const override { return colMax; }
    wxString        GetColumnType(unsigned int col) const override;
    void            Rescale();

    wxDataViewItem  Delete(const wxDataViewItem& item);
    void            Clear();

    wxDataViewItem  GetParent(const wxDataViewItem& item) const override;
    unsigned int    GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const override;

    void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const override;
    bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) override;

    bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override;
    bool IsContainer(const wxDataViewItem& item) const override;
    // Shade category/group rows (whole-row background) so they read as section headers.
    bool GetAttr(const wxDataViewItem &item, unsigned int col, wxDataViewItemAttr &attr) const override;
    // Is the container just a header or an item with all columns
    // In our case it is an item with all columns
    bool HasContainerColumns(const wxDataViewItem& WXUNUSED(item)) const override { return true; }
};


// ----------------------------------------------------------------------------
//                  DiffViewCtrl
// ----------------------------------------------------------------------------

class DiffViewCtrl : public wxDataViewCtrl
{
    bool                    m_has_long_strings{ false };
    bool                    m_empty_selection { false };
    int                     m_em_unit;

    struct ItemData
    {
        std::string     opt_key;
        wxString        opt_name;
        wxString        old_val;
        wxString        new_val;
        Preset::Type    type;
        bool            is_long{ false };
    };

    // tree items related to the options
    std::map<wxDataViewItem, ItemData> m_items_map;
    std::map<unsigned int, int>        m_columns_width;
    wxFont                             m_header_font;    // invalid => keep the app's normal font
    int                                m_header_height = 0;  // DIP; <= 0 => backend default

public:
    DiffViewCtrl(wxWindow* parent, wxSize size);
    ~DiffViewCtrl();

    DiffModel* model{ nullptr };

    void    AppendBmpTextColumn(const wxString& label, unsigned model_column, int width, bool set_expander = false);
    void    AppendToggleColumn_(const wxString& label, unsigned model_column, int width);
    void    Rescale(int em = 0);
    /**
     * \brief Set the font used for the column-header row.
     * \param font Header font; applied immediately and re-applied on every theme/DPI refresh.
     *
     * Must be routed through here rather than SetHeaderAttr() directly: GUI_App::UpdateDVCDarkUI()
     * rewrites the whole header wxItemAttr (font included) on each dark-mode pass, so a font set
     * behind its back is silently reverted.
     */
    void    SetHeaderFont(const wxFont& font);
    /**
     * \brief Set the height of the column-header row.
     * \param height Header height in unscaled DIP; <= 0 restores the backend default.
     *
     * Generic backends only (MSW): the native wxDataViewCtrl offers no header-height hook.
     */
    void    SetHeaderHeight(int height);
    /**
     * \brief Re-apply the dark-mode theme together with the stored header font.
     *
     * Call instead of GUI_App::UpdateDVCDarkUI() on this control so the header font survives.
     */
    void    ApplyDarkUI();
    void    Append(const std::string& opt_key, Preset::Type type, wxString category_name, wxString group_name, wxString option_name,
                   wxString old_value, wxString new_value, const std::string category_icon_name);
    // Appends an expandable option row with per-extruder-variant child rows. The old/new_value pair
    // is the collapsed aggregate; the parallel variant_* arrays carry one entry per variant.
    void    AppendVariant(const std::string           &opt_key,
                          Preset::Type                 type,
                          wxString                     category_name,
                          wxString                     group_name,
                          wxString                     option_name,
                          wxString                     old_value,
                          wxString                     new_value,
                          const std::string            category_icon_name,
                          const std::vector<wxString> &variant_labels,
                          const std::vector<wxString> &variant_old_values,
                          const std::vector<wxString> &variant_new_values);
    void    Clear();

    wxString    get_short_string(wxString full_string);
    bool        has_selection() { return !m_empty_selection; }
    void        context_menu(wxDataViewEvent& event);
    void        item_value_changed(wxDataViewEvent& event);
    void        set_em_unit(int em) { m_em_unit = em; }
    bool        has_unselected_options();

    std::vector<std::string> options(Preset::Type type, bool selected);
    std::vector<std::string> selected_options();
};


//------------------------------------------
//          UnsavedChangesDialog
//------------------------------------------
#define BOTH_SIDES_BORDER 25

struct PresetItem
{
    Preset::Type type;
    std::string  opt_key;
    wxString     category_name;
    wxString     group_name;
    wxString     option_name;
    wxString     old_value;
    wxString     new_value;
};

enum ForceOption {
    fopTransfer,
    fopSave,
    fopDiscard,
    fopNone
};

class UnsavedChangesDialog : public DPIDialog
{
protected:
    wxPanel *     m_top_line;
    wxPanel *     m_panel_tab;
    wxPanel *     m_table_top;
    wxPanel *     title_block_middle;
    wxPanel *     title_block_right;
    wxStaticText *static_temp_title;
    wxStaticText *static_oldv_title;
    wxStaticText *static_newv_title;
    wxBoxSizer *  m_sizer_bottom;

    //DiffViewCtrl*           m_tree          { nullptr };
    Button*                 m_save_btn      { nullptr };
    Button*                 m_transfer_btn  { nullptr };
    Button*                 m_discard_btn   { nullptr };
    Button*                 m_cancel_btn    { nullptr };
    wxStaticText*           m_action_line   { nullptr };
    wxStaticText*           m_info_line     { nullptr };
    wxScrolledWindow*       m_scrolledWindow{ nullptr };

    bool                    m_has_long_strings  { false };
    int                     m_save_btn_id       { wxID_ANY };
    int                     m_move_btn_id       { wxID_ANY };
    int                     m_continue_btn_id   { wxID_ANY };

    std::string             m_app_config_key;

    enum class Action {
        Undef,
        Transfer, // Or KEEP
        Save,
        Discard,
    };

    static constexpr char ActTransfer[] = "transfer";
    static constexpr char ActDiscard[]  = "discard";
    static constexpr char ActSave[]     = "save";

    // selected action after Dialog closing
    Action m_exit_action {Action::Undef};

public:
    //BBS: add project embedded preset relate logic
    struct PresetData
    {
        std::string name;
        Preset::Type type;
        bool save_to_project;

        PresetData(std::string preset_name, Preset::Type preset_type, bool save_project)
            :name(preset_name), type(preset_type), save_to_project(save_project)
        {
        }
    };

private:
    std::vector<PresetItem> m_presetitems;
    // preset names which are modified in SavePresetDialog and related types
    std::vector<PresetData>  names_and_types;
    //std::vector<std::pair<std::string, Preset::Type>>  names_and_types;
    // additional action buttons used in dialog
    int m_buttons { ActionButtons::TRANSFER | ActionButtons::SAVE };

    std::string m_new_selected_preset_name;

public:
    // Discard and Cancel buttons are always but next buttons are optional
    enum ActionButtons {
        TRANSFER  = 1,
        KEEP      = 2,
        SAVE      = 4,
        DONT_SAVE = 8,
        REMEMBER_CHOISE = 0x10000
    };

    struct SyncExtruderParams
    {
        DynamicConfig   *config;
        int              from;
        int              to;
        bool             left_to_right;
        NozzleVolumeType nozzle;
    };

    // show unsaved changes when preset is switching
    UnsavedChangesDialog(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset, bool no_transfer = false);
    // show unsaved changes for all another cases
    UnsavedChangesDialog(const wxString& caption, const wxString& header, const std::string& app_config_key, int act_buttons);
    UnsavedChangesDialog(const wxString &caption, const wxString &header, DynamicConfig *config, int from, int to, bool left_to_right, NozzleVolumeType nozzle);
    ~UnsavedChangesDialog(){};

    int ShowModal();

    void        build(Preset::Type type, PresetCollection *dependent_presets, const std::string &new_selected_preset, const wxString &header = "");
    void update(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset, const wxString& header);
    void update_list(SyncExtruderParams *params = nullptr);
    std::string subreplace(std::string resource_str, std::string sub_str, std::string new_str);
    void        update_tree(Preset::Type type, PresetCollection *presets);
    void        update_tree(Preset::Type type, DynamicConfig *config, int from, int to);
    void show_info_line(Action action, std::string preset_name = "");
    void update_config(Action action);
    void close(Action action);
    // save information about saved presets and their types to names_and_types and show SavePresetDialog to set the names for new presets
    bool save(PresetCollection* dependent_presets, bool show_save_preset_dialog = true);

    bool save_preset() const        { return m_exit_action == Action::Save;     }
    bool transfer_changes() const   { return m_exit_action == Action::Transfer; }
    bool discard() const            { return m_exit_action == Action::Discard;  }

    // get full bundle of preset names and types for saving
    //BBS: add project embedded preset relate logic
    const std::vector<UnsavedChangesDialog::PresetData>& get_names_and_types() { return names_and_types; }
    bool get_save_to_project_option() { return names_and_types[0].save_to_project; }
    //const std::vector<std::pair<std::string, Preset::Type>>& get_names_and_types() { return names_and_types; }
    // short version of the previous function, for the case, when just one preset is modified
    std::string get_preset_name() { return names_and_types[0].name; }

    std::vector<std::string> get_unselected_options(Preset::Type type) { /* return m_tree->options(type, false);*/return std::vector<std::string>();}
    std::vector<std::string> get_selected_options  (Preset::Type type)  {
        //return m_tree->options(type, true);
         std::vector<std::string> tmp;
        for (int i = 0; i < m_presetitems.size(); i++) {
            if (m_presetitems[i].type == type) {
                tmp.push_back(m_presetitems[i].opt_key);
            }
        }

        return tmp;
    }
    std::vector<std::string> get_selected_options()                     {
        //return m_tree->selected_options();

        std::vector<std::string> tmp;
        for (int i = 0; i < m_presetitems.size(); i++)
        {
           tmp.push_back(m_presetitems[i].opt_key);
        }

        return tmp;
    }
    bool                     has_unselected_options()                   { /*return m_tree->has_unselected_options();*/return false;}

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;
    bool check_option_valid();
};


//------------------------------------------
//          FullCompareDialog
//------------------------------------------
class FullCompareDialog : public wxDialog
{
public:
    FullCompareDialog(const wxString& option_name, const wxString& old_value, const wxString& new_value,
                      const wxString& old_value_header, const wxString& new_value_header);
    ~FullCompareDialog(){};
};

// EmptyStatePanel and PresetSelectorPanel are private implementation details of DiffPresetDialog,
// defined in UnsavedChangesDialog.cpp (anonymous namespace). Forward-declared here only because
// DiffPresetDialog holds pointers to them.
class EmptyStatePanel;
class PresetSelectorPanel;

//------------------------------------------
//          DiffPresetDialog
//------------------------------------------
class DiffPresetDialog : public DPIDialog
{
    DiffViewCtrl           *m_tree{nullptr};
    // Grey400 backing panel; shows through a 1px inset as the tree's outline.
    wxPanel                *m_tree_frame{nullptr};
    wxCheckBox*             m_show_all_presets  { nullptr };
    // The content region is a wxSimplebook with two mutually-exclusive pages that share one slot:
    // the empty-state placeholder (also used to surface error / "presets are equal" messages via
    // its hint text) and the diff tree. Switch with m_content->SetSelection(kPageEmpty/kPageTree).
    wxSimplebook        *m_content{nullptr};
    EmptyStatePanel     *m_empty_state{nullptr};
    static constexpr int kPageEmpty = 0;
    static constexpr int kPageTree  = 1;
    // Process/Filament/Machine tab bar; m_tab_types maps a tab index to its Preset::Type. Both
    // are rebuilt when the printer technology changes (FFF vs SLA have different tab sets).
    TextTabbar               *m_tabbar{nullptr};
    std::vector<Preset::Type> m_tab_types;
    void                      rebuild_tabs();
    // True when the dialog was opened without a fixed preset type (from the Compare menu), so the
    // user can switch types via the tab bar and per-type compatibility filtering should run. This
    // used to be inferred from m_view_type == TYPE_INVALID, which the tab bar no longer leaves set.
    bool m_opened_generically{true};

    Preset::Type            m_view_type         { Preset::TYPE_INVALID };
    PrinterTechnology       m_pr_technology;
    std::unique_ptr<PresetBundle>   m_preset_bundle_left;
    std::unique_ptr<PresetBundle>   m_preset_bundle_right;

    void                    update_tree();
    void                    update_bundles_from_app();
    void                    update_controls_visibility(Preset::Type type = Preset::TYPE_INVALID);
    void                    update_compatibility(const std::string& preset_name, Preset::Type type, PresetBundle* preset_bundle);

    void on_empty(wxString message, std::string icon);

    // Single preset A/B selector for the active tab's type; the combo pair is recreated on tab
    // switch (see PresetSelectorPanel). Replaces the former per-type vector of combo pairs.
    PresetSelectorPanel *m_selector{nullptr};
    void                 do_update_tree(const Preset *left, const Preset *right, std::vector<std::string> &dirty_options);
    void                 make_variant_row();

public:
    DiffPresetDialog(MainFrame* mainframe);
    ~DiffPresetDialog(){};

    void                    show(Preset::Type type = Preset::TYPE_INVALID);
    void                    update_presets(Preset::Type type = Preset::TYPE_INVALID);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;

    friend class MainFrame;  // for on_sys_color_changed()
};

} // namespace Slic3r::GUI