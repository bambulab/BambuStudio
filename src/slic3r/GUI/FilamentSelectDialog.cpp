#include "FilamentSelectDialog.hpp"
#include "AMSMaterialsSetting.hpp"  // AMS_MATERIALS_SETTING_GREY* colour constants
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/statbmp.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace Slic3r { namespace GUI {

namespace {

wxColour parse_hex_color(const std::string& hex)
{
    if (hex.empty()) return wxColour(0x63, 0x63, 0x63);
    wxString s = wxString::FromUTF8(hex);
    if (!s.StartsWith("#")) s = "#" + s;
    wxColour c(s);
    return c.IsOk() ? c : wxColour(0x63, 0x63, 0x63);
}

std::vector<wxColour> spool_colors(const FilamentSpool& sp)
{
    std::vector<wxColour> out;
    if (!sp.colors.empty()) {
        out.reserve(sp.colors.size());
        for (const std::string& h : sp.colors) out.push_back(parse_hex_color(h));
        return out;
    }
    if (!sp.color_code.empty()) out.push_back(parse_hex_color(sp.color_code));
    return out;
}

wxBitmap make_spool_color_chip(wxWindow* ctx, const FilamentSpool& sp)
{
    const int    size_px = ctx ? ctx->FromDIP(28) : 28;
    const double radius  = std::max(2.0, std::round(size_px / 6.0));

    wxBitmap bmp(size_px, size_px);
#if defined(__WXMSW__) || defined(__WXOSX__)
    bmp.UseAlpha();
#endif
    wxMemoryDC dc(bmp);
    dc.SetBackground(*wxTRANSPARENT_BRUSH);
    dc.Clear();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc) { dc.SelectObject(wxNullBitmap); return bmp; }
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    wxGraphicsPath chip_path = gc->CreatePath();
    chip_path.AddRoundedRectangle(0, 0, size_px, size_px, radius);

    const auto colors        = spool_colors(sp);
    const bool is_multicolor = (sp.color_type == 1) && colors.size() > 1;
    const bool is_gradient   = (sp.color_type == 0) && colors.size() > 1;

    if (is_multicolor) {
        const int n         = static_cast<int>(colors.size());
        const int base_w    = size_px / n;
        int       x         = 0;
        for (int i = 0; i < n; ++i) {
            const int w = (i == n - 1) ? (size_px - base_w * (n - 1)) : base_w;
            gc->PushState();
            gc->Clip(x, 0, w, size_px);
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(colors[i]));
            gc->FillPath(chip_path);
            gc->ResetClip();
            gc->PopState();
            x += w;
        }
    } else if (is_gradient) {
        const int n = static_cast<int>(colors.size());
        if (n == 2) {
            gc->PushState();
            gc->Clip(0, 0, size_px, size_px);
            gc->SetBrush(gc->CreateLinearGradientBrush(0, 0, size_px, 0, colors[0], colors[1]));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->FillPath(chip_path);
            gc->ResetClip();
            gc->PopState();
        } else {
            const double seg_w = static_cast<double>(size_px) / (n - 1);
            for (int i = 0; i < n - 1; ++i) {
                const double x0 = i * seg_w;
                const double x1 = (i == n - 2) ? size_px : (i + 1) * seg_w;
                gc->PushState();
                gc->Clip(x0, 0, x1 - x0, size_px);
                gc->SetBrush(gc->CreateLinearGradientBrush(x0, 0, x1, 0, colors[i], colors[i + 1]));
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->FillPath(chip_path);
                gc->ResetClip();
                gc->PopState();
            }
        }
    } else {
        const wxColour fill = colors.empty() ? wxColour(0x88, 0x88, 0x88) : colors.front();
        gc->SetBrush(wxBrush(fill));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->FillPath(chip_path);
    }

    const bool     dark       = wxGetApp().dark_mode();
    const wxColour ring_color = dark ? wxColour(255, 255, 255, 26) : wxColour(0, 0, 0, 46);
    wxGraphicsPath ring_path  = gc->CreatePath();
    ring_path.AddRoundedRectangle(0.5, 0.5, size_px - 1.0, size_px - 1.0, radius);
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(ring_color, 1));
    gc->StrokePath(ring_path);

    delete gc;
    dc.SelectObject(wxNullBitmap);
    return bmp;
}


int spool_weight_grams(const FilamentSpool& sp)
{
    if (sp.net_weight > 0.0) return static_cast<int>(std::round(sp.net_weight));
    const double total = sp.effective_total_net_weight();
    if (total > 0.0) {
        const int pct = std::max(0, std::min(100, sp.remain_percent));
        return static_cast<int>(total * pct / 100.0);
    }
    return 0;
}

// Slot badge text — desensitized: slot label only, never dev_id / SN / name.
wxString slot_label(const FilamentSpool& sp)
{
    if (!sp.in_printer || sp.slot_id.empty() || sp.ams_id < 0) return wxString();
    try {
        const wxString s = wxGetApp().transition_tridid(sp.ams_id * 4 + std::stoi(sp.slot_id));
        if (!s.empty()) return s;
    } catch (...) {}
    return wxString::FromUTF8(sp.slot_id);
}

bool is_bbl_brand(const std::string& brand)
{
    std::string b = brand;
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return std::tolower(c); });
    return b.find("bambu") != std::string::npos;
}

static const std::vector<wxString> k_brand_prio{ "Bambu Lab", "Generic", "Polymaker" };

