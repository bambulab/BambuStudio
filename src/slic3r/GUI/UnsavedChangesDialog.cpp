#include "UnsavedChangesDialog.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <string>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/nowide/convert.hpp>

#include <wx/dataview.h>
#include <wx/headerctrl.h> // complete type for GenericGetHeader()->SetMinSize()
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/layout.h>
#include <wx/string.h>
#include <wx/tokenzr.h>
#include <wx/simplebook.h>

#include "Widgets/StateColor.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "format.hpp"
#include "GUI_App.hpp"
#include "DeviceCore/DevConfigUtil.h"
#include "Plater.hpp"
#include "Tab.hpp"
#include "ExtraRenderers.hpp"
#include "wxExtensions.hpp"
#include "SavePresetDialog.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"

//#define FTS_FUZZY_MATCH_IMPLEMENTATION
//#include "fts_fuzzy_match.h"

#include "BitmapCache.hpp"
#include "PresetComboBoxes.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/TextTabbar.hpp"

using boost::optional;

#ifdef __linux__
#define wxLinux true
#else
#define wxLinux false
#endif

namespace Slic3r {

namespace GUI {

// ----------------------------------------------------------------------------
//                  ModelNode: a node inside DiffModel
// ----------------------------------------------------------------------------

static const std::map<Preset::Type, std::string> type_icon_names = {
    {Preset::TYPE_PRINT,        "cog"           },
    {Preset::TYPE_SLA_PRINT,    "cog"           },
    {Preset::TYPE_FILAMENT,     "spool"         },
    {Preset::TYPE_SLA_MATERIAL, "blank_16"      },
    {Preset::TYPE_PRINTER,      "printer"       },
};

static std::string def_text_color()
{
    wxColour def_colour = wxGetApp().get_label_clr_default();//wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), def_colour.Red(), def_colour.Green(), def_colour.Blue());
    return clr_str.ToStdString();
}
static std::string grey     = "#808080";
static std::string orange   = "#ed6b21";

static void color_string(wxString& str, const std::string& color)
{
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
    str = from_u8((boost::format("<span color=\"%1%\">%2%</span>") % color % into_u8(str)).str());
#endif
}

static void make_string_bold(wxString& str)
{
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
    str = from_u8((boost::format("<b>%1%</b>") % into_u8(str)).str());
#endif
}

static void markup_string(wxString &category_name, wxString &group_name, wxString &option_name)
{
    // "color" strings
    color_string(category_name, def_text_color());
    color_string(group_name, def_text_color());
    color_string(option_name, def_text_color());

    // "make" strings bold
    make_string_bold(category_name);
    make_string_bold(group_name);
}

// top-level category node (the categories are the tree's roots; no parent node)
ModelNode::ModelNode(wxWindow *parent_win, const wxString &text, const std::string &icon_name) : m_parent_win(parent_win), m_parent(nullptr), m_icon_name(icon_name), m_text(text)
{
    m_kind = NodeKind::Category;
    UpdateIcons();
}

// group node
ModelNode::ModelNode(ModelNode *parent, const wxString &text) : m_parent_win(parent->m_parent_win), m_parent(parent), m_icon_name("node_dot"), m_text(text)
{
    m_kind = NodeKind::Group;
    UpdateIcons();
}

#ifdef __linux__
wxIcon ModelNode::get_bitmap(const wxString& color)
#else
wxBitmap ModelNode::get_bitmap(const wxString& color)
#endif // __linux__
{
    /* It's supposed that standard size of an icon is 48px*16px for 100% scaled display.
     * So set sizes for solid_colored icons used for filament preset
     * and scale them in respect to em_unit value
     */
    const double em = em_unit(m_parent_win);
    const int icon_width    = lround(6.4 * em);
    const int icon_height   = lround(1.6 * em);

    BitmapCache bmp_cache;
    unsigned char rgb[3];
    BitmapCache::parse_color(into_u8(color), rgb);
    // there is no need to scale created solid bitmap
#ifndef __linux__
    return bmp_cache.mksolid(icon_width, icon_height, rgb, true);
#else
    wxIcon icon;
    icon.CopyFromBitmap(bmp_cache.mksolid(icon_width, icon_height, rgb, true));
    return icon;
#endif // __linux__
}

// option node
ModelNode::ModelNode(ModelNode *parent, const wxString &text, const wxString &old_value, const wxString &new_value, bool is_container, const std::string &icon_name)
    : m_parent_win(parent->m_parent_win)
    , m_parent(parent)
    , m_icon_name(icon_name)
    , m_old_color(old_value.StartsWith("#") ? old_value : "")
    , m_new_color(new_value.StartsWith("#") ? new_value : "")
    , m_text(text)
    , m_old_value(old_value)
    , m_new_value(new_value)
    , m_container(is_container)
{
    // check if old/new_value is color
    if (m_old_color.IsEmpty()) {
        if (!m_new_color.IsEmpty())
            m_old_value = _L("Undef");
    }
    else {
        m_old_color_bmp = get_bitmap(m_old_color);
        m_old_value.Clear();
    }

    if (m_new_color.IsEmpty()) {
        if (!m_old_color.IsEmpty())
            m_new_value = _L("Undef");
    }
    else {
        m_new_color_bmp = get_bitmap(m_new_color);
        m_new_value.Clear();
    }

    // "color" strings
    color_string(m_old_value, def_text_color());
    color_string(m_new_value, orange);

    UpdateIcons();
}

void ModelNode::UpdateEnabling()
{
    auto change_text_color = [](wxString& str, const std::string& clr_from, const std::string& clr_to)
    {
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
        std::string old_val = into_u8(str);
        boost::replace_all(old_val, clr_from, clr_to);
        str = from_u8(old_val);
#endif
    };

    if (!m_toggle) {
        change_text_color(m_text,      def_text_color(), grey);
        change_text_color(m_old_value, def_text_color(), grey);
        change_text_color(m_new_value, orange,grey);
    }
    else {
        change_text_color(m_text,      grey, def_text_color());
        change_text_color(m_old_value, grey, def_text_color());
        change_text_color(m_new_value, grey, orange);
    }
    // update icons for the colors
    UpdateIcons();
}

void ModelNode::UpdateIcons()
{
    // update icons for the colors, if any exists
    if (!m_old_color.IsEmpty())
        m_old_color_bmp = get_bitmap(m_toggle ? m_old_color : wxString::FromUTF8(grey.c_str()));
    if (!m_new_color.IsEmpty())
        m_new_color_bmp = get_bitmap(m_toggle ? m_new_color : wxString::FromUTF8(grey.c_str()));

    // update main icon, if any exists
    if (m_icon_name.empty())
        return;

#ifdef __linux__
    m_icon.CopyFromBitmap(create_scaled_bitmap(m_icon_name, m_parent_win, 16, !m_toggle));
#else
    m_icon = create_scaled_bitmap(m_icon_name, m_parent_win, 16, !m_toggle);
#endif //__linux__
}


// ----------------------------------------------------------------------------
//                          DiffModel
// ----------------------------------------------------------------------------

DiffModel::DiffModel(wxWindow* parent) :
    m_parent_win(parent)
{
}

wxDataViewItem DiffModel::AddPreset(Preset::Type type)
{
    m_type = type;
    return wxDataViewItem(nullptr);
}

ModelNode *DiffModel::GetCategory(wxString category_name, const std::string &category_icon_name)
{
    auto category = std::find_if(m_preset_nodes.begin(), m_preset_nodes.end(), [&category_name](const auto &category) { return category->text() == category_name; });
    if (category != m_preset_nodes.end()) return (*category).get();

    m_preset_nodes.emplace_back(std::make_unique<ModelNode>(m_parent_win, category_name, category_icon_name));
    ModelNode *category_node = m_preset_nodes.back().get();
    ItemAdded(wxDataViewItem(nullptr), wxDataViewItem((void *) category_node));

    return category_node;
}

ModelNode *DiffModel::GetGroup(ModelNode *category_node, wxString group_name)
{
    auto &groups = category_node->GetChildren();
    auto  group  = std::find_if(groups.begin(), groups.end(), [group_name](const std::unique_ptr<ModelNode> &group) { return group->text() == group_name; });
    if (group != groups.end()) return (*group).get();

    category_node->Append(std::make_unique<ModelNode>(category_node, group_name));
    ModelNode *group_node = category_node->GetChildren().back().get();
    ItemAdded(wxDataViewItem((void *) category_node), wxDataViewItem((void *) group_node));

    return group_node;
}

// category->group->option
ModelNode *DiffModel::AddOption(ModelNode *group_node, wxString option_name, wxString old_value, wxString new_value, std::string icon)
{
    group_node->Append(std::make_unique<ModelNode>(group_node, option_name, old_value, new_value, !icon.empty(), icon));
    ModelNode     *option     = group_node->GetChildren().back().get();
    wxDataViewItem group_item = wxDataViewItem((void *) group_node);
    ItemAdded(group_item, wxDataViewItem((void *) option));

    m_ctrl->Expand(group_item);
    return option;
}

// category->group->option
wxDataViewItem DiffModel::AddOption(wxString           category_name,
                                    const std::string &category_icon_name,
                                    wxString           group_name,
                                    wxString           option_name,
                                    wxString           old_value,
                                    wxString           new_value,
                                    const std::string &option_icon)
{
    auto *category = GetCategory(category_name, category_icon_name);
    auto *group    = GetGroup(category, group_name);

    return wxDataViewItem((void *) AddOption(group, option_name, old_value, new_value, option_icon));
}

// category->group->option
//                  variant1
//                  variant2
//                  variant3
wxDataViewItem DiffModel::AddVariantOption(wxString                     category_name,
                                           wxString                     group_name,
                                           wxString                     option_name,
                                           wxString                     old_value,
                                           wxString                     new_value,
                                           const std::string            category_icon_name,
                                           const std::vector<wxString> &variant_labels,
                                           const std::vector<wxString> &variant_old_values,
                                           const std::vector<wxString> &variant_new_values)
{
    markup_string(category_name, group_name, option_name);
    auto *category = GetCategory(category_name, category_icon_name);
    auto *group    = GetGroup(category, group_name);

    ModelNode     *parent = AddOption(group, option_name, old_value, new_value, "diff_multi_value");
    wxDataViewItem parent_item(parent);

    // per-variant child rows
    for (size_t i = 0; i < variant_labels.size(); i++) { AddOption(parent, variant_labels[i], variant_old_values[i], variant_new_values[i]); }

    return parent_item;
}

static void update_children(ModelNode* parent)
{
    if (parent->IsContainer()) {
        bool toggle = parent->IsToggled();
        for (std::unique_ptr<ModelNode> &child : parent->GetChildren()) {
            child->Toggle(toggle);
            child->UpdateEnabling();
            update_children(child.get());
        }
    }
}

static void update_parents(ModelNode* node)
{
    ModelNode* parent = node->GetParent();
    if (parent) {
        bool toggle = false;
        for (std::unique_ptr<ModelNode> &child : parent->GetChildren()) {
            if (child->IsToggled()) {
                toggle = true;
                break;
            }
        }
        parent->Toggle(toggle);
        parent->UpdateEnabling();
        update_parents(parent);
    }
}

void DiffModel::UpdateItemEnabling(wxDataViewItem item)
{
    assert(item.IsOk());
    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    node->UpdateEnabling();

    update_children(node);
    update_parents(node);
}

bool DiffModel::IsEnabledItem(const wxDataViewItem& item)
{
    assert(item.IsOk());
    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    return node->IsToggled();
}

void DiffModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());

    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    switch (col)
    {
    case colToggle:
        variant = node->m_toggle;
        break;
#ifdef __linux__
    case colIconText:
        variant << wxDataViewIconText(node->m_text, node->m_icon);
        break;
    case colOldValue:
        variant << wxDataViewIconText(node->m_old_value, node->m_old_color_bmp);
        break;
    case colNewValue:
        variant << wxDataViewIconText(node->m_new_value, node->m_new_color_bmp);
        break;
#else
    case colIconText:
        variant << DataViewBitmapText(node->m_text, node->m_icon);
        break;
    case colOldValue:
        variant << DataViewBitmapText(node->m_old_value, node->m_old_color_bmp);
        break;
    case colNewValue:
        variant << DataViewBitmapText(node->m_new_value, node->m_new_color_bmp);
        break;
#endif //__linux__

    default:
        wxLogError("DiffModel::GetValue: wrong column %d", col);
    }
}

bool DiffModel::GetAttr(const wxDataViewItem &item, unsigned int /*col*/, wxDataViewItemAttr &attr) const
{
    if (!item.IsOk()) return false;
    ModelNode *node = static_cast<ModelNode *>(item.GetID());

    // Shade category rows so they read as headers: a grey surface with bold text. Pick the shade by
    // the current theme so it reads correctly in both modes (light surface in light mode, dark
    // surface in dark mode). Option rows stay plain.
    if (node->kind() == ModelNode::NodeKind::Category) {
        attr.SetBackgroundColour(wxGetApp().dark_mode() ? StateColor::darkModeColorFor(ThemeColor::Grey300) : ThemeColor::Grey300);
        attr.SetBold(true);
        return true;
    } else {
        attr.SetBackgroundColour(wxGetApp().dark_mode() ? StateColor::darkModeColorFor(ThemeColor::Grey200) : ThemeColor::Grey200);
        return true;
    }
    return false;
}

bool DiffModel::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    assert(item.IsOk());

    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    switch (col)
    {
    case colToggle:
        node->m_toggle = variant.GetBool();
        return true;
#ifdef __linux__
    case colIconText: {
        wxDataViewIconText data;
        data << variant;
        node->m_icon = data.GetIcon();
        node->m_text = data.GetText();
        return true; }
    case colOldValue: {
        wxDataViewIconText data;
        data << variant;
        node->m_old_color_bmp   = data.GetIcon();
        node->m_old_value       = data.GetText();
        return true; }
    case colNewValue: {
        wxDataViewIconText data;
        data << variant;
        node->m_new_color_bmp   = data.GetIcon();
        node->m_new_value       = data.GetText();
        return true; }
#else
    case colIconText: {
        DataViewBitmapText data;
        data << variant;
        node->m_icon = data.GetBitmap();
        node->m_text = data.GetText();
        return true; }
    case colOldValue: {
        DataViewBitmapText data;
        data << variant;
        node->m_old_color_bmp   = data.GetBitmap();
        node->m_old_value       = data.GetText();
        return true; }
    case colNewValue: {
        DataViewBitmapText data;
        data << variant;
        node->m_new_color_bmp   = data.GetBitmap();
        node->m_new_value       = data.GetText();
        return true; }
#endif //__linux__
    default:
        wxLogError("DiffModel::SetValue: wrong column");
    }
    return false;
}

