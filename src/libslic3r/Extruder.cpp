#include "Extruder.hpp"

namespace Slic3r {

std::vector<double> Extruder::m_share_E = {0.,0.};
std::vector<double> Extruder::m_share_retracted = {0.,0.};


// TODO(米粒)：后续增加通过 (GCodeConfig, volume_type, extruder_type) 取参数的接口

Extruder::Extruder(unsigned int id, GCodeConfig *config, bool share_extruder) :
    m_id(id),
    m_config(config),
    m_share_extruder(share_extruder)
{
    reset();
    // cache values that are going to be called often
    m_e_per_mm3 = this->filament_flow_ratio();
    m_e_per_mm3 /= this->filament_crossection();
}

unsigned int Extruder::extruder_id() const
{
    assert(m_config);
    if (m_id < m_config->filament_map.size()) {
        return m_config->filament_map.get_at(m_id) - 1;
    }
    return 0;
}

// The park state of a filament belongs to the filament path holding it, i.e. to its hotend.
// On single-extruder multi-nozzle machines (handle_hotend_as_extruder) filament_map is always
// all-1, so extruder_id() would collapse every hotend onto one slot and a filament would
// deretract whatever the previously printed one retracted. Index by hotend there instead;
// filaments sharing a hotend still share the slot, which is what happens physically.
unsigned int Extruder::retracted_slot_id() const
{
    assert(m_config);
    if (m_config->handle_hotend_as_extruder && m_id < m_config->filament_nozzle_map.size()) {
        // filament_nozzle_map is a 0 based index into the nozzle list.
        int nozzle_idx = m_config->filament_nozzle_map.get_at(m_id);
        if (nozzle_idx >= 0)
            return unsigned(nozzle_idx);
    }
    return extruder_id();
}

// filament_nozzle_map is refreshed per layer and may address more hotends than the initial
// slot count, so grow on demand instead of assuming a fixed size.
double& Extruder::share_retracted(unsigned int slot)
{
    if (slot >= m_share_retracted.size())
        m_share_retracted.resize(slot + 1, 0.);
    return m_share_retracted[slot];
}

double Extruder::get_share_retracted_length() const
{
    return share_retracted(retracted_slot_id());
}

unsigned int Extruder::nozzle_id() const
{
    assert(m_config);
    if (m_id < m_config->filament_nozzle_map.size()) {
        return m_config->filament_nozzle_map.get_at(m_id) - 1;
    }
    return 0;
}

double Extruder::extrude(double dE)
{
    // BBS
    if (m_share_extruder) {
        if (m_config->use_relative_e_distances)
            m_share_E[extruder_id()] = 0.;
        m_share_E[extruder_id()] += dE;
        m_absolute_E += dE;
        if (dE < 0.)
            share_retracted(retracted_slot_id()) -= dE;
    } else {
        // in case of relative E distances we always reset to 0 before any output
        if (m_config->use_relative_e_distances)
            m_E = 0.;
        m_E          += dE;
        m_absolute_E += dE;
        if (dE < 0.)
            m_retracted -= dE;
    }
    return dE;
}

/* This method makes sure the extruder is retracted by the specified amount
   of filament and returns the amount of filament retracted.
   If the extruder is already retracted by the same or a greater amount,
   this method is a no-op.
   The restart_extra argument sets the extra length to be used for
   unretraction. If we're actually performing a retraction, any restart_extra
   value supplied will overwrite the previous one if any. */
double Extruder::retract(double length, double restart_extra)
{
    // BBS
    if (m_share_extruder) {
        if (m_config->use_relative_e_distances)
            m_share_E[extruder_id()] = 0.;
        const double retracted = share_retracted(retracted_slot_id());
        double to_retract = std::max(0., length - retracted);
        // A shorter no-op retract must not replace the restart extra of the retract that parked the filament.
        if (to_retract > 0. || length + EPSILON >= retracted)
            m_restart_extra = restart_extra;
        if (to_retract > 0.) {
            m_share_E[extruder_id()]             -= to_retract;
            m_absolute_E          -= to_retract;
            share_retracted(retracted_slot_id()) += to_retract;
        }
        return to_retract;
    } else {
        // in case of relative E distances we always reset to 0 before any output
        if (m_config->use_relative_e_distances)
            m_E = 0.;
        double to_retract = std::max(0., length - m_retracted);
        if (to_retract > 0. || length + EPSILON >= m_retracted)
            m_restart_extra = restart_extra;
        if (to_retract > 0.) {
            m_E             -= to_retract;
            m_absolute_E    -= to_retract;
            m_retracted     += to_retract;
        }
        return to_retract;
    }
}

double Extruder::unretract()
{
    // BBS
    if (m_share_extruder) {
        double dE = share_retracted(retracted_slot_id()) + m_restart_extra;
        this->extrude(dE);
        share_retracted(retracted_slot_id()) = 0.;
        m_restart_extra                  = 0.;
        return dE;
    } else {
        double dE = m_retracted + m_restart_extra;
        this->extrude(dE);
        m_retracted     = 0.;
        m_restart_extra = 0.;
        return dE;
    }
}

// Used filament volume in mm^3.
double Extruder::extruded_volume() const
{
    // BBS
    if (m_share_extruder) {
        // FIXME: need to count m_retracted for share extruder machine
        return this->used_filament() * this->filament_crossection();
    } else {
        return this->used_filament() * this->filament_crossection();
    }
}

// Used filament length in mm.
double Extruder::used_filament() const
{
    // BBS
    if (m_share_extruder) {
        // FIXME: need to count retracted length for share-extruder machine
        return m_absolute_E;
    } else {
        return m_absolute_E + m_retracted;
    }
}

double Extruder::filament_diameter() const
{
    return m_config->filament_diameter.get_at(m_id);
}

double Extruder::filament_density() const
{
    return m_config->filament_density.get_at(m_id);
}

double Extruder::filament_cost() const
{
    return m_config->filament_cost.get_at(m_id);
}

double Extruder::filament_flow_ratio() const
{
    return m_config->filament_flow_ratio.get_at(get_filament_config_idx(*m_config, m_id));
}

// Return a "retract_before_wipe" percentage as a factor clamped to <0, 1>
double Extruder::retract_before_wipe() const
{
    return std::min(1., std::max(0., m_config->retract_before_wipe.get_at(get_filament_config_idx(*m_config, m_id)) * 0.01));
}

double Extruder::retraction_length() const
{
    return m_config->retraction_length.get_at(get_filament_config_idx(*m_config, m_id));
}

int Extruder::retract_speed() const
{
    return int(floor(m_config->retraction_speed.get_at(get_filament_config_idx(*m_config, m_id))+0.5));
}

int Extruder::deretract_speed() const
{
    int speed = int(floor(m_config->deretraction_speed.get_at(get_filament_config_idx(*m_config, m_id)) + 0.5));
    return (speed > 0) ? speed : this->retract_speed();
}

double Extruder::retract_restart_extra() const
{
    return m_config->retract_restart_extra.get_at(get_filament_config_idx(*m_config, m_id));
}

const ConfigOptionFloatsNullable *Extruder::filament_toolchange_override(const ConfigOptionFloatsNullable &opt) const
{
    // Filament toolchange overrides only affect the retract / deretract emitted by the slicer on
    // handle_hotend_as_extruder machines. The printer values stay untouched, so change_filament_gcode
    // still sees them. PVA sharing a hotend with other filaments keeps the printer values.
    if (!m_config->handle_hotend_as_extruder || !m_exclusive_nozzle || m_id >= m_config->filament_type.size() ||
        m_config->filament_type.get_at(m_id) != "PVA")
        return nullptr;

    const size_t filament_config_idx = get_filament_config_idx(*m_config, m_id);
    if (filament_config_idx >= opt.size() || opt.is_nil(filament_config_idx))
        return nullptr;
    return &opt;
}

double Extruder::retract_length_toolchange() const
{
    if (const ConfigOptionFloatsNullable *filament_len = filament_toolchange_override(m_config->filament_retract_length_toolchange))
        return filament_len->get_at(get_filament_config_idx(*m_config, m_id));
    return m_config->retract_length_toolchange.get_at(extruder_id());
}

double Extruder::retract_restart_extra_toolchange() const
{
    if (const ConfigOptionFloatsNullable *filament_extra = filament_toolchange_override(m_config->filament_retract_restart_extra_toolchange))
        return filament_extra->get_at(get_filament_config_idx(*m_config, m_id));
    return m_config->retract_restart_extra_toolchange.get_at(extruder_id());
}

}
