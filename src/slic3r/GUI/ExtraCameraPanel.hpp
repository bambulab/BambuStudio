#ifndef slic3r_GUI_ExtraCameraPanel_hpp_
#define slic3r_GUI_ExtraCameraPanel_hpp_

#include <wx/panel.h>
#include <string>
#include <vector>

class wxChoice;
class wxButton;
class wxWebView;

namespace Slic3r {
namespace GUI {

// A panel for the Device tab that shows a user-configurable list of web
// streams (e.g. a Raspberry Pi running mjpg-streamer / ustreamer /
// camera-streamer) in an embedded web view. Entries are stored in AppConfig.
class ExtraCameraPanel : public wxPanel
{
public:
    explicit ExtraCameraPanel(wxWindow *parent);

    // Call with true when the tab becomes visible and false when it is hidden,
    // so the stream is only pulled while someone is looking at it.
    void set_active(bool active);

private:
    struct Entry
    {
        std::string name;
        std::string url;
    };

    void load_entries();
    void save_entries();
    void refresh_choice();
    void load_selected();
    void unload();
    void on_manage();

    std::vector<Entry> m_entries;
    int                m_selected{0};
    bool               m_active{false};
    std::string        m_loaded_url;

    wxChoice * m_choice{nullptr};
    wxButton * m_reload{nullptr};
    wxButton * m_manage{nullptr};
    wxWebView *m_web{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif
