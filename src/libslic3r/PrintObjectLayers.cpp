#include "Print.hpp"

#include "Layer.hpp"
#include "Utils.hpp"

namespace Slic3r {

void PrintObject::clear_layers()
{
    if (!m_shared_object) {
        for (Layer *l : m_layers)
            delete l;
        m_layers.clear();
    }
}

Layer* PrintObject::add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    m_layers.emplace_back(new Layer(id, this, height, print_z, slice_z));
    return m_layers.back();
}

const SupportLayer* PrintObject::get_support_layer_at_printz(coordf_t print_z, coordf_t epsilon) const
{
    coordf_t limit = print_z - epsilon;
    auto it = Slic3r::lower_bound_by_predicate(m_support_layers.begin(), m_support_layers.end(), [limit](const SupportLayer* layer) { return layer->print_z < limit; });
    return (it == m_support_layers.end() || (*it)->print_z > print_z + epsilon) ? nullptr : *it;
}

SupportLayer* PrintObject::get_support_layer_at_printz(coordf_t print_z, coordf_t epsilon)
{
    return const_cast<SupportLayer*>(std::as_const(*this).get_support_layer_at_printz(print_z, epsilon));
}

void PrintObject::clear_support_layers()
{
    if (!m_shared_object) {
        for (SupportLayer* l : m_support_layers)
            delete l;
        m_support_layers.clear();
        for (auto l : m_layers) {
            l->sharp_tails.clear();
            l->sharp_tails_height.clear();
            l->cantilevers.clear();
        }
    }
}

SupportLayer* PrintObject::add_support_layer(int id, int interface_id, coordf_t height, coordf_t print_z)
{
    m_support_layers.emplace_back(new SupportLayer(id, interface_id, this, height, print_z, -1));
    return m_support_layers.back();
}

SupportLayerPtrs::iterator PrintObject::insert_support_layer(SupportLayerPtrs::iterator pos, size_t id, size_t interface_id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    return m_support_layers.insert(pos, new SupportLayer(id, interface_id, this, height, print_z, slice_z));
}

const Layer* PrintObject::get_layer_at_printz(coordf_t print_z) const
{
    auto it = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [print_z](const Layer *layer) { return layer->print_z < print_z; });
    return (it == m_layers.end() || (*it)->print_z != print_z) ? nullptr : *it;
}

Layer* PrintObject::get_layer_at_printz(coordf_t print_z)
{
    return const_cast<Layer*>(std::as_const(*this).get_layer_at_printz(print_z));
}

const Layer* PrintObject::get_layer_at_printz(coordf_t print_z, coordf_t epsilon) const
{
    coordf_t limit = print_z - epsilon;
    auto it = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [limit](const Layer *layer) { return layer->print_z < limit; });
    return (it == m_layers.end() || (*it)->print_z > print_z + epsilon) ? nullptr : *it;
}

Layer* PrintObject::get_layer_at_printz(coordf_t print_z, coordf_t epsilon)
{
    return const_cast<Layer*>(std::as_const(*this).get_layer_at_printz(print_z, epsilon));
}

const Layer *PrintObject::get_first_layer_bellow_printz(coordf_t print_z, coordf_t epsilon) const
{
    coordf_t limit = print_z + epsilon;
    auto it = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [limit](const Layer *layer) { return layer->print_z < limit; });
    return (it == m_layers.begin()) ? nullptr : *(--it);
}

int PrintObject::get_layer_idx_get_printz(coordf_t print_z, coordf_t epsilon)
{
    coordf_t limit = print_z + epsilon;
    auto     it    = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [limit](const Layer *layer) { return layer->print_z < limit; });
    return (it == m_layers.begin()) ? -1 : std::distance(m_layers.begin(), it);
}

const Layer* PrintObject::get_layer_at_bottomz(coordf_t bottom_z, coordf_t epsilon) const
{
    coordf_t limit_upper = bottom_z + epsilon;
    coordf_t limit_lower = bottom_z - epsilon;

    for (const Layer* layer : m_layers) {
        if (layer->bottom_z() > limit_lower)
            return layer->bottom_z() < limit_upper ? layer : nullptr;
    }

    return nullptr;
}

Layer* PrintObject::get_layer_at_bottomz(coordf_t bottom_z, coordf_t epsilon)
{
    return const_cast<Layer*>(std::as_const(*this).get_layer_at_bottomz(bottom_z, epsilon));
}

} // namespace Slic3r
