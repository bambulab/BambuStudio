#ifndef ORIENTJOB_HPP
#define ORIENTJOB_HPP

#include "PlaterJob.hpp"
#include "libslic3r/Orient.hpp"

namespace Slic3r {

class ModelObject;

namespace GUI {

class OrientJob : public PlaterJob
{
    using OrientMesh = orientation::OrientMesh;
    using OrientMeshs = orientation::OrientMeshs;

    OrientMeshs m_selected, m_unselected, m_unprintable;

    // clear m_selected and m_unselected, reserve space for next usage
    void clear_input();

    // Prepare all objects on the bed regardless of the selection
    void prepare_all();
    
    // Prepare the selected and unselected items separately. If nothing is
    // selected, behaves as if everything would be selected.
    void prepare_selected();
    
protected:
    
    void prepare() override;

    void on_exception(const std::exception_ptr &) override;
    
public:
    OrientJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
        : PlaterJob{std::move(pri), plater}
    {}    
    
    void process() override;
    
    void finalize() override;
#if 0
    static
    orientation::OrientMesh get_orient_mesh(ModelObject* obj, const Plater* plater)
    {
        using OrientMesh = orientation::OrientMesh;
        OrientMesh om;
        om.name = obj->name;
        om.mesh = obj->mesh(); // don't know the difference to obj->raw_mesh(). Both seem OK
        om.setter = [obj, plater](const OrientMesh& p) {
            obj->rotate(p.angle, p.axis);
            obj->ensure_on_bed();
        };
        return om;
    }
#endif
    static
    orientation::OrientMesh get_orient_mesh(ModelInstance* instance, const Plater* plater)
    {
        using OrientMesh = orientation::OrientMesh;
        OrientMesh om;
        om.name = instance->get_object()->name;
        om.mesh = instance->get_object()->mesh(); // don't know the difference to obj->raw_mesh(). Both seem OK
        om.setter = [instance, plater](const OrientMesh& p) {
            instance->rotate(p.rotation_matrix);
            instance->get_object()->ensure_on_bed();
        };
        return om;
    }
};


}} // namespace Slic3r::GUI

#endif // ORIENTJOB_HPP
