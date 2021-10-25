#ifndef slic3r_GUI_AuxiliaryDataViewModel_hpp_
#define slic3r_GUI_AuxiliaryDataViewModel_hpp_

#include "wx/wxprec.h"
#include "wx/dataview.h"
#include "wx/hashmap.h"
#include "wx/vector.h"

#include "I18N.hpp"

#include <filesystem>

class AuxiliaryModelNode;
WX_DEFINE_ARRAY_PTR(AuxiliaryModelNode*, AuxiliaryModelNodePtrArray);

class AuxiliaryModelNode
{
public:
    AuxiliaryModelNode()
    {
        m_parent = NULL;
        title = "";
        m_container = true;
        m_root = true;
    }

    AuxiliaryModelNode(AuxiliaryModelNode* parent, const wxString& txt, bool is_container)
    {
        m_parent = parent;
        m_container = is_container;
        m_root = false;
        if (!m_container) {
            path = txt;
            boost::filesystem::path path_obj(path.ToStdWstring());
            title = path_obj.filename().wstring();
        }
        else {
            title = txt;
        }
    }

    ~AuxiliaryModelNode()
    {
        // free all our children nodes
        size_t count = m_children.GetCount();
        for (size_t i = 0; i < count; i++)
        {
            AuxiliaryModelNode* child = m_children[i];
            delete child;
        }
    }

    bool IsContainer() const
    {
        return m_container;
    }

    AuxiliaryModelNode* GetParent()
    {
        return m_parent;
    }
    void Reparent(AuxiliaryModelNode* parent)
    {
        m_parent->GetChildren().Remove(this);
        this->m_parent = parent;
        parent->Append(this);
    }
    AuxiliaryModelNodePtrArray& GetChildren()
    {
        return m_children;
    }
    AuxiliaryModelNode* GetNthChild(unsigned int n)
    {
        return m_children.Item(n);
    }
    void Insert(AuxiliaryModelNode* child, unsigned int n)
    {
        m_children.Insert(child, n);
    }
    void Append(AuxiliaryModelNode* child)
    {
        m_children.Add(child);
    }
    unsigned int GetChildCount() const
    {
        return m_children.GetCount();
    }

public:
    wxString    title;
    wxString    path;

private:
    AuxiliaryModelNode* m_parent;
    AuxiliaryModelNodePtrArray   m_children;
    bool m_container;
    bool m_root;

};

class AuxiliaryModel : public wxDataViewModel
{
public:
    AuxiliaryModel();
    ~AuxiliaryModel()
    {
        delete m_root;
    }

    // helper methods to change the model
    wxDataViewItem CreateFolder(wxString name = wxEmptyString);
    wxDataViewItem ImportFile(AuxiliaryModelNode* sel, wxString& path);
    void Delete(const wxDataViewItem& item);
    void MoveItem(const wxDataViewItem& dropped_item, const wxDataViewItem& dragged_item);
    bool IsOrphan(const wxDataViewItem& item);
    bool Rename(const wxDataViewItem& item, const wxString& name);

    // override sorting to always sort branches ascendingly

    int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
        unsigned int column, bool ascending) const wxOVERRIDE;

    // implementation of base class virtuals to define model

    virtual unsigned int GetColumnCount() const wxOVERRIDE
    {
        return 6;
    }

    virtual wxString GetColumnType(unsigned int col) const wxOVERRIDE
    {
        if (col == 2)
            return "long";

        return "string";
    }

    virtual void GetValue(wxVariant& variant,
        const wxDataViewItem& item, unsigned int col) const wxOVERRIDE;
    virtual bool SetValue(const wxVariant& variant,
        const wxDataViewItem& item, unsigned int col) wxOVERRIDE;

    virtual bool IsEnabled(const wxDataViewItem& item,
        unsigned int col) const wxOVERRIDE;

    virtual wxDataViewItem GetParent(const wxDataViewItem& item) const wxOVERRIDE;
    virtual bool IsContainer(const wxDataViewItem& item) const wxOVERRIDE;
    virtual unsigned int GetChildren(const wxDataViewItem& parent,
        wxDataViewItemArray& array) const wxOVERRIDE;

private:
    AuxiliaryModelNode* m_root;
};

#endif // slic3r_GUI_AuxiliaryDataViewModel_hpp_

