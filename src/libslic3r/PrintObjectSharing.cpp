#include "Print.hpp"

#include "Layer.hpp"

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {

void PrintObject::set_shared_object(PrintObject *object)
{
    m_shared_object = object;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": this=%1%, found shared object from %2%") % this % m_shared_object;
}

void PrintObject::clear_shared_object()
{
    if (m_shared_object) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": this=%1%, clear previous shared object data %2%") % this % m_shared_object;
        m_layers.clear();
        m_support_layers.clear();

        m_shared_object = nullptr;

        invalidate_all_steps_without_cancel();
    }
}

void PrintObject::copy_layers_from_shared_object()
{
    if (m_shared_object) {
        m_layers.clear();
        m_support_layers.clear();

        firstLayerObjSliceByVolume.clear();
        firstLayerObjSliceByGroups.clear();

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": this=%1%, copied layers from object %2%") % this % m_shared_object;
        m_layers         = m_shared_object->layers();
        m_support_layers = m_shared_object->support_layers();

        firstLayerObjSliceByVolume = m_shared_object->firstLayerObjSlice();
        firstLayerObjSliceByGroups = m_shared_object->firstLayerObjGroups();
    }
}

void PrintObject::copy_layers_overhang_from_shared_object()
{
    if (m_shared_object) {
        for (size_t index = 0; index < m_layers.size() && index < m_shared_object->m_layers.size(); index++) {
            Layer *layer_src           = m_layers[index];
            layer_src->loverhangs      = m_shared_object->m_layers[index]->loverhangs;
            layer_src->loverhangs_bbox = m_shared_object->m_layers[index]->loverhangs_bbox;
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": this=%1%, copied layer overhang from object %2%") % this % m_shared_object;
    }
}

} // namespace Slic3r