bool DiffModel::IsEnabled(const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());
    if (col == colToggle)
        return true;

    // disable unchecked nodes
    return (static_cast<ModelNode*>(item.GetID()))->IsToggled();
}

wxDataViewItem DiffModel::GetParent(const wxDataViewItem& item) const
{
    // the invisible root node has no parent
    if (!item.IsOk())
        return wxDataViewItem(nullptr);

    ModelNode* node = static_cast<ModelNode*>(item.GetID());

    if (node->IsRoot())
        return wxDataViewItem(nullptr);

    return wxDataViewItem((void*)node->GetParent());
}

bool DiffModel::IsContainer(const wxDataViewItem& item) const
{
    // the invisble root node can have children
    if (!item.IsOk())
        return true;

    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    return node->IsContainer();
}

unsigned int DiffModel::GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const
{
    ModelNode* parent_node = (ModelNode*)parent.GetID();

    const ModelNodePtrArray& children = parent_node ? parent_node->GetChildren() : m_preset_nodes;
    for (const std::unique_ptr<ModelNode>& child : children)
        array.Add(wxDataViewItem((void*)child.get()));

    return array.size();
}


wxString DiffModel::GetColumnType(unsigned int col) const
{
    switch (col)
    {
    case colToggle:
        return "bool";
    case colIconText:
    case colOldValue:
    case colNewValue:
    default:
        return "DataViewBitmapText";//"string";
    }
}

static void rescale_children(ModelNode* parent)
{
    if (parent->IsContainer()) {
        for (std::unique_ptr<ModelNode> &child : parent->GetChildren()) {
            child->UpdateIcons();
            rescale_children(child.get());
        }
    }
}

void DiffModel::Rescale()
{
    for (std::unique_ptr<ModelNode> &node : m_preset_nodes) {
        node->UpdateIcons();
        rescale_children(node.get());
    }
}

wxDataViewItem DiffModel::Delete(const wxDataViewItem& item)
{
    auto ret_item = wxDataViewItem(nullptr);
    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    if (!node)      // happens if item.IsOk()==false
        return ret_item;

    // first remove the node from the parent's array of children;
    // NOTE: m_preset_nodes is only a vector of _pointers_
    //       thus removing the node from it doesn't result in freeing it
    ModelNodePtrArray& children = node->GetChildren();
    // Delete all children
    while (!children.empty())
        Delete(wxDataViewItem(children.back().get()));

    auto node_parent = node->GetParent();
    wxDataViewItem parent(node_parent);

    ModelNodePtrArray& parents_children = node_parent ? node_parent->GetChildren() : m_preset_nodes;
    auto it = find_if(parents_children.begin(), parents_children.end(),
                      [node](std::unique_ptr<ModelNode>& child) { return child.get() == node; });
    assert(it != parents_children.end());
    it = parents_children.erase(it);

    if (it != parents_children.end())
        ret_item = wxDataViewItem(it->get());

    // set m_container to FALSE if parent has no child
    if (node_parent) {
#ifndef __WXGTK__
        if (node_parent->GetChildCount() == 0)
            node_parent->m_container = false;
#endif //__WXGTK__
        ret_item = parent;
    }

    // notify control
    ItemDeleted(parent, item);
    return ret_item;
}

void DiffModel::Clear()
{
    while (!m_preset_nodes.empty())
        Delete(wxDataViewItem(m_preset_nodes.back().get()));
}


static std::string get_pure_opt_key(std::string opt_key)
{
    int pos = opt_key.find("#");
    if (pos > 0)
        boost::erase_tail(opt_key, opt_key.size() - pos);
    return opt_key;
}

// ----------------------------------------------------------------------------
//                  DiffViewCtrl
// ----------------------------------------------------------------------------

DiffViewCtrl::DiffViewCtrl(wxWindow *parent, wxSize size)
    : wxDataViewCtrl(parent,
                     wxID_ANY,
                     wxDefaultPosition,
                     size,
                     // wxBORDER_NONE: the visible outline is a Grey400 wrapper panel behind a 1px
                     // inset, which wxBORDER_SIMPLE (a system-coloured, non-themeable line) would
                     // otherwise double up on. wxDV_HORIZ_RULES still draws the inner row rules.
                     wxDV_VARIABLE_LINE_HEIGHT | wxDV_HORIZ_RULES | wxBORDER_NONE)
    , m_em_unit(em_unit(parent))
{
    ApplyDarkUI();

    model = new DiffModel(parent);
    this->AssociateModel(model);
    model->SetAssociatedControl(this);

    this->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &DiffViewCtrl::context_menu, this);
    this->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,    &DiffViewCtrl::context_menu, this);
    this->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &DiffViewCtrl::item_value_changed, this);
}

DiffViewCtrl::~DiffViewCtrl() {
    this->AssociateModel(nullptr);
    delete model;
}

void DiffViewCtrl::AppendBmpTextColumn(const wxString& label, unsigned model_column, int width, bool set_expander/* = false*/)
{
    m_columns_width.emplace(this->GetColumnCount(), width);
#ifdef __linux__
    wxDataViewIconTextRenderer* rd = new wxDataViewIconTextRenderer();
#ifdef SUPPORTS_MARKUP
    rd->EnableMarkup(true);
#endif
#else
    wxDataViewRenderer *rd = new BitmapTextRenderer(true, wxDATAVIEW_CELL_INERT);
#endif //__linux__
    // Left-align + vertically center the cell text (lines up with the dark header labels), and
    // ellipsize overlong values at the end ("abc…") rather than the default middle ("a…c").
    rd->SetAlignment(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    rd->EnableEllipsize(wxELLIPSIZE_END);

    // Column alignment drives the HEADER TITLE only (generic: headerctrlg m_labelAlignment; MSW:
    // HDF_CENTER) - cell content keeps the renderer's left alignment set above. The previous
    // wxALIGN_TOP is numerically wxALIGN_NOT, i.e. it silently meant "left".
    wxDataViewColumn *column = new wxDataViewColumn(label, rd, model_column, width * m_em_unit, wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);
    this->AppendColumn(column);
    if (set_expander)
        this->SetExpanderColumn(column);

}

void DiffViewCtrl::AppendToggleColumn_(const wxString& label, unsigned model_column, int width)
{
    m_columns_width.emplace(this->GetColumnCount(), width);
    AppendToggleColumn(label, model_column, wxDATAVIEW_CELL_ACTIVATABLE, width * m_em_unit);
}

void DiffViewCtrl::SetHeaderFont(const wxFont &font)
{
    m_header_font = font;
    ApplyDarkUI();
}

void DiffViewCtrl::SetHeaderHeight(int height)
{
    m_header_height = height;
    ApplyDarkUI();
}

void DiffViewCtrl::ApplyDarkUI()
{
#ifdef __WINDOWS__
    // UpdateDVCDarkUI() owns the header wxItemAttr on MSW (it also sets the dark text colour), so
    // the font has to be handed to it rather than applied separately.
    wxGetApp().UpdateDVCDarkUI(this, /*highlited*/ false, m_header_font.IsOk() ? &m_header_font : nullptr);
    // ...and it unconditionally forces wxBORDER_SIMPLE back on, which would draw a system-coloured
    // line inside the Grey400 frame. Strip it again.
    if (GetBorder() == wxBORDER_SIMPLE) SetWindowStyle(GetWindowStyle() & ~wxBORDER_SIMPLE);
#else
    // Elsewhere UpdateDVCDarkUI() is a no-op, so apply the header font directly.
    if (m_header_font.IsOk()) {
        wxItemAttr attr;
        attr.SetFont(m_header_font);
        SetHeaderAttr(attr);
    }
#endif

#ifdef wxHAS_GENERIC_DATAVIEWCTRL
    // The header sits in the control's sizer at proportion 0, so its height is its effective min
    // size; a native backend offers no equivalent hook, hence the generic-only guard.
    if (m_header_height > 0)
        if (wxHeaderCtrl *header = GenericGetHeader()) header->SetMinSize(wxSize(-1, FromDIP(m_header_height)));
#endif

    Layout();
}

void DiffViewCtrl::Rescale(int em /*= 0*/)
{
    // A theme or DPI pass rewrites the header attr, so restore our font afterwards.
    ApplyDarkUI();

    if (em > 0) {
        for (auto item : m_columns_width)
            GetColumn(item.first)->SetWidth(item.second * em);
        m_em_unit = em;
    }

    model->Rescale();
    Refresh();
}

void DiffViewCtrl::Append(  const std::string& opt_key, Preset::Type type,
                            wxString category_name, wxString group_name, wxString option_name,
                            wxString old_value, wxString new_value, const std::string category_icon_name)
{
    ItemData item_data = { opt_key, option_name, old_value, new_value, type };

    wxString old_val = get_short_string(item_data.old_val);
    wxString new_val = get_short_string(item_data.new_val);
    if (old_val != item_data.old_val || new_val != item_data.new_val)
        item_data.is_long = true;

    m_items_map.emplace(model->AddOption(category_name, category_icon_name, group_name, option_name, old_val, new_val, {}), item_data);
}

void DiffViewCtrl::AppendVariant(const std::string           &opt_key,
                                 Preset::Type                 type,
                                 wxString                     category_name,
                                 wxString                     group_name,
                                 wxString                     option_name,
                                 wxString                     old_value,
                                 wxString                     new_value,
                                 const std::string            category_icon_name,
                                 const std::vector<wxString> &variant_labels,
                                 const std::vector<wxString> &variant_old_values,
                                 const std::vector<wxString> &variant_new_values)
{
    ItemData item_data = {opt_key, option_name, old_value, new_value, type};

    wxString old_val = get_short_string(item_data.old_val);
    wxString new_val = get_short_string(item_data.new_val);
    if (old_val != item_data.old_val || new_val != item_data.new_val) item_data.is_long = true;

    std::vector<wxString> child_old, child_new;
    child_old.reserve(variant_old_values.size());
    child_new.reserve(variant_new_values.size());
    for (const wxString &v : variant_old_values) child_old.push_back(get_short_string(v));
    for (const wxString &v : variant_new_values) child_new.push_back(get_short_string(v));

    m_items_map.emplace(model->AddVariantOption(category_name, group_name, option_name, old_val, new_val, category_icon_name, variant_labels, child_old, child_new), item_data);
}

void DiffViewCtrl::Clear()
{
    model->Clear();
    m_items_map.clear();
}

wxString DiffViewCtrl::get_short_string(wxString full_string)
{
    if (full_string.IsEmpty() || full_string.StartsWith("#")) return full_string;

    const int n_pos = full_string.Find("\n");
    if (n_pos == wxNOT_FOUND) return full_string;

    m_has_long_strings = true;
    full_string.Truncate(n_pos);
    return full_string + dots;
}

void DiffViewCtrl::context_menu(wxDataViewEvent& event)
{
    if (!m_has_long_strings)
        return;

    wxDataViewItem item = event.GetItem();
    if (!item) {
        wxPoint mouse_pos = wxGetMousePosition() - this->GetScreenPosition();
        wxDataViewColumn* col = nullptr;
        this->HitTest(mouse_pos, item, col);

        if (!item)
            item = this->GetSelection();

        if (!item)
            return;
    }

    auto it = m_items_map.find(item);
    if (it == m_items_map.end() || !it->second.is_long)
        return;

    size_t column_cnt = this->GetColumnCount();
    const wxString old_value_header = this->GetColumn(column_cnt - 2)->GetTitle();
    const wxString new_value_header = this->GetColumn(column_cnt - 1)->GetTitle();
    FullCompareDialog(it->second.opt_name, it->second.old_val, it->second.new_val,
                      old_value_header, new_value_header).ShowModal();

#ifdef __WXOSX__
    wxWindow* parent = this->GetParent();
    if (parent && parent->IsShown()) {
        // if this dialog is shown it have to be Hide and show again to be placed on the very Top of windows
        parent->Hide();
        parent->Show();
    }
#endif // __WXOSX__
}

void DiffViewCtrl::item_value_changed(wxDataViewEvent& event)
{
    if (event.GetColumn() != DiffModel::colToggle)
        return;

    wxDataViewItem item = event.GetItem();

    model->UpdateItemEnabling(item);
    Refresh();

    // update an enabling of the "save/move" buttons
    m_empty_selection = selected_options().empty();
}

bool DiffViewCtrl::has_unselected_options()
{
    for (auto item : m_items_map)
        if (!model->IsEnabledItem(item.first))
            return true;

    return false;
}

std::vector<std::string> DiffViewCtrl::options(Preset::Type type, bool selected)
{
    std::vector<std::string> ret;

    for (auto item : m_items_map) {
        if (item.second.type == type && model->IsEnabledItem(item.first) == selected)
            ret.emplace_back(get_pure_opt_key(item.second.opt_key));
    }

    return ret;
}

std::vector<std::string> DiffViewCtrl::selected_options()
{
    std::vector<std::string> ret;

    for (auto item : m_items_map)
        if (model->IsEnabledItem(item.first))
            ret.emplace_back(get_pure_opt_key(item.second.opt_key));

    return ret;
}


//------------------------------------------
//          UnsavedChangesDialog
//------------------------------------------

static std::string none{"none"};
#define UNSAVE_CHANGE_DIALOG_SCROLL_WINDOW_SIZE wxSize(FromDIP(490), FromDIP(374))
#define UNSAVE_CHANGE_DIALOG_ACTION_LINE_SIZE wxSize(FromDIP(490), FromDIP(-1))
#define UNSAVE_CHANGE_DIALOG_FIRST_VALUE_WIDTH FromDIP(190)
#define UNSAVE_CHANGE_DIALOG_VALUE_WIDTH FromDIP(150)
#define UNSAVE_CHANGE_DIALOG_ITEM_HEIGHT FromDIP(24)
#define UNSAVE_CHANGE_DIALOG_BUTTON_SIZE wxSize(FromDIP(70), FromDIP(24))

#define THUMB_COLOR wxColor(196, 196, 196)
#define GREY900 wxColour(38, 46, 48)
#define GREY700 wxColour(107,107,107)
#define GREY400 wxColour(206,206,206)
#define GREY300 wxColour(238,238,238)
#define GREY200 wxColour(248,248,248)


UnsavedChangesDialog::UnsavedChangesDialog(const wxString &caption, const wxString &header, const std::string &app_config_key, int act_buttons)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                caption + ": " + (caption == _L("Creating a new project") ? _L("Discard or Use Modified Value") :
                              caption == _L("Load project")           ? _L("Save or Discard Modified Value") :
                                                                        _L("Unsaved Changes")),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
    , m_app_config_key(app_config_key)
    , m_buttons(act_buttons)
{
    if(caption == _L("Creating a new project"))
        m_buttons &= ~ActionButtons::SAVE;
    build(Preset::TYPE_INVALID, nullptr, "", header);
    this->CenterOnScreen();
    wxGetApp().UpdateDlgDarkUI(this);
}

UnsavedChangesDialog::UnsavedChangesDialog(const wxString &caption, const wxString &header, DynamicConfig *config, int from, int to, bool left_to_right, NozzleVolumeType nozzle)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                caption,
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
    , m_buttons(ActionButtons::SAVE | ActionButtons::DONT_SAVE)
{
    SyncExtruderParams params { config, from, to, left_to_right, nozzle };
    build(Preset::TYPE_PRINT, reinterpret_cast<PresetCollection*>(&params), "SyncExtruderParams", header);
    this->CenterOnScreen();
    wxGetApp().UpdateDlgDarkUI(this);
}

UnsavedChangesDialog::UnsavedChangesDialog(Preset::Type type, PresetCollection *dependent_presets, const std::string &new_selected_preset, bool no_transfer)
    : m_new_selected_preset_name(new_selected_preset)
    , DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                (!no_transfer && !new_selected_preset.empty() && dependent_presets) ?
                    dependent_presets->type() == Preset::Type::TYPE_PRINT    ? _L("Use Modified Value of Process Preset") :
                    dependent_presets->type() == Preset::Type::TYPE_FILAMENT ? _L("Use Modified Value of Filament Preset") :
                    dependent_presets->type() == Preset::Type::TYPE_PRINTER  ? _L("Use Modified Value of Printer Preset") :
                                                                               _L("Save or Discard Modified Value") :
                    _L("Save or Discard Modified Value"),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    if (new_selected_preset.empty() || no_transfer)
        m_buttons &= ~ActionButtons::TRANSFER;
    if (dependent_presets && (dependent_presets->type() == Preset::Type::TYPE_PRINT || !dependent_presets->find_preset(new_selected_preset)))
        m_buttons &= ~ActionButtons::SAVE;
    build(type, dependent_presets, new_selected_preset);
    this->CenterOnScreen();
    wxGetApp().UpdateDlgDarkUI(this);
}


