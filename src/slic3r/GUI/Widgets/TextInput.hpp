#ifndef slic3r_GUI_TextInput_hpp_
#define slic3r_GUI_TextInput_hpp_

#include <wx/textctrl.h>
#include "StaticBox.hpp"

class TextInputValChecker;
class TextInput : public wxNavigationEnabled<StaticBox>
{

    wxSize labelSize;
    ScalableBitmap icon;
    ScalableBitmap icon_1;
    StateColor     label_color;
    StateColor     text_color;
    wxTextCtrl * text_ctrl;
    
    wxString       m_unit;
    wxString  static_tips;
    wxSize    static_tips_size;
    wxBitmap  static_tips_icon;

    std::vector<std::shared_ptr<TextInputValChecker>> m_checkers;

    static const int TextInputWidth = 200;
    static const int TextInputHeight = 50;

public:
    TextInput();

    TextInput(wxWindow *     parent,
              wxString       text,
              wxString       label = "",
              wxString       icon  = "",
              const wxPoint &pos   = wxDefaultPosition,
              const wxSize & size  = wxDefaultSize,
              long           style = 0,
              wxString       uint  = "");
    virtual ~TextInput() {};

public:
    void Create(wxWindow *     parent,
              wxString       text,
              wxString       label = "",
              wxString       icon  = "",
              const wxPoint &pos   = wxDefaultPosition,
              const wxSize & size  = wxDefaultSize,
              long           style = 0, 
              wxString       uint  = "");

    void SetCornerRadius(double radius);

    void SetLabel(const wxString& label);

    void SetStaticTips(const wxString& tips, const wxBitmap& bitmap);

    void SetIcon(const wxBitmap & icon);
    void SetIcon(const wxString & icon);

    void SetIcon_1(const wxString &icon);

    void SetLabelColor(StateColor const &color);

    void SetTextColor(StateColor const &color);

    virtual void Rescale();

    virtual bool Enable(bool enable = true) override;

    virtual void SetMinSize(const wxSize& size) override;

    wxTextCtrl *GetTextCtrl() { return text_ctrl; }

    wxTextCtrl const *GetTextCtrl() const { return text_ctrl; }

    void SetValCheckers(const std::vector<std::shared_ptr<TextInputValChecker>>& checkers) { m_checkers = checkers; }
    bool CheckValid(bool pop_dlg = true) const;

protected:
    virtual void OnEdit() {}

    virtual void DoSetSize(
        int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);

    void DoSetToolTipText(wxString const &tip) override;

private:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void messureSize();

    DECLARE_EVENT_TABLE()
};


class TextInputValChecker
{
protected:
    TextInputValChecker() = default;

public:
    virtual ~TextInputValChecker() = default;
    virtual wxString CheckValid(const wxString& value) const { return wxEmptyString; };// return wxEmptyString if valid, otherwise return error message

    static std::shared_ptr<TextInputValChecker> CreateIntMinChecker(int val);
    static std::shared_ptr<TextInputValChecker> CreateIntRangeChecker(int min, int max);
    static std::shared_ptr<TextInputValChecker> CreateDoubleMinChecker(double min);
    static std::shared_ptr<TextInputValChecker> CreateDoubleRangeChecker(double min, double max, bool enable);
};

class TextInputValIntMinChecker : public TextInputValChecker
{
public:
    TextInputValIntMinChecker(int min_value) : m_min_value(min_value) {};
    virtual wxString CheckValid(const wxString& value)  const override;

protected:
    int m_min_value{ 0 };
};

class TextInputValIntRangeChecker : public TextInputValChecker
{
public:
    TextInputValIntRangeChecker(int min_val, int max_val) : m_min_value(min_val), m_max_value(max_val) {};
    virtual wxString CheckValid(const wxString& value) const override;

protected:
    int m_min_value{ 0 };
    int m_max_value{ 0 };
};

class TextInputValDoubleMinChecker : public TextInputValChecker
{
public:
    TextInputValDoubleMinChecker(double min_val) : m_min_value(min_val) {};
    virtual wxString CheckValid(const wxString& value) const override;

protected:
    double m_min_value{ 0.0 };
};

class TextInputValDoubleRangeChecker : public TextInputValChecker
{
public:
    TextInputValDoubleRangeChecker(double min_val, double max_val, bool enable_empty = false) : m_min_value(min_val), m_max_value(max_val) {EnableEmpty(enable_empty); };
    virtual wxString CheckValid(const wxString& value) const override;

public:
    void EnableEmpty(bool enable_empty) { m_enable_empty = enable_empty;};

protected:
    bool m_enable_empty = false;
    double m_min_value{ 0.0 };
    double m_max_value{ 0.0 };
};

#endif // !slic3r_GUI_TextInput_hpp_
