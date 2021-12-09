#include "../libslic3r.h"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"

#include "STEP.hpp"

#include <string>

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

#include "STEPCAFControl_Reader.hxx"
#include "BRepMesh_IncrementalMesh.hxx"
#include "Interface_Static.hxx"
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include "XCAFApp_Application.hxx"
#include "TopoDS_Solid.hxx"
#include "TopoDS_Compound.hxx"
#include "TopoDS_Builder.hxx"
#include "TopoDS.hxx"
#include "TDataStd_Name.hxx"
#include "BRepBuilderAPI_Transform.hxx"
#include "TopExp_Explorer.hxx"
#include "TopExp_Explorer.hxx"
#include "BRep_Tool.hxx"

namespace Slic3r {

struct NamedSolid {
    NamedSolid(const TopoDS_Solid& s,
               const std::string& n) : solid{s}, name{n} {}
    const TopoDS_Solid solid;
    const std::string  name;
};

static void getNamedSolids(const TopLoc_Location& location, const std::string& prefix,
                           unsigned int& id, const Handle(XCAFDoc_ShapeTool) shapeTool,
                           const TDF_Label label, std::vector<NamedSolid>& namedSolids) {
    TDF_Label referredLabel{label};
    if (shapeTool->IsReference(label))
        shapeTool->GetReferredShape(label, referredLabel);

    std::string name;
    Handle(TDataStd_Name) shapeName;
    if (referredLabel.FindAttribute(TDataStd_Name::GetID(), shapeName))
        name = TCollection_AsciiString(shapeName->Get()).ToCString();

    if (name == "")
        name = std::to_string(id++);
    std::string fullName{name};

    TopLoc_Location localLocation = location * shapeTool->GetLocation(label);
    TDF_LabelSequence components;
    if (shapeTool->GetComponents(referredLabel, components)) {
        for (Standard_Integer compIndex = 1; compIndex <= components.Length(); ++compIndex) {
            getNamedSolids(localLocation, fullName, id, shapeTool, components.Value(compIndex), namedSolids);
        }
    } else {
        TopoDS_Shape shape;
        shapeTool->GetShape(referredLabel, shape);
        if (shape.ShapeType() == TopAbs_SOLID) {
            BRepBuilderAPI_Transform transform(shape, localLocation, Standard_True);
            namedSolids.emplace_back(TopoDS::Solid(transform.Shape()), fullName);
        }
    }
}

bool load_step(const char *path, Model *model)
{
    std::vector<NamedSolid> namedSolids;

    Handle(TDocStd_Document) document;
    Handle(XCAFApp_Application) application = XCAFApp_Application::GetApplication();
    application->NewDocument(path, document);
    STEPCAFControl_Reader reader;
    reader.SetNameMode(true);
    IFSelect_ReturnStatus stat = reader.ReadFile(path);
    if (stat != IFSelect_RetDone || !reader.Transfer(document)) {
        application->Close(document);
        throw std::logic_error{ std::string{"Could not read '"} + path + "'" };
        return false;
    }

    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
    TDF_LabelSequence topLevelShapes;
    shapeTool->GetFreeShapes(topLevelShapes);
    unsigned int id{1};
    for (Standard_Integer iLabel = 1; iLabel <= topLevelShapes.Length(); ++iLabel) {
        getNamedSolids(TopLoc_Location{}, "", id, shapeTool, topLevelShapes.Value(iLabel), namedSolids);
    }

    ModelObject* new_object = model->add_object();
    //BBS: todo use assemble name
    new_object->name = "step_file";
    new_object->input_file = path;

    for (int i = 0; i < namedSolids.size(); i++) {
        BRepMesh_IncrementalMesh mesh(namedSolids[i].solid, 1e-3, true, 0.1, true);

        //BBS: calculate total number of the nodes and triangles
        int aNbNodes = 0;
        int aNbTriangles = 0;
        for (TopExp_Explorer anExpSF (namedSolids[i].solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
            TopLoc_Location aLoc;
            Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation (TopoDS::Face (anExpSF.Current()), aLoc);
            if (! aTriangulation.IsNull()) {
                aNbNodes += aTriangulation->NbNodes ();
                aNbTriangles += aTriangulation->NbTriangles ();
            }
        }

        if (aNbTriangles == 0) {
            //BBS: No triangulation on the shape.
            model->delete_object(new_object);
            application->Close(document);
            return false;
        }

        Pointf3s points;
        points.reserve(aNbNodes);
        std::vector<Vec3i> facets;
        facets.reserve(aNbTriangles);
        //BBS: count faces missing triangulation
        Standard_Integer aNbFacesNoTri = 0;
        //BBS: fill temporary triangulation
        Standard_Integer aNodeOffset = 0;
        Standard_Integer aTriangleOffet = 0;
        for (TopExp_Explorer anExpSF (namedSolids[i].solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
            const TopoDS_Shape& aFace = anExpSF.Current();
            TopLoc_Location aLoc;
            Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation (TopoDS::Face (aFace), aLoc);
            if (aTriangulation.IsNull()) {
                ++aNbFacesNoTri;
                continue;
            }
            //BBS: copy nodes
            gp_Trsf aTrsf = aLoc.Transformation();
            for (Standard_Integer aNodeIter = 1; aNodeIter <= aTriangulation->NbNodes(); ++aNodeIter) {
                gp_Pnt aPnt = aTriangulation->Node (aNodeIter);
                aPnt.Transform (aTrsf);
                points.emplace_back(std::move(Vec3d(aPnt.X(), aPnt.Y(), aPnt.Z())));
            }
            //BBS: copy triangles
            const TopAbs_Orientation anOrientation = anExpSF.Current().Orientation();
            for (Standard_Integer aTriIter = 1; aTriIter <= aTriangulation->NbTriangles(); ++aTriIter) {
                Poly_Triangle aTri = aTriangulation->Triangle (aTriIter);

                Standard_Integer anId[3];
                aTri.Get (anId[0], anId[1], anId[2]);
                if (anOrientation == TopAbs_REVERSED) {
                    //BBS: swap 1, 2.
                    Standard_Integer aTmpIdx = anId[1];
                    anId[1] = anId[2];
                    anId[2] = aTmpIdx;
                }
                //BBS: Update nodes according to the offset.
                anId[0] += aNodeOffset;
                anId[1] += aNodeOffset;
                anId[2] += aNodeOffset;
                facets.emplace_back(std::move(Vec3i(anId[0] - 1, anId[1] - 1, anId[2] - 1)));
            }

            aNodeOffset += aTriangulation->NbNodes();
            aTriangleOffet += aTriangulation->NbTriangles();
        }
        TriangleMesh triagle_mesh(points, facets);
        triagle_mesh.repair();
        if (triagle_mesh.facets_count() == 0) {
            // BBS: die "This step file couldn't be read because has invalid solid object.\n"
            model->delete_object(new_object);
            application->Close(document);
            return false;
        }

        ModelVolume *new_volume = new_object->add_volume(std::move(triagle_mesh));
        new_volume->name = namedSolids[i].name;
        new_volume->source.input_file = path;
        new_volume->source.object_idx = (int)model->objects.size() - 1;
        new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
    }
    application->Close(document);

    return true;
}

}; // namespace Slic3r