inline int UnsavedChangesDialog::ShowModal()
{
    auto choise_key = "save_preset_choise";
    auto choise     = wxGetApp().app_config->get(choise_key);
    long result = 0;
    if ((m_buttons & REMEMBER_CHOISE) && !choise.empty() && wxString(choise).ToLong(&result) && (1 << result) & (m_buttons | DONT_SAVE)) {
        m_exit_action = Action(result);
        return 0;
    }
    int r = wxDialog::ShowModal();
    if (r != wxID_CANCEL && dynamic_cast<::CheckBox*>(FindWindowById(wxID_APPLY))->GetValue()) {
        wxGetApp().app_config->set(choise_key, std::to_string(int(m_exit_action)));
    }
    return r;
}

void UnsavedChangesDialog::build(Preset::Type type, PresetCollection *dependent_presets, const std::string &new_selected_preset, const wxString &header)
{
    SetBackgroundColour(*wxWHITE);
    // icon
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_top_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_top_line->SetBackgroundColour(wxColour(166, 169, 170));

    m_sizer_main->Add(m_top_line, 0, wxEXPAND, 0);

    m_sizer_main->Add(0, 0, 0, wxTOP, 20);

    m_action_line = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, UNSAVE_CHANGE_DIALOG_ACTION_LINE_SIZE, 0);
    m_action_line->SetFont(::Label::Body_13);
    m_action_line->SetForegroundColour(GREY900);
    m_action_line->Wrap(UNSAVE_CHANGE_DIALOG_ACTION_LINE_SIZE.GetWidth());
    m_sizer_main->Add(m_action_line, 0, wxLEFT | wxRIGHT, 20);

    m_sizer_main->Add(0, 0, 0, wxTOP, 12);

    SyncExtruderParams *params = nullptr;
    if (new_selected_preset == "SyncExtruderParams") {
        params = reinterpret_cast<SyncExtruderParams *>(dependent_presets);
        dependent_presets = nullptr;
    }

    if (params || (dependent_presets &&
        ((dependent_presets->type() != Preset::Type::TYPE_FILAMENT && dependent_presets->type() != Preset::Type::TYPE_PRINTER) || !dependent_presets->find_preset(new_selected_preset)))) {
        m_panel_tab = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(UNSAVE_CHANGE_DIALOG_SCROLL_WINDOW_SIZE.x, -1), wxTAB_TRAVERSAL);
        m_panel_tab->SetBackgroundColour(GREY200);
        wxBoxSizer *m_sizer_tab = new wxBoxSizer(wxVERTICAL);

        m_table_top = new wxPanel(m_panel_tab, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
        m_table_top->SetBackgroundColour(wxColour(107, 107, 107));

        wxBoxSizer *m_sizer_top = new wxBoxSizer(wxHORIZONTAL);

        // m_sizer_top->Add(0, 0, 0, wxLEFT, UNSAVE_CHANGE_DIALOG_FIRST_VALUE_WIDTH);
        auto        m_panel_temp   = new wxPanel(m_table_top, wxID_ANY, wxDefaultPosition, wxSize(UNSAVE_CHANGE_DIALOG_FIRST_VALUE_WIDTH, -1), wxTAB_TRAVERSAL);
        wxBoxSizer *top_title_temp_v = new wxBoxSizer(wxVERTICAL);
        top_title_temp_v->SetMinSize(wxSize(UNSAVE_CHANGE_DIALOG_VALUE_WIDTH, -1));
        wxBoxSizer *top_title_temp_h = new wxBoxSizer(wxHORIZONTAL);
        static_temp_title            = new wxStaticText(m_panel_temp, wxID_ANY, _L("Settings"), wxDefaultPosition, wxDefaultSize, 0);
        static_temp_title->SetFont(::Label::Body_13);
        static_temp_title->Wrap(-1);
        static_temp_title->SetForegroundColour(*wxWHITE);
        top_title_temp_h->Add(static_temp_title, 0, wxALIGN_CENTER | wxBOTTOM | wxTOP, 5);
        top_title_temp_v->Add(top_title_temp_h, 1, wxALIGN_CENTER, 0);
        m_panel_temp->SetSizer(top_title_temp_v);
        m_panel_temp->Layout();
        m_sizer_top->Add(m_panel_temp, 1, wxALIGN_CENTER, 0);

        title_block_middle = new wxPanel(m_table_top, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
        title_block_middle->SetBackgroundColour(wxColour(172, 172, 172));

        m_sizer_top->Add(title_block_middle, 0, wxBOTTOM | wxEXPAND | wxTOP, 2);
        auto m_panel_oldv = new wxPanel( m_table_top, wxID_ANY, wxDefaultPosition, wxSize(UNSAVE_CHANGE_DIALOG_VALUE_WIDTH,-1), wxTAB_TRAVERSAL );
        wxBoxSizer *top_title_oldv = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer *top_title_oldv_h = new wxBoxSizer(wxHORIZONTAL);

        std::string ucd_pt = wxGetApp().preset_bundle->printers.get_edited_preset().get_printer_type(wxGetApp().preset_bundle);
        static_oldv_title = new wxStaticText(m_panel_oldv, wxID_ANY, params ? _L(DevPrinterConfigUtil::get_toolhead_display_name(ucd_pt, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::SentenceCase)) + ": " + get_nozzle_volume_type_name(params->nozzle) : _L("Preset(Old)"), wxDefaultPosition, wxDefaultSize, 0);
        static_oldv_title->SetFont(::Label::Body_13);
        static_oldv_title->Wrap(-1);
        static_oldv_title->SetForegroundColour(params && params->left_to_right ? wxGetApp().get_label_clr_modified() : *wxWHITE);
        top_title_oldv_h->Add(static_oldv_title, 0, wxALIGN_CENTER | wxBOTTOM | wxTOP, 5);
        top_title_oldv->Add(top_title_oldv_h, 1, wxALIGN_CENTER, 0);
        m_panel_oldv->SetSizer(top_title_oldv);
        m_panel_oldv->Layout();
        m_sizer_top->Add(m_panel_oldv, 0, wxALIGN_CENTER, 0);

        title_block_right = new wxPanel(m_table_top, wxID_ANY, wxDefaultPosition, wxSize(1, -1), wxTAB_TRAVERSAL);
        title_block_right->SetBackgroundColour(wxColour(172, 172, 172));

        m_sizer_top->Add(title_block_right, 0, wxBOTTOM | wxEXPAND | wxTOP, 2);

        auto m_panel_newv = new wxPanel( m_table_top, wxID_ANY, wxDefaultPosition, wxSize( UNSAVE_CHANGE_DIALOG_VALUE_WIDTH,-1 ), wxTAB_TRAVERSAL );
        wxBoxSizer *top_title_newv = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer *top_title_newv_h = new wxBoxSizer(wxHORIZONTAL);

        static_newv_title = new wxStaticText(m_panel_newv, wxID_ANY, params ? _L(DevPrinterConfigUtil::get_toolhead_display_name(ucd_pt, MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::SentenceCase)) + ": " + get_nozzle_volume_type_name(params->nozzle) : _L("Modified Value(New)"),
                                             wxDefaultPosition, wxDefaultSize, 0);
        static_newv_title->SetFont(::Label::Body_13);
        static_newv_title->Wrap(-1);
        static_newv_title->SetForegroundColour(params && !params->left_to_right ? wxGetApp().get_label_clr_modified() : *wxWHITE);

        top_title_newv_h->Add(static_newv_title, 0, wxALIGN_CENTER | wxBOTTOM | wxTOP, 5);

        top_title_newv->Add(top_title_newv_h, 1, wxALIGN_CENTER, 0);

        m_panel_newv->SetSizer(top_title_newv);
        m_panel_newv->Layout();
        m_sizer_top->Add(m_panel_newv, 0, wxALIGN_CENTER, 0);
        // m_sizer_top->Add(top_title_newv, 1, wxALIGN_CENTER, 0);

        m_table_top->SetSizer(m_sizer_top);
        m_table_top->Layout();
        m_sizer_top->Fit(m_table_top);
        m_sizer_tab->Add(m_table_top, 1, 0, 0);

        m_scrolledWindow = new wxScrolledWindow(m_panel_tab, wxID_ANY, wxDefaultPosition, UNSAVE_CHANGE_DIALOG_SCROLL_WINDOW_SIZE,  wxNO_BORDER|wxVSCROLL);
        m_scrolledWindow->SetScrollRate(0, 5);
        m_scrolledWindow->SetBackgroundColour(GREY200);
        m_sizer_bottom = new wxBoxSizer(wxVERTICAL);
        m_sizer_bottom->Add(m_scrolledWindow, 1, wxEXPAND, 0);
        m_sizer_tab->Add(m_sizer_bottom, 0, wxEXPAND, 0);

        m_panel_tab->SetSizer(m_sizer_tab);
        m_panel_tab->Layout();
        m_sizer_tab->Fit(m_panel_tab);
        m_sizer_main->Add(m_panel_tab, 0, wxEXPAND | wxLEFT | wxRIGHT, 20);

        m_sizer_main->Add(0, 0, 0, wxTOP, 9);
    }
   /* m_info_line = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 44), 0);
     m_info_line->SetFont(::Label::Body_13);
     m_info_line->Wrap(-1);
     m_info_line->SetForegroundColour(wxColour(255, 111, 0));
     m_sizer_main->Add(m_info_line, 0, wxLEFT | wxRIGHT, 20);*/

    wxBoxSizer *m_sizer_button = new wxBoxSizer(wxHORIZONTAL);

    auto checkbox_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto checkbox       = new ::CheckBox(this, wxID_APPLY);
    checkbox_sizer->Add(checkbox, 0, wxALL | wxALIGN_CENTER, FromDIP(2));

    auto checkbox_text = new wxStaticText(this, wxID_ANY, _L("Remember my choice."), wxDefaultPosition, wxDefaultSize, 0);
    checkbox_sizer->Add(checkbox_text, 0, wxALL | wxALIGN_CENTER, FromDIP(2));
    checkbox_text->SetFont(::Label::Body_13);
    checkbox_text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3D")));
    m_sizer_button->Add(checkbox_sizer, 0, wxLEFT, FromDIP(22));
    checkbox_sizer->Show(bool(m_buttons & REMEMBER_CHOISE));
    m_sizer_button->Add(0, 0, 1, 0, 0);

     // Add Buttons
    wxFont      btn_font = this->GetFont().Scaled(1.4f);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    auto add_btn = [this, m_sizer_button, btn_font, dependent_presets, btn_bg_green](Button **btn, int &btn_id, Action close_act, const wxString &label,
                                                                              bool focus, bool process_enable = true) {
        *btn = new Button(this, _L(label));

        if (focus) {
            (*btn)->SetBackgroundColor(btn_bg_green);
            (*btn)->SetBorderColor(wxColour(0, 174, 66));
            (*btn)->SetTextColor(wxColour("#FFFFFE"));
        } else {
            (*btn)->SetTextColor(wxColour(107, 107, 107));
        }

        (*btn)->SetMinSize(UNSAVE_CHANGE_DIALOG_BUTTON_SIZE);
        (*btn)->SetCornerRadius(FromDIP(12));

        (*btn)->Bind(wxEVT_BUTTON, [this, close_act, dependent_presets](wxEvent &) {
            bool save_names_and_types = close_act == Action::Save || (close_act == Action::Transfer && ActionButtons::KEEP & m_buttons);
            if (save_names_and_types && !save(dependent_presets, close_act == Action::Save)) return;
            close(close_act);
        });

        // if (process_enable) (*btn)->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent &evt) { evt.Enable(m_tree->has_selection()); });
        (*btn)->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
            show_info_line(Action::Undef);
            e.Skip();
        });

        m_sizer_button->Add(*btn, 0, wxLEFT, 5);
    };

    bool is_copy = new_selected_preset == "SyncExtruderParams";
    // "Save" button
    if (ActionButtons::SAVE & m_buttons) add_btn(&m_save_btn, m_save_btn_id, is_copy ? Action::Transfer : Action::Save, is_copy ? _L("Yes") : _L("Save"), true);

    { // "Don't save" / "Discard" button
        std::string btn_icon  = (ActionButtons::DONT_SAVE & m_buttons) ? "" : (dependent_presets || (ActionButtons::KEEP & m_buttons)) ? "blank_16" : "exit";
        wxString    btn_label = (ActionButtons::TRANSFER & m_buttons) ? _L("Discard Modified Value") : is_copy ? _L("No") : _L("Don't save");
        add_btn(&m_discard_btn, m_continue_btn_id, Action::Discard, btn_label, false);
    }

    // "Transfer" / "Keep" button
    if (ActionButtons::TRANSFER & m_buttons) {
        const PresetCollection* switched_presets = type == Preset::TYPE_INVALID ? nullptr : wxGetApp().get_tab(type)->get_presets();
        if (dependent_presets && switched_presets && (type == dependent_presets->type() ?
            dependent_presets->get_edited_preset().printer_technology() == dependent_presets->find_preset(new_selected_preset)->printer_technology() :
            switched_presets->get_edited_preset().printer_technology() == switched_presets->find_preset(new_selected_preset)->printer_technology()))
            add_btn(&m_transfer_btn, m_move_btn_id, Action::Transfer, /*switched_presets->get_edited_preset().name == new_selected_preset ? */_L("Use Modified Value"), true);
    }
    if (!m_transfer_btn && (ActionButtons::KEEP & m_buttons))
        add_btn(&m_transfer_btn, m_move_btn_id, Action::Transfer, _L("Use Modified Value"), true);

    /* ScalableButton *cancel_btn = new ScalableButton(this, wxID_CANCEL, "cross", _L("Cancel"), wxDefaultSize, wxDefaultPosition, wxBORDER_DEFAULT, true, 24);
      buttons->Add(cancel_btn, 1, wxLEFT | wxRIGHT, 5);
      cancel_btn->SetFont(btn_font);*/
    /* m_cancel_btn = new Button(this, _L("Cancel"));
     m_cancel_btn->SetTextColor(wxColour(107, 107, 107));
     m_cancel_btn->Bind(wxEVT_LEFT_DOWN, [this](wxEvent &) { this->EndModal(wxID_CANCEL); });
     m_cancel_btn->SetMinSize(UNSAVE_CHANGE_DIALOG_BUTTON_SIZE);
     m_cancel_btn->SetCornerRadius(12);
     m_sizer_button->Add(m_cancel_btn, 0, wxLEFT, 5);
     m_sizer_button->Add(0,0,0,wxRIGHT,20);*/

    if (!m_app_config_key.empty()) {}

    m_sizer_button->Add(0, 0, 0, wxRIGHT, 20);
    m_sizer_main->Add(m_sizer_button, 0, wxEXPAND | wxTOP, 6);
    m_sizer_main->Add(0, 0, 1, wxTOP, 18);

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Centre(wxBOTH);

    if (params) {
        if (params->left_to_right)
            update_tree(type, params->config, params->from, params->to);
        else
            update_tree(type, params->config, params->to, params->from);
        m_action_line->SetLabel(header);
        m_action_line->Wrap(UNSAVE_CHANGE_DIALOG_SCROLL_WINDOW_SIZE.x);
        update_list(params);
    } else {
        update(type, dependent_presets, new_selected_preset, header);
    }
    //SetSizer(topSizer);
    //topSizer->SetSizeHints(this);

    show_info_line(Action::Undef);
}

