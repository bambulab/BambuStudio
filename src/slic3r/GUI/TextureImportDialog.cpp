#include <GL/glew.h>

#include "TextureImportDialog.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "FilamentPickerDialog.hpp"

#include <boost/algorithm/string/predicate.hpp>

#include <wx/button.h>
#include <wx/colour.h>
#include <wx/colordlg.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/statline.h>
#include <wx/msgdlg.h>
#include <wx/valtext.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <boost/log/trivial.hpp>

static constexpr const char* DEFAULT_VIRTUAL_FILAMENT_NAME = "Bambu PLA Basic";

static int draw_brand_icon_and_strip(wxDC& dc, wxWindow* win, wxString& name, int x, int cy)
{
    int icon_sz = win->FromDIP(16);
    if (name.StartsWith("Bambu ")) {
        name = name.Mid(6);
        wxBitmap bmp = create_scaled_bitmap("BambuStudioBlack", nullptr, 16);
        if (bmp.IsOk())
            dc.DrawBitmap(bmp, x, cy - icon_sz / 2, true);
        x += icon_sz + win->FromDIP(4);
    }
    return x;
}

// ============================================================
// GreenSlider — thin track + green triangle thumb
// ============================================================

class GreenSlider : public wxPanel {
public:
    GreenSlider(wxWindow* parent, int value, int minVal, int maxVal,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize);
    int  GetValue() const;
    void SetValue(int val);
    bool Enable(bool enable = true) override;
private:
    void OnPaint(wxPaintEvent&);
    void OnMouse(wxMouseEvent&);
    int  xFromValue() const;
    int  valueFromX(int x) const;
    int  m_value, m_min, m_max;
    bool m_dragging = false;
};

GreenSlider::GreenSlider(wxWindow* parent, int value, int minVal, int maxVal,
                         const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, wxID_ANY, pos, size.IsFullySpecified() ? size : wxSize(-1, parent->FromDIP(24)))
    , m_value(std::clamp(value, minVal, maxVal)), m_min(minVal), m_max(maxVal)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, FromDIP(24)));

    Bind(wxEVT_PAINT,     &GreenSlider::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &GreenSlider::OnMouse, this);
    Bind(wxEVT_LEFT_UP,   &GreenSlider::OnMouse, this);
    Bind(wxEVT_MOTION,    &GreenSlider::OnMouse, this);
}

int GreenSlider::GetValue() const { return m_value; }

void GreenSlider::SetValue(int val)
{
    val = std::clamp(val, m_min, m_max);
    if (val != m_value) { m_value = val; Refresh(); }
}

bool GreenSlider::Enable(bool enable)
{
    bool ok = wxPanel::Enable(enable);
    Refresh();
    return ok;
}

int GreenSlider::xFromValue() const
{
    wxSize sz = GetClientSize();
    int margin = FromDIP(6);
    int track_w = sz.x - 2 * margin;
    if (m_max <= m_min || track_w <= 0) return margin;
    return margin + (m_value - m_min) * track_w / (m_max - m_min);
}

int GreenSlider::valueFromX(int x) const
{
    wxSize sz = GetClientSize();
    int margin = FromDIP(6);
    int track_w = sz.x - 2 * margin;
    if (track_w <= 0 || m_max <= m_min) return m_min;
    int val = m_min + (x - margin) * (m_max - m_min) / track_w;
    return std::clamp(val, m_min, m_max);
}

void GreenSlider::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    wxSize sz = GetClientSize();

    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();

    int margin = FromDIP(6);
    int track_y = sz.y / 2;
    int ts = FromDIP(8);
    int pen_w = FromDIP(2);

    wxColour greenClr = IsEnabled() ? wxColour(0, 174, 66) : wxColour(180, 180, 180);
    wxColour grayClr  = IsEnabled() ? wxColour(200, 200, 200) : wxColour(220, 220, 220);

    int tx = xFromValue();

    dc.SetPen(wxPen(greenClr, pen_w));
    dc.DrawLine(margin, track_y, tx, track_y);

    dc.SetPen(wxPen(grayClr, pen_w));
    dc.DrawLine(tx, track_y, sz.x - margin, track_y);

    wxPoint tri[3] = {
        {tx,          track_y + FromDIP(1)},
        {tx - ts / 2, track_y + FromDIP(1) + ts},
        {tx + ts / 2, track_y + FromDIP(1) + ts}
    };
    dc.SetBrush(wxBrush(greenClr));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawPolygon(3, tri);
}

void GreenSlider::OnMouse(wxMouseEvent& evt)
{
    if (!IsEnabled()) return;

    auto update = [&](int x) {
        int nv = valueFromX(x);
        if (nv != m_value) {
            m_value = nv;
            Refresh();
            wxCommandEvent e(wxEVT_SLIDER, GetId());
            e.SetEventObject(this);
            ProcessWindowEvent(e);
        }
    };

    if (evt.LeftDown()) {
        m_dragging = true;
        CaptureMouse();
        update(evt.GetX());
    } else if (evt.LeftUp()) {
        m_dragging = false;
        if (HasCapture()) ReleaseMouse();
    } else if (evt.Dragging() && m_dragging) {
        update(evt.GetX());
    }
}

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_TEXTURE_COMPUTE_DONE, wxCommandEvent);
wxDEFINE_EVENT(EVT_TEXTURE_COMPUTE_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_TEXTURE_COMPUTE_ERROR, wxCommandEvent);

static std::array<float, 4> parse_color_string(const std::string& hex)
{
    std::array<float, 4> c = {1.f, 1.f, 1.f, 1.f};
    if (hex.size() >= 7 && hex[0] == '#') {
        unsigned long val = std::strtoul(hex.c_str() + 1, nullptr, 16);
        c[0] = ((val >> 16) & 0xFF) / 255.f;
        c[1] = ((val >>  8) & 0xFF) / 255.f;
        c[2] = ((val      ) & 0xFF) / 255.f;
    }
    return c;
}

static wxString rgb_to_hex(const std::array<std::size_t, 3>& c)
{
    return wxString::Format("#%02X%02X%02X",
        (unsigned)c[0], (unsigned)c[1], (unsigned)c[2]);
}

// ============================================================
// FilamentSelectPopup
// ============================================================

class FilamentSelectPopup : public PopupWindow
{
public:
    FilamentSelectPopup(wxWindow* parent,
                        const std::vector<std::array<float, 4>>& colors_rgba,
                        const std::vector<std::string>&          names,
                        const std::vector<std::string>&          color_strs,
                        size_t                                   existing_count,
                        std::function<void(int)>                 on_select,
                        std::function<void(int, wxColour)>       on_color_changed,
                        std::function<void(wxColour)>            on_add_filament,
                        const std::vector<std::string>&          fila_ids   = {},
                        const std::vector<std::string>&          fila_types = {})
        : PopupWindow(parent)
        , m_colors_rgba(colors_rgba)
        , m_names(names)
        , m_existing_count(existing_count)
        , m_on_select(std::move(on_select))
        , m_on_color_changed(std::move(on_color_changed))
        , m_on_add_filament(std::move(on_add_filament))
        , m_fila_ids(fila_ids)
        , m_fila_types(fila_types)
    {
        SetBackgroundColour(*wxWHITE);

        m_content = new wxPanel(this, wxID_ANY);
        m_content->SetBackgroundColour(*wxWHITE);
        auto* outer = new wxBoxSizer(wxVERTICAL);

        const int pop_w   = FromDIP(213);
        const int row_h   = FromDIP(32);
        const int pad     = FromDIP(8);
        const wxColour header_clr(0xAC, 0xAC, 0xAC);

        auto add_section_header = [&](const wxString& label) {
            auto* hdr = new wxStaticText(m_content, wxID_ANY, label);
            wxFont hf = hdr->GetFont();
            hf.SetPointSize(9);
            hdr->SetFont(hf);
            hdr->SetForegroundColour(header_clr);
            outer->Add(hdr, 0, wxLEFT | wxRIGHT | wxTOP, pad);
            auto* line = new wxStaticLine(m_content, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
            outer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, pad);
        };

        // Section 1: project filaments
        add_section_header(_L("Project Filaments"));
        for (size_t i = 0; i < m_colors_rgba.size(); ++i) {
            if (i == m_existing_count && m_existing_count < m_colors_rgba.size()) {
                add_section_header(_L("New Filaments"));
            }
            wxPanel* row = create_item_row(i, row_h);
            outer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
        }

        // "添加材料" link
        outer->AddSpacer(FromDIP(4));
        auto* sep_line = new wxStaticLine(m_content, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
        outer->Add(sep_line, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

        auto* add_label = new wxStaticText(m_content, wxID_ANY, _L("Add Material"));
        wxFont af = add_label->GetFont();
        af.SetPointSize(10);
        add_label->SetFont(af);
        add_label->SetForegroundColour(wxColour(0x00, 0xAE, 0x42));
        add_label->SetCursor(wxCursor(wxCURSOR_HAND));
        add_label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            Dismiss();
            wxColourData cd;
            cd.SetChooseFull(true);
            wxColourDialog dlg(GetParent(), &cd);
            if (dlg.ShowModal() == wxID_OK) {
                wxColour clr = dlg.GetColourData().GetColour();
                if (m_on_add_filament) m_on_add_filament(clr);
            }
        });
        outer->Add(add_label, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, pad);

        m_content->SetSizer(outer);

        auto* top_sizer = new wxBoxSizer(wxVERTICAL);
        top_sizer->Add(m_content, 1, wxEXPAND);
        SetSizerAndFit(top_sizer);

        int total_h = outer->GetMinSize().y;
        SetSize(pop_w, total_h);
    }

private:
    wxPanel* create_item_row(size_t idx, int row_h)
    {
        wxPanel* row = new wxPanel(m_content, wxID_ANY, wxDefaultPosition, wxSize(-1, row_h));
        row->SetBackgroundColour(*wxWHITE);
        row->SetBackgroundStyle(wxBG_STYLE_PAINT);
        row->SetCursor(wxCursor(wxCURSOR_HAND));

        const int sq     = row->FromDIP(24);
        const int sq_r   = row->FromDIP(2);
        const int sq_x   = row->FromDIP(4);
        const int gap1   = row->FromDIP(8);

        wxColour fil_clr = idx < m_colors_rgba.size()
            ? wxColour((unsigned char)(m_colors_rgba[idx][0] * 255.f),
                       (unsigned char)(m_colors_rgba[idx][1] * 255.f),
                       (unsigned char)(m_colors_rgba[idx][2] * 255.f))
            : wxColour(128, 128, 128);

        wxString name_str = (idx < m_names.size()) ? wxString(m_names[idx])
                                                    : wxString::Format("Filament %d", (int)(idx + 1));

        row->Bind(wxEVT_PAINT, [this, idx, sq, sq_r, sq_x, gap1, fil_clr, name_str, row_h](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            wxAutoBufferedPaintDC dc(p);
            wxSize sz = p->GetClientSize();

            bool hovered = (m_hover_idx == (int)idx);
            dc.SetBrush(wxBrush(hovered ? wxColour(245, 245, 245) : *wxWHITE));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            int sq_y = (sz.y - sq) / 2;
            wxColour paint_clr = fil_clr;
            if (idx < m_colors_rgba.size()) {
                paint_clr = wxColour((unsigned char)(m_colors_rgba[idx][0] * 255.f),
                                     (unsigned char)(m_colors_rgba[idx][1] * 255.f),
                                     (unsigned char)(m_colors_rgba[idx][2] * 255.f));
            }
            dc.SetBrush(wxBrush(paint_clr));
            dc.DrawRoundedRectangle(sq_x, sq_y, sq, sq, sq_r);

            {
                wxFont nf = p->GetFont();
                nf.SetPointSize(9);
                dc.SetFont(nf);
                dc.SetTextForeground(paint_clr.GetLuminance() < 0.6 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30));
                wxString ns = wxString::Format("%d", (int)(idx + 1));
                wxSize tsz = dc.GetTextExtent(ns);
                dc.DrawText(ns, sq_x + (sq - tsz.x) / 2, sq_y + (sq - tsz.y) / 2);
            }

            // Brand icon + material name
            {
                wxFont mf = p->GetFont();
                mf.SetPointSize(10);
                dc.SetFont(mf);
                dc.SetTextForeground(wxColour(38, 46, 48));
                wxString display = name_str;
                int tx = draw_brand_icon_and_strip(dc, p, display, sq_x + sq + gap1, sz.y / 2);
                wxSize tsz = dc.GetTextExtent(display);
                dc.DrawText(display, tx, (sz.y - tsz.y) / 2);
            }
        });

