#include "ParamsDialog.hpp"
#include "I18N.hpp"
#include "ParamsPanel.hpp"

#include "libslic3r/Utils.hpp"

namespace pt = boost::property_tree;
typedef pt::ptree JSON;

namespace Slic3r { 
namespace GUI {


ParamsDialog::ParamsDialog(wxWindow * parent)
	: DPIDialog(parent, wxID_ANY,  _L(""), wxDefaultPosition,
		wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	m_panel = new ParamsPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);

	//m_btn_submit = new wxButton(this, wxID_ANY, _L("Submit"), wxDefaultPosition, wxDefaultSize);
	//m_btn_submit->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
	//		this->submit();
	//	});

	auto* topsizer = new wxBoxSizer(wxVERTICAL);
	topsizer->Add(m_panel, 1, wxALL | wxEXPAND, 5, NULL);
	//topsizer->Add(m_btn_submit, 0, wxRIGHT, 20);
	topsizer->Add(-1, 5);

	SetSizerAndFit(topsizer);
	SetSize({80 * em_unit(), 50 * em_unit()});

	Layout();
	Center();

	Bind(wxEVT_SHOW, [this](auto & event) {
		if (IsShown()) {
			m_winDisabler = new wxWindowDisabler(this);
			m_panel->OnActivate();
		}
		else {
			delete m_winDisabler;
			m_winDisabler = nullptr;
		}
	});
}

void ParamsDialog::submit()
{
	this->Hide();
}

void ParamsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
	Fit();
	SetSize({80 * em_unit(), 50 * em_unit()});
	m_panel->msw_rescale();
	Refresh();
}

} // namespace GUI
} // namespace Slic3r