void UnsavedChangesDialog::show_info_line(Action action, std::string preset_name)
{
    return;
    if (action == Action::Undef && !m_has_long_strings)
        m_info_line->SetLabel(wxEmptyString);
    else {
        wxString text;
        if (action == Action::Undef)
            text = _L("Click the right mouse button to display the full text.");
        else if (action == Action::Discard)
            text = ActionButtons::DONT_SAVE & m_buttons ? _L("All changes will not be saved") :_L("All changes will be discarded.");
        else {
            if (preset_name.empty())
                text = action == Action::Save           ? _L("Save the selected options.") :
                       ActionButtons::KEEP & m_buttons  ? _L("Keep the selected options.") :
                                                          _L("Transfer the selected options to the newly selected preset.");
            else
                text = format_wxstr(
                    action == Action::Save ?
                        _L("Save the selected options to preset \n\"%1%\".") :
                        _L("Transfer the selected options to the newly selected preset \n\"%1%\"."),
                    preset_name);
            //text += "\n" + _L("Unselected options will be reverted.");
        }
        m_info_line->SetLabel(text);
        m_info_line->Show();
    }

    //Layout();
    //Refresh();
}

void UnsavedChangesDialog::close(Action action)
{
    if (action == Action::Transfer) {
        check_option_valid();
    }
    m_exit_action = action;
    wxDialog::EndModal(wxID_CLOSE);
}

bool UnsavedChangesDialog::save(PresetCollection* dependent_presets, bool show_save_preset_dialog/* = true*/)
{
    names_and_types.clear();

    // save one preset
    if (dependent_presets) {
        const Preset& preset = dependent_presets->get_edited_preset();
        std::string name = preset.name;

        // for system/default/external presets we should take an edited name
        //BBS: add project embedded preset logic and refine is_external
        bool save_to_project = false;
        if (preset.is_system || preset.is_default) {
        //if (preset.is_system || preset.is_default || preset.is_external) {
            SavePresetDialog save_dlg(this, preset.type);
            if (save_dlg.ShowModal() != wxID_OK) {
                m_exit_action = Action::Discard;
                return false;
            }
            name = save_dlg.get_name();
            save_to_project = save_dlg.get_save_to_project_selection(preset.type);
        }

        //BBS: add project embedded preset relate logic
        PresetData preset_data(name, preset.type, save_to_project);
        names_and_types.emplace_back(preset_data);
        //names_and_types.emplace_back(make_pair(name, preset.type));
    }
    // save all presets
    else
    {
        std::vector<Preset::Type> types_for_save;

        PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();

        for (Tab* tab : wxGetApp().tabs_list)
            if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty()) {
                const Preset& preset = tab->get_presets()->get_edited_preset();
                //BBS: add project embedded preset logic and refine is_external
                if (preset.is_system || preset.is_default)
                //if (preset.is_system || preset.is_default || preset.is_external)
                    types_for_save.emplace_back(preset.type);

                //BBS: add project embedded preset relate logic
                PresetData preset_data(preset.name, preset.type, preset.is_project_embedded);
                names_and_types.emplace_back(preset_data);
                //names_and_types.emplace_back(make_pair(preset.name, preset.type));
            }


        if (show_save_preset_dialog && !types_for_save.empty()) {
            SavePresetDialog save_dlg(this, types_for_save);
            if (save_dlg.ShowModal() != wxID_OK) {
                m_exit_action = Action::Discard;
                return false;
            }

            //BBS: add project embedded preset relate logic
            for (PresetData& nt : names_and_types) {
                const std::string& name = save_dlg.get_name(nt.type);
                if (!name.empty())
                    nt.name = name;
                nt.save_to_project = save_dlg.get_save_to_project_selection(nt.type);
            }
            //for (std::pair<std::string, Preset::Type>& nt : names_and_types) {
            //    const std::string& name = save_dlg.get_name(nt.second);
            //    if (!name.empty())
            //        nt.first = name;
            //}
        }
    }
    return true;
}

wxString get_string_from_enum(const std::string& opt_key, const DynamicPrintConfig& config, bool is_infill = false, int idx = -1)
{
    const ConfigOptionDef& def = config.def()->options.at(opt_key);
    const std::vector<std::string>& names = def.enum_labels;//ConfigOptionEnum<T>::get_enum_names();
    int val = 0;

    if (idx >= 0)
        val = dynamic_cast<const ConfigOptionInts*>(config.option(opt_key))->get_at(idx);
    else
        val = config.option(opt_key)->getInt();

    // Each infill doesn't use all list of infill declared in PrintConfig.hpp.
    // So we should "convert" val to the correct one
    if (is_infill) {
        for (auto key_val : *def.enum_keys_map)
            if (int(key_val.second) == val) {
                auto it = std::find(def.enum_values.begin(), def.enum_values.end(), key_val.first);
                if (it == def.enum_values.end())
                    return "";
                return from_u8(_utf8(names[it - def.enum_values.begin()]));
            }
        return _L("Undef");
    }
    return from_u8(_utf8(names[val]));
}

// BBS
#if 0
static size_t get_id_from_opt_key(std::string opt_key)
{
    int pos = opt_key.find("#");
    if (pos > 0) {
        boost::erase_head(opt_key, pos + 1);
        return static_cast<size_t>(atoi(opt_key.c_str()));
    }
    return 0;
}
#endif

static wxString get_full_label(std::string opt_key, const DynamicPrintConfig& config)
{
    opt_key = get_pure_opt_key(opt_key);
    auto option = config.option(opt_key);

    if (!option || option->is_nil())
        return _L("N/A");

    const ConfigOptionDef* opt = config.def()->get(opt_key);
    return opt->full_label.empty() ? opt->label : opt->full_label;
}

static wxString get_string_value(std::string opt_key, const DynamicPrintConfig& config)
{
    int orig_opt_idx = -1;
    int opt_idx = -1;
    int pos = opt_key.find("#");
    std::string temp_str = opt_key;
    if (pos > 0) {
        boost::erase_head(temp_str, pos + 1);
        orig_opt_idx = static_cast<size_t>(atoi(temp_str.c_str()));
    }
    opt_idx = orig_opt_idx >= 0 ? orig_opt_idx : 0;
    opt_key = get_pure_opt_key(opt_key);
    auto option = config.option(opt_key);
    if (!option) {
        return _L("N/A");
    }
    auto opt_vector = dynamic_cast<const ConfigOptionVectorBase *>(option);

    if (option->is_scalar() && config.option(opt_key)->is_nil() ||
        option->is_vector() && opt_vector && opt_idx >= 0 && opt_idx < opt_vector->size() && opt_vector->is_nil(opt_idx))
        return _L("N/A");

    wxString out;

    const ConfigOptionDef* opt = config.def()->get(opt_key);
    bool is_nullable = opt->nullable;

    switch (opt->type) {
    case coInt:
        return from_u8((boost::format("%1%") % config.opt_int(opt_key)).str());
    case coInts: {
        if (is_nullable) {
            auto values = config.opt<ConfigOptionIntsNullable>(opt_key);
            if (opt_idx < values->size())
                return from_u8((boost::format("%1%") % values->get_at(opt_idx)).str());
        }
        else {
            auto values = config.opt<ConfigOptionInts>(opt_key);
            if (orig_opt_idx >= 0 && orig_opt_idx < values->size()) {
                return from_u8((boost::format("%1%") % values->get_at(opt_idx)).str());
            }
            else {
                std::string value_str;
                for (int i = 0; i < values->size(); i++) {
                    value_str += std::to_string(values->get_at(i));
                    if (i != values->size() - 1) {
                        value_str += ",";
                    }
                }
                return from_u8(value_str);
            }
        }
        return _L("Undef");
    }
    case coBool:
        return config.opt_bool(opt_key) ? "true" : "false";
    case coBools: {
        if (is_nullable) {
            auto values = config.opt<ConfigOptionBoolsNullable>(opt_key);
            if (opt_idx < values->size())
                return values->get_at(opt_idx) ? "true" : "false";
        }
        else {
            auto values = config.opt<ConfigOptionBools>(opt_key);
            if (opt_idx < values->size())
                return values->get_at(opt_idx) ? "true" : "false";
        }
        return _L("Undef");
    }
    case coPercent:
        return from_u8((boost::format("%1%%%") % int(config.optptr(opt_key)->getFloat())).str());
    case coPercents: {
        if (is_nullable) {
            auto values = config.opt<ConfigOptionPercentsNullable>(opt_key);
            if (opt_idx < values->size())
                return from_u8((boost::format("%1%%%") % values->get_at(opt_idx)).str());
        }
        else {
            auto values = config.opt<ConfigOptionPercents>(opt_key);
            if (opt_idx < values->size())
                return from_u8((boost::format("%1%%%") % values->get_at(opt_idx)).str());
        }
        return _L("Undef");
    }
    case coFloat:
        return double_to_string(config.opt_float(opt_key));
    case coFloats: {
        if (is_nullable) {
            auto values = config.opt<ConfigOptionFloatsNullable>(opt_key);
            if (opt_idx < values->size())
                return double_to_string(values->get_at(opt_idx));
        }
        else {
            auto values = config.opt<ConfigOptionFloats>(opt_key);
            if (values && opt_idx < values->size())
                return double_to_string(values->get_at(opt_idx));
        }
        return _L("Undef");
    }
    case coString:
        return from_u8(config.opt_string(opt_key));
    case coStrings: {
        const ConfigOptionStrings* strings = config.opt<ConfigOptionStrings>(opt_key);
        if (strings) {
            if (opt_key == "compatible_printers" || opt_key == "compatible_prints") {
                if (strings->empty())
                    return _L("All");
                for (size_t id = 0; id < strings->size(); id++)
                    out += from_u8(strings->get_at(id)) + "\n";
                out.RemoveLast(1);
                return out;
            }
            if (!strings->empty() && opt_idx < strings->values.size())
                return from_u8(strings->get_at(opt_idx));
        }
        break;
        }
    case coFloatOrPercent: {
        const ConfigOptionFloatOrPercent* opt = config.opt<ConfigOptionFloatOrPercent>(opt_key);
        if (opt)
            out = double_to_string(opt->value) + (opt->percent ? "%" : "");
        return out;
    }
    case coFloatsOrPercents: {
        const ConfigOptionFloatsOrPercents *opt = config.opt<ConfigOptionFloatsOrPercents>(opt_key);
        if (opt && opt_idx < opt->size()) {
            const FloatOrPercent &val = opt->get_at(opt_idx);
            out                       = double_to_string(val.value) + (val.percent ? "%" : "");
        }
        return out;
    }
    case coEnum: {
        return get_string_from_enum(opt_key, config,
            opt_key == "top_surface_pattern" ||
            opt_key == "bottom_surface_pattern" ||
            opt_key == "internal_solid_infill_pattern" ||
            opt_key == "sub_top_surface_pattern" ||
            opt_key == "sparse_infill_pattern" ||
            opt_key == "locked_skin_infill_pattern" ||
            opt_key == "locked_skeleton_infill_pattern" ||
            opt_key == "ironing_pattern");
    }
    case coEnums: {
        return get_string_from_enum(opt_key, config,
            opt_key == "top_surface_pattern" ||
            opt_key == "bottom_surface_pattern" ||
            opt_key == "internal_solid_infill_pattern" ||
            opt_key == "sub_top_surface_pattern" ||
            opt_key == "sparse_infill_pattern" ||
            opt_key == "locked_skin_infill_pattern" ||
            opt_key == "locked_skeleton_infill_pattern" ||
            opt_key == "ironing_pattern",
            opt_idx);
    }
    case coPoint: {
        Vec2d val = config.opt<ConfigOptionPoint>(opt_key)->value;
        return from_u8((boost::format("[%1%]") % ConfigOptionPoint(val).serialize()).str());
    }
    case coPoints: {
        //BBS: add bed_exclude_area
        if (opt_key == "printable_area") {
            ConfigOptionPoints points = *config.option<ConfigOptionPoints>(opt_key);
            //BuildVolume build_volume = {points.values, 0.};
            return get_thumbnails_string(points.values);
        }
        else if (opt_key == "bed_exclude_area") {
            return get_thumbnails_string(config.option<ConfigOptionPoints>(opt_key)->values);
        }
        else if (opt_key == "thumbnail_size") {
            return get_thumbnails_string(config.option<ConfigOptionPoints>(opt_key)->values);
        }
        else if (opt_key == "head_wrap_detect_zone") {
            return get_thumbnails_string(config.option<ConfigOptionPoints>(opt_key)->values);
        }
        else if (opt_key == "wrapping_exclude_area") {
            return get_thumbnails_string(config.option<ConfigOptionPoints>(opt_key)->values);
        }
        Vec2d val = config.opt<ConfigOptionPoints>(opt_key)->get_at(opt_idx);
        return from_u8((boost::format("[%1%]") % ConfigOptionPoint(val).serialize()).str());
    }
    default:
        break;
    }
    return out;
}