        row->Bind(wxEVT_MOTION, [this, row, idx](wxMouseEvent& evt) {
            if (m_hover_idx != (int)idx) {
                m_hover_idx = (int)idx;
                m_content->Refresh();
            }
            evt.Skip();
        });
        row->Bind(wxEVT_LEAVE_WINDOW, [this, row](wxMouseEvent& evt) {
            if (m_hover_idx != -1) {
                m_hover_idx = -1;
                m_content->Refresh();
            }
            evt.Skip();
        });

        row->Bind(wxEVT_LEFT_DOWN, [this, idx, sq, sq_x](wxMouseEvent& evt) {
            int click_x = evt.GetX();
            int square_right = sq_x + sq;
            if (click_x <= square_right) {
                Dismiss();
                bool handled = false;
                if (idx < m_existing_count && idx < m_fila_types.size() &&
                    boost::algorithm::starts_with(m_fila_types[idx], "Bambu")) {
                    FilamentColor fila_color;
                    if (idx < m_colors_rgba.size()) {
                        fila_color.m_colors.insert(wxColour(
                            (unsigned char)(m_colors_rgba[idx][0] * 255.f),
                            (unsigned char)(m_colors_rgba[idx][1] * 255.f),
                            (unsigned char)(m_colors_rgba[idx][2] * 255.f)));
                        fila_color.EndSet(0);
                    }
                    wxString fila_id = idx < m_fila_ids.size()
                        ? wxString::FromUTF8(m_fila_ids[idx]) : wxString("GFA00");
                    FilamentPickerDialog dialog(GetParent(), fila_id, fila_color, m_fila_types[idx]);
                    if (dialog.IsDataLoaded() && dialog.ShowModal() == wxID_OK) {
                        FilamentColor result = dialog.GetSelectedFilamentColor();
                        wxColour clr;
                        if (!result.m_colors.empty())
                            clr = *result.m_colors.begin();
                        else
                            clr = dialog.GetSelectedColour();
                        if (clr.IsOk() && m_on_color_changed)
                            m_on_color_changed((int)idx, clr);
                        handled = true;
                    }
                }
                if (!handled) {
                    wxColourData cd;
                    cd.SetChooseFull(true);
                    if (idx < m_colors_rgba.size()) {
                        cd.SetColour(wxColour(
                            (unsigned char)(m_colors_rgba[idx][0] * 255.f),
                            (unsigned char)(m_colors_rgba[idx][1] * 255.f),
                            (unsigned char)(m_colors_rgba[idx][2] * 255.f)));
                    }
                    wxColourDialog dlg(GetParent(), &cd);
                    if (dlg.ShowModal() == wxID_OK) {
                        wxColour clr = dlg.GetColourData().GetColour();
                        if (m_on_color_changed) m_on_color_changed((int)idx, clr);
                    }
                }
            } else {
                if (m_on_select) m_on_select((int)idx);
                Dismiss();
            }
        });

        return row;
    }

    wxPanel*                                   m_content = nullptr;
    std::vector<std::array<float, 4>>          m_colors_rgba;
    std::vector<std::string>                   m_names;
    size_t                                     m_existing_count = 0;
    std::function<void(int)>                   m_on_select;
    std::function<void(int, wxColour)>         m_on_color_changed;
    std::function<void(wxColour)>              m_on_add_filament;
    std::vector<std::string>                   m_fila_ids;
    std::vector<std::string>                   m_fila_types;
    int                                        m_hover_idx = -1;
};

// ============================================================
// TexturePreviewCanvas
// ============================================================

TexturePreviewCanvas::TexturePreviewCanvas(wxWindow* parent, const wxGLAttributes& attrs)
    : wxGLCanvas(parent, attrs, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE)
{
    m_context = new wxGLContext(this);

    Bind(wxEVT_PAINT, &TexturePreviewCanvas::on_paint, this);
    Bind(wxEVT_SIZE,  &TexturePreviewCanvas::on_size,  this);
    Bind(wxEVT_MOUSEWHEEL,  &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_LEFT_DOWN,   &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_LEFT_UP,     &TexturePreviewCanvas::on_mouse, this);
    Bind(wxEVT_MOTION,      &TexturePreviewCanvas::on_mouse, this);
}

TexturePreviewCanvas::~TexturePreviewCanvas()
{
    if (m_context) {
        SetCurrent(*m_context);
        if (m_tex_id)
            glDeleteTextures(1, &m_tex_id);
        for (unsigned int id : m_gl_tex_ids)
            if (id) glDeleteTextures(1, &id);
        delete m_context;
    }
}

void TexturePreviewCanvas::set_mesh_data(
    const std::vector<std::array<float, 3>>& vertices,
    const std::vector<std::array<int, 3>>&   indices)
{
    m_vertices = vertices;
    m_indices  = indices;
    update_bounding_box();
    compute_smooth_normals();
    Refresh();
}

void TexturePreviewCanvas::compute_smooth_normals()
{
    m_vertex_normals.clear();
    if (m_vertices.empty() || m_indices.empty()) return;

    m_vertex_normals.resize(m_vertices.size(), {0.f, 0.f, 0.f});

    for (const auto& face : m_indices) {
        int i0 = face[0], i1 = face[1], i2 = face[2];
        if (i0 < 0 || i0 >= (int)m_vertices.size() ||
            i1 < 0 || i1 >= (int)m_vertices.size() ||
            i2 < 0 || i2 >= (int)m_vertices.size())
            continue;

        const auto& v0 = m_vertices[i0];
        const auto& v1 = m_vertices[i1];
        const auto& v2 = m_vertices[i2];

        float nx = (v1[1]-v0[1])*(v2[2]-v0[2]) - (v1[2]-v0[2])*(v2[1]-v0[1]);
        float ny = (v1[2]-v0[2])*(v2[0]-v0[0]) - (v1[0]-v0[0])*(v2[2]-v0[2]);
        float nz = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v1[1]-v0[1])*(v2[0]-v0[0]);

        m_vertex_normals[i0][0] += nx; m_vertex_normals[i0][1] += ny; m_vertex_normals[i0][2] += nz;
        m_vertex_normals[i1][0] += nx; m_vertex_normals[i1][1] += ny; m_vertex_normals[i1][2] += nz;
        m_vertex_normals[i2][0] += nx; m_vertex_normals[i2][1] += ny; m_vertex_normals[i2][2] += nz;
    }

    for (auto& n : m_vertex_normals) {
        float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (len > 1e-8f) { n[0] /= len; n[1] /= len; n[2] /= len; }
    }
}

void TexturePreviewCanvas::set_texture_data(
    const std::vector<std::array<float, 2>>& uvs,
    const unsigned char* tex_data, int tex_w, int tex_h, int tex_channels)
{
    m_uvs = uvs;
    m_tex_w = tex_w;
    m_tex_h = tex_h;
    m_tex_channels = tex_channels;
    m_tex_dirty = true;

    size_t sz = (size_t)tex_w * tex_h * tex_channels;
    m_tex_data.assign(tex_data, tex_data + sz);
    Refresh();
}

void TexturePreviewCanvas::set_texture_render_data(
    const std::vector<std::vector<unsigned char>>& tex_pixels_rgb,
    const std::vector<int>& tex_widths,
    const std::vector<int>& tex_heights,
    const std::vector<std::array<std::array<float,2>, 3>>& face_uvs,
    const std::vector<int>& face_tex_ids)
{
    m_tex_pixels_rgb = tex_pixels_rgb;
    m_tex_widths     = tex_widths;
    m_tex_heights    = tex_heights;
    m_face_uvs       = face_uvs;
    m_face_tex_ids   = face_tex_ids;
    m_multi_tex_dirty = true;
    Refresh();
}

