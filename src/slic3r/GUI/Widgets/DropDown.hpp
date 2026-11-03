#ifndef slic3r_GUI_DropDown_hpp_
#define slic3r_GUI_DropDown_hpp_

#include <boost/date_time/posix_time/posix_time.hpp>
#include <wx/stattext.h>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"
#include "PopupWindow.hpp"

#define DD_NO_CHECK_ICON    0x0001
#define DD_NO_TEXT          0x0002
#define DD_STYLE_MASK       0x0003

#define DD_ITEM_STYLE_SPLIT_ITEM  0x0001 // ----text----, text with horizontal line arounds
#define DD_ITEM_STYLE_DISABLED    0x0002 // ----text----, text with horizontal line arounds
#define DD_ITEM_STYLE_DIMMED      0x0004 // gray text, but still selectable
#define DD_ITEM_STYLE_SYNCED      0x0008 // draw a small synced badge at the row's right edge

wxDECLARE_EVENT(EVT_DISMISS, wxCommandEvent);

class DropDown : public PopupWindow
{
public:
    struct Item
    {
        wxString text;
        wxString text_static_tips;// display static tips for TextInput.eg.PrinterInfoBox
        wxBitmap icon;
        wxBitmap icon_textctrl;// display icon for TextInput.eg.PrinterInfoBox
        void *   data{nullptr};
        wxString group{};
        wxString alias{};
        wxString tip{};
        wxString suffix{};
        int      flag{0};
        int      style{ 0 };// the style of item
    };

private:
    std::vector<Item> &items;
    size_t             count = 0;
    wxString           group;
    bool               need_sync  = false;
    int                selection  = -1;
    int                hover_item = -1;

    DropDown * subDropDown { nullptr };
    DropDown * mainDropDown { nullptr };

    double radius = 0;
    bool   use_content_width = false;
    bool   limit_max_content_width = false;
    bool   align_icon        = false;
    bool   text_off          = false;
    int    max_visible_rows  = 15;
    int    trailing_padding  = 0; // DIP

    wxSize textSize;
    wxSize iconSize;
    wxSize rowSize;
    int    fit_width = 0; // row width before the display clamp, so a clamped popup can grow back

    StateHandler state_handler;
    StateColor   text_color;
    StateColor   border_color;
    StateColor   selector_border_color;
    StateColor   selector_background_color;
    ScalableBitmap check_bitmap;
    ScalableBitmap arrow_bitmap;
    ScalableBitmap synced_bitmap;

    bool pressedDown = false;
    boost::posix_time::ptime dismissTime;
    wxPoint                  offset; // x not used
    wxPoint                  dragStart;

public:
    DropDown(std::vector<Item> &items);

    DropDown(wxWindow *parent, std::vector<Item> &items, long style = 0);

    void Create(wxWindow * parent, long style = 0);

public:
    void Invalidate(bool clear = false);

    int GetSelection() const { return selection; }

    void SetSelection(int n);

    wxString GetValue() const;
    void     SetValue(const wxString &value);

public:
    void SetCornerRadius(double radius);

    void SetBorderColor(StateColor const & color);

    void SetSelectorBorderColor(StateColor const & color);

    void SetTextColor(StateColor const &color);

    void SetSelectorBackgroundColor(StateColor const &color);

    /**
     * \brief Size the list to its widest item instead of the control width
     *
     * \param use                      list width follows the widest item, never narrower than the control
     * \param limit_max_content_width  list width stops at a fixed maximum; longer items end with "..."
     */
    void SetUseContentWidth(bool use, bool limit_max_content_width = false);

    void SetAlignIcon(bool align);

    /**
     * \brief Reserve extra blank space after each item's content, which widens the list
     *
     * \param padding    extra width in DIP; only grows the list when it sizes to its content
     */
    void SetTrailingPadding(int padding);

public:
    void Rescale();

    bool HasDismissLongTime();

protected:
    void Dismiss() override;

    void OnDismiss() override;

private:
    void paintEvent(wxPaintEvent& evt);
    void paintNow();

    void render(wxDC& dc);

    int hoverIndex();

    int selectedItem();

    /**
     * \brief Width to keep free at the row's right edge for the synced badge, or 0 when no row shows it
     */
    int synced_badge_width() const;

    friend class ComboBox;
    void messureSize();
    void autoPosition();
    void fitWidthToDisplay(wxSize &size);

    // some useful events
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent &event);
    void mouseCaptureLost(wxMouseCaptureLostEvent &event);
    void mouseMove(wxMouseEvent &event);
    void mouseWheelMoved(wxMouseEvent &event);

    void sendDropDownEvent();


    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_DropDown_hpp_