std::vector<wxString> order_brands(std::vector<wxString> keys, const wxString& other)
{
    const auto& prio = k_brand_prio;
    std::vector<wxString> out;
    for (const wxString& p : prio)
        if (std::find(keys.begin(), keys.end(), p) != keys.end()) out.push_back(p);
    std::vector<wxString> tail;
    for (const wxString& k : keys) {
        if (k == other) continue;
        if (std::find(prio.begin(), prio.end(), k) != prio.end()) continue;
        tail.push_back(k);
    }
    std::sort(tail.begin(), tail.end());
    for (const wxString& t : tail) out.push_back(t);
    if (std::find(keys.begin(), keys.end(), other) != keys.end()) out.push_back(other);
    return out;
}

StateColor sc(const wxColour& c) { return StateColor(std::make_pair(c, (int)StateColor::Normal)); }

// dark-mode color helpers
// Evaluated at call time so each data-fill method picks up the current theme.

inline wxColour dlg_bg()           { return StateColor::darkModeColorFor(*wxWHITE); }
inline wxColour dlg_separator()    { return wxGetApp().dark_mode() ? wxColour(0x50,0x52,0x54) : wxColour(230,230,230); }
inline wxColour dlg_divider()      { return wxGetApp().dark_mode() ? wxColour(0x50,0x52,0x54) : wxColour(220,220,220); }
inline wxColour dlg_chip_sel_bg()  { return wxGetApp().dark_mode() ? wxColour(0x1F,0x35,0x29) : wxColour(0xDB,0xFD,0xE7); }
inline wxColour dlg_chip_border()  { return wxGetApp().dark_mode() ? wxColour(0x50,0x52,0x54) : wxColour(220,220,220); }
inline wxColour dlg_row_hl()       { return wxGetApp().dark_mode() ? wxColour(0x1A,0x3A,0x28) : wxColour(238,248,242); }
inline wxColour dlg_brand_sel_bg() { return wxGetApp().dark_mode() ? wxColour(0x4A,0x4C,0x4E) : wxColour(240,240,240); }
inline wxColour dlg_badge_bg()     { return wxGetApp().dark_mode() ? wxColour(0x1A,0x2E,0x22) : wxColour(219,244,228); }
inline wxColour dlg_search_border(){ return wxGetApp().dark_mode() ? wxColour(0x50,0x52,0x54) : wxColour(238,238,238); }
inline wxColour dlg_text_primary()  { return wxGetApp().dark_mode() ? wxColour(0xF0,0xF0,0xF0) : AMS_MATERIALS_SETTING_GREY900; }
inline wxColour dlg_text_secondary(){ return wxGetApp().dark_mode() ? wxColour(0xC8,0xCA,0xCC) : AMS_MATERIALS_SETTING_GREY800; }

// recently-used spool LRU (Filament Manager spools only)

constexpr size_t      SPOOL_RECENT_MAX = 6;
constexpr const char* SPOOL_RECENT_KEY = "filament_mgr_recent_spools";

std::vector<wxString> load_recent_spool_ids()
{
    auto* cfg = wxGetApp().app_config;
    if (!cfg) return {};
    const std::string raw = cfg->get(SPOOL_RECENT_KEY);
    std::vector<wxString> out;
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty())
            out.push_back(wxString::FromUTF8(line));
    }
    return out;
}

} // namespace

void FilamentSelectDialog::remember_recent_spool(const wxString& spool_id)
{
    if (spool_id.IsEmpty()) return;
    auto* cfg = wxGetApp().app_config;
    if (!cfg) return;

    std::vector<wxString> items = load_recent_spool_ids();
    items.erase(std::remove(items.begin(), items.end(), spool_id), items.end());
    items.insert(items.begin(), spool_id);
    if (items.size() > SPOOL_RECENT_MAX)
        items.resize(SPOOL_RECENT_MAX);

    std::string out;
    for (const wxString& id : items) {
        if (!id.IsEmpty()) {
            if (!out.empty()) out += '\n';
            out += id.ToUTF8().data();
        }
    }
    cfg->set(SPOOL_RECENT_KEY, out);
}

FilamentSelectDialog::FilamentSelectDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Select Filament"), wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    create();
    wxGetApp().UpdateDlgDarkUI(this);
}

