#include "OrientJob.hpp"

#include "libslic3r/Model.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"


namespace Slic3r { namespace GUI {


void OrientJob::clear_input()
{
    const Model &model = m_plater->model();
    
    size_t count = 0, cunprint = 0; // To know how much space to reserve
    for (auto obj : model.objects)
        for (auto mi : obj->instances)
            mi->printable ? count++ : cunprint++;
        
    m_selected.clear();
    m_unselected.clear();
    m_unprintable.clear();
    m_selected.reserve(count);
    m_unselected.reserve(count);
    m_unprintable.reserve(cunprint);
}

void OrientJob::prepare_all() {
    clear_input();
    
    for (ModelObject* obj : m_plater->model().objects)
    {
        for (size_t inst_idx = 0; inst_idx < obj->instances.size(); ++inst_idx)
        {
            ModelInstance* mi = obj->instances[inst_idx];
            auto& cont = mi->printable ? m_selected : m_unprintable;
            cont.emplace_back(get_orient_mesh(mi, m_plater));
        }
    }
}

void OrientJob::prepare_selected() {
    clear_input();
    
    Model &model = m_plater->model();
    
    std::vector<const Selection::InstanceIdxsList *>
            obj_sel(model.objects.size(), nullptr);
    
    for (auto &s : m_plater->get_selection().get_content())
        if (s.first < int(obj_sel.size()))
            obj_sel[size_t(s.first)] = &s.second;
    
    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx) {
        const Selection::InstanceIdxsList * instlist = obj_sel[oidx];
        ModelObject *mo = model.objects[oidx];

        for (size_t inst_idx = 0; inst_idx < mo->instances.size(); ++inst_idx)
        {
            ModelInstance* mi = mo->instances[inst_idx];
            auto& cont = mo->printable ? (instlist ? m_selected : m_unselected) : m_unprintable;
            OrientMesh&& om = get_orient_mesh(mi, m_plater);
            cont.emplace_back(std::move(om));
        }
    }
    
}

void OrientJob::prepare()
{
    wxGetKeyState(WXK_SHIFT) ? prepare_selected() : prepare_all();
}

void OrientJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &) {
        PlaterJob::on_exception(eptr);
    }
}

void OrientJob::process()
{
    auto start = std::chrono::steady_clock::now();
    static const auto arrangestr = _(L("Orienting"));

    const GLCanvas3D::OrientSettings& settings = m_plater->canvas3D()->get_orient_settings();

    orientation::OrientParams params;
    orientation::OrientParamsArea params_area;
    if (settings.min_area) {
        memcpy(&params, &params_area, sizeof(params));
        params.min_volume = false;
    }
    else {
        params.min_volume = true;
    }
  
    auto count = unsigned(m_selected.size() + m_unprintable.size());
    params.stopcondition = [this]() { return was_canceled(); };

    params.progressind = [this, count](unsigned st, std::string orientstr) {
        st += m_unprintable.size();
        if (st > 0) update_status(int(count - st), orientstr);
    };

    orientation::orient(m_selected, m_unselected, params);

    auto time_elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(3) << "Orient " << m_selected.back().name << " in " << time_elapsed.count() << " seconds. "
        << "Orientation: " << m_selected.back().orientation.transpose() << "; v,phi: " << m_selected.back().axis.transpose() << ", " << m_selected.back().angle << "; euler: " << m_selected.back().euler_angles.transpose();

    // finalize just here.
    update_status(int(count),
        was_canceled() ? _(L("Orienting canceled."))
        : _(L(ss.str().c_str())));
}

void OrientJob::finalize() {
    // Ignore the arrange result if aborted.
    if (was_canceled()) return;

    for (OrientMesh& mesh : m_selected)
    {
        mesh.apply();
    }
    
    
    m_plater->update();

    // BBS
    //wxGetApp().obj_manipul()->set_dirty();

    Job::finalize();
}

}} // namespace Slic3r::GUI