// Returns the number of sub-values stored per extruder variant for opt_key:
// 2 for options in printer_options_with_variant_2 (e.g. normal/stealth mode), 1 otherwise.
static int get_variant_stride(const std::string &opt_key) { return printer_options_with_variant_2.count(get_pure_opt_key(opt_key)) > 0 ? 2 : 1; }

// Returns the displayed value of a single extruder variant of a (possibly stride-2) vector option.
// For stride 2 the two sub-values of the variant are joined with ',' (e.g. "normal,stealth").
// A variant index beyond the stored range renders as "N/A".
static wxString get_variant_string_value(const std::string &opt_key, const DynamicPrintConfig &config, int variant_idx, int stride)
{
    const std::string   pure_key   = get_pure_opt_key(opt_key);
    const ConfigOption *option     = config.option(pure_key);
    auto                opt_vector = dynamic_cast<const ConfigOptionVectorBase *>(option);
    const int           size       = opt_vector ? (int) opt_vector->size() : 0;

    wxString out;
    for (int sub = 0; sub < stride; sub++) {
        const int raw_idx = variant_idx * stride + sub;
        wxString  sub_val = raw_idx < size ? get_string_value(pure_key + "#" + std::to_string(raw_idx), config) : _L("N/A");
        out += sub == 0 ? sub_val : "," + sub_val;
    }
    return out;
}

static wxString get_collapsed_variant_value(const std::vector<wxString> &variant_values)
{
    if (variant_values.empty()) return {};

    wxString joined;
    bool     is_same = true;
    for (size_t v = 0; v < variant_values.size(); v++) {
        if (v != 0 && variant_values[v] != variant_values[v - 1]) is_same = false;
        joined += v == 0 ? variant_values[v] : "/" + variant_values[v];
    }
    return is_same ? variant_values[0] : joined;
}

// Splits a raw extruder-variant string (e.g. "Direct Drive Standard") into a localized
// "Drive: Nozzle" label. The split logic mirrors Tab::generate_extruder_options but is
// project-independent so it can label variants of any two compared presets.
static wxString get_variant_label(const std::string &variant)
{
    std::string drive, nozzle;

    static std::vector<std::string> known_nozzle_types;
    if (known_nozzle_types.empty()) {
        for (auto nvt : get_valid_nozzle_volume_type()) known_nozzle_types.push_back(get_nozzle_volume_type_string(nvt));
        std::sort(known_nozzle_types.begin(), known_nozzle_types.end(), [](const std::string &a, const std::string &b) { return a.size() > b.size(); });
    }

    bool found = false;
    for (const auto &nozzle_type : known_nozzle_types) {
        if (variant.size() > nozzle_type.size() && variant.substr(variant.size() - nozzle_type.size()) == nozzle_type && variant[variant.size() - nozzle_type.size() - 1] == ' ') {
            drive  = variant.substr(0, variant.size() - nozzle_type.size() - 1);
            nozzle = nozzle_type;
            found  = true;
            break;
        }
    }
    if (!found) {
        size_t pos = variant.rfind(' ');
        if (pos != std::string::npos) {
            drive  = variant.substr(0, pos);
            nozzle = variant.substr(pos + 1);
        } else {
            drive  = variant;
            nozzle = "";
        }
    }
    return nozzle.empty() ? _L(drive) : wxString::Format(_L("%s: %s"), _L(drive), _L(nozzle));
}

void UnsavedChangesDialog::update(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset, const wxString& header)
{
    PresetCollection* presets = dependent_presets;

    // activate buttons and labels
    if (m_save_btn)
        m_save_btn->Bind(wxEVT_ENTER_WINDOW, [this, presets](wxMouseEvent &e) {
            show_info_line(Action::Save, presets ? presets->get_selected_preset().name : "");
            e.Skip();
        });


    if (m_transfer_btn) {
        bool is_empty_name = dependent_presets && type != dependent_presets->type();
        m_transfer_btn->Bind(wxEVT_ENTER_WINDOW, [this, new_selected_preset, is_empty_name](wxMouseEvent& e) { show_info_line(Action::Transfer, is_empty_name ? "" : new_selected_preset); e.Skip(); });
    }
    if (m_discard_btn)
        m_discard_btn ->Bind(wxEVT_ENTER_WINDOW, [this]                                    (wxMouseEvent& e) { show_info_line(Action::Discard); e.Skip(); });

    if (type == Preset::TYPE_INVALID) {
        PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();
        int presets_cnt = 0;
        for (Tab* tab : wxGetApp().tabs_list)
            if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                presets_cnt++;
        /*m_action_line->SetLabel((header.IsEmpty() ? "" : header + "\n\n") +
                                _L_PLURAL("The following preset was modified",
                                          "The following presets were modified", presets_cnt));*/
    }
    else {
        wxString action_msg;
        if (type == dependent_presets->type()) {
            action_msg = format_wxstr(_L("Preset \"%1%\" contains the following unsaved changes:"), presets->get_edited_preset().name);
        }
        else {
            action_msg = format_wxstr(type == Preset::TYPE_PRINTER ?
                _L("Preset \"%1%\" is not compatible with the new printer profile and it contains the following unsaved changes:") :
                _L("Preset \"%1%\" is not compatible with the new process profile and it contains the following unsaved changes:"),
                presets->get_edited_preset().name);
        }
        //m_action_line->SetLabel(action_msg);
    }

    wxString action_msg;

    if (dependent_presets) {
        action_msg = format_wxstr(_L("You have changed the preset \"%1%\". "), dependent_presets->get_edited_preset().name);
        if (m_transfer_btn) {
            action_msg += _L("\nDo you want to use the modified value in the new preset that you selected?");
        }
    } else {
        action_msg = _L("You have changed the preset. ");
    }
    if (!m_transfer_btn)
        action_msg += _L("\nDo you want to save the modified values?");

    m_action_line->SetLabel(action_msg);

    update_tree(type, presets);
    update_list();
}