void FilamentSelectDialog::create()
{
    SetBackgroundColour(dlg_bg());

    m_btn_bg_green = StateColor(
        std::pair<wxColour, int>(wxColour(27,  136, 68),  StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61,  203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0,   174, 66),  StateColor::Normal));

    m_btn_bg_gray = StateColor(
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(*wxWHITE,                StateColor::Focused),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE,                StateColor::Normal));

    auto* main = new wxBoxSizer(wxVERTICAL);

    auto* tab_row = new wxBoxSizer(wxHORIZONTAL);

    // Tab unit helper
    auto make_tab_unit = [&](const wxString& label, wxPanel*& underline_out) -> Button* {
        auto* unit = new wxBoxSizer(wxVERTICAL);
        auto* btn = new Button(this, label);
        btn->SetFont(Label::Body_14);
        btn->SetCornerRadius(0);
        btn->SetPaddingSize(wxSize(FromDIP(16), FromDIP(10)));
        btn->SetBackgroundColor(sc(dlg_bg()));
        btn->SetBorderColor(sc(dlg_bg()));  // no visible border; underline panel carries the indicator
        btn->SetTextColor(sc(AMS_MATERIALS_SETTING_GREY900));
        unit->Add(btn, 0, 0, 0);

        underline_out = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(3)));
        underline_out->SetBackgroundColour(dlg_bg());  // overridden by set_tab()
        unit->Add(underline_out, 0, wxEXPAND, 0);

        tab_row->Add(unit, 0, 0, 0);
        return btn;
    };

    m_tab_mgr = make_tab_unit(_L("Filament Manager"), m_tab_underline_mgr);
    m_tab_mgr->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { set_tab(0); });

    m_tab_def = make_tab_unit(_L("System presets"), m_tab_underline_def);
    m_tab_def->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { set_tab(1); });

    tab_row->AddStretchSpacer(1);
    main->Add(tab_row, 0, wxEXPAND | wxLEFT, FromDIP(20));

    // Full-width 1px grey separator below the tab row
    auto* sep = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(1)));
    sep->SetBackgroundColour(dlg_separator());
    main->Add(sep, 0, wxEXPAND);

    m_book = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_book->SetBackgroundColour(dlg_bg());
    m_book->AddPage(build_manager_page(m_book), wxEmptyString, true);
    m_book->AddPage(build_default_page(m_book), wxEmptyString, false);
    main->Add(m_book, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    // bottom buttons
    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer(1);

    auto* ok = new Button(this, _L("Confirm"));
    ok->SetBackgroundColor(m_btn_bg_green);
    ok->SetBorderColor(sc(wxColour(0, 174, 66)));
    ok->SetTextColor(sc(wxColour("#FFFFFE")));
    ok->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    ok->SetCornerRadius(FromDIP(12));
    ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_confirm(); });

    auto* close = new Button(this, _L("Close"));
    close->SetBackgroundColor(m_btn_bg_gray);
    close->SetBorderColor(sc(AMS_MATERIALS_SETTING_GREY900));
    close->SetTextColor(sc(AMS_MATERIALS_SETTING_GREY900));
    close->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    close->SetCornerRadius(FromDIP(12));
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    btn_sizer->Add(ok,    0, wxALIGN_CENTER | wxRIGHT, FromDIP(20));
    btn_sizer->Add(close, 0, wxALIGN_CENTER, 0);
    main->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(12));

    SetSizer(main);
    set_tab(0);   // set initial tab visual state
    SetMinSize(FromDIP(wxSize(493, 556)));
    SetSize(FromDIP(wxSize(493, 556)));
    Layout();
}

void FilamentSelectDialog::set_tab(int n)
{
    if (m_book) m_book->SetSelection(n);

    const wxColour green(0, 174, 66);
    const wxColour active_text   = AMS_MATERIALS_SETTING_GREY900;
    const wxColour inactive_text = AMS_MATERIALS_SETTING_GREY700;

    auto apply = [&](Button* btn, wxPanel* underline, bool active) {
        if (!btn) return;
        btn->SetBackgroundColor(sc(dlg_bg()));
        btn->SetBorderColor(sc(dlg_bg()));
        btn->SetTextColor(sc(active ? active_text : inactive_text));
        wxFont f = Label::Body_14;
        if (active) f.MakeBold();
        btn->SetFont(f);
        btn->Refresh();

        if (underline) {
            underline->SetBackgroundColour(active ? green : dlg_bg());
            underline->Refresh();
        }
    };
    apply(m_tab_mgr, m_tab_underline_mgr, n == 0);
    apply(m_tab_def, m_tab_underline_def, n == 1);
}