void TexturePreviewCanvas::upload_textures()
{
    if (!m_multi_tex_dirty) return;
    m_multi_tex_dirty = false;

    for (unsigned int id : m_gl_tex_ids)
        if (id) glDeleteTextures(1, &id);
    m_gl_tex_ids.clear();

    m_gl_tex_ids.resize(m_tex_pixels_rgb.size(), 0);
    for (size_t i = 0; i < m_tex_pixels_rgb.size(); ++i) {
        if (m_tex_pixels_rgb[i].empty() || m_tex_widths[i] <= 0 || m_tex_heights[i] <= 0)
            continue;
        GLuint tex_id = 0;
        glGenTextures(1, &tex_id);
        glBindTexture(GL_TEXTURE_2D, tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_tex_widths[i], m_tex_heights[i],
                     0, GL_RGB, GL_UNSIGNED_BYTE, m_tex_pixels_rgb[i].data());
        m_gl_tex_ids[i] = tex_id;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TexturePreviewCanvas::set_painted_mesh_data(
    const std::vector<std::array<float, 3>>& vertices,
    const std::vector<std::array<int, 3>>&   indices)
{
    m_painted_vertices = vertices;
    m_painted_indices  = indices;
    Refresh();
}

static void convert_face_colors(const std::vector<std::array<std::size_t, 3>>& src,
                                std::vector<std::array<float, 3>>& dst)
{
    dst.resize(src.size());
    for (size_t i = 0; i < src.size(); ++i)
        dst[i] = { src[i][0] / 255.f, src[i][1] / 255.f, src[i][2] / 255.f };
}

void TexturePreviewCanvas::set_face_colors(const std::vector<std::array<std::size_t, 3>>& face_colors)
{
    convert_face_colors(face_colors, m_face_colors_rgb);
    Refresh();
}

void TexturePreviewCanvas::set_original_face_colors(const std::vector<std::array<std::size_t, 3>>& face_colors)
{
    convert_face_colors(face_colors, m_original_face_colors_rgb);
    Refresh();
}

void TexturePreviewCanvas::set_filament_color_map(
    const std::map<std::array<std::size_t, 3>, std::array<float, 3>>& color_map)
{
    m_color_map = color_map;
    m_filament_colors_rgb.resize(m_face_colors_rgb.size());
    for (size_t i = 0; i < m_face_colors_rgb.size(); ++i) {
        std::array<std::size_t, 3> key = {
            (std::size_t)(m_face_colors_rgb[i][0] * 255.f + 0.5f),
            (std::size_t)(m_face_colors_rgb[i][1] * 255.f + 0.5f),
            (std::size_t)(m_face_colors_rgb[i][2] * 255.f + 0.5f)
        };
        auto it = color_map.find(key);
        if (it != color_map.end())
            m_filament_colors_rgb[i] = it->second;
        else
            m_filament_colors_rgb[i] = m_face_colors_rgb[i];
    }
    Refresh();
}

void TexturePreviewCanvas::set_render_mode(RenderMode mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        Refresh();
    }
}

void TexturePreviewCanvas::set_computing_overlay(bool /*show*/)
{
    Refresh();
}

void TexturePreviewCanvas::reset_view()
{
    m_zoom  = 1.0f;
    m_rot_x = -30.0f;
    m_rot_y = 30.0f;
    Refresh();
}

void TexturePreviewCanvas::update_bounding_box()
{
    if (m_vertices.empty()) return;
    std::array<float, 3> mn = m_vertices[0], mx = m_vertices[0];
    for (const auto& v : m_vertices) {
        for (int i = 0; i < 3; ++i) {
            mn[i] = std::min(mn[i], v[i]);
            mx[i] = std::max(mx[i], v[i]);
        }
    }
    m_center = { (mn[0]+mx[0])/2, (mn[1]+mx[1])/2, (mn[2]+mx[2])/2 };
    float dx = mx[0]-mn[0], dy = mx[1]-mn[1], dz = mx[2]-mn[2];
    m_radius = std::sqrt(dx*dx + dy*dy + dz*dz) / 2.0f;
    if (m_radius < 1e-6f) m_radius = 1.0f;
}

void TexturePreviewCanvas::ensure_gl_ready()
{
    if (m_gl_initialized) return;

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        BOOST_LOG_TRIVIAL(error) << "TexturePreviewCanvas: glewInit failed: "
                                 << glewGetErrorString(err);
        return;
    }
    while (glGetError() != GL_NO_ERROR) {}

    m_gl_initialized = true;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    GLfloat light_pos[]     = { 0.5f, 1.0f, 1.0f, 0.0f };
    GLfloat light_ambient[] = { 0.3f, 0.3f, 0.3f, 1.0f };
    GLfloat light_diffuse[] = { 0.8f, 0.8f, 0.8f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  light_diffuse);

    glClearColor(0.933f, 0.933f, 0.933f, 1.0f);
}

void TexturePreviewCanvas::on_paint(wxPaintEvent&)
{
    wxPaintDC dc(this);
    if (!m_context) return;
    SetCurrent(*m_context);
    ensure_gl_ready();
    render();
    SwapBuffers();
}

void TexturePreviewCanvas::on_size(wxSizeEvent&)
{
    Refresh();
}

void TexturePreviewCanvas::on_mouse(wxMouseEvent& evt)
{
    if (evt.LeftDown()) {
        m_dragging = true;
        m_last_mouse_pos = evt.GetPosition();
        CaptureMouse();
    }
    else if (evt.LeftUp()) {
        m_dragging = false;
        if (HasCapture()) ReleaseMouse();
    }
    else if (evt.Dragging() && m_dragging) {
        wxPoint pos = evt.GetPosition();
        float dx = (float)(pos.x - m_last_mouse_pos.x);
        float dy = (float)(pos.y - m_last_mouse_pos.y);
        m_rot_y += dx * 0.5f;
        m_rot_x += dy * 0.5f;
        m_rot_x = std::max(-89.0f, std::min(89.0f, m_rot_x));
        m_last_mouse_pos = pos;
        Refresh();
    }
    else if (evt.GetWheelRotation() != 0) {
        float delta = evt.GetWheelRotation() > 0 ? 1.1f : 0.9f;
        m_zoom *= delta;
        m_zoom = std::max(0.1f, std::min(20.0f, m_zoom));
        Refresh();
    }
}

void TexturePreviewCanvas::render()
{
    wxSize sz = GetClientSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    glViewport(0, 0, sz.x, sz.y);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)sz.x / (float)sz.y;
    float dist = m_radius * 3.0f / m_zoom;
    float near_plane = dist * 0.01f;
    float far_plane  = dist * 10.0f;
    float fov_rad    = 45.0f * static_cast<float>(M_PI) / 180.0f;
    float f          = 1.0f / std::tan(fov_rad / 2.0f);
    float proj[16]   = {};
    proj[0]  = f / aspect;
    proj[5]  = f;
    proj[10] = (far_plane + near_plane) / (near_plane - far_plane);
    proj[11] = -1.0f;
    proj[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
    glMultMatrixf(proj);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -dist);
    glRotatef(m_rot_x, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rot_y, 0.0f, 1.0f, 0.0f);
    glTranslatef(-m_center[0], -m_center[1], -m_center[2]);

    render_mesh();
}

void TexturePreviewCanvas::render_textured_original()
{
    if (m_vertices.empty() || m_indices.empty()) return;
    if (m_face_uvs.empty() || m_face_tex_ids.empty()) return;
    if (m_face_uvs.size() != m_indices.size()) return;

    upload_textures();

    const bool has_smooth = (m_vertex_normals.size() == m_vertices.size());

    // Group faces by texture id for batch rendering
    std::map<int, std::vector<size_t>> tex_groups;
    for (size_t fi = 0; fi < m_indices.size(); ++fi) {
        int tid = (fi < m_face_tex_ids.size()) ? m_face_tex_ids[fi] : -1;
        tex_groups[tid].push_back(fi);
    }

    glEnable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 1.0f);

    for (const auto& [tid, face_list] : tex_groups) {
        bool tex_bound = false;
        if (tid >= 0 && tid < (int)m_gl_tex_ids.size() && m_gl_tex_ids[tid] != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, m_gl_tex_ids[tid]);
            tex_bound = true;
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        glBegin(GL_TRIANGLES);
        for (size_t fi : face_list) {
            const auto& face = m_indices[fi];
            const auto& uvs  = m_face_uvs[fi];

            if (!tex_bound) {
                if (fi < m_original_face_colors_rgb.size())
                    glColor3fv(m_original_face_colors_rgb[fi].data());
                else
                    glColor3f(0.7f, 0.7f, 0.7f);
            }

            for (int vi = 0; vi < 3; ++vi) {
                int idx = face[vi];
                if (idx < 0 || idx >= (int)m_vertices.size()) continue;

                if (has_smooth) {
                    glNormal3fv(m_vertex_normals[idx].data());
                } else if (vi == 0) {
                    const auto& v0 = m_vertices[face[0]];
                    const auto& v1 = m_vertices[face[1]];
                    const auto& v2 = m_vertices[face[2]];
                    float nx = (v1[1]-v0[1])*(v2[2]-v0[2]) - (v1[2]-v0[2])*(v2[1]-v0[1]);
                    float ny = (v1[2]-v0[2])*(v2[0]-v0[0]) - (v1[0]-v0[0])*(v2[2]-v0[2]);
                    float nz = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v1[1]-v0[1])*(v2[0]-v0[0]);
                    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
                    if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
                    glNormal3f(nx, ny, nz);
                }

                if (tex_bound)
                    glTexCoord2fv(uvs[vi].data());
                glVertex3fv(m_vertices[idx].data());
            }
        }
        glEnd();
    }

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TexturePreviewCanvas::render_mesh()
{
    if (m_vertices.empty() || m_indices.empty()) return;

    // Original mode with texture data: use proper texture mapping
    if (m_mode == RenderMode::Original && !m_face_uvs.empty()) {
        render_textured_original();
        return;
    }

    // For Multi-Color / FilamentMap, use the painted (remeshed) geometry if available;
    // the face color arrays match the painted mesh, not the original mesh.
    const bool use_painted = (m_mode != RenderMode::Original)
                             && !m_painted_vertices.empty()
                             && !m_painted_indices.empty();

    const auto& verts   = use_painted ? m_painted_vertices : m_vertices;
    const auto& faces   = use_painted ? m_painted_indices  : m_indices;

    const std::vector<std::array<float, 3>>* colors_ptr = nullptr;
    if (m_mode == RenderMode::Original && !m_original_face_colors_rgb.empty()
        && m_original_face_colors_rgb.size() == m_indices.size()) {
        colors_ptr = &m_original_face_colors_rgb;
    } else if (m_mode == RenderMode::FilamentMap && !m_filament_colors_rgb.empty()
               && m_filament_colors_rgb.size() == faces.size()) {
        colors_ptr = &m_filament_colors_rgb;
    } else if (!m_face_colors_rgb.empty() && m_face_colors_rgb.size() == faces.size()) {
        colors_ptr = &m_face_colors_rgb;
    }

    // Use smooth normals for the original mesh when available
    const bool has_smooth = !use_painted
                            && (m_vertex_normals.size() == m_vertices.size());

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);

    glBegin(GL_TRIANGLES);
    for (size_t fi = 0; fi < faces.size(); ++fi) {
        if (colors_ptr)
            glColor3fv((*colors_ptr)[fi].data());
        else
            glColor3f(0.7f, 0.7f, 0.7f);

        const auto& face = faces[fi];
        for (int vi = 0; vi < 3; ++vi) {
            int idx = face[vi];
            if (idx < 0 || idx >= (int)verts.size()) continue;

            if (has_smooth && idx < (int)m_vertex_normals.size()) {
                glNormal3fv(m_vertex_normals[idx].data());
            } else if (vi == 0) {
                const auto& v0 = verts[face[0]];
                const auto& v1 = verts[face[1]];
                const auto& v2 = verts[face[2]];
                float nx = (v1[1]-v0[1])*(v2[2]-v0[2]) - (v1[2]-v0[2])*(v2[1]-v0[1]);
                float ny = (v1[2]-v0[2])*(v2[0]-v0[0]) - (v1[0]-v0[0])*(v2[2]-v0[2]);
                float nz = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v1[1]-v0[1])*(v2[0]-v0[0]);
                float len = std::sqrt(nx*nx + ny*ny + nz*nz);
                if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
                glNormal3f(nx, ny, nz);
            }

            glVertex3fv(verts[idx].data());
        }
    }
    glEnd();
}


