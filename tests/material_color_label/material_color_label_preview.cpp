#include "slic3r/GUI/MaterialColorLabel.hpp"
#include <wx/app.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/bitmap.h>
#include <iostream>

class PreviewApp : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(PreviewApp);

int main(int argc, char** argv)
{
    if (argc != 2) { std::cerr << "Usage: material_color_label_preview output.png\n"; return 1; }
    const wxString output = wxString::FromUTF8(argv[1]);
    if (!wxEntryStart(argc, argv)) return 1;
    wxTheApp->CallOnInit();
    wxInitAllImageHandlers();
    bool saved = false;
    {
        wxBitmap bitmap(840, 570);
        wxMemoryDC dc(bitmap);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        dc.SetTextForeground(wxColour(30, 30, 30));
        dc.SetFont(wxFont(15, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.DrawText("Material color labels: synthetic test data", 24, 18);
        dc.SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.DrawText("Shared production formatter. No printer or AMS connection.", 24, 50);
        const wxColour blue(0x1D,0x5F,0xBF), green(0x0A,0xA6,0x6A), red(0xD7,0x27,0x2F);
        struct Sample { wxString name; int type; std::vector<wxColour> colors; const char* kind; };
        const std::vector<Sample> samples = {
            {"Blue", 2, {blue}, "Named single color"},
            {"", 2, {blue}, "Unnamed single color"},
            {"Blue-green", 1, {blue,green}, "Named multicolor"},
            {"", 1, {blue,green}, "Unnamed multicolor"},
            {"Red-blue", 0, {red,blue}, "Named gradient"},
            {"", 0, {red,blue}, "Unnamed gradient"}
        };
        int y = 92;
        for (const auto& sample : samples) {
            Slic3r::FilamentColor color;
            for (const auto& value : sample.colors) color.AddColor(value);
            color.EndSet(sample.type);
            dc.SetTextForeground(wxColour(100,100,100));
            dc.DrawText(sample.kind, 24, y);
            wxRect swatch(24, y+23, 56, 30);
            dc.SetPen(*wxTRANSPARENT_PEN);
            if (sample.type == 0) dc.GradientFillLinear(swatch, sample.colors.front(), sample.colors.back());
            else {
                int width = swatch.width / sample.colors.size();
                for (size_t i=0; i<sample.colors.size(); ++i) {
                    dc.SetBrush(wxBrush(sample.colors[i]));
                    dc.DrawRectangle(swatch.x + i*width, swatch.y, width, swatch.height);
                }
            }
            dc.SetTextForeground(wxColour(30,30,30));
            dc.DrawText(Slic3r::GUI::format_material_color_label(sample.name, color, "Unknown"), 96, y+28);
            y += 76;
        }
        dc.SelectObject(wxNullBitmap);
        saved = bitmap.SaveFile(output, wxBITMAP_TYPE_PNG);
    }
    wxTheApp->OnExit();
    wxEntryCleanup();
    return saved ? 0 : 1;
}