wxWindow* FilamentSelectDialog::build_manager_page(wxWindow* parent)
{
    auto* page = new wxPanel(parent, wxID_ANY);
    page->SetBackgroundColour(dlg_bg());
    auto* v = new wxBoxSizer(wxVERTICAL);

    // search box
    auto* search_box = new StaticBox(page);
    search_box->SetBackgroundColor(sc(dlg_bg()));
    search_box->SetBorderColor(sc(dlg_search_border()));
    search_box->SetCornerRadius(FromDIP(6));
    auto* sbs = new wxBoxSizer(wxHORIZONTAL);
    m_search = new TextInput(search_box, wxEmptyString, wxEmptyString, "search",
                             wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_search->GetTextCtrl()->SetHint(_L("Search filament"));
    m_search->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { apply_filters(); });
    sbs->Add(m_search, 1, wxEXPAND | wxALL, FromDIP(2));
    search_box->SetSizer(sbs);
    v->Add(search_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    // chip row: arrows on the right
    auto* chip_row = new wxBoxSizer(wxHORIZONTAL);

    m_chip_scroll = new wxPanel(page, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(34)));
    m_chip_scroll->SetBackgroundColour(dlg_bg());
    m_chip_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_chip_scroll->SetSizer(m_chip_sizer);
    m_chip_scroll->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        m_chip_scroll_width = m_chip_scroll->GetClientSize().x;
        refresh_chip_visibility();
    });

    // SVG arrow icons — preload all four states
    m_bmp_left_on   = create_scaled_bitmap("chip_arrow_left",           page, 16);
    m_bmp_left_off  = create_scaled_bitmap("chip_arrow_left_disabled",  page, 16);
    m_bmp_right_on  = create_scaled_bitmap("chip_arrow_right",          page, 16);
    m_bmp_right_off = create_scaled_bitmap("chip_arrow_right_disabled", page, 16);

    m_left_arrow = new wxStaticBitmap(page, wxID_ANY, m_bmp_left_off);
    m_left_arrow->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        if (!m_left_arrow->IsEnabled()) return;
        --m_chip_offset;
        refresh_chip_visibility();
    });

    m_right_arrow = new wxStaticBitmap(page, wxID_ANY, m_bmp_right_on);
    m_right_arrow->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        if (!m_right_arrow->IsEnabled()) return;
        ++m_chip_offset;
        refresh_chip_visibility();
    });

    chip_row->Add(m_chip_scroll, 1, wxEXPAND | wxRIGHT, FromDIP(4));
    chip_row->Add(m_left_arrow,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    chip_row->Add(m_right_arrow, 0, wxALIGN_CENTER_VERTICAL);
    v->Add(chip_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

    // spool list
    m_mgr_list = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_mgr_list->SetScrollRate(0, 10);
    m_mgr_list->SetBackgroundColour(dlg_bg());
    m_mgr_list_sizer = new wxBoxSizer(wxVERTICAL);
    m_mgr_list->SetSizer(m_mgr_list_sizer);
    v->Add(m_mgr_list, 1, wxEXPAND | wxALL, FromDIP(6));

    page->SetSizer(v);
    return page;
}

wxWindow* FilamentSelectDialog::build_default_page(wxWindow* parent)
{
    auto* page = new wxPanel(parent, wxID_ANY);
    page->SetBackgroundColour(dlg_bg());
    auto* h = new wxBoxSizer(wxHORIZONTAL);

    m_brand_list = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_brand_list->SetScrollRate(0, 10);
    m_brand_list->SetBackgroundColour(dlg_bg());
    m_brand_list_sizer = new wxBoxSizer(wxVERTICAL);
    m_brand_list->SetSizer(m_brand_list_sizer);

    m_type_list = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_type_list->SetScrollRate(0, 10);
    m_type_list->SetBackgroundColour(dlg_bg());
    m_type_list_sizer = new wxBoxSizer(wxVERTICAL);
    m_type_list->SetSizer(m_type_list_sizer);

    auto* divider = new wxPanel(page, wxID_ANY, wxDefaultPosition, wxSize(1, -1));
    divider->SetBackgroundColour(dlg_divider());

    h->Add(m_brand_list, 2, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, FromDIP(4));
    h->Add(divider,      0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));
    h->Add(m_type_list,  3, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(4));
    page->SetSizer(h);
    return page;
}

// data population

void FilamentSelectDialog::Popup(const wxArrayString&                          filament_items,
                                 const std::unordered_map<wxString, wxString>& vendors,
                                 const std::unordered_map<wxString, wxString>& types,
                                 const wxString&                               current_alias,
                                 std::set<std::string>                         printer_names)
{
    m_filament_items = filament_items;
    m_vendors        = vendors;
    m_types          = types;
    m_printer_names  = std::move(printer_names);
    if (!current_alias.IsEmpty()) {
        m_checked_alias = current_alias;
    } else {
        const auto recent_ids = load_recent_spool_ids();
        m_checked_alias = recent_ids.empty() ? wxString() : recent_ids.front();
    }

    fill_manager_tab();
    fill_default_tab();

    Layout();
    wxGetApp().UpdateDlgDarkUI(this);
    ShowModal();
}

