#pragma once

#include "Widgets/RoundedPopupWindow.hpp"
#include <vector>

class wxStaticText;
class wxBoxSizer;

namespace Slic3r { namespace GUI {

class DeviceOperationsTooltip : public RoundedPopupWindow
{
public:
    explicit DeviceOperationsTooltip(wxWindow *parent);

    void SetOperations(const std::vector<wxString> &operations);

    void Dismiss() override;

private:
    wxBoxSizer   *m_sizer{nullptr};
    wxStaticText *m_title{nullptr};
    std::vector<wxStaticText *> m_rows;
};

}} // namespace Slic3r::GUI