// ============================================================
// TextureImportDialog
// ============================================================

wxBEGIN_EVENT_TABLE(TextureImportDialog, DPIDialog)
    EVT_BUTTON(TextureImportDialog::ID_COLOR_4,    TextureImportDialog::on_color_preset_clicked)
    EVT_BUTTON(TextureImportDialog::ID_COLOR_8,    TextureImportDialog::on_color_preset_clicked)
    EVT_BUTTON(TextureImportDialog::ID_COLOR_16,   TextureImportDialog::on_color_preset_clicked)
    EVT_BUTTON(TextureImportDialog::ID_COLOR_AUTO,  TextureImportDialog::on_color_preset_clicked)
    EVT_BUTTON(TextureImportDialog::ID_BTN_APPLY,   TextureImportDialog::on_apply_clicked)
    EVT_BUTTON(TextureImportDialog::ID_BTN_SKIP,    TextureImportDialog::on_skip_clicked)
    EVT_BUTTON(wxID_OK,                              TextureImportDialog::on_ok_clicked)
wxEND_EVENT_TABLE()

TextureImportDialog::TextureImportDialog(
    wxWindow*                        parent,
    const Slic3r::TexturedMesh&      textured_mesh,
    const std::vector<std::string>&  filament_color_strs,
    const std::vector<std::string>&  filament_names)
    : DPIDialog(parent, wxID_ANY, _L("Import Model"),
                wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX)
    , m_textured_mesh(textured_mesh)
    , m_filament_color_strs(filament_color_strs)
    , m_filament_names(filament_names)
{
    SetSize(wxSize(FromDIP(960), FromDIP(640)));

    m_filament_colors_rgba.reserve(filament_color_strs.size());
    for (const auto& s : filament_color_strs)
        m_filament_colors_rgba.push_back(parse_color_string(s));

    m_existing_filament_count = m_filament_colors_rgba.size();

    {
        auto& preset_bundle = *wxGetApp().preset_bundle;
        for (size_t i = 0; i < m_existing_filament_count; ++i) {
            std::string fila_id;
            std::string fila_type;
            if (i < preset_bundle.filament_presets.size()) {
                const auto* preset = preset_bundle.filaments.find_preset(preset_bundle.filament_presets[i]);
                if (preset) {
                    fila_id = preset->filament_id;
                    fila_type = Preset::remove_suffix_modified(preset->label(false));
                }
            }
            m_filament_preset_ids.push_back(fila_id);
            m_filament_preset_types.push_back(fila_type);
        }
    }

    if (m_filament_names.empty()) {
        for (size_t i = 0; i < filament_color_strs.size(); ++i)
            m_filament_names.push_back("Filament " + std::to_string(i + 1));
    }

    Bind(EVT_TEXTURE_COMPUTE_DONE,     &TextureImportDialog::on_computation_complete, this);
    Bind(EVT_TEXTURE_COMPUTE_PROGRESS, &TextureImportDialog::on_computation_progress, this);
    Bind(EVT_TEXTURE_COMPUTE_ERROR,    &TextureImportDialog::on_computation_error,    this);

    build_ui();
    SetMinSize(wxSize(FromDIP(800), FromDIP(500)));
    CenterOnParent();

    m_preview_canvas->set_mesh_data(m_textured_mesh.vertices, m_textured_mesh.indices);

    // Prepare texture rendering data for the Original tab
    if (!m_textured_mesh.textures.empty()) {
        std::vector<std::vector<unsigned char>> tex_pixels_rgb;
        std::vector<int> tex_widths, tex_heights;
        tex_pixels_rgb.reserve(m_textured_mesh.textures.size());
        tex_widths.reserve(m_textured_mesh.textures.size());
        tex_heights.reserve(m_textured_mesh.textures.size());

        for (const auto& ti : m_textured_mesh.textures) {
            std::vector<unsigned char> bgr_pixels;
            int w = 0, h = 0;
            if (Slic3r::decode_texture_to_pixels(ti, bgr_pixels, w, h) && !bgr_pixels.empty()) {
                // Convert BGR to RGB for OpenGL
                for (size_t p = 0; p < bgr_pixels.size(); p += 3)
                    std::swap(bgr_pixels[p], bgr_pixels[p + 2]);
                tex_pixels_rgb.push_back(std::move(bgr_pixels));
            } else {
                tex_pixels_rgb.push_back({});
            }
            tex_widths.push_back(w);
            tex_heights.push_back(h);
        }

        const size_t nf = m_textured_mesh.indices.size();
        const bool has_mapping = !m_textured_mesh.material_texture_map.empty();

        // Build per-face UV array
        std::vector<std::array<std::array<float,2>, 3>> face_uvs(nf);
        for (size_t fi = 0; fi < nf; ++fi) {
            if (m_textured_mesh.has_face_uvs()) {
                const auto& ui = m_textured_mesh.uv_indices[fi];
                for (int vi = 0; vi < 3; ++vi) {
                    int idx = ui[vi];
                    if (idx >= 0 && static_cast<size_t>(idx) < m_textured_mesh.uv_coords.size())
                        face_uvs[fi][vi] = m_textured_mesh.uv_coords[idx];
                    else
                        face_uvs[fi][vi] = {0.f, 0.f};
                }
            } else if (!m_textured_mesh.uvs.empty()) {
                const auto& face = m_textured_mesh.indices[fi];
                for (int vi = 0; vi < 3; ++vi) {
                    int idx = face[vi];
                    if (idx >= 0 && static_cast<size_t>(idx) < m_textured_mesh.uvs.size())
                        face_uvs[fi][vi] = m_textured_mesh.uvs[idx];
                    else
                        face_uvs[fi][vi] = {0.f, 0.f};
                }
            }
        }

        // Build per-face texture index
        std::vector<int> face_tex_ids(nf, 0);
        for (size_t fi = 0; fi < nf; ++fi) {
            int mat_idx = (fi < m_textured_mesh.material_ids.size())
                          ? m_textured_mesh.material_ids[fi] : -1;
            if (has_mapping && mat_idx >= 0
                && static_cast<size_t>(mat_idx) < m_textured_mesh.material_texture_map.size())
                face_tex_ids[fi] = m_textured_mesh.material_texture_map[mat_idx];
            else if (!tex_pixels_rgb.empty())
                face_tex_ids[fi] = 0;
            else
                face_tex_ids[fi] = -1;
        }

        m_preview_canvas->set_texture_render_data(
            tex_pixels_rgb, tex_widths, tex_heights, face_uvs, face_tex_ids);

        // Still sample per-face colors as fallback
        std::vector<std::array<std::size_t, 3>> orig_colors;
        if (Slic3r::sample_original_face_colors(m_textured_mesh, orig_colors))
            m_preview_canvas->set_original_face_colors(orig_colors);
    }

    set_state(TextureImportState::Idle);

    CallAfter([this]() { start_computation(true); });
}

TextureImportDialog::~TextureImportDialog()
{
    m_cancel_flag = true;
    if (m_worker && m_worker->joinable())
        m_worker->join();
}

