#ifndef slic3r_GUI_VersionPolicyDialog_hpp_
#define slic3r_GUI_VersionPolicyDialog_hpp_

/**
 * @file VersionPolicyDialog.hpp
 * @brief Dialog presenting the cloud version policy to the user.
 */

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "slic3r/Utils/VersionPolicyManager.hpp"

class wxBoxSizer;
class wxHtmlLinkEvent;
class wxHtmlWindow;
class wxSizer;

namespace Slic3r { namespace GUI {

/**
 * @brief Shows the policies that matched one check point.
 *
 * The widgets are built once, empty; UpdateByPolicyHits() then fills the body.
 * Every hit is merged into a single body, title and links included, so a check
 * point never raises more than one dialog.
 *
 * The dialog carries no button of its own: two check points can share a
 * severity and still owe the user a different way out, so every button is
 * registered by the caller through add_button().
 */
class VersionPolicyDialog : public DPIDialog
{
public:
    /** @brief Identity of a button, deciding the modal result it ends on. */
    enum class ButtonId {
        Acknowledge, ///< Acknowledges a block, ends on wxID_OK.
        Continue,    ///< Lets a warning through, ends on wxID_YES.
        Back         ///< Aborts on a warning, ends on wxID_NO.
    };

    /** @brief Look of a button, following the design of the dialog. */
    enum class ButtonStyle {
        Primary,  ///< Filled, for the action the check point suggests.
        Secondary ///< Outlined, for the way back.
    };

    /** @brief What a check point wants to run when a button is pressed. */
    using ButtonHandler = std::function<void()>;

    /** @param parent Parent window, may be nullptr. */
    explicit VersionPolicyDialog(wxWindow *parent = nullptr);
    ~VersionPolicyDialog() override;

    /**
     * @brief Fills the dialog with the outcome of a check point.
     *
     * Merges every hit into one body; which buttons the user gets is left to
     * the caller, see add_button().
     *
     * @param result Outcome of the check point; a result without hits leaves
     *               the dialog empty and is not worth showing.
     */
    void UpdateByPolicyHits(const PolicyCheckResult &result);

    /**
     * @brief Adds a button and what pressing it does.
     *
     * Buttons appear from left to right in the order they are added, so the
     * one the check point suggests should come last, next to the edge.
     *
     * @param id      Identity of the button; it decides the modal result and
     *                what run() reports. Adding the same id twice updates the
     *                existing button instead of adding a second one.
     * @param label   Text of the button, already translated.
     * @param handler Invoked on the main thread right before the dialog closes,
     *                may be empty. It cannot change the verdict: whether the
     *                caller may continue still follows the button and the
     *                severity. Exceptions escaping it are logged and swallowed,
     *                so a check point cannot take the dialog down.
     * @param style   Look of the button.
     * @return The button, for a caller that needs to style it further.
     */
    Button *add_button(ButtonId id, const wxString &label, ButtonHandler handler = nullptr, ButtonStyle style = ButtonStyle::Primary);

    /**
     * @brief Replaces what a button does, without touching its label.
     *
     * @param id      Button to hook. Hooking an id that was never added is
     *                harmless, the handler is simply never called.
     * @param handler See add_button().
     */
    void set_handler(ButtonId id, ButtonHandler handler);

    /**
     * @brief Runs the dialog modally.
     *
     * @return true when the user pressed ButtonId::Continue. A blocking result
     *         always returns false, whichever way the dialog was dismissed.
     */
    bool run();

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    /** @brief Builds the widgets, before any policy is known. */
    void CreateGUI();

    /** @brief Builds the body, the message renderer of the dialog. */
    void CreateBody(wxSizer *sizer);

    /** @brief Builds the empty row the buttons go into. */
    void CreateButtons(wxSizer *sizer);

    /** @brief The modal result a button ends the dialog on. */
    static int modal_result_of(ButtonId id);

    /** @brief Sends a link inside the message to the browser, not to the body. */
    void on_body_link(wxHtmlLinkEvent &event);

    /** @brief Runs the handler of a button, if the check point registered one. */
    void fire(ButtonId id);

    bool m_blocking{false};

    std::map<ButtonId, ButtonHandler> m_handlers;

    wxHtmlWindow *m_body{nullptr};

    /** @brief The row every added button goes into, empty until add_button(). */
    wxBoxSizer *m_sizer_buttons{nullptr};

    /** @brief The buttons the caller added, by identity. */
    std::map<ButtonId, Button *> m_buttons;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_VersionPolicyDialog_hpp_
