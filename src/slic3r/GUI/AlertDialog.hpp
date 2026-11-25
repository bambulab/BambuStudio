/**
 * \file AlertDialog.hpp
 * \brief Message, confirmation and warning dialog in the current dialog design
 *
 * Use AlertDialog for every new message dialog. MsgDialog, MessageDialog and
 * RichMessageDialog (MsgDialog.hpp) are deprecated.
 */
#pragma once
#include <memory>
#include <wx/bitmap.h>
#include "GUI_Utils.hpp"

namespace Slic3r::GUI {

/**
 * \brief Visual weight of an AlertDialog button
 *
 * Primary and Danger buttons always sit to the right of Secondary ones.
 */
enum class AlertButtonRole
{
    Secondary, ///< outlined, for dismissive or alternative actions
    Primary,   ///< filled brand colour, the recommended action
    Danger,    ///< filled red, a destructive recommended action
};

/**
 * \brief Message / confirmation dialog in the current dialog design
 *
 * Sections are stacked top-down in a fixed order whatever the call order:
 * image, message, custom content, tips, warnings, "don't show again" checkbox,
 * then a right-aligned button row with recommended actions rightmost.
 * Sections that were never set take no space. The dialog is 520 DIP wide
 * unless custom content is wider.
 *
 * Clicking a button ends the modal loop and ShowModal() returns its id;
 * closing the window or pressing Esc returns wxID_CANCEL. Shown modeless, the
 * dialog closes instead and GetReturnCode() holds that id.
 */
class AlertDialog : public DPIDialog
{
public:
    /**
     * \param parent  owner window; the main frame is used when null
     * \param title   window caption
     * \param message body text, wrapped to the dialog width; scrolls when very long
     */
    AlertDialog(wxWindow *parent, const wxString &title, const wxString &message = {});
    ~AlertDialog() override;

    /**
     * \brief Replace the body text; an empty message hides it
     */
    void SetMessage(const wxString &message);

    /**
     * \brief Show an illustration above the message
     *
     * \param image scaled to fit a 360x240 DIP (3:2) frame; wxNullBitmap hides it
     */
    void SetImage(const wxBitmap &image);

    /**
     * \brief Append a secondary note below the message; may be called repeatedly
     */
    void AddTip(const wxString &tip);

    /**
     * \brief Append a highlighted warning line below the tips; may be called repeatedly
     */
    void AddWarning(const wxString &warning);

    /**
     * \brief Append custom content below the message, e.g. a progress bar or input field
     *
     * \param window must be created with this dialog as parent
     */
    void AddContent(wxWindow *window);

    /**
     * \brief Show the "don't show again" checkbox
     *
     * \param label checkbox text; a default text is used when empty
     */
    void ShowDontShowAgain(const wxString &label = {});

    /**
     * \return true when the "don't show again" checkbox is shown and checked
     */
    bool IsDontShowAgainChecked() const;

    /**
     * \brief Add a button that ends the modal loop with \p id
     *
     * \param id    value returned by ShowModal()
     * \param label button text
     * \param role  visual weight; also decides the position in the button row
     */
    void AddButton(wxWindowID id, const wxString &label, AlertButtonRole role = AlertButtonRole::Secondary);

    int  ShowModal() override;
    bool Show(bool show = true) override;

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Slic3r::GUI
