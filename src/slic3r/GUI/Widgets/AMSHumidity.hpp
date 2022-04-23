#ifndef slic3r_GUI_AMSHumidity_hpp_
#define slic3r_GUI_AMSHumidity_hpp_

#include <wx/window.h>
#include "../wxExtensions.hpp"

class AMSHumidity : public wxWindow
{
public:
    AMSHumidity(wxWindow *parent, wxWindowID id = wxID_ANY,  const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize);

    void create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size);

    ~AMSHumidity();

public:
    void SetValue(int step);

protected:
    void         paintEvent(wxPaintEvent &evt);
    void         render(wxDC &dc);
    void         doRender(wxDC &dc);
    virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_AMSHumidity_hpp_