#include "Exception.hpp"
#include "Print.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "Support/SupportMaterial.hpp"
#include "Support/TreeSupport.hpp"
#include "Utils.hpp"
#include "format.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/trivial.hpp>

#include <chrono>
#include <iomanip>
#include <map>

#include <tbb/task_group.h>

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

struct POProfiler
{
    uint32_t duration1;
    uint32_t duration2;
};

void PrintObject::generate_support_preview()
{
    POProfiler profiler;

    boost::posix_time::ptime ts1 = boost::posix_time::microsec_clock::local_time();
    this->slice();
    boost::posix_time::ptime ts2 = boost::posix_time::microsec_clock::local_time();
    profiler.duration1 = (ts2 - ts1).total_milliseconds();

    this->generate_support_material();
    boost::posix_time::ptime ts3 = boost::posix_time::microsec_clock::local_time();
    profiler.duration2 = (ts3 - ts2).total_milliseconds();
}

void PrintObject::generate_support_material()
{
    if (this->set_started(posSupportMaterial)) {
        this->clear_support_layers();

        if (!has_support() && !m_print->get_no_check_flag()) {
            // Check for truly floating layers: empty layers between non-empty
            // layers indicate a physical gap that makes printing impossible
            // without support. Gaps > 2x layer_height throw an error and
            // guide the user to enable support; thinner gaps only warn.
            {
                const double gap_hard_thresh = 2.0 * m_config.layer_height.value;
                coordf_t     last_non_empty_top = 0;
                bool         found_non_empty    = false;
                bool         in_gap             = false;
                for (const Layer *layer : m_layers) {
                    if (!layer->empty()) {
                        if (in_gap) {
                            double gap_mm = layer->bottom_z() - last_non_empty_top;
                            if (gap_mm > gap_hard_thresh) {
                                throw Slic3r::SlicingError(L("Levitating objects cannot be printed without supports."), this->id().id, "enable_support");
                            } else {
                                std::string warning_message = Slic3r::format(
                                    L("It seems object %s has floating regions. Please re-orient the object or enable support generation."),
                                    this->model_object()->name);
                                this->active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL, warning_message, PrintStateBase::SlicingNeedSupportOn);
                            }
                            in_gap = false;
                        }
                        last_non_empty_top = layer->print_z;
                        found_non_empty    = true;
                    } else if (found_non_empty) {
                        in_gap = true;
                    }
                }
            }

            m_print->set_status(50, L("Checking support necessity"));
            using clock_  = std::chrono::high_resolution_clock;
            using second_ = std::chrono::duration<double, std::ratio<1>>;
            std::chrono::time_point<clock_> t0{clock_::now()};

            SupportNecessaryType sntype = this->is_support_necessary();

            double duration{std::chrono::duration_cast<second_>(clock_::now() - t0).count()};
            BOOST_LOG_TRIVIAL(info) << std::fixed << std::setprecision(0) << "is_support_necessary takes " << duration << " secs.";

            if (sntype != NoNeedSupp) {
                std::map<SupportNecessaryType, std::string> reasons = {
                    {SharpTail, L("floating regions")},
                    {Cantilever, L("floating cantilever")},
                    {LargeOverhang, L("large overhangs")}
                };
                std::string warning_message =
                    Slic3r::format(L("It seems object %s has %s. Please re-orient the object or enable support generation."),
                                   this->model_object()->name,
                                   reasons[sntype]);
                this->active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL, warning_message, PrintStateBase::SlicingNeedSupportOn);
            }
        }

        if ((this->has_support() && m_layers.size() > 1) || (this->has_raft() && !m_layers.empty())) {
            m_print->set_status(50, L("Generating support"));

            this->_generate_support_material();
            m_print->throw_if_canceled();

            // Nested TBB cancellation may interrupt support generation before
            // set_done(), so surface that state explicitly.
            if (tbb::is_current_task_group_canceling())
                throw Slic3r::CanceledException();
        }
        this->set_done(posSupportMaterial);
    }
}

std::shared_ptr<TreeSupportData> PrintObject::alloc_tree_support_preview_cache()
{
    if (!m_tree_support_preview_cache) {
        const coordf_t xy_distance = m_config.support_object_xy_distance.value;
        m_tree_support_preview_cache = std::make_shared<TreeSupportData>(*this, xy_distance, g_config_tree_support_collision_resolution);
    }

    return m_tree_support_preview_cache;
}

SupportLayer *PrintObject::add_tree_support_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    m_support_layers.emplace_back(new SupportLayer(id, 0, this, height, print_z, slice_z));
    return m_support_layers.back();
}

void PrintObject::_generate_support_material()
{
    if (is_tree(m_config.support_type.value)) {
        TreeSupport tree_support(*this, m_slicing_params);
        tree_support.throw_on_cancel = [this]() { this->throw_if_canceled(); };
        tree_support.generate();
    } else {
        PrintObjectSupportMaterial support_material(this, m_slicing_params);
        support_material.generate(*this);
    }
}

SupportNecessaryType PrintObject::is_support_necessary()
{
    const double cantilevel_dist_thresh = scale_(6);

    TreeSupport tree_support(*this, m_slicing_params);
    tree_support.support_type = SupportType::stTreeAuto;
    tree_support.detect_overhangs(true);
    this->clear_support_layers();
    if (tree_support.has_sharp_tails)
        return SharpTail;
    else if (tree_support.has_cantilever && tree_support.max_cantilever_dist > cantilevel_dist_thresh)
        return Cantilever;

    return NoNeedSupp;
}

} // namespace Slic3r
