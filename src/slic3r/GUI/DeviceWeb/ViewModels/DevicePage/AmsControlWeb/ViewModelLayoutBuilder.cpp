#include "ViewModelLayoutBuilder.hpp"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI { namespace AmsControlWebLayout {

namespace SchemaFormat = AmsControlWebSchema::format;
namespace SchemaValues = AmsControlWebSchema::values;

namespace {

// A group is one page of a column, and the room in it is counted in single-slot
// objects: a 4-slot AMS takes two, a single-slot AMS or an external spool takes
// one. A full-width panel fits twice what a half-width one does, which is the
// only difference the two layout styles make to grouping.
constexpr int WEIGHT_SINGLE_SLOT = 1;
constexpr int WEIGHT_MULTI_SLOT  = 2;

constexpr int GROUP_CAPACITY_FULL_WIDTH = 4;
constexpr int GROUP_CAPACITY_HALF_WIDTH = 2;

// The panels the device can ask for, one per case of the layout spec.
enum class Shape
{
    SpoolsOnly,   // one extruder, no AMS: a single full-width column
    LiteMixedRow, // one extruder, the AMS-Lite Mixed unit alongside other AMS
    KindSplit,    // one extruder otherwise: AMS on the left, spools on the right
    WiredSplit,   // two extruders: each side keeps what it is wired to
};

// What one column may draw.
struct PanelPlan
{
    const char* pos            = SchemaValues::panel_pos::single;
    int         group_capacity = GROUP_CAPACITY_FULL_WIDTH;
};

// The shape of the panel, settled before a single unit is placed.
struct LayoutPlan
{
    Shape                  shape        = Shape::SpoolsOnly;
    const char*            layout_style = SchemaValues::layout_style::single;
    std::vector<PanelPlan> panels;
};

bool is_multi_slot(const SchemaFormat::Unit& unit) { return unit.trays.size() > 1; }

// The AMS-Lite Mixed unit is drawn compact between its neighbours, so it takes
// the room of a single-slot object despite having four slots. That is what lets
// an AMS, the mixed unit and a spool share one full-width group.
int weight_of(const SchemaFormat::Unit& unit)
{
    if (unit.is_ams_lite_mixed) return WEIGHT_SINGLE_SLOT;
    return is_multi_slot(unit) ? WEIGHT_MULTI_SLOT : WEIGHT_SINGLE_SLOT;
}

const SchemaFormat::Unit* find_lite_mixed(const SchemaFormat::AmsListData& data)
{
    const auto it = std::find_if(data.ams_units.begin(), data.ams_units.end(),
                                 [](const SchemaFormat::Unit& unit) { return unit.is_ams_lite_mixed; });
    return it == data.ams_units.end() ? nullptr : &*it;
}

// One unit on its way into a column: the room it takes, whether it is a spool,
// and the wiring the two extruder split reads. `closes_group` ends the group
// after this unit even when there is room left, which is how a group of a fixed
// make-up keeps later units out.
struct Placement
{
    std::string      ams_id;
    std::string      switcher_port;
    std::vector<int> binded_extruder_ids;
    int              weight       = WEIGHT_SINGLE_SLOT;
    bool             is_spool     = false;
    bool             closes_group = false;
};

Placement placement_of(const SchemaFormat::Unit& unit)
{
    return Placement{unit.ams_id, unit.switcher_port, unit.binded_extruder_ids, weight_of(unit), false};
}

Placement placement_of(const SchemaFormat::Tray& tray)
{
    return Placement{tray.ams_id, tray.switcher_port, tray.binded_extruder_ids, WEIGHT_SINGLE_SLOT, true};
}

// One column under construction, holding a group per simplebook page of the
// classic panel in draw order. Units go into the group that is open until one no
// longer fits, which closes that group and opens the next.
class PanelBuilder
{
public:
    explicit PanelBuilder(const PanelPlan& plan) : m_capacity(plan.group_capacity)
    {
        m_panel.pos = plan.pos;
    }

