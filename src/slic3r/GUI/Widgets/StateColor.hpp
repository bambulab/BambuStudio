#ifndef slic3r_GUI_StateColor_hpp_
#define slic3r_GUI_StateColor_hpp_

#include <wx/colour.h>

#include <map>

// Semantic light-mode color tokens. Same hex values as the literals
// scattered across the codebase — this is just a vocabulary that makes
// intent obvious at call sites
// The light→dark pairs themselves live in gDarkColors (StateColor.cpp).
// That table accepts raw hex too, so unmigrated callers stay unaffected.
namespace ThemeColor {

// Brand
inline const wxColour BrandGreen{"#00AE42"};        // primary accent — buttons, selected borders, focus rings
inline const wxColour BrandGreenHovered{"#3DCB73"}; // BrandGreen button hover state (== rgb 61,203,115)
inline const wxColour BrandGreenPressed{"#1B8844"}; // BrandGreen button pressed state (== rgb 27,136,68)

// Feedback / status — pre-declared because their meaning is obvious.
inline const wxColour Warning{"#FF6F00"}; // attention / needs-action — orange. 14+ raw-hex consumers.
inline const wxColour Danger{"#D01B1B"};  // error / destructive — red

// Hyperlink / clickable text — blue.
inline const wxColour Link{"#0078D4"};

// Text
inline const wxColour TextPrimary{"#262E30"};   // default body text on light surfaces (== DESIGN_GRAY900)
inline const wxColour TextSecondary{"#323A3D"}; // slightly softer heading/label text (== DESIGN_GRAY800)
inline const wxColour TextMuted{"#6B6B6B"};     // secondary / placeholder (same hex as Grey700 below)
inline const wxColour TextDisabled{"#909090"};  // disabled / inactive text (== DESIGN_GRAY600)

// Pure white (card / dialog / hub fill)
inline const wxColour White{"#FFFFFF"};

// Neutral grey scale — lightest (200) → darkest (700). Suffixes follow the
// legacy WXCOLOUR_GREY* macro numbering (which skips 600); 250 and 350 are
// half-steps for shades that sit between the macro rungs.
inline const wxColour Grey200{"#F8F8F8"};
inline const wxColour Grey250{"#F1F1F1"}; // panel wrap bg
inline const wxColour Grey300{"#EEEEEE"};
inline const wxColour Grey350{"#E8E8E8"}; // borders / dividers
inline const wxColour Grey400{"#CECECE"};
inline const wxColour Grey450{"#A6A9AA"}; // dividers / disabled borders (== DESIGN_GRAY400)
inline const wxColour Grey500{"#9E9E9E"};
inline const wxColour Grey700{"#6B6B6B"}; // same hex as TextMuted

} // namespace ThemeColor

// Legacy macros. Prefer ThemeColor::GreyNNN directly in new code
#define WXCOLOUR_GREY200 ThemeColor::Grey200
#define WXCOLOUR_GREY300 ThemeColor::Grey300
#define WXCOLOUR_GREY400 ThemeColor::Grey400
#define WXCOLOUR_GREY500 ThemeColor::Grey500
#define WXCOLOUR_GREY700 ThemeColor::Grey700

class StateColor
{
public:
    enum State {
        Normal = 0, 
        Enabled = 1,
        Checked = 2,
        Focused = 4,
        Hovered = 8,
        Pressed = 16,
        Disabled = 1 << 16,
        NotChecked = 2 << 16,
        NotFocused = 4 << 16,
        NotHovered = 8 << 16,
        NotPressed = 16 << 16,
    };

public:
    static void SetDarkMode(bool dark);

    static std::map<wxColour, wxColour> const & GetDarkMap();
    static wxColour darkModeColorFor(wxColour const &color);
    static wxColour lightModeColorFor(wxColour const &color);

    // Button style
    static StateColor createButtonStyleGray();

public:
    template<typename ...Colors>
    StateColor(std::pair<Colors, int>... colors) {
        fill(colors...);
    }

    // single color
    StateColor(wxColour const & color);

    // single color
    StateColor(wxString const &color);

    // single color
    StateColor(unsigned long color);

    // operator==
    bool operator==(StateColor const& other) const{
        return statesList_ == other.statesList_ && colors_ == other.colors_ && takeFocusedAsHovered_ == other.takeFocusedAsHovered_;
    };

    // operator!=
    bool operator!=(StateColor const& other) const{
        return !(*this == other);
    };

public:
    void append(wxColour const & color, int states);

    void append(wxString const &color, int states);

    void append(unsigned long color, int states);

    void clear();

public:
    int count() const { return statesList_.size(); }

    int states() const;

public:
    wxColour defaultColor();

    wxColour colorForStates(int states);

    wxColour colorForStatesNoDark(int states);

    int colorIndexForStates(int states);

    bool setColorForStates(wxColour const & color, int states);

    void setTakeFocusedAsHovered(bool set);

private:
    template<typename Color, typename ...Colors>
    void fill(std::pair<Color, int> color, std::pair<Colors, int>... colors) {
        fillOne(color);
        fill(colors...);
    }

    template<typename Color>
    void fillOne(std::pair<Color, int> color) {
        append(color.first, color.second);
    }

    void fill() {
    }

private:
    std::vector<int> statesList_;
    std::vector<wxColour> colors_;
    bool takeFocusedAsHovered_ = true;
};

#endif // !slic3r_GUI_StateColor_hpp_
