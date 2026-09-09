#ifndef slic3r_AssemblyExportProgressWindow_hpp_
#define slic3r_AssemblyExportProgressWindow_hpp_

#include <functional>
#include <wx/frame.h>
#include <wx/string.h>

class wxGauge;
class wxStaticText;
class wxWindow;
class Button;

namespace Slic3r {
namespace GUI {

class AssemblyExportProgressWindow : public wxFrame
{
public:
    explicit AssemblyExportProgressWindow(wxWindow *parent);

    void set_cancel_callback(std::function<void()> cb);
    void enable_cancel(bool enable);
    void update_progress(const wxString &message, int value, int maximum, wxWindow *anchor);
    void position_near_anchor(wxWindow *anchor);

private:
    void on_cancel();

    wxStaticText *m_message{nullptr};
    wxGauge      *m_gauge{nullptr};
    wxStaticText *m_percent{nullptr};
    Button       *m_cancel{nullptr};
    std::function<void()> m_cancel_cb;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_AssemblyExportProgressWindow_hpp_
