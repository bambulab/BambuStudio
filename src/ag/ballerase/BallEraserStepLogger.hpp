#ifndef slic3r_BallEraserStepLogger_hpp_
#define slic3r_BallEraserStepLogger_hpp_

#include <boost/dll/runtime_symbol_info.hpp>

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace Slic3r {
namespace BallEraser {

enum class SculptApplyStep : int {
    FinalizePendingStrokeCapture = 1,
    ResolveSourceContext = 2,
    LogSourceObjectState = 3,
    BuildAppearanceSnapshot = 4,
    LogPendingStrokes = 5,
    ValidateBooleanInputs = 6,
    CollectModelPartVolumes = 7,
    BuildSourceMesh = 8,
    BuildStrokeCutters = 9,
    ApplyStrokeCutters = 10,
    NormalizeBooleanResult = 11,
    BuildOutputObjects = 12,
    CompareCoordinateSpaces = 13,
    RegisterResidualSurfaceColorBinding = 14,
    ReapplyPreservedAppearance = 15,
    SimplifyMmuSegmentation = 16,
    MarkResidualSurfaceTriangles = 17,
    RecolorResidualSurfaceTriangles = 18,
    ValidateResultObjects = 19,
    CommitModelUpdate = 20,
    CloseTool = 21,
};

inline const char *sculpt_apply_step_name(SculptApplyStep step)
{
    switch (step) {
    case SculptApplyStep::FinalizePendingStrokeCapture:      return "Finalize pending stroke capture";
    case SculptApplyStep::ResolveSourceContext:              return "Resolve source context";
    case SculptApplyStep::LogSourceObjectState:              return "Log source object state";
    case SculptApplyStep::BuildAppearanceSnapshot:           return "Build appearance snapshot";
    case SculptApplyStep::LogPendingStrokes:                 return "Log pending strokes";
    case SculptApplyStep::ValidateBooleanInputs:             return "Validate boolean inputs";
    case SculptApplyStep::CollectModelPartVolumes:           return "Collect model-part volumes";
    case SculptApplyStep::BuildSourceMesh:                   return "Build transformed source mesh";
    case SculptApplyStep::BuildStrokeCutters:                return "Build stroke cutter meshes";
    case SculptApplyStep::ApplyStrokeCutters:                return "Subtract stroke cutters";
    case SculptApplyStep::NormalizeBooleanResult:            return "Normalize boolean result components";
    case SculptApplyStep::BuildOutputObjects:                return "Build output objects";
    case SculptApplyStep::CompareCoordinateSpaces:           return "Compare coordinate spaces";
    case SculptApplyStep::RegisterResidualSurfaceColorBinding:return "Register residual surface color binding";
    case SculptApplyStep::ReapplyPreservedAppearance:        return "Reapply preserved appearance";
    case SculptApplyStep::SimplifyMmuSegmentation:           return "Simplify MMU segmentation";
    case SculptApplyStep::MarkResidualSurfaceTriangles:      return "Mark residual surface triangles";
    case SculptApplyStep::RecolorResidualSurfaceTriangles:   return "Recolor residual surface triangles";
    case SculptApplyStep::ValidateResultObjects:             return "Validate result objects";
    case SculptApplyStep::CommitModelUpdate:                 return "Commit model update";
    case SculptApplyStep::CloseTool:                         return "Close BallEraser tool";
    }
    return "Unknown sculpt step";
}

inline std::string sculpt_log_file_path()
{
    return (boost::dll::program_location().parent_path() / "s.log").string();
}

inline std::mutex &sculpt_log_mutex()
{
    static std::mutex mutex;
    return mutex;
}

inline void sculpt_log_append(const std::string &message)
{
    std::lock_guard<std::mutex> lock(sculpt_log_mutex());
    std::ofstream out(sculpt_log_file_path(), std::ios::app);
    if (!out)
        return;
    out << message << std::endl;
}

inline void sculpt_log_reset()
{
    std::lock_guard<std::mutex> lock(sculpt_log_mutex());
    std::ofstream out(sculpt_log_file_path(), std::ios::trunc);
}

inline std::string sculpt_step_label(SculptApplyStep step)
{
    std::ostringstream stream;
    stream << "STEP " << static_cast<int>(step) << ": " << sculpt_apply_step_name(step);
    return stream.str();
}

class ScopedSculptStepLog {
public:
    ScopedSculptStepLog(SculptApplyStep step, std::string details = {})
        : m_step(step), m_finished(false)
    {
        std::string message = "[START] " + sculpt_step_label(step);
        if (!details.empty())
            message += " | details: " + details;
        sculpt_log_append(message);
    }

    ScopedSculptStepLog(const ScopedSculptStepLog &) = delete;
    ScopedSculptStepLog &operator=(const ScopedSculptStepLog &) = delete;

    ~ScopedSculptStepLog()
    {
        if (!m_finished)
            finish("scope exited without explicit completion comment");
    }

    void finish(std::string comments = {})
    {
        if (m_finished)
            return;

        std::string message = "[END] " + sculpt_step_label(m_step);
        if (!comments.empty())
            message += " | comments: " + comments;
        sculpt_log_append(message);
        m_finished = true;
    }

private:
    SculptApplyStep m_step;
    bool m_finished;
};

} // namespace BallEraser
} // namespace Slic3r

#endif // slic3r_BallEraserStepLogger_hpp_