    void Add(const Placement& item)
    {
        if (m_open_weight + item.weight > m_capacity) CloseGroup();
        m_open.push_back(item.ams_id);
        m_open_weight += item.weight;
        if (item.closes_group) CloseGroup();
    }

    SchemaFormat::PanelView Take()
    {
        CloseGroup();
        m_panel.visible = !m_panel.groups.empty();
        return std::move(m_panel);
    }

private:
    void CloseGroup()
    {
        if (m_open.empty()) return;
        m_panel.groups.push_back(SchemaFormat::PanelGroup{std::move(m_open)});
        m_open.clear();
        m_open_weight = 0;
    }

    int                      m_capacity;
    SchemaFormat::PanelView  m_panel;
    std::vector<std::string> m_open;
    int                      m_open_weight = 0;
};

// AMSinfo::GetDefaultPanelPos for two extruders: a unit routed through a switch
// port follows that port, then a unit bound to exactly one extruder follows that
// extruder, and anything still ambiguous lands on the right.
bool on_left_panel(const std::string& switcher_port, const std::vector<int>& binded_extruder_ids)
{
    if (switcher_port == SchemaValues::switcher_port::a) return true;
    if (switcher_port == SchemaValues::switcher_port::b) return false;
    if (binded_extruder_ids.size() == 1)
        return binded_extruder_ids.front() == DEPUTY_EXTRUDER_ID;
    return false;
}

// AMSControl::m_current_show_ams_left / _right: a column opens the group the user
// last picked in it, else the one holding the selected slot, else its first group.
// External spools are eligible, the same way AddAms pushes every info it draws
// into m_item_ids.
void resolve_panel_active(const std::vector<std::string>& active_ams_ids,
                          const std::string&              selected_ams_id,
                          SchemaFormat::PanelView&        panel)
{
    std::vector<std::string> ids;
    for (const auto& group : panel.groups) {
        ids.insert(ids.end(), group.ams_ids.begin(), group.ams_ids.end());
    }

    if (ids.empty()) {
        panel.active_ams_id.clear();
        return;
    }

    const auto owns = [&ids](const std::string& ams_id) {
        return std::find(ids.begin(), ids.end(), ams_id) != ids.end();
    };
    // Ordered most recent first, so the first hit is the group this column was
    // last switched to and a switch aimed at the other column is ignored here.
    for (const auto& ams_id : active_ams_ids) {
        if (owns(ams_id)) {
            panel.active_ams_id = ams_id;
            return;
        }
    }
    panel.active_ams_id = owns(selected_ams_id) ? selected_ams_id : ids.front();
}

Shape shape_of(int extruder_count, const SchemaFormat::AmsListData& data)
{
    if (extruder_count >= 2) return Shape::WiredSplit;
    if (data.ams_units.empty()) return Shape::SpoolsOnly;
    if (find_lite_mixed(data) && data.ams_units.size() > 1) return Shape::LiteMixedRow;
    return Shape::KindSplit;
}

// The only place that reads the machine, so the fill pass below never has to ask
// which one it runs on.
LayoutPlan plan_layout(MachineObject* machine_obj, const SchemaFormat::AmsListData& data)
{
    auto*     extder_system  = machine_obj ? machine_obj->GetExtderSystem() : nullptr;
    const int extruder_count = extder_system ? extder_system->GetTotalExtderCount() : 0;

    LayoutPlan plan;
    // Nothing to plan while the extruder count is still unknown.
    if (extruder_count <= 0) {
        return plan;
    }

    plan.shape = shape_of(extruder_count, data);
    if (plan.shape == Shape::SpoolsOnly || plan.shape == Shape::LiteMixedRow) {
        plan.layout_style = SchemaValues::layout_style::single;
        plan.panels.push_back(PanelPlan{SchemaValues::panel_pos::single, GROUP_CAPACITY_FULL_WIDTH});
    } else {
        plan.layout_style = SchemaValues::layout_style::left_right;
        plan.panels.push_back(PanelPlan{SchemaValues::panel_pos::left, GROUP_CAPACITY_HALF_WIDTH});
        plan.panels.push_back(PanelPlan{SchemaValues::panel_pos::right, GROUP_CAPACITY_HALF_WIDTH});
    }
    return plan;
}

// Draw order for the panel as a whole, left to right: the 4-slot AMS units, then
// the single-slot ones, then the external spools. The device reports its units
// keyed by ams_id, which happens to order them the same way, but the order the
// panel draws in should not hinge on how the ids are numbered.
std::vector<Placement> placements_in_draw_order(const LayoutPlan&                plan,
                                                const SchemaFormat::AmsListData& data)
{
    const SchemaFormat::Unit* mixed = find_lite_mixed(data);

    std::vector<const SchemaFormat::Unit*> units;
    units.reserve(data.ams_units.size());
    for (const auto& unit : data.ams_units) {
        if (&unit != mixed && is_multi_slot(unit)) units.push_back(&unit);
    }
    for (const auto& unit : data.ams_units) {
        if (&unit != mixed && !is_multi_slot(unit)) units.push_back(&unit);
    }

    std::vector<Placement> out;
    out.reserve(data.ams_units.size() + data.ext_slots.size());

    const auto push_spools = [&] {
        for (const auto& tray : data.ext_slots) out.push_back(placement_of(tray));
    };

    if (plan.shape == Shape::LiteMixedRow) {
        // The column opens with the row the classic panel draws across its three
        // areas: the leading AMS, the mixed unit, then the spools. That group has
        // a fixed make-up, so it is closed by hand and every remaining AMS starts
        // the next one even where the row still had room.
        std::size_t next = 0;
        if (!units.empty()) {
            out.push_back(placement_of(*units.front()));
            next = 1;
        }
        if (mixed) out.push_back(placement_of(*mixed));
        push_spools();
        if (!out.empty()) out.back().closes_group = true;

        for (; next < units.size(); ++next) out.push_back(placement_of(*units[next]));
        return out;
    }

    for (const auto* unit : units) out.push_back(placement_of(*unit));
    if (mixed) out.push_back(placement_of(*mixed));
    push_spools();
    return out;
}

// The column a unit belongs to, and the lone column when the panel has only one.
std::size_t preferred_column(const LayoutPlan& plan, const Placement& item)
{
    if (plan.panels.size() < 2) return 0;
    if (plan.shape == Shape::WiredSplit)
        return on_left_panel(item.switcher_port, item.binded_extruder_ids) ? 0 : 1;

    // One extruder: the AMS units hold the left column, the spools the right one.
    return item.is_spool ? 1 : 0;
}

// Walks the panel in draw order and hands every unit to the column it belongs to.
std::vector<SchemaFormat::PanelView> fill_panels(const LayoutPlan&                plan,
                                                 const SchemaFormat::AmsListData& data)
{
    if (plan.panels.empty()) return {};

    std::vector<PanelBuilder> columns;
    columns.reserve(plan.panels.size());
    for (const auto& panel_plan : plan.panels) {
        columns.emplace_back(panel_plan);
    }

    for (const auto& item : placements_in_draw_order(plan, data)) {
        columns[preferred_column(plan, item)].Add(item);
    }

    std::vector<SchemaFormat::PanelView> panels;
    panels.reserve(columns.size());
    for (auto& column : columns) {
        panels.push_back(column.Take());
    }
    return panels;
}

} // namespace

SchemaFormat::PanelLayout Build(MachineObject*                   machine_obj,
                                const SchemaFormat::AmsListData& data,
                                const std::vector<std::string>&  active_ams_ids,
                                const std::string&               selected_ams_id)
{
    const LayoutPlan plan = plan_layout(machine_obj, data);

    SchemaFormat::PanelLayout layout;
    layout.layout_style = plan.layout_style;
    layout.panels       = fill_panels(plan, data);

    for (auto& panel : layout.panels) {
        resolve_panel_active(active_ams_ids, selected_ams_id, panel);
    }

    return layout;
}

}}} // namespace Slic3r::GUI::AmsControlWebLayout
