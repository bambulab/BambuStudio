#include <wx/button.h>
#include "GUI_AuxiliaryList.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"

AuxiliaryList::AuxiliaryList(wxWindow* parent)
	: wxDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_NO_HEADER)
{
	wxDataViewTextRenderer* tr = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT);
	wxDataViewColumn* column0 = new wxDataViewColumn("", tr, 0, 200, wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);
	this->AppendColumn(column0);

	m_accessory_model = new AuxiliaryModel();
	this->AssociateModel(m_accessory_model);
	m_sizer = new wxBoxSizer(wxVERTICAL);
	m_sizer->Add(this, 1, wxEXPAND | wxALL, 5);

	wxPanel* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(21)));
	//panel->SetBackgroundColour(*wxLIGHT_GREY);

#if 0
	wxBitmap if_bitmap = create_scaled_bitmap("import_file.png", nullptr, FromDIP(21));
	wxBitmap nf_bitmap = create_scaled_bitmap("new_folder.png", nullptr, FromDIP(21));
	wxBitmap del_bitmap = create_scaled_bitmap("delete.png", nullptr, FromDIP(21));

	wxBitmapButton* if_btn = new wxBitmapButton(panel, wxID_OPEN, if_bitmap);
	wxBitmapButton* nf_btn = new wxBitmapButton(panel, wxID_NEW, nf_bitmap);
	wxBitmapButton* del_btn = new wxBitmapButton(panel, wxID_DELETE, del_bitmap);
#endif

	wxButton* nf_btn = new wxButton(panel, wxID_NEW, _L("New Folder"));
	wxButton* if_btn = new wxButton(panel, wxID_ADD, _L("Import File"));
	wxButton* of_btn = new wxButton(panel, wxID_OPEN, _("Open File"));
	wxButton* del_btn = new wxButton(panel, wxID_DELETE, _L("Delete"));

	wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
	hsizer->Add(nf_btn, 0, wxRIGHT, 5);
	hsizer->Add(if_btn, 0, wxLEFT | wxRIGHT, 5);
	hsizer->Add(of_btn, 0, wxLEFT | wxRIGHT, 5);
	hsizer->Add(del_btn, 0, wxLEFT | wxRIGHT, 5);
	panel->SetSizer(hsizer);

	m_sizer->Add(panel, 0, wxEXPAND | wxALL, 5);

	EnableDragSource(wxDF_UNICODETEXT);
	EnableDropTarget(wxDF_UNICODETEXT);

	nf_btn->Bind(wxEVT_BUTTON, &AuxiliaryList::on_create_folder, this, wxID_NEW);
	if_btn->Bind(wxEVT_BUTTON, &AuxiliaryList::on_import_file, this, wxID_ADD);
	of_btn->Bind(wxEVT_BUTTON, &AuxiliaryList::on_open_file, this, wxID_OPEN);
	del_btn->Bind(wxEVT_BUTTON, &AuxiliaryList::on_delete, this, wxID_DELETE);

	this->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &AuxiliaryList::on_context_menu, this);
	this->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, &AuxiliaryList::on_begin_drag, this);
	this->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, &AuxiliaryList::on_drop_possible, this);
	this->Bind(wxEVT_DATAVIEW_ITEM_DROP, &AuxiliaryList::on_drop, this);
	this->Bind(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, &AuxiliaryList::on_editing_started, this);
	this->Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE, &AuxiliaryList::on_editing_done, this);
}

void AuxiliaryList::create_default_folders()
{
	m_accessory_model->CreateFolder(_L("Model Pictures"));
	m_accessory_model->CreateFolder(_L("Bill of Materials"));
	m_accessory_model->CreateFolder(_L("Assembly Guide"));
	m_accessory_model->CreateFolder(_L("Others"));
}

void AuxiliaryList::do_create_folder()
{
	wxDataViewItem folder_item = m_accessory_model->CreateFolder();
	Select(folder_item);

	wxDataViewColumn* col = GetColumn(0);
	wxDataViewCellMode mode = col->GetRenderer()->GetMode();
	col->GetRenderer()->SetMode(wxDATAVIEW_CELL_EDITABLE);
	EditItem(folder_item, col);
	col->GetRenderer()->SetMode(mode);
}

void AuxiliaryList::do_import_file(AuxiliaryModelNode* folder)
{
	if (folder == nullptr || !folder->IsContainer())
		return;

	wxString path;
	wxFileDialog dialog(this, _L("Choose one file"), wxEmptyString, wxEmptyString,
		wxFileSelectorDefaultWildcardStr, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dialog.ShowModal() == wxID_OK) {
		path = dialog.GetPath();
		wxDataViewItem file_item = m_accessory_model->ImportFile(folder, path);
		AuxiliaryModelNode* file_node = (AuxiliaryModelNode*)file_item.GetID();

		if (file_node != nullptr) {
			if (!m_accessory_model->IsOrphan(file_item)) {
				Expand(wxDataViewItem(file_node->GetParent()));
			}
			Select(file_item);
		}
	}
}