void TextureImportDialog::build_ui()
{
    SetBackgroundColour(*wxWHITE);
    SetForegroundColour(wxColour(50, 58, 61));

    wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

    auto line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line_top->SetBackgroundColour(wxColour(166, 169, 170));
    root_sizer->Add(line_top, 0, wxEXPAND);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* left_sizer = new wxBoxSizer(wxVERTICAL);
    build_preview_panel(this, left_sizer);
    main_sizer->Add(left_sizer, 3, wxEXPAND | wxALL, FromDIP(8));

    wxBoxSizer* right_sizer = new wxBoxSizer(wxVERTICAL);
    build_params_panel(this, right_sizer);
    build_mapping_panel(this, right_sizer);
    build_bottom_buttons(right_sizer);
    main_sizer->Add(right_sizer, 2, wxEXPAND | wxALL, FromDIP(8));

    root_sizer->Add(main_sizer, 1, wxEXPAND);

    SetSizer(root_sizer);
    Layout();
}

void TextureImportDialog::build_preview_panel(wxWindow* parent, wxSizer* sizer)
{
    wxPanel* preview_container = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    preview_container->SetBackgroundColour(wxColour(238, 238, 238));
    preview_container->SetBackgroundStyle(wxBG_STYLE_PAINT);
    preview_container->Bind(wxEVT_PAINT, [](wxPaintEvent& e) {
        auto* p = static_cast<wxPanel*>(e.GetEventObject());
        wxAutoBufferedPaintDC dc(p);
        wxSize sz = p->GetClientSize();
        dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, sz.x, sz.y);
        dc.SetBrush(wxBrush(wxColour(238, 238, 238)));
        dc.SetPen(wxPen(wxColour(206, 206, 206), 1));
        dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, 4);
    });

    wxBoxSizer* container_sizer = new wxBoxSizer(wxVERTICAL);

    wxGLAttributes canvas_attrs;
    canvas_attrs.PlatformDefaults().RGBA().DoubleBuffer().Depth(24).EndList();
    m_preview_canvas = new TexturePreviewCanvas(preview_container, canvas_attrs);
    container_sizer->Add(m_preview_canvas, 1, wxEXPAND | wxALL, FromDIP(1));

    preview_container->SetSizer(container_sizer);
    sizer->Add(preview_container, 1, wxEXPAND);

    m_tab_panel = new wxPanel(preview_container, wxID_ANY);
    m_tab_panel->SetBackgroundColour(wxColour(238, 238, 238));

    m_btn_view_original   = new Button(m_tab_panel, _L("Original"));
    m_btn_view_original->SetId(ID_VIEW_ORIGINAL);
    m_btn_view_multicolor = new Button(m_tab_panel, _L("Multi-Color"));
    m_btn_view_multicolor->SetId(ID_VIEW_MULTICOLOR);
    m_btn_view_filament   = new Button(m_tab_panel, _L("Filament Mapping"));
    m_btn_view_filament->SetId(ID_VIEW_FILAMENT);

    m_btn_view_original->SetCornerRadius(FromDIP(12));
    m_btn_view_original->SetMinSize(wxSize(FromDIP(80), FromDIP(28)));
    m_btn_view_multicolor->SetCornerRadius(FromDIP(12));
    m_btn_view_multicolor->SetMinSize(wxSize(FromDIP(90), FromDIP(28)));
    m_btn_view_filament->SetCornerRadius(FromDIP(12));
    m_btn_view_filament->SetMinSize(wxSize(FromDIP(100), FromDIP(28)));

    wxBoxSizer* tab_sizer = new wxBoxSizer(wxHORIZONTAL);
    tab_sizer->Add(m_btn_view_original,   0, wxRIGHT, FromDIP(2));
    tab_sizer->Add(m_btn_view_multicolor, 0, wxRIGHT, FromDIP(2));
    tab_sizer->Add(m_btn_view_filament,   0);
    m_tab_panel->SetSizer(tab_sizer);
    m_tab_panel->Fit();

    m_btn_view_multicolor->Hide();
    m_btn_view_filament->Hide();

    m_btn_view_original->Bind(wxEVT_BUTTON,   &TextureImportDialog::on_view_button_clicked, this);
    m_btn_view_multicolor->Bind(wxEVT_BUTTON,  &TextureImportDialog::on_view_button_clicked, this);
    m_btn_view_filament->Bind(wxEVT_BUTTON,    &TextureImportDialog::on_view_button_clicked, this);

    preview_container->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        if (m_tab_panel) {
            m_tab_panel->Fit();
            wxSize cs = e.GetSize();
            wxSize ts = m_tab_panel->GetSize();
            m_tab_panel->SetPosition(wxPoint((cs.x - ts.x) / 2, FromDIP(8)));
            m_tab_panel->Raise();
        }
    });

    highlight_view_button(0);
}

