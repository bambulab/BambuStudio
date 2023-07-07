#include "Extruder.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

double Extruder::m_share_E = 0.;
double Extruder::m_share_retracted = 0.;

Extruder::Extruder(unsigned int id, GCodeConfig *config, bool share_extruder) :
    m_id(id),
    m_config(config),
    m_share_extruder(m_share_extruder)
{
    reset();
    
    // cache values that are going to be called often
    m_e_per_mm3 = this->filament_flow_ratio();
    m_e_per_mm3 /= this->filament_crossection();
}

double Extruder::extrude(double dE)
{
    // BBS
    if (m_share_extruder) {
        if (m_config->use_relative_e_distances)
            m_share_E = 0.;
        m_share_E += dE;
        m_absolute_E += dE;
        if (dE < 0.)
            m_share_retracted -= dE;
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
            m_share_E = 0.;
        double to_retract = std::max(0., length - m_share_retracted);
        if (to_retract > 0.) {
            m_share_E             -= to_retract;
            m_absolute_E          -= to_retract;
            m_share_retracted     += to_retract;
        }
        return to_retract;
    } else {
        // in case of relative E distances we always reset to 0 before any output
        if (m_config->use_relative_e_distances)
            m_E = 0.;
        double to_retract = std::max(0., length - m_retracted);
        if (to_retract > 0.) {
            m_E             -= to_retract;
            m_absolute_E    -= to_retract;
            m_retracted     += to_retract;
            m_restart_extra = restart_extra;
        }
        return to_retract;
    }
}

double Extruder::unretract()
{
    // BBS
    if (m_share_extruder) {
        double dE = m_share_retracted;
        this->extrude(dE);
        m_share_retracted     = 0.;
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
    return m_config->filament_flow_ratio.get_at(m_id);
}

// Return a "retract_before_wipe" percentage as a factor clamped to <0, 1>
double Extruder::retract_before_wipe() const
{
    return std::min(1., std::max(0., m_config->retract_before_wipe.get_at(m_id) * 0.01));
}

double Extruder::retraction_length() const
{
    return m_config->retraction_length.get_at(m_id);
}

double Extruder::retract_lift() const
{
    return m_config->z_hop.get_at(m_id);
}

int Extruder::retract_speed() const
{
    return int(floor(m_config->retraction_speed.get_at(m_id)+0.5));
}

int Extruder::deretract_speed() const
{
    int speed = int(floor(m_config->deretraction_speed.get_at(m_id)+0.5));
    return (speed > 0) ? speed : this->retract_speed();
}

double Extruder::retract_restart_extra() const
{
    return m_config->retract_restart_extra.get_at(m_id);
}

double Extruder::retract_length_toolchange() const
{
    return m_config->retract_length_toolchange.get_at(m_id);
}

double Extruder::retract_restart_extra_toolchange() const
{
    return m_config->retract_restart_extra_toolchange.get_at(m_id);
}

}
