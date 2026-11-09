/*****************************************************************//**
 * \file   ViewModel.hpp
 * \brief  ViewModel of the Web Select Filament dialog.
 *
 * Layers:
 *   \li ViewModel.cpp        - command dispatch, state report, theme, debug.
 *   \li ViewModelBuilder.cpp - input -> snapshot -> Schema::format::State.
 *   \li ViewModelActions.cpp - confirm / close.
 *
 * The page keeps the tab, the selected row and the search text locally and
 * only submits them on confirm; the snapshot is taken once in Open().
 *********************************************************************/
#ifndef FILAMENTSELECTVM_HPP
#define FILAMENTSELECTVM_HPP

#include "libslic3r_version.h" // BBL_RELEASE_TO_PUBLIC
#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"
#include "ViewModelBuilder.hpp"

#include <functional>
#include <string>

namespace Slic3r { namespace GUI {

class FilamentSelectActions;

class FilamentSelectVM : public IViewModel {
    friend class FilamentSelectActions;

public:
    /// Ends the host dialog with a wx return code (wxID_OK / wxID_CANCEL).
    using CloseHandler = std::function<void(int)>;

    std::string GetModule() const override;

    nlohmann::json OnCommand(const std::string& submod,
                             const std::string& action,
                             const nlohmann::json& payload) override;
    void ReportState(const std::string& submod, const std::string& action) override;
    void OnSysColorChanged() override;

    /// Called by the host dialog before ShowModal(): takes the snapshot and
    /// binds where the confirmed result goes and how to close the dialog.
    void Open(const FilamentSelectInput& in, FilamentSelectResult* result_out, CloseHandler close_handler);

private:
    nlohmann::json BuildStateJson() const;
    void           RequestClose(int code) const;
#if !BBL_RELEASE_TO_PUBLIC
    nlohmann::json HandleDebug(const std::string& action, const nlohmann::json& payload);
#endif

    FilamentSelectSnapshot m_snapshot;
    FilamentSelectResult*  m_result{nullptr}; ///< owned by the host dialog
    CloseHandler           m_close_handler;
    bool                   m_opened{false};
};

}} // namespace Slic3r::GUI

#endif // FILAMENTSELECTVM_HPP