void FilamentSelectDialog::fill_manager_tab()
{
    // Clear old content
    m_mgr_list_sizer->Clear(true);
    m_chip_sizer->Clear(true);
    m_selected_row = nullptr;
    m_mgr_rows.clear();
    m_row_to_spool.clear();
    m_active_chip = wxString();

    const wxString other = _L("Other");
    std::map<wxString, std::vector<FilamentSpool>> brand_to_spools;
    std::vector<FilamentSpool> unsupported;

    auto* store  = wxGetApp().fila_manager_store();
    auto* bundle = wxGetApp().preset_bundle;
    if (store) {
        for (const auto& id : store->all_spool_ids()) {
            const FilamentSpool* sp = store->get_spool(id);
            if (!sp) continue;
            bool has_preset = false;
            if (bundle && !sp->filament_id.empty()) {
                for (const auto& printer_name : m_printer_names) {
                    if (bundle->get_filament_by_filament_id(sp->filament_id, printer_name).has_value()) {
                        has_preset = true;
                        break;
                    }
                }
            }
            if (has_preset) {
                const wxString b = sp->brand.empty() ? other : wxString::FromUTF8(sp->brand);
                brand_to_spools[b].push_back(*sp);
            } else {
                unsupported.push_back(*sp);
            }
        }
    }
    for (auto& kv : brand_to_spools)
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const FilamentSpool& a, const FilamentSpool& b) { return a.series < b.series; });

    std::sort(unsupported.begin(), unsupported.end(),
        [](const FilamentSpool& a, const FilamentSpool& b) {
            auto rank = [](const std::string& br) -> int {
                for (int i = 0; i < (int)k_brand_prio.size(); ++i)
                    if (k_brand_prio[i] == wxString::FromUTF8(br)) return i;
                return (int)k_brand_prio.size();
            };
            int ra = rank(a.brand), rb = rank(b.brand);
            if (ra != rb) return ra < rb;
            if (a.brand != b.brand) return a.brand < b.brand;
            return a.series < b.series;
        });

    std::vector<wxString> keys;
    for (auto& kv : brand_to_spools) keys.push_back(kv.first);
    const std::vector<wxString> ordered = order_brands(keys, other);

    // chips: All + brands + Unsupported
    std::vector<wxString> chip_labels;
    chip_labels.push_back(_L("All"));
    for (const wxString& b : ordered) chip_labels.push_back(b);
    chip_labels.push_back(_L("Unsupported Filaments"));
    fill_brand_chips(chip_labels);

    const std::vector<wxString> recent_ids = load_recent_spool_ids();

    if (brand_to_spools.empty() && unsupported.empty()) {
        auto* empty = new Label(m_mgr_list, _L("No filaments"));
        empty->SetFont(Label::Body_13);
        empty->SetForegroundColour(AMS_MATERIALS_SETTING_GREY300);
        m_mgr_list_sizer->Add(empty, 0, wxALL, FromDIP(16));
    } else {
        auto make_search_key = [](const FilamentSpool& sp) {
            wxString s;
            s += wxString::FromUTF8(sp.brand)         + " ";
            s += wxString::FromUTF8(sp.series)        + " ";
            s += wxString::FromUTF8(sp.material_type) + " ";
            s += wxString::FromUTF8(sp.color_name);
            return s.Lower();
        };

        // Flatten supported spools in brand+series order for the three-pass sort below.
        std::vector<std::pair<wxString, FilamentSpool>> all_supported;
        for (const wxString& b : ordered)
            for (const FilamentSpool& sp : brand_to_spools.at(b))
                all_supported.push_back({b, sp});

        auto add_row = [&](const wxString& brand, const FilamentSpool& sp, bool is_recent) {
            auto* row = make_spool_row(m_mgr_list, sp, false, is_recent);
            m_mgr_list_sizer->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(1));
            m_mgr_rows.push_back({row, brand, make_search_key(sp)});
        };

        // Pass 1: recently used (LRU order, newest first), excluding in-printer
        for (const wxString& sid : recent_ids) {
            for (auto& [b, sp] : all_supported) {
                if (sp.in_printer) continue;
                if (wxString::FromUTF8(sp.spool_id) != sid) continue;
                add_row(b, sp, true);
                break;
            }
        }

        // Pass 2: remaining supported spools (not in-printer, not recently used)
        for (auto& [b, sp] : all_supported) {
            if (sp.in_printer) continue;
            const wxString sid = wxString::FromUTF8(sp.spool_id);
            if (std::find(recent_ids.begin(), recent_ids.end(), sid) != recent_ids.end()) continue;
            add_row(b, sp, false);
        }

        // Pass 3: AMS in-printer spools (slot badge shown, dimmed = not selectable)
        for (auto& [b, sp] : all_supported) {
            if (!sp.in_printer) continue;
            auto* row = make_spool_row(m_mgr_list, sp, true /*dimmed*/, false);
            m_mgr_list_sizer->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(1));
            m_mgr_rows.push_back({row, b, make_search_key(sp)});
        }

        // Pass 4: unsupported section (chip filtering only, always dimmed)
        for (const FilamentSpool& sp : unsupported) {
            auto* row = make_spool_row(m_mgr_list, sp, true, false);
            m_mgr_list_sizer->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(1));
            m_mgr_rows.push_back({row, _L("Unsupported Filaments"), make_search_key(sp)});
        }
    }

    m_mgr_list->FitInside();
    m_chip_offset = 0;
    refresh_chip_visibility();
}

void FilamentSelectDialog::on_confirm()
{
    m_result = SelectionResult{};
    const int page = m_book->GetSelection();

    if (page == 0 && m_selected_row) {
        // Manager tab: look up the spool we stored when the row was clicked
        auto it = m_row_to_spool.find(m_selected_row);
        if (it == m_row_to_spool.end()) return;
        const FilamentSpool& sp = it->second;
        m_result.spool_id   = sp.spool_id;
        m_result.color_type = sp.color_type;
        m_result.colors     = sp.colors;
        m_result.color_code = sp.color_code;

    } else if (page == 1 && !m_checked_alias.IsEmpty()) {
        // System presets tab: caller resolves filament_id/type/temp from alias
        m_result.preset_alias = m_checked_alias;
    }

    if (!m_result.is_valid()) return;
    EndModal(wxID_OK);
}

void FilamentSelectDialog::filter_by_chip(const wxString& chip_label)
{
    m_active_chip = chip_label;
    apply_filters(false);
}

void FilamentSelectDialog::apply_filters(bool reset_chip_offset)
{
    const wxString q = m_search
        ? m_search->GetTextCtrl()->GetValue().Lower().Trim(false).Trim(true)
        : wxString();
    const bool all = m_active_chip.empty() || m_active_chip == _L("All");

    for (auto& row : m_mgr_rows) {
        const bool chip_ok   = all || row.brand == m_active_chip;
        const bool search_ok = q.IsEmpty() || row.search_key.Contains(q);
        row.w->Show(chip_ok && search_ok);
    }

    m_mgr_list->FitInside();
    m_mgr_list->Layout();
    m_mgr_list->Refresh();

    if (reset_chip_offset)
        m_chip_offset = 0;
    refresh_chip_visibility();
}