void UnsavedChangesDialog::update_list(SyncExtruderParams *params)
{
    if (!m_scrolledWindow) {
        Layout();
        Fit();
        return;
    }

    std::map<wxString, std::vector<PresetItem>> class_g_list;
    std::map<wxString, std::vector<wxString>>   class_c_list;
    std::vector<wxString>                       category_list;

    // group
    for (auto i = 0; i < m_presetitems.size(); i++) {
        auto name = m_presetitems[i].category_name + ":" + m_presetitems[i].group_name;
        if (class_g_list.count(name) <= 0) {
            std::vector<PresetItem> vp;
            vp.push_back(m_presetitems[i]);
            class_g_list.emplace(name, vp);
        } else {
            //for (auto iter = class_g_list.begin(); iter != class_g_list.end(); iter++) iter->second.push_back(m_presetitems[i]);
            class_g_list[name].push_back(m_presetitems[i]);
        }
    }

    // category
    for (auto i = 0; i < m_presetitems.size(); i++) {
        auto name = m_presetitems[i].category_name + ":" + m_presetitems[i].group_name;
        if (class_c_list.count(m_presetitems[i].category_name) <= 0) {
            std::vector<wxString> vp;
            vp.push_back(name);
            class_c_list.emplace(m_presetitems[i].category_name, vp);
            category_list.push_back(m_presetitems[i].category_name);
        } else {
            /*for (auto iter = class_c_list.begin(); iter != class_c_list.end(); iter++)
                iter->second.push_back(m_presetitems[i].group_name);*/
            //class_c_list[m_presetitems[i].category_name].push_back(m_presetitems[i].group_name);
            std::vector<wxString>::iterator it;
            it = find(class_c_list[m_presetitems[i].category_name].begin(), class_c_list[m_presetitems[i].category_name].end(), name);
            if (it == class_c_list[m_presetitems[i].category_name].end()) {
                class_c_list[m_presetitems[i].category_name].push_back(name);
            }
        }
    }


    auto m_listsizer = new wxBoxSizer(wxVERTICAL);
    for (auto category : category_list) {
        auto iter = class_c_list.find(category);
        //category
        auto panel_category = new wxPanel(m_scrolledWindow, wxID_ANY, wxDefaultPosition, wxSize(-1, UNSAVE_CHANGE_DIALOG_ITEM_HEIGHT), wxTAB_TRAVERSAL);
        panel_category->SetBackgroundColour(GREY300);

        wxBoxSizer *sizer_category   = new wxBoxSizer(wxHORIZONTAL);
        wxBoxSizer *sizer_category_v = new wxBoxSizer(wxHORIZONTAL);

        auto text_category = new wxStaticText(panel_category, wxID_ANY, iter->first, wxDefaultPosition, wxSize(-1, -1), 0);
        text_category->SetFont(::Label::Head_13);
        text_category->SetForegroundColour(GREY900);
        text_category->Wrap(-1);

        sizer_category_v->Add(text_category, 0, wxALIGN_CENTER | wxLEFT, 23);

        sizer_category->Add(sizer_category_v, 1, wxEXPAND, 0);

        panel_category->SetSizer(sizer_category);
        panel_category->Layout();
        m_listsizer->Add(panel_category, 0, wxEXPAND, 0);

        /*auto item_line = new wxStaticLine(list, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
        item_line->SetForegroundColour(wxColour(206, 206, 206));
        item_line->SetBackgroundColour(wxColour(206, 206, 206));
        sizer_list->Add(item_line, 0, wxALL, 0); */

        // isset group
        for (auto i = 0; i < iter->second.size(); i++) {
            auto gname = iter->second[i];
            for (auto g = 0; g < class_g_list[gname].size(); g++) {

                 //first group
                if (g == 0) {
                     auto panel_item = new wxWindow(m_scrolledWindow, -1, wxDefaultPosition, wxSize(-1, UNSAVE_CHANGE_DIALOG_ITEM_HEIGHT));
                     panel_item->SetBackgroundColour(GREY200);

                     wxBoxSizer *sizer_item = new wxBoxSizer(wxHORIZONTAL);

                     auto panel_left = new wxPanel(panel_item, wxID_ANY, wxDefaultPosition, wxSize(UNSAVE_CHANGE_DIALOG_FIRST_VALUE_WIDTH, -1), wxTAB_TRAVERSAL);
                     panel_left->SetBackgroundColour(GREY200);

                     wxBoxSizer *sizer_left_v = new wxBoxSizer(wxVERTICAL);

                     auto text_left = new wxStaticText(panel_left, wxID_ANY, class_g_list[gname][0].group_name, wxDefaultPosition, wxSize(-1, -1), 0);
                     text_left->SetFont(::Label::Head_13);
                     text_left->Wrap(-1);
#ifdef __linux__
// ubuntu dark mode issue. https://github.com/bambulab/BambuStudio/issues/4943
                    text_left->SetForegroundColour(wxGetApp().dark_mode() ? *wxLIGHT_GREY : GREY700);
#else
                    text_left->SetForegroundColour(GREY700);
#endif

                     sizer_left_v->Add(text_left, 0, wxLEFT, 37);

                     panel_left->SetSizer(sizer_left_v);
                     panel_left->Layout();
                     sizer_item->Add(panel_left, 0, wxALIGN_CENTER, 0);

                     panel_item->SetSizer(sizer_item);
                     panel_item->Layout();
                     m_listsizer->Add(panel_item, 0, wxEXPAND, 0);
                }

                auto data = class_g_list[gname][g];

                auto panel_item = new wxWindow(m_scrolledWindow, -1, wxDefaultPosition, wxSize(-1, UNSAVE_CHANGE_DIALOG_ITEM_HEIGHT));
                panel_item->SetBackgroundColour(GREY200);

                wxBoxSizer *sizer_item = new wxBoxSizer(wxHORIZONTAL);

                auto panel_left = new wxPanel(panel_item, wxID_ANY, wxDefaultPosition, wxSize(UNSAVE_CHANGE_DIALOG_FIRST_VALUE_WIDTH , -1), wxTAB_TRAVERSAL);
                panel_left->SetBackgroundColour(GREY200);

                wxBoxSizer *sizer_left_v = new wxBoxSizer(wxVERTICAL);

                auto text_left = new wxStaticText(panel_left, wxID_ANY, data.option_name, wxDefaultPosition, wxSize(-1, -1), 0);
                text_left->SetFont(::Label::Body_13);
                text_left->Wrap(-1);
#ifdef __linux__
// ubuntu dark mode issue. https://github.com/bambulab/BambuStudio/issues/4943
                text_left->SetForegroundColour(wxGetApp().dark_mode() ? *wxLIGHT_GREY : GREY700);
#else
                text_left->SetForegroundColour(GREY700);
#endif

                sizer_left_v->Add(text_left, 0, wxLEFT, 51 );

                panel_left->SetSizer(sizer_left_v);
                panel_left->Layout();
                sizer_item->Add(panel_left, 0, wxALIGN_CENTER, 0);

                auto        panel_oldv  = new wxPanel(panel_item, wxID_ANY, wxDefaultPosition, wxSize(UNSAVE_CHANGE_DIALOG_VALUE_WIDTH, -1), wxTAB_TRAVERSAL);
                wxBoxSizer *sizer_old_v = new wxBoxSizer(wxVERTICAL);


                data.old_value = subreplace(data.old_value.ToStdString(), "\n", " ");
                auto text_oldv = new wxStaticText(panel_oldv, wxID_ANY, data.old_value, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
                text_oldv->SetFont(::Label::Body_13);
                text_oldv->Wrap(-1);
                text_oldv->SetForegroundColour(params && params->left_to_right ? wxGetApp().get_label_clr_modified() : GREY700);
                sizer_old_v->Add(text_oldv, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, 5);

                panel_oldv->SetSizer(sizer_old_v);
                panel_oldv->Layout();
                sizer_item->Add(panel_oldv, 0, wxALIGN_CENTER, 0);

                auto        panel_newv  = new wxPanel(panel_item, wxID_ANY, wxDefaultPosition, wxSize(UNSAVE_CHANGE_DIALOG_VALUE_WIDTH, -1), wxTAB_TRAVERSAL);
                wxBoxSizer *sizer_new_v = new wxBoxSizer(wxVERTICAL);

                data.new_value = subreplace(data.new_value.ToStdString(), "\n", " ");
                auto text_newv = new wxStaticText(panel_newv, wxID_ANY, data.new_value, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
                text_newv->SetFont(::Label::Body_13);
                text_newv->Wrap(-1);
                text_newv->SetForegroundColour(params && !params->left_to_right ? wxGetApp().get_label_clr_modified() : GREY700);

                sizer_new_v->Add(text_newv, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, 5);

                panel_newv->SetSizer(sizer_new_v);
                panel_newv->Layout();
                sizer_item->Add(panel_newv, 0, wxALIGN_CENTER, 0);

                panel_item->SetSizer(sizer_item);
                panel_item->Layout();
                m_listsizer->Add(panel_item, 0, wxEXPAND, 0);



                ////if (g == class_g_list[gname].size() - 1) {
                //    auto item_line = new wxStaticLine(list, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
                //    item_line->SetForegroundColour(wxColour(206, 206, 206));
                //    item_line->SetBackgroundColour(wxColour(206, 206, 206));
                //    sizer_list->Add(item_line, 0, wxALL, 0);
                ////}
            }
        }
    }

       m_scrolledWindow->SetSizer(m_listsizer);
       m_scrolledWindow->Layout();
       /*wxSize text_size = m_action_line->GetTextExtent(m_action_line->GetLabel());
       int    width     = UNSAVE_CHANGE_DIALOG_ACTION_LINE_SIZE.GetWidth();
       // +2: Ensure that there is at least one line and that the content contains '\n'
       int    rows      = int(text_size.GetWidth() / width) + 2;
       int    height    = rows * text_size.GetHeight();
       m_action_line->SetMinSize(wxSize(width, height));
       m_action_line->Wrap(UNSAVE_CHANGE_DIALOG_ACTION_LINE_SIZE.GetWidth());*/
       Layout();
       Fit();
}

std::string UnsavedChangesDialog::subreplace(std::string resource_str, std::string sub_str, std::string new_str)
{
    std::string            dst_str = resource_str;
    std::string::size_type pos     = 0;
    while ((pos = dst_str.find(sub_str)) != std::string::npos)
    {
        dst_str.replace(pos, sub_str.length(), new_str);
    }
    return dst_str;
}

void UnsavedChangesDialog::update_tree(Preset::Type type, DynamicConfig * config, int from, int to)
{
    Search::OptionsSearcher &searcher = wxGetApp().sidebar().get_searcher();
    searcher.sort_options_by_key();

    for (const std::string &opt_key : config->keys()) {
        int                   variant_index = -2;
        const Search::Option &option        = searcher.get_option(opt_key, type, variant_index);
        auto category = option.category_local;
        auto opt = dynamic_cast<ConfigOptionVectorBase*>(config->option(opt_key));
        std::string           value_from    = opt->vserialize()[from];
        std::string           value_to    = opt->vserialize()[to];
        PresetItem            pi            = {type, opt_key, category, option.group_local, option.label_local, into_u8(value_from), into_u8(value_to)};
        m_presetitems.push_back(pi);
    }
}

void UnsavedChangesDialog::update_tree(Preset::Type type, PresetCollection* presets_)
{
    Search::OptionsSearcher& searcher = wxGetApp().sidebar().get_searcher();
    searcher.sort_options_by_key();

    // list of the presets with unsaved changes
    std::vector<PresetCollection*> presets_list;
    if (type == Preset::TYPE_INVALID)
    {
        PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();

        for (Tab* tab : wxGetApp().tabs_list)
            if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                presets_list.emplace_back(tab->get_presets());
    }
    else
        presets_list.emplace_back(presets_);

    // Display a dialog showing the dirty options in a human readable form.
    for (PresetCollection* presets : presets_list)
    {
        const DynamicPrintConfig& old_config = presets->get_selected_preset().config;
        const PrinterTechnology&  old_pt     = presets->get_selected_preset().printer_technology();
        const DynamicPrintConfig& new_config = presets->get_edited_preset().config;
        type = presets->type();

        const std::map<wxString, std::string>& category_icon_map = wxGetApp().get_tab(type)->get_category_icon_map();

        //m_tree->model->AddPreset(type, from_u8(presets->get_edited_preset().name), old_pt);

        // Collect dirty options.
        const bool deep_compare = (type == Preset::TYPE_PRINTER || type == Preset::TYPE_PRINT || type == Preset::TYPE_FILAMENT || type == Preset::TYPE_SLA_MATERIAL);
        auto dirty_options = presets->current_dirty_options(deep_compare);

        // process changes of extruders count
        if (type == Preset::TYPE_PRINTER && old_pt == ptFFF &&
            old_config.opt<ConfigOptionStrings>("extruder_colour")->values.size() != new_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()) {
            wxString local_label = _L("Extruders count");
            wxString old_val = from_u8((boost::format("%1%") % old_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());
            wxString new_val = from_u8((boost::format("%1%") % new_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());

            //BBS: the page "General" changed to "Basic information" instead
            //m_tree->Append("extruders_count", type, _L("General"), _L("Capabilities"), local_label, old_val, new_val, category_icon_map.at("Basic information"));
            //m_tree->Append("extruders_count", type, _L("General"), _L("Capabilities"), local_label, old_val, new_val, category_icon_map.at("General"));

            PresetItem pi = {type, "extruders_count", _L("General"), _L("Capabilities"), local_label, old_val, new_val};
            m_presetitems.push_back(pi);
        }

        auto variant_key      = Preset::get_iot_type_string(type) + "_extruder_variant";
        auto id_key           = Preset::get_iot_type_string(type) + "_extruder_id";
        auto extruder_variant = dynamic_cast<ConfigOptionStrings const *>(old_config.option(variant_key));
        auto extruder_id      = dynamic_cast<ConfigOptionInts const *>(old_config.option(id_key));

        for (const std::string& opt_key : dirty_options) {
            int variant_index = -2;
            const Search::Option &option = searcher.get_option(opt_key, type, variant_index);
            if (option.opt_key() != opt_key && variant_index < -1) {
                // When founded option isn't the correct one.
                // It can be for dirty_options: "default_print_profile", "printer_model", "printer_settings_id",
                // because of they don't exist in searcher
                continue;
            }
            auto category = option.category_local;
            if (variant_index >= 0) {
                if (printer_options_with_variant_2.count(opt_key.substr(0, opt_key.find_last_of('#'))) > 0)
                    variant_index /= 2;
                if (boost::nowide::narrow(category).find("Extruder ") == 0)
                    category = category.substr(0, 8);

                if (variant_index >= (int) extruder_variant->values.size()) {
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": variant_index " << variant_index
                            << " out of range for extruder_variant (size=" << extruder_variant->values.size()
                            << ") on opt_key=" << opt_key << "; skip variant suffix";
                } else {
                    if (extruder_id)
                        category = category + (wxString(" {") + (extruder_id->values[variant_index] == 1 ? _L("Left: ") : _L("Right: "))
                                + L(extruder_variant->values[variant_index]) + "}");
                    else
                        category = category + (" {" + L(extruder_variant->values[variant_index]) + "}");
                    }
            }

            /*m_tree->Append(opt_key, type, option.category_local, option.group_local, option.label_local,
                get_string_value(opt_key, old_config), get_string_value(opt_key, new_config), category_icon_map.at(option.category));*/


            //PresetItem pi = {opt_key, type, 1983};
            //m_presetitems.push_back()
            PresetItem pi = {type, opt_key, category, option.group_local, option.label_local, get_string_value(opt_key, old_config), get_string_value(opt_key, new_config)};
            m_presetitems.push_back(pi);

        }
    }

    // Revert sort of searcher back
    searcher.sort_options_by_label();
}

void UnsavedChangesDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    int em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CANCEL, m_move_btn_id, m_continue_btn_id });
    for (auto btn : {m_transfer_btn, m_discard_btn, m_save_btn})
        if (btn) btn->SetMinSize(UNSAVE_CHANGE_DIALOG_BUTTON_SIZE);

    //m_cancel_btn->SetMinSize(UNSAVE_CHANGE_DIALOG_BUTTON_SIZE);
    const wxSize& size = wxSize(70 * em, 30 * em);
    SetMinSize(size);
    //m_tree->Rescale(em);

    Fit();
    Refresh();
}

void UnsavedChangesDialog::on_sys_color_changed()
{
    //for (auto btn : { m_save_btn, m_transfer_btn, m_discard_btn } )
        //btn->msw_rescale();
    // msw_rescale updates just icons, so use it
    //m_tree->Rescale();

    Refresh();
}

bool UnsavedChangesDialog::check_option_valid()
{
    return true;
}


//------------------------------------------
//          FullCompareDialog
//------------------------------------------

FullCompareDialog::FullCompareDialog(const wxString& option_name, const wxString& old_value, const wxString& new_value,
                                     const wxString& old_value_header, const wxString& new_value_header)
    : wxDialog(nullptr, wxID_ANY, option_name, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetBackgroundColour(*wxWHITE);

    int border = 10;

    wxStaticBoxSizer* sizer = new wxStaticBoxSizer(wxVERTICAL, this);

    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(2, 2, 1, 0);
    grid_sizer->SetFlexibleDirection(wxBOTH);
    grid_sizer->AddGrowableCol(0,1);
    grid_sizer->AddGrowableCol(1,1);
    grid_sizer->AddGrowableRow(1,1);

    auto add_header = [grid_sizer, border, this](wxString label) {
        wxStaticText* text = new wxStaticText(this, wxID_ANY, label);
        text->SetFont(this->GetFont().Bold());
        grid_sizer->Add(text, 0, wxALL, border);
    };

    add_header(old_value_header);
    add_header(new_value_header);

    auto get_set_from_val = [](wxString str) {
        if (str.Find("\n") == wxNOT_FOUND)
            str.Replace(" ", "\n");

        std::set<wxString> str_set;

        wxStringTokenizer strings(str, "\n");
        while (strings.HasMoreTokens())
            str_set.emplace(strings.GetNextToken());

        return str_set;
    };

    std::set<wxString> old_set = get_set_from_val(old_value);
    std::set<wxString> new_set = get_set_from_val(new_value);
    std::set<wxString> old_new_diff_set;
    std::set<wxString> new_old_diff_set;

    std::set_difference(old_set.begin(), old_set.end(), new_set.begin(), new_set.end(), std::inserter(old_new_diff_set, old_new_diff_set.begin()));
    std::set_difference(new_set.begin(), new_set.end(), old_set.begin(), old_set.end(), std::inserter(new_old_diff_set, new_old_diff_set.begin()));

    auto add_value = [grid_sizer, border, this](wxString label, const std::set<wxString>& diff_set, bool is_colored = false) {
        wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, label, wxDefaultPosition, wxSize(400, 400), wxTE_MULTILINE | wxTE_READONLY | wxBORDER_DEFAULT | wxTE_RICH);
        wxGetApp().UpdateDarkUI(text);
        text->SetStyle(0, label.Len(), wxTextAttr(is_colored ? wxColour(orange) : wxNullColour, wxNullColour, this->GetFont()));

        for (const wxString& str : diff_set) {
            int pos = label.First(str);
            if (pos == wxNOT_FOUND)
                continue;
            text->SetStyle(pos, pos + (int)str.Len(), wxTextAttr(is_colored ? wxColour(orange) : wxNullColour, wxNullColour, this->GetFont().Bold()));
        }

        grid_sizer->Add(text, 1, wxALL | wxEXPAND, border);
    };
    add_value(old_value, old_new_diff_set);
    add_value(new_value, new_old_diff_set, true);

    sizer->Add(grid_sizer, 1, wxEXPAND);

    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK);
    wxGetApp().UpdateDarkUI(static_cast<wxButton*>(this->FindWindowById(wxID_OK, this)), true);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(sizer,   1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(buttons, 0, wxEXPAND | wxALL, border);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    wxGetApp().UpdateDlgDarkUI(this);
}


static PresetCollection* get_preset_collection(Preset::Type type, PresetBundle* preset_bundle = nullptr) {
    if (!preset_bundle)
        preset_bundle = wxGetApp().preset_bundle;
    return  type == Preset::Type::TYPE_PRINT        ? &preset_bundle->prints :
            type == Preset::Type::TYPE_SLA_PRINT    ? &preset_bundle->sla_prints :
            type == Preset::Type::TYPE_FILAMENT     ? &preset_bundle->filaments :
            type == Preset::Type::TYPE_SLA_MATERIAL ? &preset_bundle->sla_materials :
            type == Preset::Type::TYPE_PRINTER      ? &preset_bundle->printers :
            nullptr;
}

/**
 * \brief Centered illustration-plus-hint placeholder shown in DiffPresetDialog when there is
 *        nothing to compare (the two presets are equal, or no distinct pair is selected).
 */
class EmptyStatePanel : public wxPanel
{
public:
    EmptyStatePanel(wxWindow *parent);
    ~EmptyStatePanel() {}

    /// \brief Set the centered hint text (used to surface error / "presets are equal" messages here). \param text Hint to display.
    void set_hint(const wxString &text)
    {
        m_hint->SetLabel(text);
        Layout();
    }

    /// \brief Reload the illustration bitmap for the current DPI/theme.
    void Rescale()
    {
        m_bmp->SetBitmap(create_scaled_bitmap("diff_empty_state", this, 160));
        Layout();
    }

private:
    wxStaticBitmap *m_bmp{nullptr};
    wxStaticText   *m_hint{nullptr};
};

EmptyStatePanel::EmptyStatePanel(wxWindow *parent) : wxPanel(parent, wxID_ANY)
{
    const int border = FromDIP(10);
    m_bmp            = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("diff_empty_state", this, 160));
    m_hint           = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->AddStretchSpacer();
    sizer->Add(m_bmp, 0, wxALIGN_CENTER_HORIZONTAL);
    sizer->Add(m_hint, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, border);
    sizer->AddStretchSpacer();
    SetSizer(sizer);
}

/**
 * \brief The preset A/B selector area of DiffPresetDialog: two static captions ("Preset A" /
 *        "Preset B") each above a PresetComboBox, with an "equal" copy button between them.
 *
 * Only one preset type is compared at a time (driven by the dialog's tab bar). Because
 * PresetComboBox fixes its type at construction, the two combos are (re)created for the active
 * type via set_type() on every tab switch, rather than pre-building one pair per type.
 */
class PresetSelectorPanel : public wxPanel
{
public:
    /**
     * \brief Build the selector layout (labels + empty combo columns + equal button).
     *
     * \param parent       Parent window (the DiffPresetDialog).
     * \param bundle_left  Preset bundle backing the left (Preset A) combo.
     * \param bundle_right Preset bundle backing the right (Preset B) combo.
     */
    PresetSelectorPanel(wxWindow *parent, PresetBundle *bundle_left, PresetBundle *bundle_right);
    ~PresetSelectorPanel() {}

