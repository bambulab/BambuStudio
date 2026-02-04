#ifndef FILAMENT_GROUP_HOVER_HPP
#define FILAMENT_GROUP_HOVER_HPP

#include <wx/bitmap.h>
#include <wx/bmpbuttn.h>
#include <wx/timer.h>
#include "libslic3r/PrintConfig.hpp"
#include "Widgets/PopupWindow.hpp"
#include "Widgets/Label.hpp"

namespace Slic3r { namespace GUI {

class PartPlate;
class Plater;


bool play_dual_extruder_slice_video();
bool play_dual_extruder_print_tpu_video();
bool open_filament_group_wiki();

class FilamentGroupPopup : public PopupWindow
{
public:
    FilamentGroupPopup(wxWindow *parent, const std::vector<FilamentMapMode>& available_modes = {});
    void tryPopup(Plater* plater,PartPlate* plate, bool slice_all);
    void tryClose();

    FilamentMapMode GetSelectedMode() const { return m_mode; }
private:
    void OnPaint(wxPaintEvent&event);
    void StartTimer();
    void ResetTimer();

    void OnRadioBtn(int idx);
    void OnLeaveWindow(wxMouseEvent &);
    void OnEnterWindow(wxMouseEvent &);
    void OnTimer(wxTimerEvent &event);
    void Dismiss();

    void CreateBmps();
    void RecreateUIElements();
    void Init(const std::vector<FilamentMapMode>& available_modes);
    void UpdateButtonStatus(int hover_idx = -1);
    void DrawRoundedCorner(int radius);
private:
    FilamentMapMode GetFilamentMapMode() const;
    void SetFilamentMapMode(const FilamentMapMode mode);

private:
    std::vector<FilamentMapMode> m_all_modes;
    std::vector<FilamentMapMode> m_available_modes;

    bool m_connected{ false };
    bool m_active{ false };
    bool m_support_quality_mode{ false };

    bool m_sync_plate{ false };
    bool m_slice_all{ false };
    bool m_fila_switch_ready{ false };
    FilamentMapMode m_mode;
    wxTimer        *m_timer;

    std::vector<wxBitmapButton*> radio_btns;
    std::vector<Label *>   button_labels;
    std::vector<Label *>   button_desps;
    std::vector<Label *>   detail_infos;
    std::vector<wxSizer *> button_sizers;
    std::vector<wxSizer *> label_sizers;

    wxBitmap checked_bmp;
    wxBitmap unchecked_bmp;
    wxBitmap disabled_bmp;
    wxBitmap checked_hover_bmp;
    wxBitmap unchecked_hover_bmp;
    wxBitmap global_tag_bmp;


    wxStaticText *wiki_link;
    wxStaticText *video_link;

    PartPlate* partplate_ref{ nullptr };
    Plater* plater_ref{ nullptr };
};
}} // namespace Slic3r::GUI
#endif