void FilamentSelectDialog::refresh_chip_visibility()
{
    if (!m_left_arrow || !m_right_arrow || !m_chip_scroll || m_chips.empty()) return;

    const int avail = m_chip_scroll_width > 0
                    ? m_chip_scroll_width
                    : m_chip_scroll->GetClientSize().x;
    if (avail <= 0) return;

    // Determine which chips have search results (brand chips only; All/Unsupported always kept)
    const wxString q = m_search
        ? m_search->GetTextCtrl()->GetValue().Lower().Trim(false).Trim(true)
        : wxString();

    std::vector<bool> chip_has_results(m_chips.size(), true);
    if (!q.IsEmpty()) {
        for (int i = 0; i < (int)m_chips.size(); ++i) {
            const wxString label = m_chips[i]->GetLabel();
            if (label == _L("All"))
                continue;
            bool found = false;
            for (const auto& row : m_mgr_rows) {
                if (row.brand == label && row.search_key.Contains(q)) { found = true; break; }
            }
            chip_has_results[i] = found;
        }
    }

    const int gap = FromDIP(6);
    const int pad = FromDIP(10) * 2;

    // "All" chip is always pinned visible; measure its width to reserve space.
    int all_chip_idx = -1;
    int all_chip_w   = 0;
    for (int i = 0; i < (int)m_chips.size(); ++i) {
        if (m_chips[i]->GetLabel() == _L("All")) {
            all_chip_idx = i;
            all_chip_w   = m_chips[i]->GetTextRect().width + pad + gap;
            break;
        }
    }
    if (all_chip_idx >= 0)
        m_chip_sizer->Show(m_chips[all_chip_idx], true);

    // Build ordered list of non-"All" chips that survive the search filter.
    // m_chip_offset and the scroll arrows only operate on this sub-list.
    std::vector<int> vi;
    for (int i = 0; i < (int)m_chips.size(); ++i) {
        if (i == all_chip_idx) continue;
        if (chip_has_results[i]) vi.push_back(i);
    }

    // clamp offset into the surviving list
    m_chip_offset = std::max(0, std::min(m_chip_offset, (int)vi.size() - 1));

    // Greedy: show surviving brand chips from m_chip_offset until they no longer fit.
    // Subtract the pinned "All" chip width from the available space.
    const int brand_avail = avail - all_chip_w;
    int used = 0;
    int last_vi = m_chip_offset - 1;
    for (int k = m_chip_offset; k < (int)vi.size(); ++k) {
        const int w = m_chips[vi[k]]->GetTextRect().width + pad + gap;
        if (used + w > brand_avail && last_vi >= m_chip_offset) break;
        used += w;
        last_vi = k;
    }

    // show/hide brand chips: only those in [m_chip_offset, last_vi] of the surviving list
    std::vector<bool> show_chip(m_chips.size(), false);
    if (all_chip_idx >= 0) show_chip[all_chip_idx] = true;   // always show "All"
    for (int k = m_chip_offset; k <= last_vi; ++k)
        show_chip[vi[k]] = true;

    for (int i = 0; i < (int)m_chips.size(); ++i)
        m_chip_sizer->Show(m_chips[i], show_chip[i]);
    m_chip_sizer->Layout();

    const bool can_left  = (m_chip_offset > 0);
    const bool can_right = (last_vi < (int)vi.size() - 1);
    m_left_arrow->SetBitmap(can_left  ? m_bmp_left_on  : m_bmp_left_off);
    m_right_arrow->SetBitmap(can_right ? m_bmp_right_on : m_bmp_right_off);
    m_left_arrow->Enable(can_left);
    m_right_arrow->Enable(can_right);
}