void AuxiliaryList::on_create_folder(wxCommandEvent& evt)
{
	do_create_folder();
}

void AuxiliaryList::on_import_file(wxCommandEvent& evt)
{
	wxDataViewItem sel_item = this->GetSelection();
	AuxiliaryModelNode* sel_node = (AuxiliaryModelNode*)sel_item.GetID();
	if (sel_node == nullptr)
		return;

	AuxiliaryModelNode* folder_node = sel_node;
	if (!folder_node->IsContainer()) {
		wxDataViewItem folder_item = m_accessory_model->GetParent(sel_item);
		folder_node = (AuxiliaryModelNode*)folder_item.GetID();

		if (folder_node == nullptr)
			return;
	}

	do_import_file(folder_node);
}

void AuxiliaryList::on_open_file(wxCommandEvent& evt)
{
	wxDataViewItem sel_item = this->GetSelection();
	AuxiliaryModelNode* sel = (AuxiliaryModelNode*)sel_item.GetID();
	if (sel != nullptr && !sel->IsContainer()) {
		wxLaunchDefaultApplication(sel->path, 0);
	}
}

void AuxiliaryList::on_delete(wxCommandEvent& evt)
{
	m_accessory_model->Delete(this->GetSelection());
}

void AuxiliaryList::on_context_menu(wxDataViewEvent& evt)
{
	wxMenu* menu = new wxMenu();
	wxDataViewItem item = evt.GetItem();
	AuxiliaryModelNode* node = (AuxiliaryModelNode*)item.GetID();
	if (node == nullptr) {
		append_menu_item(menu, wxID_ANY, _L("New Folder"), wxEmptyString,
			[this](wxCommandEvent&)
			{
				do_create_folder();
			});
	}
	else if (node->IsContainer()) {
		append_menu_item(menu, wxID_ANY, _L("Rename"), wxEmptyString,
			[this, item](wxCommandEvent&)
			{
				wxDataViewColumn* col = this->GetColumn(0);
				wxDataViewCellMode mode = col->GetRenderer()->GetMode();
				col->GetRenderer()->SetMode(wxDATAVIEW_CELL_EDITABLE);
				this->EditItem(item, col);
				col->GetRenderer()->SetMode(mode);
			});
		append_menu_item(menu, wxID_ANY, _L("Import File"), wxEmptyString,
			[this, node](wxCommandEvent&)
			{
				do_import_file(node);
			});
		append_menu_item(menu, wxID_ANY, _L("Delete"), wxEmptyString,
			[this, item](wxCommandEvent&)
			{
				m_accessory_model->Delete(item);
			});
	}
	else {
		append_menu_item(menu, wxID_ANY, _L("Open"), wxEmptyString,
			[this, node](wxCommandEvent&)
			{
				wxLaunchDefaultApplication(node->path, 0);
			});
		append_menu_item(menu, wxID_ANY, _L("Delete"), wxEmptyString,
			[this, item](wxCommandEvent&)
			{
				m_accessory_model->Delete(item);
			});
	}

	PopupMenu(menu);
}

void AuxiliaryList::on_begin_drag(wxDataViewEvent& evt)
{
	wxDataViewItem sel_item = evt.GetItem();
	AuxiliaryModelNode* sel = (AuxiliaryModelNode*)sel_item.GetID();
	if (sel == nullptr || sel->IsContainer())
		return;

	m_dragged_item = sel_item;

	wxTextDataObject* obj = new wxTextDataObject;
	obj->SetText("Some text");
	evt.SetDataObject(obj);
	evt.SetDragFlags(wxDrag_DefaultMove);
}

void AuxiliaryList::on_drop_possible(wxDataViewEvent& evt)
{
	evt.Allow();
}

void AuxiliaryList::on_drop(wxDataViewEvent& evt)
{
	m_accessory_model->MoveItem(evt.GetItem(), m_dragged_item);

	Expand(evt.GetItem());
	Select(m_dragged_item);
	m_dragged_item = wxDataViewItem(nullptr);
}

void AuxiliaryList::on_editing_started(wxDataViewEvent& evt)
{
}

void AuxiliaryList::on_editing_done(wxDataViewEvent& evt)
{
	wxVariant value = evt.GetValue();
	wxString name = value.GetString();

	bool is_done = m_accessory_model->Rename(evt.GetItem(), name);
	if (!is_done)
		evt.Veto();
}
