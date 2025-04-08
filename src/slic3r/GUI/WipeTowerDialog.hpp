#ifndef _WIPE_TOWER_DIALOG_H_
#define _WIPE_TOWER_DIALOG_H_

#include <wx/dialog.h>
#include <wx/webview.h>
#include "libslic3r/PrintConfig.hpp"

class WipingDialog : public wxDialog
{
public:
	using VolumeMatrix = std::vector<std::vector<double>>;

	WipingDialog(wxWindow* parent,const std::vector<std::vector<int>>& extra_flush_volume, const int max_flush_volume = Slic3r::g_max_flush_volume);
	VolumeMatrix CalcFlushingVolumes(int extruder_id);
	std::vector<double> GetFlattenMatrix()const;
	std::vector<double> GetMultipliers()const;
	bool GetSubmitFlag() const { return m_submit_flag; }

private:
	int CalcFlushingVolume(const wxColour& from_, const wxColour& to_, int min_flush_volume, bool is_multi_extruder, Slic3r::NozzleVolumeType volume_type);
	wxString BuildTableObjStr();
	wxString BuildTextObjStr(bool multi_language = true);
	void StoreFlushData(int extruder_num, const std::vector<std::vector<double>>& flush_volume_vecs, const std::vector<double>& flush_multipliers);

	wxWebView* m_webview;
	std::vector<std::vector<int>> m_extra_flush_volume;
	int m_max_flush_volume;

	VolumeMatrix m_raw_matrixs;
	std::vector<double> m_flush_multipliers;
	bool m_submit_flag{ false };
};

#endif  // _WIPE_TOWER_DIALOG_H_