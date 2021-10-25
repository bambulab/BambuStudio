#include "AuxiliaryDataViewModel.hpp"

AuxiliaryModel::AuxiliaryModel()
{
    m_root = new AuxiliaryModelNode();
}

int AuxiliaryModel::Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
    unsigned int column, bool ascending) const
{
    wxASSERT(item1.IsOk() && item2.IsOk());
    // should never happen

    if (IsContainer(item1) && IsContainer(item2))
    {
        wxVariant value1, value2;
        GetValue(value1, item1, 0);
        GetValue(value2, item2, 0);

        wxString str1 = value1.GetString();
        wxString str2 = value2.GetString();
        int res = str1.Cmp(str2);
        if (res) return res;

        // items must be different
        wxUIntPtr litem1 = (wxUIntPtr)item1.GetID();
        wxUIntPtr litem2 = (wxUIntPtr)item2.GetID();

        return litem1 - litem2;
    }

    return wxDataViewModel::Compare(item1, item2, column, ascending);
}

void AuxiliaryModel::GetValue(wxVariant& variant,
    const wxDataViewItem& item, unsigned int col) const
{
    wxASSERT(item.IsOk());

    AuxiliaryModelNode* node = (AuxiliaryModelNode*)item.GetID();
    switch (col)
    {
    case 0:
        variant = node->title;
        break;

    default:
        wxLogError("AuxiliaryModel::GetValue: wrong column %d", col);
    }
}

bool AuxiliaryModel::SetValue(const wxVariant& variant,
    const wxDataViewItem& item, unsigned int col)
{
    wxASSERT(item.IsOk());

    AuxiliaryModelNode* node = (AuxiliaryModelNode*)item.GetID();
    switch (col)
    {
    case 0:
        node->title = variant.GetString();
        return true;

    default:
        wxLogError("AuxiliaryModel::SetValue: wrong column");
    }
    return false;
}

bool AuxiliaryModel::IsEnabled(const wxDataViewItem& item,
    unsigned int col) const
{
    return true;
}

wxDataViewItem AuxiliaryModel::GetParent(const wxDataViewItem& item) const
{
    AuxiliaryModelNode* node = (AuxiliaryModelNode*)item.GetID();

    if (node == m_root || node->GetParent() == m_root)
        return wxDataViewItem(nullptr);

    return wxDataViewItem((void*)node->GetParent());
}

bool AuxiliaryModel::IsContainer(const wxDataViewItem& item) const
{
    // the invisible root node can have children
    // (in our model always "MyMusic")
    if (!item.IsOk())
        return true;

    AuxiliaryModelNode* node = (AuxiliaryModelNode*)item.GetID();
    return node->IsContainer();
}

static unsigned int count = 0;

unsigned int AuxiliaryModel::GetChildren(const wxDataViewItem& parent,
    wxDataViewItemArray& array) const
{
    AuxiliaryModelNode* node = (AuxiliaryModelNode*)parent.GetID();
    if (!node)
    {
        node = m_root;
    }

    count = node->GetChildren().GetCount();
    for (unsigned int pos = 0; pos < count; pos++)
    {
        AuxiliaryModelNode* child = node->GetChildren().Item(pos);
        array.Add(wxDataViewItem((void*)child));
    }

    return count;
}

wxDataViewItem AuxiliaryModel::CreateFolder(wxString name)
{
    wxString folder_name = name;
    if (folder_name == wxEmptyString) {
        folder_name = _L("New Folder");
        for (int i = 1; i <= 1000; i++) {
            bool exist = false;
            for (AuxiliaryModelNode* node : m_root->GetChildren()) {
                if (!node->IsContainer())
                    continue;

                if (node->title == folder_name) {
                    exist = true;
                    break;
                }
            }

            if (!exist)
                break;

            folder_name = _L("New Folder");
            folder_name << "(" << i << ")";
        }
    }
    else {
        for (AuxiliaryModelNode* node : m_root->GetChildren()) {
            if (!node->IsContainer())
                continue;

            if (node->title == folder_name) {
                return wxDataViewItem(nullptr);
            }
        }
    }

    AuxiliaryModelNode* folder = new AuxiliaryModelNode(m_root, folder_name, true);
    m_root->Append(folder);

    wxDataViewItem folder_item(folder);
    ItemAdded(wxDataViewItem(NULL), folder_item);
    return folder_item;
}

wxDataViewItem AuxiliaryModel::ImportFile(AuxiliaryModelNode* sel, wxString& path)
{
    if (sel == nullptr) {
        sel = m_root;
    }

    AuxiliaryModelNode* parent = sel->IsContainer() ? sel : sel->GetParent();
    for (AuxiliaryModelNode* node : parent->GetChildren()) {
        if (node->path == path)
            return wxDataViewItem(nullptr);
    }

    AuxiliaryModelNode* file = new AuxiliaryModelNode(parent, path, false);
    parent->Append(file);

    wxDataViewItem file_item(file);
    if (parent == m_root)
        parent = nullptr;
    wxDataViewItem parent_item(parent);
    ItemAdded(parent_item, file_item);
    return file_item;
}

void AuxiliaryModel::Delete(const wxDataViewItem& item)
{
    AuxiliaryModelNode* node = (AuxiliaryModelNode*)item.GetID();
    if (!node)      // happens if item.IsOk()==false
        return;

    node->GetParent()->GetChildren().Remove(node);

    // notify control
    wxDataViewItem parent_item = GetParent(item);
    ItemDeleted(parent_item, item);

    delete node;
}

void AuxiliaryModel::MoveItem(const wxDataViewItem& dropped_item, const wxDataViewItem& dragged_item)
{
    AuxiliaryModelNode* dropped = (AuxiliaryModelNode*)dropped_item.GetID();
    AuxiliaryModelNode* dragged = (AuxiliaryModelNode*)dragged_item.GetID();

    if (dragged == nullptr || dragged->IsContainer())
        return;

    AuxiliaryModelNode* target_folder = nullptr;
    if (dropped == nullptr) {
        target_folder = m_root;
    }
    else if (dropped->IsContainer()) {
        target_folder = dropped;
    }
    else {
        target_folder = dropped->GetParent();
    }

    if (dragged->GetParent() == target_folder)
        return;

    for (AuxiliaryModelNode* node : target_folder->GetChildren()) {
        if (node->path == dragged->path)
            return;
    }

    wxDataViewItem old_parent_item = this->GetParent(dragged_item);
    dragged->Reparent(target_folder);
    ItemDeleted(old_parent_item, wxDataViewItem(dragged));
    ItemAdded(wxDataViewItem(target_folder == m_root ? nullptr : target_folder), wxDataViewItem(dragged));
}

bool AuxiliaryModel::IsOrphan(const wxDataViewItem& item)
{
    AuxiliaryModelNode* node = (AuxiliaryModelNode*)item.GetID();
    return node->GetParent() != m_root;
}

bool AuxiliaryModel::Rename(const wxDataViewItem& item, const wxString& name)
{
    AuxiliaryModelNode* node = (AuxiliaryModelNode*)item.GetID();
    AuxiliaryModelNode* parent = node->GetParent();

    if (!node->IsContainer())
        return false;

    for (AuxiliaryModelNode* cur_node : parent->GetChildren()) {
        if (cur_node->title == name)
            return false;
    }

    node->title = name;
    return true;
}