void FilamentSelectDialog::fill_brand_chips(const std::vector<wxString>& brands)
{
    // m_chip_sizer already cleared by fill_manager_tab()
    m_chips.clear();
    auto chips = std::make_shared<std::vector<Button*>>();

    auto apply_style = [](Button* b, bool sel) {
        const wxColour green(0, 174, 66);
        if (sel) {
            b->SetBackgroundColor(sc(dlg_chip_sel_bg()));            // #DBFDE7 / dark #1F3529
            b->SetTextColor(sc(green));
            b->SetBorderColor(sc(green));
        } else {
            b->SetBackgroundColor(sc(dlg_bg()));                    // transparent (matches panel)
            b->SetTextColor(sc(AMS_MATERIALS_SETTING_GREY800));
            b->SetBorderColor(sc(dlg_chip_border()));               // light gray frame
        }
        b->Refresh();
    };

    for (size_t i = 0; i < brands.size(); ++i) {
        auto* chip = new Button(m_chip_scroll, brands[i]);
        chip->SetFont(Label::Body_12);
        chip->SetCornerRadius(FromDIP(4));   // 4px rounded-rect per design
        chip->SetPaddingSize(wxSize(FromDIP(10), FromDIP(4)));
        apply_style(chip, i == 0);
        chip->Bind(wxEVT_BUTTON, [this, chips, chip, apply_style, label = brands[i]](wxCommandEvent&) {
            for (auto* c : *chips) apply_style(c, c == chip);
            filter_by_chip(label);
        });
        chips->push_back(chip);
        m_chips.push_back(chip);
        m_chip_sizer->Add(chip, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    }
}

wxWindow* FilamentSelectDialog::make_spool_row(wxWindow* parent, const FilamentSpool& sp, bool dimmed, bool is_recent)
{
    auto* row = new wxPanel(parent, wxID_ANY);
    row->SetBackgroundColour(dlg_bg());
    row->SetMinSize(wxSize(-1, FromDIP(48)));
    auto* h = new wxBoxSizer(wxHORIZONTAL);

    // colour swatch
    wxBitmap chip = make_spool_color_chip(row, sp);
    if (chip.IsOk())
        h->Add(new wxStaticBitmap(row, wxID_ANY, chip), 0,
               wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));

    if (is_bbl_brand(sp.brand)) {
        wxBitmap logo = create_scaled_bitmap("BambuStudioBlack", row, 16);
        if (logo.IsOk())
            h->Add(new wxStaticBitmap(row, wxID_ANY, logo), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    }

    wxString name;
    if (is_bbl_brand(sp.brand)) {
        if (!sp.series.empty())             name = wxString::FromUTF8(sp.series);
        else if (!sp.material_type.empty()) name = wxString::FromUTF8(sp.material_type);
        else if (!sp.color_name.empty())    name = wxString::FromUTF8(sp.color_name);
        else                                name = _L("Filament");
    } else {
        name = spool_display_name(sp);
        if (!sp.color_name.empty()) name += " " + wxString::FromUTF8(sp.color_name);
    }
    auto* name_lbl = new Label(row, name);
    name_lbl->SetFont(Label::Body_14);
    name_lbl->SetForegroundColour(dimmed ? AMS_MATERIALS_SETTING_GREY300 : AMS_MATERIALS_SETTING_GREY900);
    h->Add(name_lbl, 1, wxALIGN_CENTER_VERTICAL, 0);

    // weight — fixed min-width so the column edge is consistent across all rows
    auto* w_lbl = new Label(row, wxString::Format("%dg", spool_weight_grams(sp)));
    w_lbl->SetFont(Label::Body_14);
    w_lbl->SetForegroundColour(dimmed ? AMS_MATERIALS_SETTING_GREY300 : AMS_MATERIALS_SETTING_GREY700);
    w_lbl->SetMinSize(wxSize(FromDIP(52), -1));
    h->Add(w_lbl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));

    const wxString slot = slot_label(sp);
    const int badge_col_w  = FromDIP(88);
    const int badge_margin = FromDIP(8);
    if (!slot.empty()) {
        auto* slot_lbl = new Label(row, slot + " " + _L("slot"));
        slot_lbl->SetFont(Label::Body_14);
        slot_lbl->SetForegroundColour(dimmed ? AMS_MATERIALS_SETTING_GREY300 : AMS_MATERIALS_SETTING_GREY700);
        slot_lbl->SetMinSize(wxSize(badge_col_w, -1));
        h->Add(slot_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, badge_margin);
    } else if (is_recent && !dimmed) {
        const wxColour green(0, 174, 66);
        auto* badge = new Button(row, _L("Recently used"));
        badge->SetFont(Label::Body_12);
        badge->SetCornerRadius(FromDIP(6));
        badge->SetPaddingSize(wxSize(FromDIP(6), FromDIP(2)));
        badge->SetBackgroundColor(StateColor(std::make_pair(dlg_badge_bg(), (int)StateColor::Normal)));
        badge->SetTextColor(StateColor(std::make_pair(green, (int)StateColor::Normal)));
        badge->SetBorderColor(StateColor(std::make_pair(green, (int)StateColor::Normal)));
        badge->SetCanFocus(false);
        h->Add(badge, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, badge_margin);
    } else {
        auto* placeholder = new wxPanel(row, wxID_ANY, wxDefaultPosition, wxSize(badge_col_w, -1));
        placeholder->SetBackgroundColour(dlg_bg());
        h->Add(placeholder, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, badge_margin);
    }

    row->SetSizer(h);

    if (!dimmed) {
        // record row → spool for result lookup in on_confirm()
        m_row_to_spool[row] = sp;

        auto on_click = [this, row](wxMouseEvent&) {
            if (m_selected_row && m_selected_row != row) {
                const wxColour prev_bg = dlg_bg();
                m_selected_row->SetBackgroundColour(prev_bg);
                for (auto* c : m_selected_row->GetChildren()) c->SetBackgroundColour(prev_bg);
                m_selected_row->Refresh();
            }
            m_selected_row = row;
            const wxColour hl = dlg_row_hl();
            row->SetBackgroundColour(hl);
            for (auto* c : row->GetChildren()) c->SetBackgroundColour(hl);
            row->Refresh();
        };
        row->Bind(wxEVT_LEFT_DOWN, on_click);
        for (auto* c : row->GetChildren()) c->Bind(wxEVT_LEFT_DOWN, on_click);
    }
    return row;
}