    /**
     * \brief Rebuild the two combos for the given preset type and populate them.
     *
     * Destroys any existing pair and creates a fresh PresetComboBox pair bound to \p type, wires
     * their selection callbacks, applies the remembered show-all flag, and repopulates.
     *
     * \param type Preset type to compare (Print/Filament/Printer/SLA variants).
     * \param tech Current printer technology (FFF/SLA), forwarded to combo population.
     */
    void set_type(Preset::Type type, PrinterTechnology tech);

    /// \brief Remember the "show incompatible presets" flag and apply it to the live pair. \param show_all True to list incompatible presets.
    void set_show_all(bool show_all);

    /// \brief Forget all remembered per-type selections (call when the dialog re-syncs its bundles from the app).
    void forget_selections()
    {
        m_last_sel_left.clear();
        m_last_sel_right.clear();
    }

    /// \brief Reload combo/button bitmaps for the current DPI/theme.
    void Rescale();

    PresetComboBox *left() const { return m_combox_left; }
    PresetComboBox *right() const { return m_combox_right; }
    Preset::Type    type() const { return m_type; }
    void            setEqualIcon(std::string icon);

    /// Called after either combo's selection changes (dialog wires this to refresh the diff tree).
    std::function<void()> on_selection_changed;

    /// Called when a combo selection requires a compatibility refresh (preset_name, type, bundle).
    std::function<void(const std::string &, Preset::Type, PresetBundle *)> on_compatibility;

private:
    /**
     * \brief (Re)build one side's combo for the current m_type.
     *
     * Destroys the existing combo (if any) and creates a fresh PresetComboBox in \p column, wired
     * to \p bundle. The side-specific state is passed in by the caller rather than selected from a
     * left/right flag, so this method carries no notion of which side it is operating on.
     *
     * \param cb     Reference to the member combo pointer for this side; repointed at the new combo.
     * \param column Sizer column the combo is added to.
     * \param bundle Preset bundle the combo reads from.
     * \param memory Per-type last-selection map for this side (read to restore, written on change).
     */
    void rebuild_combo(PresetComboBox *&cb, wxBoxSizer *column, PresetBundle *bundle, std::map<Preset::Type, std::string> &memory);

    PresetBundle   *m_bundle_left{nullptr};
    PresetBundle   *m_bundle_right{nullptr};
    wxBoxSizer     *m_col_left{nullptr};
    wxBoxSizer     *m_col_right{nullptr};
    PresetComboBox *m_combox_left{nullptr};
    PresetComboBox *m_combox_right{nullptr};
    ScalableButton *m_equal{nullptr};
    Preset::Type    m_type{Preset::TYPE_INVALID};
    bool            m_show_all{false};

    // Per-type, per-side last user selection. Combos are destroyed on every tab switch, so the
    // chosen preset is remembered here and restored when the pair for that type is rebuilt (a
    // widget-only selection would otherwise be lost, reverting to the bundle default).
    std::map<Preset::Type, std::string> m_last_sel_left;
    std::map<Preset::Type, std::string> m_last_sel_right;
};

static std::string get_selection(PresetComboBox* preset_combo)
{
    return into_u8(preset_combo->GetString(preset_combo->GetSelection()));
}

PresetSelectorPanel::PresetSelectorPanel(wxWindow *parent, PresetBundle *bundle_left, PresetBundle *bundle_right)
    : wxPanel(parent, wxID_ANY), m_bundle_left(bundle_left), m_bundle_right(bundle_right)
{
    SetBackgroundColour(*wxWHITE);

    auto *label_left = new wxStaticText(this, wxID_ANY, _L("Preset A"));
    label_left->SetFont(::Label::Head_14);
    m_col_left       = new wxBoxSizer(wxVERTICAL);
    m_col_left->Add(label_left, 0, wxBOTTOM, 2);

    m_equal = new ScalableButton(this, wxID_ANY, "equal");

    auto *label_right = new wxStaticText(this, wxID_ANY, _L("Preset B"));
    label_right->SetFont(::Label::Head_14);
    m_col_right       = new wxBoxSizer(wxVERTICAL);
    m_col_right->Add(label_right, 0, wxBOTTOM, 2);

    wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(m_col_left, 1, wxEXPAND);
    row->Add(m_equal, 0, wxRIGHT | wxLEFT | wxALIGN_BOTTOM, 5);
    row->Add(m_col_right, 1, wxEXPAND);
    SetSizer(row);

    // copy left to right
    m_equal->Bind(wxEVT_BUTTON, [this](wxEvent &) {
        if (!m_combox_left || !m_combox_right) return;
        std::string preset_name = get_selection(m_combox_left);
        m_combox_right->update(preset_name);
        m_last_sel_right[m_type] = preset_name;
        if (on_compatibility) on_compatibility(Preset::remove_suffix_modified(preset_name), m_combox_right->get_type(), m_bundle_right);
        if (on_selection_changed) on_selection_changed();
    });
}

void PresetSelectorPanel::setEqualIcon(std::string icon) { m_equal->SetBitmap_(ScalableBitmap(this, icon)); }

void PresetSelectorPanel::rebuild_combo(PresetComboBox *&cb, wxBoxSizer *column, PresetBundle *bundle, std::map<Preset::Type, std::string> &memory)
{
    if (cb) {
        column->Detach(cb);
        cb->Destroy();
        cb = nullptr;
    }

    const int em = em_unit(this);
    cb           = new PresetComboBox(this, m_type, wxSize(em * 35, -1), bundle);

    // Snapshot the raw pointer into a plain local for the lambda to capture by value: cb is a
    // reference to the member pointer, which is repointed at a fresh combo on the next rebuild
    // (tab switch). Capturing this local instead binds the callback to its own combo for good.
    PresetComboBox *combo = cb;
    combo->set_selection_changed_function([this, &memory, bundle, combo](int selection) {
        std::string preset_name = combo->GetString(selection).ToUTF8().data();
        // Remember the pick so it survives the combo being rebuilt on a later tab switch.
        memory[m_type] = preset_name;
        if (on_compatibility) on_compatibility(Preset::remove_suffix_modified(preset_name), m_type, bundle);
        if (on_selection_changed) on_selection_changed();
    });
    if (m_show_all) combo->show_all(true);

    // Restore the last user pick for this type/side; fall back to the bundle's selected preset.
    auto it = memory.find(m_type);
    if (it != memory.end()) {
        combo->update(it->second);
    } else {
        const PresetCollection *collection = get_preset_collection(m_type, bundle);
        if (collection && collection->get_selected_idx() != (size_t) -1) combo->update(collection->get_selected_preset().name);
    }

    column->Add(combo, 0, wxEXPAND);
}

void PresetSelectorPanel::set_type(Preset::Type type, PrinterTechnology /*tech*/)
{
    m_type = type;
    rebuild_combo(m_combox_left, m_col_left, m_bundle_left, m_last_sel_left);
    rebuild_combo(m_combox_right, m_col_right, m_bundle_right, m_last_sel_right);
    Layout();
}

void PresetSelectorPanel::set_show_all(bool show_all)
{
    m_show_all = show_all;
    if (m_combox_left) m_combox_left->show_all(show_all);
    if (m_combox_right) m_combox_right->show_all(show_all);
}

void PresetSelectorPanel::Rescale()
{
    if (m_combox_left) m_combox_left->msw_rescale();
    if (m_combox_right) m_combox_right->msw_rescale();
    if (m_equal) m_equal->msw_rescale();
    Layout();
}

DiffPresetDialog::DiffPresetDialog(MainFrame* mainframe)
    : DPIDialog(mainframe, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    m_pr_technology(wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology())
{
#if defined(__WXMSW__)
    // ys_FIXME! temporary workaround for correct font scaling
    // Because of from wxWidgets 3.1.3 auto rescaling is implemented for the Fonts,
    // From the very beginning set dialog font to the wxSYS_DEFAULT_GUI_FONT
    this->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#endif // __WXMSW__

    int em = em_unit();
    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

    // Window/taskbar icon (mirrors UnsavedChangesDialog::build).
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    assert(wxGetApp().preset_bundle);

    m_preset_bundle_left  = std::make_unique<PresetBundle>(*wxGetApp().preset_bundle);
    m_preset_bundle_right = std::make_unique<PresetBundle>(*wxGetApp().preset_bundle);

    // Single preset A/B selector; its combo pair is (re)built for the active type by set_type().
    m_selector                       = new PresetSelectorPanel(this, m_preset_bundle_left.get(), m_preset_bundle_right.get());
    m_selector->on_selection_changed = [this]() { update_tree(); };
    m_selector->on_compatibility     = [this](const std::string &preset_name, Preset::Type type, PresetBundle *bundle) {
        if (m_opened_generically) update_compatibility(preset_name, type, bundle);
    };

    m_show_all_presets = new wxCheckBox(this, wxID_ANY, _L("Show all presets (including incompatible)"));
    m_show_all_presets->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
        m_selector->set_show_all(m_show_all_presets->GetValue());
        if (m_opened_generically) update_tree();
    });

    // Page 0 = empty state, page 1 = diff tree.
    m_content     = new wxSimplebook(this, wxID_ANY);
    m_empty_state = new EmptyStatePanel(m_content);

    // The tree's outline is a Grey400 panel showing through a 1px inset on every side; the control
    // itself is borderless. Only the outline is coloured - the inner row rules stay as drawn by
    // wxDV_HORIZ_RULES.
    m_tree_frame = new wxPanel(m_content);
    m_tree_frame->SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::Grey400));

    m_tree = new DiffViewCtrl(m_tree_frame, wxSize(em * 65, em * 40));
    m_tree->AppendBmpTextColumn(_L("Parameters"), DiffModel::colIconText, 35, true);
    m_tree->AppendBmpTextColumn(_L("Preset A"), DiffModel::colOldValue, 15);
    m_tree->AppendBmpTextColumn(_L("Preset B"), DiffModel::colNewValue, 15);
    // Larger, bolder header than the app default so the Parameters/A/B row reads as a table head.
    m_tree->SetHeaderFont(::Label::Head_14);
    m_tree->SetHeaderHeight(40);

    wxBoxSizer *tree_frame_sizer = new wxBoxSizer(wxVERTICAL);
    tree_frame_sizer->Add(m_tree, 1, wxEXPAND | wxALL, FromDIP(1));
    m_tree_frame->SetSizer(tree_frame_sizer);

    m_content->AddPage(m_empty_state, wxEmptyString); // kPageEmpty
    m_content->AddPage(m_tree_frame, wxEmptyString);  // kPageTree
    m_content->SetSelection(kPageEmpty);

    // Process/Filament/Machine tab bar; selecting a tab drives which preset type is compared.
    // Wider inter-tab gap than the default so the three tabs read as distinct sections.
    m_tabbar = new TextTabbar(this, TextTabbar::Align::Left, 48);
    rebuild_tabs();
    m_tabbar->Bind(wxEVT_CHOICE, [this](wxCommandEvent &e) {
        const int idx = e.GetInt();
        if (idx < 0 || idx >= (int) m_tab_types.size()) return;
        m_view_type = m_tab_types[idx];
        update_controls_visibility(m_view_type);
        update_tree();
        Layout();
    });

    // Build the initial combo pair for the tab selected by rebuild_tabs().
    m_selector->set_type(m_view_type, m_pr_technology);

    // Shared horizontal inset so the selector, "show all" checkbox and the content book keep a
    // common left/right edge with extra breathing room from the window frame; the content also
    // gets the same generous inset along the bottom.
    const int side_margin = FromDIP(36);

    wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);
    topSizer->AddSpacer(FromDIP(24));
    topSizer->Add(m_tabbar, 0, wxEXPAND | wxLEFT | wxRIGHT, side_margin);

    topSizer->AddSpacer(FromDIP(24));
    topSizer->Add(m_selector, 0, wxEXPAND | wxLEFT | wxRIGHT, side_margin);
    topSizer->AddSpacer(FromDIP(8));
    topSizer->Add(m_show_all_presets, 0, wxEXPAND | wxLEFT | wxRIGHT, side_margin);

    topSizer->AddSpacer(FromDIP(24));
    topSizer->Add(m_content, 1, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, side_margin);

    this->SetSizer(topSizer);
    // Fixed 800x600 default (DPI-scaled), pinned as the min so preset switches never shrink it.
    // Deliberately not driven by Fit(): Fit() sizes to the content's best size, which for a
    // scrolling data-view tree is far smaller than useful and would snap the dialog tiny on every
    // update. Layout() (not Fit()) is used on the refresh paths to re-flow within this fixed size.
    this->SetMinSize(FromDIP(wxSize(800, 600)));
    this->SetSize(FromDIP(wxSize(800, 600)));
    wxGetApp().UpdateDlgDarkUI(this);
}

void DiffPresetDialog::rebuild_tabs()
{
    if (!m_tabbar) return;

    // Tab set depends on printer technology: FFF compares Process/Filament/Machine, SLA compares
    // SLA Process/SLA Material/Machine. Preserve the current type across a rebuild when possible.
    const Preset::Type prev_type = (m_tabbar->GetSelection() >= 0 && m_tabbar->GetSelection() < (int) m_tab_types.size()) ? m_tab_types[m_tabbar->GetSelection()] : m_view_type;

    m_tabbar->ClearTabs();
    m_tab_types.clear();

    auto add = [this](const wxString &label, Preset::Type type) {
        m_tabbar->AddTab(label);
        m_tab_types.push_back(type);
    };

    if (m_pr_technology == ptFFF) {
        add(_L("Process Preset"), Preset::TYPE_PRINT);
        add(_L("Filament Preset"), Preset::TYPE_FILAMENT);
    } else {
        add(_L("Process Preset"), Preset::TYPE_SLA_PRINT);
        add(_L("Material Preset"), Preset::TYPE_SLA_MATERIAL);
    }
    add(_L("Machine Preset"), Preset::TYPE_PRINTER);

    int sel = 0;
    for (int i = 0; i < (int) m_tab_types.size(); i++)
        if (m_tab_types[i] == prev_type) {
            sel = i;
            break;
        }
    m_tabbar->SetSelection(sel);
    m_view_type = m_tab_types[sel];
}

void DiffPresetDialog::update_controls_visibility(Preset::Type type /* = Preset::TYPE_INVALID*/)
{
    const Preset::Type target = type != Preset::TYPE_INVALID ? type : m_view_type;

    // Point the single selector at the active type. Rebuild the combo pair only when the type
    // actually changes; otherwise just refresh the existing pair from the bundles.
    if (m_selector->type() != target)
        m_selector->set_type(target, m_pr_technology);
    else {
        if (m_selector->left()) m_selector->left()->update_from_bundle();
        if (m_selector->right()) m_selector->right()->update_from_bundle();
    }

    m_show_all_presets->Show(target != Preset::TYPE_PRINTER);
}