void TextureImportDialog::build_params_panel(wxWindow* parent, wxSizer* sizer)
{
    wxBoxSizer* color_header_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* lbl_colors = new wxStaticText(parent, wxID_ANY, _L("Color Count"));
    lbl_colors->SetForegroundColour(wxColour(50, 58, 61));
    lbl_colors->SetFont(lbl_colors->GetFont().Bold());
    color_header_sizer->Add(lbl_colors, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    m_btn_color_4    = new wxButton(parent, ID_COLOR_4,    "4",    wxDefaultPosition, wxSize(FromDIP(28), FromDIP(22)));
    m_btn_color_8    = new wxButton(parent, ID_COLOR_8,    "8",    wxDefaultPosition, wxSize(FromDIP(28), FromDIP(22)));
    m_btn_color_16   = new wxButton(parent, ID_COLOR_16,   "16",   wxDefaultPosition, wxSize(FromDIP(28), FromDIP(22)));
    m_btn_color_auto = new Button(parent, _L("Auto"));
    m_btn_color_auto->SetId(ID_COLOR_AUTO);
    color_header_sizer->Add(m_btn_color_4,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
    color_header_sizer->Add(m_btn_color_8,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
    color_header_sizer->Add(m_btn_color_16, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(color_header_sizer, 0, wxBOTTOM, FromDIP(4));

    wxBoxSizer* color_slider_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_color_slider = new GreenSlider(parent, m_param_color_count, 1, 32);
    m_color_spin = new SpinInput(parent, wxString::Format("%d", m_param_color_count),
                                 wxEmptyString, wxDefaultPosition,
                                 wxSize(FromDIP(60), FromDIP(28)),
                                 wxTE_PROCESS_ENTER, 1, 32, m_param_color_count);

    m_color_slider->Bind(wxEVT_SLIDER, &TextureImportDialog::on_color_slider_changed, this);
    m_color_spin->Bind(wxEVT_SPINCTRL, &TextureImportDialog::on_color_spin_changed, this);

    color_slider_sizer->Add(m_color_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    color_slider_sizer->Add(m_color_spin, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(color_slider_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    wxStaticText* lbl_smooth = new wxStaticText(parent, wxID_ANY, _L("Smooth Level"));
    lbl_smooth->SetForegroundColour(wxColour(50, 58, 61));
    lbl_smooth->SetFont(lbl_smooth->GetFont().Bold());
    sizer->Add(lbl_smooth, 0, wxBOTTOM, FromDIP(4));

    wxBoxSizer* smooth_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_smooth_slider = new GreenSlider(parent, m_param_smooth, 0, 10);
    m_smooth_spin = new SpinInput(parent, wxString::Format("%d", m_param_smooth),
                                  wxEmptyString, wxDefaultPosition,
                                  wxSize(FromDIP(60), FromDIP(28)),
                                  wxTE_PROCESS_ENTER, 0, 10, m_param_smooth);

    m_smooth_slider->Bind(wxEVT_SLIDER, &TextureImportDialog::on_smooth_slider_changed, this);
    m_smooth_spin->Bind(wxEVT_SPINCTRL, &TextureImportDialog::on_smooth_spin_changed, this);

    smooth_sizer->Add(m_smooth_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    smooth_sizer->Add(m_smooth_spin, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(smooth_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    m_btn_apply = new Button(parent, _L("Apply"));
    m_btn_apply->SetId(ID_BTN_APPLY);

    {
        StateColor btn_bg_white(
            std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
        StateColor btn_bd_green(
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor btn_text_green(
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

        m_btn_color_auto->SetCornerRadius(FromDIP(12));
        m_btn_color_auto->SetMinSize(wxSize(FromDIP(60), FromDIP(28)));
        m_btn_color_auto->SetBackgroundColor(btn_bg_white);
        m_btn_color_auto->SetBorderColor(btn_bd_green);
        m_btn_color_auto->SetTextColor(btn_text_green);

        m_btn_apply->SetCornerRadius(FromDIP(12));
        m_btn_apply->SetMinSize(wxSize(FromDIP(60), FromDIP(28)));
        m_btn_apply->SetBackgroundColor(btn_bg_white);
        m_btn_apply->SetBorderColor(btn_bd_green);
        m_btn_apply->SetTextColor(btn_text_green);
    }

    m_btn_color_auto->SetToolTip(_L("Automatically determine the optimal color count and convert texture to painting"));
    m_btn_apply->SetToolTip(_L("Convert texture to painting using the specified color count and smooth level"));

    wxBoxSizer* apply_sizer = new wxBoxSizer(wxHORIZONTAL);
    apply_sizer->Add(m_btn_color_auto, 0, wxRIGHT, FromDIP(4));
    apply_sizer->Add(m_btn_apply, 0);
    sizer->Add(apply_sizer, 0, wxALIGN_RIGHT | wxBOTTOM, FromDIP(8));

    sizer->Add(new wxStaticLine(parent), 0, wxEXPAND | wxBOTTOM, FromDIP(8));
}

void TextureImportDialog::build_mapping_panel(wxWindow* parent, wxSizer* sizer)
{
    wxBoxSizer* header_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxStaticText* lbl_mapping = new wxStaticText(parent, wxID_ANY, _L("Filament Mapping"));
    lbl_mapping->SetForegroundColour(wxColour(107, 107, 107));
    lbl_mapping->SetFont(lbl_mapping->GetFont().Bold());
    header_sizer->Add(lbl_mapping, 0, wxALIGN_CENTER_VERTICAL);

    header_sizer->AddStretchSpacer();

    m_auto_merge_cb = new wxCheckBox(parent, wxID_ANY, _L("Auto-merge same filament"));
    m_auto_merge_cb->SetToolTip(_L("Automatically merge identical filaments into existing filaments in the project"));
    m_auto_merge_cb->SetForegroundColour(wxColour(107, 107, 107));
    m_auto_merge_cb->SetValue(true);
    m_auto_merge_cb->Bind(wxEVT_CHECKBOX, &TextureImportDialog::on_auto_merge_toggled, this);
    header_sizer->Add(m_auto_merge_cb, 0, wxALIGN_CENTER_VERTICAL);

    sizer->Add(header_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    m_mapping_scroll = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition,
                                             wxSize(-1, FromDIP(300)));
    m_mapping_scroll->SetScrollRate(0, FromDIP(10));
    m_mapping_scroll->SetBackgroundColour(wxColour(255, 255, 255));

    m_mapping_sizer = new wxBoxSizer(wxVERTICAL);
    m_mapping_scroll->SetSizer(m_mapping_sizer);

    sizer->Add(m_mapping_scroll, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
}

void TextureImportDialog::build_bottom_buttons(wxSizer* sizer)
{
    wxBoxSizer* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_btn_skip = new Button(this, _L("Skip Matching"));
    m_btn_skip->SetId(ID_BTN_SKIP);
    m_btn_skip->SetToolTip(_L("Skip filament mapping and import as a single-color model"));
    m_btn_skip->SetCornerRadius(FromDIP(20));
    m_btn_skip->SetMinSize(wxSize(FromDIP(106), FromDIP(36)));
    {
        StateColor skip_bg(
            std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
        StateColor skip_bd(
            std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Normal));
        StateColor skip_text(
            std::pair<wxColour, int>(wxColour(107, 107, 107), StateColor::Normal));
        m_btn_skip->SetBackgroundColor(skip_bg);
        m_btn_skip->SetBorderColor(skip_bd);
        m_btn_skip->SetTextColor(skip_text);
    }

    m_btn_ok = new Button(this, _L("Confirm"));
    m_btn_ok->SetId(wxID_OK);
    m_btn_ok->SetCornerRadius(FromDIP(20));
    m_btn_ok->SetMinSize(wxSize(FromDIP(156), FromDIP(36)));
    {
        StateColor ok_bg(
            std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor ok_bd(
            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
        StateColor ok_text(
            std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
        m_btn_ok->SetBackgroundColor(ok_bg);
        m_btn_ok->SetBorderColor(ok_bd);
        m_btn_ok->SetTextColor(ok_text);
    }

    btn_sizer->AddStretchSpacer();
    btn_sizer->Add(m_btn_skip, 0, wxRIGHT, FromDIP(12));
    btn_sizer->Add(m_btn_ok, 0);

    sizer->Add(btn_sizer, 0, wxEXPAND | wxTOP, FromDIP(8));
}

// ---- State machine ----

void TextureImportDialog::set_state(TextureImportState new_state)
{
    m_state = new_state;
    update_ui_for_state();
}

void TextureImportDialog::update_ui_for_state()
{
    bool computing = (m_state == TextureImportState::Computing);
    bool ready     = (m_state == TextureImportState::Ready);
    bool idle      = (m_state == TextureImportState::Idle);

    m_color_slider->Enable(!computing);
    m_color_spin->Enable(!computing);
    m_smooth_slider->Enable(!computing);
    m_smooth_spin->Enable(!computing);
    m_btn_apply->Enable(!computing);
    m_btn_color_4->Enable(!computing);
    m_btn_color_8->Enable(!computing);
    m_btn_color_16->Enable(!computing);
    m_btn_color_auto->Enable(!computing);

    m_btn_ok->Enable(ready);
    m_btn_skip->Enable(ready || idle);

    m_auto_merge_cb->Enable(!computing);

    m_preview_canvas->set_computing_overlay(computing);

    Layout();
}

// ---- Async computation ----

void TextureImportDialog::start_computation(bool auto_color)
{
    cancel_computation();

    m_cancel_flag = false;
    set_state(TextureImportState::Computing);

    m_progress_dlg = new ProgressDialog(
        _L("Processing"), _L("Computing texture colors..."),
        100, this, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE);

    Slic3r::TexturePaintingSettings settings;
    settings.target_colors_num = auto_color ? 0 : (size_t)m_param_color_count;
    settings.smooth_weight     = m_param_smooth / 10.0;

    Slic3r::TexturedMesh mesh_copy = m_textured_mesh;
    wxEvtHandler* handler = this;

    m_worker = std::make_unique<std::thread>([this, settings, mesh_copy, handler]() {
        Slic3r::PaintedMesh result;

        auto progress_cb = [handler](int percent, const char*) {
            auto* evt = new wxCommandEvent(EVT_TEXTURE_COMPUTE_PROGRESS);
            evt->SetInt(percent);
            wxQueueEvent(handler, evt);
        };

        auto cancel_cb = [this]() -> bool {
            return m_cancel_flag.load();
        };

        bool ok = Slic3r::texture_to_painting(mesh_copy, result, settings, progress_cb, cancel_cb);

        if (m_cancel_flag.load()) {
            wxQueueEvent(handler, new wxCommandEvent(EVT_TEXTURE_COMPUTE_ERROR));
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_result_mutex);
            m_pending_result = std::move(result);
        }

        if (ok) {
            wxQueueEvent(handler, new wxCommandEvent(EVT_TEXTURE_COMPUTE_DONE));
        } else {
            wxQueueEvent(handler, new wxCommandEvent(EVT_TEXTURE_COMPUTE_ERROR));
        }
    });
}

void TextureImportDialog::cancel_computation()
{
    m_cancel_flag = true;
    if (m_worker && m_worker->joinable())
        m_worker->join();
    m_worker.reset();

    if (m_progress_dlg) {
        m_progress_dlg->Destroy();
        m_progress_dlg = nullptr;
    }
}

void TextureImportDialog::on_computation_progress(wxCommandEvent& evt)
{
    if (m_progress_dlg) {
        if (!m_progress_dlg->Update(evt.GetInt()))
            m_cancel_flag = true;
    }
}

void TextureImportDialog::on_computation_complete(wxCommandEvent&)
{
    if (m_progress_dlg) {
        m_progress_dlg->Destroy();
        m_progress_dlg = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(m_result_mutex);
        m_painted = std::move(m_pending_result);
    }

    int actual_colors = (int)m_painted.cluster_colors.size();
    if (actual_colors >= 2 && actual_colors <= 32) {
        m_param_color_count = actual_colors;
        m_color_slider->SetValue(actual_colors);
        m_color_spin->SetValue(actual_colors);
    }

    m_preview_canvas->set_painted_mesh_data(m_painted.vertices, m_painted.indices);
    m_preview_canvas->set_face_colors(m_painted.face_colors);

    do_auto_match();
    rebuild_mapping_rows();

    set_state(TextureImportState::Ready);

    m_btn_view_multicolor->Show();
    m_btn_view_filament->Show();
    if (m_tab_panel) {
        m_tab_panel->GetSizer()->Layout();
        m_tab_panel->Fit();
        wxWindow* container = m_tab_panel->GetParent();
        if (container) {
            wxSize cs = container->GetClientSize();
            wxSize ts = m_tab_panel->GetSize();
            m_tab_panel->SetPosition(wxPoint((cs.x - ts.x) / 2, FromDIP(8)));
        }
    }
    GetSizer()->Layout();

    m_preview_canvas->set_render_mode(TexturePreviewCanvas::RenderMode::FilamentMap);
    highlight_view_button(2);
}

void TextureImportDialog::on_computation_error(wxCommandEvent&)
{
    if (m_progress_dlg) {
        m_progress_dlg->Destroy();
        m_progress_dlg = nullptr;
    }

    if (m_cancel_flag.load()) {
        set_state(TextureImportState::Idle);
        return;
    }

    set_state(TextureImportState::Error);
    wxMessageBox(_L("Computation failed. Please adjust parameters and retry."),
                 _L("Error"), wxOK | wxICON_ERROR, this);
}

// ---- Mapping ----

void TextureImportDialog::update_filament_color_map()
{
    std::map<std::array<std::size_t, 3>, std::array<float, 3>> color_map;
    for (const auto& m : m_current_matches) {
        if (m.filament_index >= 0 && m.filament_index < (int)m_filament_colors_rgba.size()) {
            color_map[m.cluster_color] = {
                m_filament_colors_rgba[m.filament_index][0],
                m_filament_colors_rgba[m.filament_index][1],
                m_filament_colors_rgba[m.filament_index][2]
            };
        }
    }
    m_preview_canvas->set_filament_color_map(color_map);
}

int TextureImportDialog::add_virtual_filament(const std::array<float, 4>& rgba, const std::string& hex)
{
    m_filament_colors_rgba.push_back(rgba);
    m_filament_color_strs.push_back(hex);
    m_filament_names.push_back(DEFAULT_VIRTUAL_FILAMENT_NAME);
    m_new_filament_colors.push_back(rgba);
    return (int)m_filament_colors_rgba.size() - 1;
}

void TextureImportDialog::show_filament_popup(size_t row_index)
{
    if (row_index >= m_mapping_rows.size()) return;

    auto on_select = [this, row_index](int idx) {
        if (row_index >= m_mapping_rows.size()) return;
        m_mapping_rows[row_index].target_filament_idx = idx;
        if (row_index < m_current_matches.size())
            m_current_matches[row_index].filament_index = idx;
        if (m_mapping_rows[row_index].target_panel)
            m_mapping_rows[row_index].target_panel->Refresh();
        update_filament_color_map();
    };

    auto on_color_changed = [this](int idx, wxColour clr) {
        if (idx < 0 || idx >= (int)m_filament_colors_rgba.size()) return;
        m_filament_colors_rgba[idx] = {clr.Red() / 255.f, clr.Green() / 255.f,
                                       clr.Blue() / 255.f, 1.0f};
        m_filament_color_strs[idx] = wxString::Format("#%02X%02X%02X",
            clr.Red(), clr.Green(), clr.Blue()).ToStdString();
        if ((size_t)idx < m_existing_filament_count) {
            m_changed_existing_colors[(size_t)idx] = m_filament_color_strs[idx];
        } else {
            size_t vi = idx - m_existing_filament_count;
            if (vi < m_new_filament_colors.size())
                m_new_filament_colors[vi] = m_filament_colors_rgba[idx];
        }
        rebuild_mapping_rows();
        update_filament_color_map();
    };

    auto on_add_filament = [this, row_index](wxColour clr) {
        std::array<float, 4> rgba = {clr.Red() / 255.f, clr.Green() / 255.f,
                                     clr.Blue() / 255.f, 1.0f};
        std::string hex = wxString::Format("#%02X%02X%02X",
            clr.Red(), clr.Green(), clr.Blue()).ToStdString();
        int new_idx = add_virtual_filament(rgba, hex);

        if (row_index < m_mapping_rows.size()) {
            m_mapping_rows[row_index].target_filament_idx = new_idx;
            if (row_index < m_current_matches.size())
                m_current_matches[row_index].filament_index = new_idx;
        }
        rebuild_mapping_rows();
        update_filament_color_map();
    };

    auto* popup = new FilamentSelectPopup(
        this, m_filament_colors_rgba, m_filament_names, m_filament_color_strs,
        m_existing_filament_count, on_select, on_color_changed, on_add_filament,
        m_filament_preset_ids, m_filament_preset_types);

    wxPanel* tp = m_mapping_rows[row_index].target_panel;
    wxPoint pos = tp->ClientToScreen(wxPoint(0, tp->GetSize().y));
    popup->Position(pos, wxSize(0, 0));
    popup->Popup();
}

void TextureImportDialog::do_auto_match()
{
    if (m_painted.cluster_colors.empty()) return;

    // Reset virtual filaments: truncate lists back to existing-only
    m_filament_color_strs.resize(m_existing_filament_count);
    m_filament_names.resize(m_existing_filament_count);
    m_filament_colors_rgba.resize(m_existing_filament_count);
    m_new_filament_colors.clear();

    if (m_auto_merge_cb && m_auto_merge_cb->GetValue()) {
        // Match clusters to closest existing filaments
        std::vector<std::string> names;
        for (size_t i = 0; i < m_existing_filament_count; ++i)
            names.push_back(m_filament_names.size() > i ? m_filament_names[i] : "Filament " + std::to_string(i + 1));

        m_current_matches = Slic3r::match_clusters_to_filaments(
            m_painted.cluster_colors, m_filament_colors_rgba, names);

        // For clusters with poor match (ΔE > 15), create virtual filaments
        constexpr double NEW_FILAMENT_THRESHOLD = 15.0;
        std::map<std::array<std::size_t, 3>, int> virtual_color_index;

        for (auto& m : m_current_matches) {
            if (m.delta_e <= NEW_FILAMENT_THRESHOLD)
                continue;

            auto it = virtual_color_index.find(m.cluster_color);
            if (it != virtual_color_index.end()) {
                m.filament_index = it->second;
            } else {
                int new_idx = (int)m_filament_colors_rgba.size();

                std::array<float, 4> rgba = {
                    m.cluster_color[0] / 255.f,
                    m.cluster_color[1] / 255.f,
                    m.cluster_color[2] / 255.f,
                    1.f
                };
                std::string hex = rgb_to_hex(m.cluster_color).ToStdString();

                new_idx = add_virtual_filament(rgba, hex);

                virtual_color_index[m.cluster_color] = new_idx;
                m.filament_index = new_idx;
            }
            m.filament_color = m_filament_colors_rgba[m.filament_index];
            m.delta_e = 0.0;
        }
    } else {
        // Each cluster gets its own virtual filament with exact cluster color
        m_current_matches.clear();
        std::map<std::array<std::size_t, 3>, int> virtual_map;

        for (size_t i = 0; i < m_painted.cluster_colors.size(); ++i) {
            const auto& cc = m_painted.cluster_colors[i];
            Slic3r::FilamentMatch fm;
            fm.cluster_index = (int)i;
            fm.cluster_color = cc;

            auto it = virtual_map.find(cc);
            if (it != virtual_map.end()) {
                fm.filament_index = it->second;
            } else {
                int idx = (int)m_filament_colors_rgba.size();
                std::array<float, 4> rgba = { cc[0] / 255.f, cc[1] / 255.f, cc[2] / 255.f, 1.f };

                idx = add_virtual_filament(rgba, rgb_to_hex(cc).ToStdString());

                virtual_map[cc] = idx;
                fm.filament_index = idx;
            }
            fm.filament_color = m_filament_colors_rgba[fm.filament_index];
            fm.delta_e = 0.0;
            m_current_matches.push_back(fm);
        }
    }

    update_filament_color_map();
}

void TextureImportDialog::rebuild_mapping_rows()
{
    m_mapping_scroll->Freeze();
    m_mapping_sizer->Clear(true);
    m_mapping_rows.clear();

    if (m_current_matches.empty()) {
        m_mapping_scroll->FitInside();
        m_mapping_scroll->Thaw();
        return;
    }

    auto get_target_wxcolor = [this](int idx) -> wxColour {
        if (idx >= 0 && idx < (int)m_filament_colors_rgba.size()) {
            const auto& c = m_filament_colors_rgba[idx];
            return wxColour((unsigned char)(c[0] * 255.f),
                            (unsigned char)(c[1] * 255.f),
                            (unsigned char)(c[2] * 255.f));
        }
        return wxColour(128, 128, 128);
    };

    auto get_filament_label = [this](int idx) -> wxString {
        if (idx >= 0 && idx < (int)m_filament_names.size())
            return wxString(m_filament_names[idx]);
        return wxString::Format("Filament %d", idx + 1);
    };

    m_mapping_rows.resize(m_current_matches.size());
    for (size_t ci = 0; ci < m_current_matches.size(); ++ci) {
        auto& row = m_mapping_rows[ci];
        row.cluster_id = m_current_matches[ci].cluster_index;
        row.source_color = m_current_matches[ci].cluster_color;
        row.source_hex = rgb_to_hex(row.source_color).ToStdString();
        row.target_filament_idx = m_current_matches[ci].filament_index;

        wxColour src_wx_color(
            (unsigned char)row.source_color[0],
            (unsigned char)row.source_color[1],
            (unsigned char)row.source_color[2]);

        // --- Row container ---
        wxPanel* row_panel = new wxPanel(m_mapping_scroll, wxID_ANY);
        row_panel->SetBackgroundColour(m_mapping_scroll->GetBackgroundColour());
        wxBoxSizer* row_sizer = new wxBoxSizer(wxHORIZONTAL);

        // --- Source card (dashed border, circle + hex) ---
        const int src_w = FromDIP(138);
        const int row_h = FromDIP(44);
        row.source_panel = new wxPanel(row_panel, wxID_ANY, wxDefaultPosition, wxSize(src_w, row_h));
        row.source_panel->SetMinSize(wxSize(src_w, row_h));
        row.source_panel->SetMaxSize(wxSize(src_w, row_h));
        row.source_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);

        row.source_panel->Bind(wxEVT_PAINT, [this, ci, src_wx_color](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            wxAutoBufferedPaintDC dc(p);
            wxSize sz = p->GetClientSize();

            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            // Dashed rounded border — pre-blend rgba(0,0,0,0.3) on white → #B3B3B3
            wxPen dash_pen(wxColour(179, 179, 179), 1, wxPENSTYLE_SHORT_DASH);
            dc.SetPen(dash_pen);
            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            int r = p->FromDIP(8);
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, r);

            // Color circle 24px
            int cd = p->FromDIP(24);
            int cx = p->FromDIP(10);
            int cy = (sz.y - cd) / 2;
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(src_wx_color));
            dc.DrawEllipse(cx, cy, cd, cd);

            // Hex text — pre-blend #262E30 at 60% on white → ~#7D8283
            if (ci < m_mapping_rows.size()) {
                wxFont hex_font = p->GetFont();
                hex_font.SetPointSize(9);
                dc.SetFont(hex_font);
                dc.SetTextForeground(wxColour(125, 130, 131));
                wxString hex_str = wxString::Format("# %s", m_mapping_rows[ci].source_hex.substr(1));
                wxSize tsz = dc.GetTextExtent(hex_str);
                dc.DrawText(hex_str, cx + cd + p->FromDIP(6), (sz.y - tsz.y) / 2);
            }
        });

        row_sizer->Add(row.source_panel, 0, wxALIGN_CENTER_VERTICAL);

        // --- Arrow panel (dashed arrow) ---
        const int arrow_w = FromDIP(24);
        wxPanel* arrow_panel = new wxPanel(row_panel, wxID_ANY, wxDefaultPosition, wxSize(arrow_w, row_h));
        arrow_panel->SetMinSize(wxSize(arrow_w, row_h));
        arrow_panel->SetMaxSize(wxSize(arrow_w, row_h));
        arrow_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
        arrow_panel->Bind(wxEVT_PAINT, [](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            wxAutoBufferedPaintDC dc(p);
            wxSize sz = p->GetClientSize();

            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            int mid_y = sz.y / 2;
            int margin = p->FromDIP(2);
            int arrow_tip = sz.x - margin;
            int arrow_start = margin;

            // Dashed line
            wxPen dash_pen(wxColour(179, 179, 179), p->FromDIP(1), wxPENSTYLE_SHORT_DASH);
            dc.SetPen(dash_pen);
            dc.DrawLine(arrow_start, mid_y, arrow_tip - p->FromDIP(4), mid_y);

            // Arrowhead
            int ah = p->FromDIP(4);
            wxPoint tri[3] = {
                {arrow_tip, mid_y},
                {arrow_tip - ah, mid_y - ah / 2},
                {arrow_tip - ah, mid_y + ah / 2}
            };
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(wxColour(179, 179, 179)));
            dc.DrawPolygon(3, tri);
        });

        row_sizer->Add(arrow_panel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(4));

        // --- Target card (numbered square + material name + chevron) ---
        row.target_panel = new wxPanel(row_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, row_h));
        row.target_panel->SetMinSize(wxSize(FromDIP(239), row_h));
        row.target_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
        row.target_panel->SetCursor(wxCursor(wxCURSOR_HAND));

        row.target_panel->Bind(wxEVT_PAINT, [this, ci, get_target_wxcolor, get_filament_label](wxPaintEvent& e) {
            auto* p = static_cast<wxPanel*>(e.GetEventObject());
            wxAutoBufferedPaintDC dc(p);
            wxSize sz = p->GetClientSize();

            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            if (ci >= m_mapping_rows.size()) return;
            int fil_idx = m_mapping_rows[ci].target_filament_idx;

            // Card background — pre-blend rgba(0,0,0,0.08) on white → #EBEBEB
            // Border — pre-blend rgba(0,0,0,0.12) on white → #E0E0E0
            int r = p->FromDIP(8);
            dc.SetBrush(wxBrush(wxColour(235, 235, 235)));
            dc.SetPen(wxPen(wxColour(224, 224, 224), 1));
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, r);

            // Numbered color square 32x32, rounded 6px
            int sq = p->FromDIP(32);
            int sq_x = p->FromDIP(6);
            int sq_y = (sz.y - sq) / 2;
            int sq_r = p->FromDIP(6);
            wxColour fil_clr = get_target_wxcolor(fil_idx);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(fil_clr));
            dc.DrawRoundedRectangle(sq_x, sq_y, sq, sq, sq_r);

            {
                wxFont num_font = p->GetFont();
                num_font.SetPointSize(10);
                dc.SetFont(num_font);
                dc.SetTextForeground(fil_clr.GetLuminance() < 0.6 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30));
                wxString num_str = wxString::Format("%d", fil_idx + 1);
                wxSize nsz = dc.GetTextExtent(num_str);
                dc.DrawText(num_str, sq_x + (sq - nsz.x) / 2, sq_y + (sq - nsz.y) / 2);
            }

            // Brand icon + material name
            {
                wxFont name_font = p->GetFont();
                name_font.SetPointSize(9);
                dc.SetFont(name_font);
                dc.SetTextForeground(wxColour(38, 46, 48));
                wxString name_str = get_filament_label(fil_idx);
                int text_x = draw_brand_icon_and_strip(dc, p, name_str, sq_x + sq + p->FromDIP(8), sz.y / 2);
                int max_text_w = sz.x - text_x - p->FromDIP(24);
                if (max_text_w > 0) {
                    wxSize tsz = dc.GetTextExtent(name_str);
                    if (tsz.x > max_text_w) {
                        while (name_str.length() > 3 && dc.GetTextExtent(name_str + "...").x > max_text_w)
                            name_str.RemoveLast();
                        name_str += "...";
                        tsz = dc.GetTextExtent(name_str);
                    }
                    dc.DrawText(name_str, text_x, (sz.y - tsz.y) / 2);
                }
            }

            // Dropdown chevron at right edge
            {
                int chev_cx = sz.x - p->FromDIP(14);
                int chev_cy = sz.y / 2;
                int hw = p->FromDIP(3);
                int hh = p->FromDIP(2);
                dc.SetPen(wxPen(wxColour(107, 107, 107), p->FromDIP(1) > 0 ? p->FromDIP(1) : 1));
                dc.DrawLine(chev_cx - hw, chev_cy - hh, chev_cx, chev_cy + hh);
                dc.DrawLine(chev_cx, chev_cy + hh, chev_cx + hw, chev_cy - hh);
            }
        });

        row.target_panel->Bind(wxEVT_LEFT_DOWN, [this, ci](wxMouseEvent&) {
            show_filament_popup(ci);
        });

        row_sizer->Add(row.target_panel, 1, wxALIGN_CENTER_VERTICAL);

        row_panel->SetSizer(row_sizer);
        m_mapping_sizer->Add(row_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
    }

    m_mapping_scroll->FitInside();
    m_mapping_scroll->Layout();
    m_mapping_scroll->Thaw();
}

std::vector<Slic3r::FilamentMatch> TextureImportDialog::build_matches_from_rows() const
{
    std::vector<Slic3r::FilamentMatch> matches(m_mapping_rows.size());
    for (size_t i = 0; i < m_mapping_rows.size(); ++i) {
        auto& m = matches[i];
        m.cluster_index = m_mapping_rows[i].cluster_id;
        m.cluster_color = m_mapping_rows[i].source_color;

        int sel = m_mapping_rows[i].target_filament_idx;
        if (sel >= 0 && sel < (int)m_filament_colors_rgba.size()) {
            m.filament_index = sel;
            m.filament_color = m_filament_colors_rgba[sel];
            m.delta_e = Slic3r::compute_delta_e(m.cluster_color, m.filament_color);
        }
    }
    return matches;
}

// ---- Event handlers ----

void TextureImportDialog::on_color_preset_clicked(wxCommandEvent& evt)
{
    int id = evt.GetId();
    if (id == ID_COLOR_4)  { m_param_color_count = 4; }
    if (id == ID_COLOR_8)  { m_param_color_count = 8; }
    if (id == ID_COLOR_16) { m_param_color_count = 16; }

    if (id == ID_COLOR_AUTO) {
        start_computation(true);
        return;
    }

    m_color_slider->SetValue(m_param_color_count);
    m_color_spin->SetValue(m_param_color_count);
}

void TextureImportDialog::on_color_slider_changed(wxCommandEvent&)
{
    m_param_color_count = m_color_slider->GetValue();
    m_color_spin->SetValue(m_param_color_count);
}

void TextureImportDialog::on_color_spin_changed(wxCommandEvent&)
{
    m_param_color_count = std::clamp(m_color_spin->GetValue(), 1, 32);
    m_color_slider->SetValue(m_param_color_count);
    m_color_spin->SetValue(m_param_color_count);
}

void TextureImportDialog::on_smooth_slider_changed(wxCommandEvent&)
{
    m_param_smooth = m_smooth_slider->GetValue();
    m_smooth_spin->SetValue(m_param_smooth);
}

void TextureImportDialog::on_smooth_spin_changed(wxCommandEvent&)
{
    m_param_smooth = std::clamp(m_smooth_spin->GetValue(), 0, 10);
    m_smooth_slider->SetValue(m_param_smooth);
    m_smooth_spin->SetValue(m_param_smooth);
}

void TextureImportDialog::on_apply_clicked(wxCommandEvent&)
{
    start_computation();
}

void TextureImportDialog::on_auto_merge_toggled(wxCommandEvent&)
{
    if (m_state == TextureImportState::Ready) {
        do_auto_match();
        rebuild_mapping_rows();
    }
}

void TextureImportDialog::on_view_button_clicked(wxCommandEvent& evt)
{
    if (!m_preview_canvas) return;

    int id = evt.GetId();
    if (id == ID_VIEW_ORIGINAL) {
        m_preview_canvas->set_render_mode(TexturePreviewCanvas::RenderMode::Original);
        highlight_view_button(0);
    } else if (id == ID_VIEW_MULTICOLOR) {
        m_preview_canvas->set_render_mode(TexturePreviewCanvas::RenderMode::MultiColor);
        highlight_view_button(1);
    } else if (id == ID_VIEW_FILAMENT) {
        m_preview_canvas->set_render_mode(TexturePreviewCanvas::RenderMode::FilamentMap);
        highlight_view_button(2);
    }
}

void TextureImportDialog::highlight_view_button(int view_index)
{
    Button* btns[] = { m_btn_view_original, m_btn_view_multicolor, m_btn_view_filament };

    StateColor active_bg(
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor active_bd(
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor active_text(
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    StateColor inactive_bg(
        std::pair<wxColour, int>(wxColour(210, 210, 210), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(225, 225, 225), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Normal));
    StateColor inactive_bd(
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Normal));
    StateColor inactive_text(
        std::pair<wxColour, int>(wxColour(160, 160, 160), StateColor::Normal));

    for (int i = 0; i < 3; ++i) {
        if (!btns[i]) continue;
        if (i == view_index) {
            btns[i]->SetBackgroundColor(active_bg);
            btns[i]->SetBorderColor(active_bd);
            btns[i]->SetTextColor(active_text);
        } else {
            btns[i]->SetBackgroundColor(inactive_bg);
            btns[i]->SetBorderColor(inactive_bd);
            btns[i]->SetTextColor(inactive_text);
        }
        btns[i]->Refresh();
    }
}

void TextureImportDialog::on_skip_clicked(wxCommandEvent&)
{
    m_skipped = true;
    m_new_filament_colors.clear();
    m_current_matches.clear();
    cancel_computation();
    EndModal(wxID_CANCEL);
}

void TextureImportDialog::on_ok_clicked(wxCommandEvent&)
{
    m_current_matches = build_matches_from_rows();

    // Rebuild m_new_filament_colors to only contain virtual filaments actually referenced
    std::set<int> used_virtual_indices;
    for (const auto& m : m_current_matches) {
        if (m.filament_index >= (int)m_existing_filament_count)
            used_virtual_indices.insert(m.filament_index);
    }

    // Compact: reassign virtual indices to be contiguous starting from m_existing_filament_count
    std::map<int, int> old_to_new;
    int next_idx = (int)m_existing_filament_count;
    m_new_filament_colors.clear();
    for (int old_idx : used_virtual_indices) {
        if (old_idx >= 0 && old_idx < (int)m_filament_colors_rgba.size()) {
            old_to_new[old_idx] = next_idx;
            m_new_filament_colors.push_back(m_filament_colors_rgba[old_idx]);
            next_idx++;
        }
    }
    for (auto& m : m_current_matches) {
        auto it = old_to_new.find(m.filament_index);
        if (it != old_to_new.end())
            m.filament_index = it->second;
    }

    EndModal(wxID_OK);
}

// ---- Result accessors ----

Slic3r::PaintedMesh TextureImportDialog::get_painted_mesh() const
{
    return m_painted;
}

std::vector<Slic3r::FilamentMatch> TextureImportDialog::get_matches() const
{
    if (!m_current_matches.empty())
        return m_current_matches;
    return build_matches_from_rows();
}

void TextureImportDialog::on_dpi_changed(const wxRect&)
{
    Fit();
    Refresh();
}

}} // namespace Slic3r::GUI
