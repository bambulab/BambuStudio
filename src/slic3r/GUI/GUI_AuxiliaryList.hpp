#ifndef slic3r_GUI_AuxiliaryList_hpp_
#define slic3r_GUI_AuxiliaryList_hpp_

#include <map>
#include <vector>
#include <set>

#include <wx/bitmap.h>
#include <wx/dataview.h>
#include <wx/menu.h>

#include "AuxiliaryDataViewModel.hpp"

class AuxiliaryList : public wxDataViewCtrl
{
public:
	AuxiliaryList(wxWindow* parent);
	wxSizer* get_top_sizer() { return m_sizer; }
	void create_default_folders();

private:
	void do_create_folder();
	void do_import_file(AuxiliaryModelNode* folder);
	void on_create_folder(wxCommandEvent& evt);
	void on_import_file(wxCommandEvent& evt);
	void on_open_file(wxCommandEvent& evt);
	void on_delete(wxCommandEvent& evt);
	void on_context_menu(wxDataViewEvent& evt);
	void on_begin_drag(wxDataViewEvent& evt);
	void on_drop_possible(wxDataViewEvent& evt);
	void on_drop(wxDataViewEvent& evt);
	void on_editing_started(wxDataViewEvent& evt);
	void on_editing_done(wxDataViewEvent& evt);

	wxDataViewItem m_dragged_item;

	AuxiliaryModel* m_accessory_model;
	wxSizer* m_sizer;
};

#endif //slic3r_GUI_AuxiliaryList_hpp_

