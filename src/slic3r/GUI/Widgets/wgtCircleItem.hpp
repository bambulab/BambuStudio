#ifndef slic3r_GUI_wgtCircleItem_hpp_
#define slic3r_GUI_wgtCircleItem_hpp_

#include "StaticBox.hpp"

class wgtCircleItem : public StaticBox
{
public:
    wgtCircleItem();
    wgtCircleItem(wxWindow *parent,
                  wxWindowID id = wxID_ANY,
                  const wxPoint &pos = wxDefaultPosition,
                  const wxSize &size = wxDefaultSize,
                  long style = 0);

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxPoint &pos = wxDefaultPosition,
                const wxSize &size = wxDefaultSize,
                long style = 0);

    void SetValue(int value);
    int GetValue() const;

    void SetCircleColor(const StateColor &color);
    void SetTextColor(const StateColor &color);

protected:
    void doRender(wxDC &dc) override;

private:
    int m_value = 0;
    StateColor m_circle_color;
    StateColor m_text_color;
    wxFont m_text_font;
};

#endif // slic3r_GUI_wgtCircleItem_hpp_