void DiffPresetDialog::update_bundles_from_app()
{
    *m_preset_bundle_left  = *wxGetApp().preset_bundle;
    *m_preset_bundle_right = *wxGetApp().preset_bundle;
    m_selector->forget_selections();
}

void DiffPresetDialog::show(Preset::Type type /* = Preset::TYPE_INVALID*/)
{
    this->SetTitle(_L("Compare presets"));
    m_opened_generically = (type == Preset::TYPE_INVALID);

    update_bundles_from_app();

    // Rebuild tabs for the current technology, then select the requested type's tab (or the first
    // tab when opened generically). The selected tab is the single visible preset type.
    rebuild_tabs();
    if (type != Preset::TYPE_INVALID) {
        for (int i = 0; i < (int) m_tab_types.size(); i++)
            if (m_tab_types[i] == type) {
                m_tabbar->SetSelection(i);
                m_view_type = type;
                break;
            }
    }

    update_controls_visibility(m_view_type);
    Layout();

    update_tree();
    wxGetApp().UpdateDlgDarkUI(this);

    // if this dialog is shown it have to be Hide and show again to be placed on the very Top of windows
    if (IsShown()) Hide();
    Show();
}

void DiffPresetDialog::update_presets(Preset::Type type)
{
    const PrinterTechnology prev_tech = m_pr_technology;
    m_pr_technology = m_preset_bundle_left.get()->printers.get_edited_preset().printer_technology();

    update_bundles_from_app();
    // A technology switch changes the tab set (FFF vs SLA); rebuild so the tabs stay valid.
    if (prev_tech != m_pr_technology) rebuild_tabs();
    update_controls_visibility(m_view_type);

    // update_controls_visibility already refreshed the active pair from the bundles. When a specific
    // type is requested and it is the one currently shown, do a full update() (re-sorts/repopulates).
    if (type != Preset::TYPE_INVALID && m_selector->type() == type) {
        if (m_selector->left()) m_selector->left()->update();
        if (m_selector->right()) m_selector->right()->update();
    }

    update_tree();
}

void DiffPresetDialog::on_empty(wxString message, std::string icon)
{
    m_selector->setEqualIcon(icon);

    m_empty_state->set_hint(message);
    m_content->SetSelection(kPageEmpty);

    Layout();
    Refresh();
}

void DiffPresetDialog::do_update_tree(const Preset *left_preset, const Preset *right_preset, std::vector<std::string> &dirty_options)
{
    Preset::Type             type    = m_selector->type();
    const PrinterTechnology &left_pt = left_preset->printer_technology();

    const DynamicPrintConfig &left_config  = left_preset->config;
    const DynamicPrintConfig &right_congig = right_preset->config;

    Search::OptionsSearcher &searcher = wxGetApp().sidebar().get_searcher();
    searcher.sort_options_by_key();

    m_tree->Clear();

    // Rows actually added; options with no Tab page are skipped, so a preset pair can differ
    // only in internal keys and still produce an empty tree.
    int shown_count = 0;

    do {
        m_tree->model->AddPreset(type);

        // process changes of extruders count
        if (type == Preset::TYPE_PRINTER && left_pt == ptFFF &&
            left_config.opt<ConfigOptionStrings>("extruder_colour")->values.size() != right_congig.opt<ConfigOptionStrings>("extruder_colour")->values.size()) {
            wxString local_label = _L("Extruders count");
            wxString left_val = from_u8((boost::format("%1%") % left_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());
            wxString right_val = from_u8((boost::format("%1%") % right_congig.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());

            m_tree->Append("extruders_count", type, "General", "Capabilities", local_label, left_val, right_val, "");
            shown_count++;
        }

        // Extruder-variant key sets for this preset type. Options in these sets store one value
        // (or two, for key_set2) per extruder variant and are shown expanded, one child per variant.
        std::string            extruder_id_name, extruder_variant_name;
        std::set<std::string> *key_set1 = nullptr;
        std::set<std::string> *key_set2 = nullptr;
        Preset::get_extruder_names_and_keysets(type, extruder_id_name, extruder_variant_name, &key_set1, &key_set2);

        // Each preset carries its own variant-name list; variants are paired across presets by name.
        auto read_variants = [&extruder_variant_name](const DynamicPrintConfig &cfg) {
            std::vector<std::string> out;
            if (!extruder_variant_name.empty())
                if (auto opt = cfg.opt<ConfigOptionStrings>(extruder_variant_name)) out = opt->values;
            return out;
        };
        const std::vector<std::string> left_variants  = read_variants(left_config);
        const std::vector<std::string> right_variants = read_variants(right_congig);

        // Physical extruder side per variant (parallel to the variant-name list): 1 = left, else right.
        // Empty for preset types without an extruder-id key (e.g. filament), which drops the side prefix.
        auto read_extruder_ids = [&extruder_id_name](const DynamicPrintConfig &cfg) {
            std::vector<int> out;
            if (!extruder_id_name.empty())
                if (auto opt = cfg.opt<ConfigOptionInts>(extruder_id_name)) out = opt->values;
            return out;
        };
        const std::vector<int> left_ids  = read_extruder_ids(left_config);
        const std::vector<int> right_ids = read_extruder_ids(right_congig);

        // Normalize deep_diff's per-index keys (opt_key#N) down to one bare key per option.
        std::vector<std::string> bare_options;
        {
            std::set<std::string> seen_keys;
            for (const std::string &opt_key : dirty_options) {
                std::string bare = get_pure_opt_key(opt_key);
                if (seen_keys.insert(bare).second) bare_options.emplace_back(bare);
            }
        }

        for (const std::string &bare_key : bare_options) {
            const bool in_variant_keyset = bare_key != extruder_variant_name && bare_key != extruder_id_name &&
                                           ((key_set1 && key_set1->count(bare_key) > 0) || (key_set2 && key_set2->count(bare_key) > 0));

            const size_t variant_count = std::max(left_variants.size(), right_variants.size());

            // An option is shown expanded only when it belongs to a variant key set AND there are
            // at least two variants to compare; with a single variant it degenerates to a plain row.
            const bool is_variant_option = in_variant_keyset && variant_count >= 2;

            Search::Option option       = searcher.get_option(bare_key, get_full_label(bare_key, left_config), type);
            const bool     option_found = get_pure_opt_key(option.opt_key()) == bare_key && !(option.category.empty() && option.group.empty());

            if (is_variant_option) {
                const int stride = get_variant_stride(bare_key);

                std::vector<wxString> labels, child_left, child_right;
                for (size_t idx = 0; idx < variant_count; idx++) {
                    const bool has_left  = idx < left_variants.size();
                    const bool has_right = idx < right_variants.size();
                    // Label from whichever preset has this index (prefer left); they share ordering.
                    const std::vector<std::string> &src_variants = has_left ? left_variants : right_variants;
                    const std::vector<int>         &src_ids      = has_left ? left_ids : right_ids;
                    wxString label = get_variant_label(src_variants[idx]);
                    // Prefix with the physical extruder side so two variants that share a drive/nozzle
                    // label (one per extruder) stay distinguishable.
                    if (idx < src_ids.size())
                        label = (src_ids[idx] == 1 ? _L("Left: ") : _L("Right: ")) + label;
                    labels.push_back(label);
                    child_left.push_back(has_left ? get_variant_string_value(bare_key, left_config, (int) idx, stride) : _L("N/A"));
                    child_right.push_back(has_right ? get_variant_string_value(bare_key, right_congig, (int) idx, stride) : _L("N/A"));
                }

                // Collapse from the paired child vectors so parent and children never disagree.
                wxString left_val  = get_collapsed_variant_value(child_left);
                wxString right_val = get_collapsed_variant_value(child_right);

                // Options with no Tab page (internal plumbing like `inherits`, extruder maps,
                // print-host credentials) have no category/group to file them under, so they are
                // skipped rather than dumped into a synthetic "Undef" bucket.
                if (!option_found) continue;

                m_tree->AppendVariant(bare_key, type, option.category_local, option.group_local, option.label_local, left_val, right_val, "", labels, child_left, child_right);
                shown_count++;
                continue;
            }

            wxString left_val  = get_string_value(bare_key, left_config);
            wxString right_val = get_string_value(bare_key, right_congig);

            // Not on any Tab page => no category/group (e.g. "default_print_profile",
            // "printer_model", "printer_settings_id"); nothing meaningful to show, so skip it.
            if (!option_found) continue;

            m_tree->Append(bare_key, type, option.category_local, option.group_local, option.label_local, left_val, right_val, "");
            shown_count++;
        }
    } while (false);

    // Revert sort of searcher back before any early return.
    searcher.sort_options_by_label();

    // Every differing option was internal-only: report "no visible differences" rather than
    // showing an empty table.
    if (shown_count == 0) return on_empty(_L("These two presets have no visible differences"), "equal");

    m_selector->setEqualIcon("not_equal");
    m_content->SetSelection(kPageTree);

    Layout();
    Refresh();
}

void DiffPresetDialog::update_tree()
{
    Preset::Type            type    = m_selector->type();
    const PresetCollection *presets = get_preset_collection(type);

    const Preset *left_preset  = presets->find_preset(get_selection(m_selector->left()));
    const Preset *right_preset = presets->find_preset(get_selection(m_selector->right()));

    if (!left_preset || !right_preset) { return on_empty(L"One of the presets does not exist", "question"); }

    const PrinterTechnology &left_pt = left_preset->printer_technology();
    if (left_pt != right_preset->printer_technology()) { return on_empty(L"Compared presets has different printer technology", "question"); }

    // Collect dirty options.

    const DynamicPrintConfig &left_config   = left_preset->config;
    const DynamicPrintConfig &right_congig  = right_preset->config;
    const bool                deep_compare  = (type == Preset::TYPE_PRINTER || type == Preset::TYPE_SLA_MATERIAL);
    auto                      dirty_options = type == Preset::TYPE_PRINTER && left_pt == ptFFF &&
                                                      left_config.opt<ConfigOptionStrings>("extruder_colour")->values.size() <
                                                          right_congig.opt<ConfigOptionStrings>("extruder_colour")->values.size() ?
                                                  presets->dirty_options(right_preset, left_preset, deep_compare) :
                                                  presets->dirty_options(left_preset, right_preset, deep_compare);

    if (dirty_options.empty()) { return on_empty(_L("Please select two different presets A and B to compare"), "equal"); }

    do_update_tree(left_preset, right_preset, dirty_options);
}

void DiffPresetDialog::on_dpi_changed(const wxRect&)
{
    int em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CANCEL});

    SetMinSize(FromDIP(wxSize(800, 600)));

    m_selector->Rescale();

    m_tree->Rescale(em);
    m_empty_state->Rescale();

    // Re-assert the fixed size at the new DPI (FromDIP re-scales) rather than Fit()-ing to content.
    SetSize(FromDIP(wxSize(800, 600)));
    Refresh();
}

void DiffPresetDialog::on_sys_color_changed()
{
#ifdef _WIN32
    // Re-theme the whole dialog tree — mirrors the ctor's UpdateDlgDarkUI so a live light/dark
    // switch also reaches the tab bar and A/B selector (both hardcode a white background); the
    // narrower UpdateAllStaticTextDarkUI(this) only covered direct children and missed them.
    wxGetApp().UpdateDlgDarkUI(this);
    m_tree_frame->SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::Grey400));
    m_tree->ApplyDarkUI();
#endif

    m_selector->Rescale();
    // msw_rescale updates just icons, so use it
    m_tree->Rescale();
    m_empty_state->Rescale();
    Refresh();
}

void DiffPresetDialog::update_compatibility(const std::string& preset_name, Preset::Type type, PresetBundle* preset_bundle)
{
    PresetCollection* presets = get_preset_collection(type, preset_bundle);

    bool print_tab = type == Preset::TYPE_PRINT || type == Preset::TYPE_SLA_PRINT;
    bool printer_tab = type == Preset::TYPE_PRINTER;
    bool technology_changed = false;

    if (printer_tab) {
        const Preset& new_printer_preset = *presets->find_preset(preset_name, true);
        PrinterTechnology    old_printer_technology = presets->get_selected_preset().printer_technology();
        PrinterTechnology    new_printer_technology = new_printer_preset.printer_technology();

        technology_changed = old_printer_technology != new_printer_technology;
    }

    // select preset
    presets->select_preset_by_name(preset_name, false);

    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    // The following method should not discard changes of current print or filament presets on change of a printer profile,
    // if they are compatible with the current printer.
    auto update_compatible_type = [](bool technology_changed, bool on_page, bool show_incompatible_presets) {
        return  technology_changed ? PresetSelectCompatibleType::Always :
            on_page ? PresetSelectCompatibleType::Never :
            show_incompatible_presets ? PresetSelectCompatibleType::OnlyIfWasCompatible : PresetSelectCompatibleType::Always;
    };
    if (print_tab || printer_tab)
        preset_bundle->update_compatible(
            update_compatible_type(technology_changed, print_tab, true),
            update_compatible_type(technology_changed, false, true));

    bool is_left_presets = preset_bundle == m_preset_bundle_left.get();
    PrinterTechnology pr_tech = preset_bundle->printers.get_selected_preset().printer_technology();

    // Refresh the active combo on the changed side if its type is affected by this compatibility
    // change. With a single active pair, a combo for a different (currently hidden) type does not
    // exist; it is (re)built fresh - and thus already current - when its tab is next selected.
    PresetComboBox *cb = is_left_presets ? m_selector->left() : m_selector->right();
    if (cb) {
        Preset::Type presets_type = cb->get_type();
        if ((print_tab && (
                (pr_tech == ptFFF && presets_type == Preset::TYPE_FILAMENT) ||
                (pr_tech == ptSLA && presets_type == Preset::TYPE_SLA_MATERIAL) )) ||
            (printer_tab && (
                (pr_tech == ptFFF && (presets_type == Preset::TYPE_PRINT || presets_type == Preset::TYPE_FILAMENT) ) ||
                (pr_tech == ptSLA && (presets_type == Preset::TYPE_SLA_PRINT || presets_type == Preset::TYPE_SLA_MATERIAL) )) ))
            cb->update();
    }

    if (technology_changed &&
        m_preset_bundle_left.get()->printers.get_selected_preset().printer_technology() ==
        m_preset_bundle_right.get()->printers.get_selected_preset().printer_technology())
    {
        m_pr_technology = m_preset_bundle_left.get()->printers.get_edited_preset().printer_technology();
        rebuild_tabs();
        update_controls_visibility(m_view_type);
    }
}

}

}    // namespace Slic3r::GUI