void FilamentSelectDialog::fill_default_tab()
{
    m_brand_to_aliases.clear();
    m_brand_list_sizer->Clear(true);
    m_type_list_sizer->Clear(true);

    const wxString other = _L("Other");
    for (const wxString& alias : m_filament_items) {
        wxString vendor;
        auto it = m_vendors.find(alias);
        if (it != m_vendors.end()) vendor = it->second;
        if (vendor.IsEmpty()) vendor = other;
        m_brand_to_aliases[vendor].push_back(alias);
    }

    static const std::vector<wxString> type_order{ "PLA", "PETG", "ABS", "TPU" };
    for (auto& kv : m_brand_to_aliases) {
        std::sort(kv.second.begin(), kv.second.end(), [this](const wxString& l, const wxString& r) {
            wxString lt, rt;
            auto il = m_types.find(l), ir = m_types.find(r);
            if (il != m_types.end()) lt = il->second;
            if (ir != m_types.end()) rt = ir->second;
            auto i1 = std::find(type_order.begin(), type_order.end(), lt);
            auto i2 = std::find(type_order.begin(), type_order.end(), rt);
            if (i1 != i2) return i1 < i2;
            return l < r;
        });
    }

    std::vector<wxString> keys;
    for (auto& kv : m_brand_to_aliases) keys.push_back(kv.first);
    m_ordered_brands = order_brands(keys, other);

    for (const wxString& b : m_ordered_brands) {
        auto* row = new wxPanel(m_brand_list, wxID_ANY);
        row->SetBackgroundColour(dlg_bg());
        auto* h = new wxBoxSizer(wxHORIZONTAL);

        auto* lbl = new Label(row, b);
        lbl->SetFont(Label::Body_14);
        lbl->SetForegroundColour(dlg_text_secondary());
        h->Add(lbl, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

        wxBitmap arrow_bmp = create_scaled_bitmap("chip_arrow_right", row, 16);
        auto* arrow = new wxStaticBitmap(row, wxID_ANY, arrow_bmp);
        h->Add(arrow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

        row->SetMinSize(wxSize(-1, FromDIP(36)));
        row->SetSizer(h);

        const wxString brand = b;
        auto on_click = [this, brand](wxMouseEvent&) { select_brand(brand); };
        row->Bind(wxEVT_LEFT_DOWN, on_click);
        for (auto* c : row->GetChildren()) c->Bind(wxEVT_LEFT_DOWN, on_click);

        m_brand_list_sizer->Add(row, 0, wxEXPAND);
    }
    m_brand_list->FitInside();

    // show first brand (prefer the one containing the current preset)
    wxString target = m_ordered_brands.empty() ? wxString() : m_ordered_brands.front();
    if (!m_checked_alias.IsEmpty()) {
        for (const auto& kv : m_brand_to_aliases) {
            if (std::find(kv.second.begin(), kv.second.end(), m_checked_alias) != kv.second.end()) {
                target = kv.first;
                break;
            }
        }
    }
    if (!target.IsEmpty()) select_brand(target);
}

void FilamentSelectDialog::select_brand(const wxString& brand)
{
    const wxString prev_brand = m_selected_brand;
    m_selected_brand = brand;

    // Freeze to suppress intermediate repaints during the list rebuild.
    m_type_list->Freeze();
    m_type_list_sizer->Clear(true);

    auto it = m_brand_to_aliases.find(brand);
    if (it != m_brand_to_aliases.end()) {
        for (const wxString& alias : it->second) {
            auto* row = new wxPanel(m_type_list, wxID_ANY);
            row->SetBackgroundColour(dlg_bg());
            auto* h = new wxBoxSizer(wxHORIZONTAL);

            auto* lbl = new Label(row, alias);
            lbl->SetFont(Label::Body_14);
            lbl->SetForegroundColour(dlg_text_primary());
            h->Add(lbl, 1, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(8));

            row->SetSizer(h);
            const wxColour row_bg = (alias == m_checked_alias) ? dlg_row_hl() : dlg_bg();
            row->SetBackgroundColour(row_bg);
            for (auto* c : row->GetChildren()) c->SetBackgroundColour(row_bg);

            const wxString cur_brand = brand;
            auto on_type_click = [this, alias, cur_brand](wxMouseEvent&) {
                m_checked_alias = alias;
                CallAfter([this, cur_brand]() { select_brand(cur_brand); });
            };
            row->Bind(wxEVT_LEFT_DOWN, on_type_click);
            for (auto* c : row->GetChildren()) c->Bind(wxEVT_LEFT_DOWN, on_type_click);

            m_type_list_sizer->Add(row, 0, wxEXPAND);
        }
    }
    m_type_list->FitInside();
    m_type_list->Layout();
    m_type_list->Thaw();

    for (auto* w : m_brand_list->GetChildren()) {
        auto* row = dynamic_cast<wxPanel*>(w);
        if (!row) continue;

        // Extract brand name from the first Label child.
        wxString row_brand;
        for (auto* c : row->GetChildren()) {
            if (auto* lbl = dynamic_cast<Label*>(c)) { row_brand = lbl->GetLabel(); break; }
        }
        const bool sel     = (row_brand == brand);
        const bool was_sel = (row_brand == prev_brand);
        if (sel == was_sel) continue;
        const wxColour sel_bg = dlg_row_hl();
        const wxColour unsel_bg = dlg_bg();
        row->SetBackgroundColour(sel ? sel_bg : unsel_bg);

        const wxColour fg = sel ? dlg_text_primary() : dlg_text_secondary();
        for (auto* c : row->GetChildren()) {
            // Children must stay transparent so the custom-painted bg shows through.
            c->SetBackgroundColour(sel ? sel_bg : unsel_bg);
            if (auto* lbl = dynamic_cast<Label*>(c))
                lbl->SetForegroundColour(fg);
            // Arrow colour unchanged — always GREY300
        }
        row->Refresh();
    }
}

void FilamentSelectDialog::on_dpi_changed(const wxRect& /*suggested_rect*/)
{
    m_bmp_left_on   = create_scaled_bitmap("chip_arrow_left",           this, 16);
    m_bmp_left_off  = create_scaled_bitmap("chip_arrow_left_disabled",  this, 16);
    m_bmp_right_on  = create_scaled_bitmap("chip_arrow_right",          this, 16);
    m_bmp_right_off = create_scaled_bitmap("chip_arrow_right_disabled", this, 16);
    refresh_chip_visibility();
    Layout();
}

}} // namespace Slic3r::GUI
