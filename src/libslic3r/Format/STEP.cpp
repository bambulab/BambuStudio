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

const double STEP_TRANS_CHORD_ERROR = 0.0012;
const double STEP_TRANS_ANGLE_RES = 0.12;

const int LOAD_STEP_STAGE_READ_FILE          = 0;
const int LOAD_STEP_STAGE_GET_SOLID          = 1;
const int LOAD_STEP_STAGE_GET_MESH           = 2;

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

bool load_step(const char *path, Model *model, ImportStepProgressFn proFn)
{
    bool cb_cancel = false;
    if (proFn) {
        proFn(LOAD_STEP_STAGE_READ_FILE, 0, 1, cb_cancel);
        if (cb_cancel)
            return false;
    }

    std::vector<NamedSolid> namedSolids;
    Handle(TDocStd_Document) document;
    Handle(XCAFApp_Application) application = XCAFApp_Application::GetApplication();
    application->NewDocument(path, document);
    STEPCAFControl_Reader reader;
    reader.SetNameMode(true);
    //BBS: Todo, read file is slow which cause the progress_bar no update and gui no response
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
    Standard_Integer topShapeLength = topLevelShapes.Length() + 1;
    for (Standard_Integer iLabel = 1; iLabel < topShapeLength; ++iLabel) {
        if (proFn) {
            proFn(LOAD_STEP_STAGE_GET_SOLID, iLabel, topShapeLength, cb_cancel);
            if (cb_cancel) {
                shapeTool.reset(nullptr);
                application->Close(document);
                return false;
            }
        }
        getNamedSolids(TopLoc_Location{}, "", id, shapeTool, topLevelShapes.Value(iLabel), namedSolids);
    }

    ModelObject* new_object = model->add_object();
    const char *last_slash = strrchr(path, DIR_SEPARATOR);
    new_object->name.assign((last_slash == nullptr) ? path : last_slash + 1);
    new_object->input_file = path;

    for (size_t i = 0; i < namedSolids.size(); ++i) {
        if (proFn) {
            proFn(LOAD_STEP_STAGE_GET_MESH, i, namedSolids.size(), cb_cancel);
            if (cb_cancel) {
                model->delete_object(new_object);
                shapeTool.reset(nullptr);
                application->Close(document);
                return false;
            }
        }

        BRepMesh_IncrementalMesh mesh(namedSolids[i].solid, STEP_TRANS_CHORD_ERROR, true, STEP_TRANS_ANGLE_RES, true);
        //BBS: calculate total number of the nodes and triangles
        int aNbNodes = 0;
        int aNbTriangles = 0;
        for (TopExp_Explorer anExpSF(namedSolids[i].solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
            TopLoc_Location aLoc;
            Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(anExpSF.Current()), aLoc);
            if (!aTriangulation.IsNull()) {
                aNbNodes += aTriangulation->NbNodes();
                aNbTriangles += aTriangulation->NbTriangles();
            }
        }

        if (aNbTriangles == 0) {
            //BBS: No triangulation on the shape.
            continue;
        }

        std::vector<Vec3f> points;
        points.reserve(aNbNodes);
        std::vector<Vec3i> facets;
        facets.reserve(aNbTriangles);
        //BBS: count faces missing triangulation
        Standard_Integer aNbFacesNoTri = 0;
        //BBS: fill temporary triangulation
        Standard_Integer aNodeOffset = 0;
        Standard_Integer aTriangleOffet = 0;
        for (TopExp_Explorer anExpSF(namedSolids[i].solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
            const TopoDS_Shape& aFace = anExpSF.Current();
            TopLoc_Location aLoc;
            Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(aFace), aLoc);
            if (aTriangulation.IsNull()) {
                ++aNbFacesNoTri;
                continue;
            }
            //BBS: copy nodes
            gp_Trsf aTrsf = aLoc.Transformation();
            for (Standard_Integer aNodeIter = 1; aNodeIter <= aTriangulation->NbNodes(); ++aNodeIter) {
                gp_Pnt aPnt = aTriangulation->Node(aNodeIter);
                aPnt.Transform(aTrsf);
                points.emplace_back(std::move(Vec3d(aPnt.X(), aPnt.Y(), aPnt.Z())));
            }
            //BBS: copy triangles
            const TopAbs_Orientation anOrientation = anExpSF.Current().Orientation();
            for (Standard_Integer aTriIter = 1; aTriIter <= aTriangulation->NbTriangles(); ++aTriIter) {
                Poly_Triangle aTri = aTriangulation->Triangle(aTriIter);

                Standard_Integer anId[3];
                aTri.Get(anId[0], anId[1], anId[2]);
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

        TriangleMesh triangle_mesh(points, facets);
        // BBS: FIXME, comment repair to avoid build error
        //triangle_mesh.repair();
        if (triangle_mesh.facets_count() == 0) {
            continue;
        }
        ModelVolume* new_volume = new_object->add_volume(std::move(triangle_mesh));
        new_volume->name = namedSolids[i].name;
        new_volume->source.input_file = path;
        new_volume->source.object_idx = (int)model->objects.size() - 1;
        new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
    }

    shapeTool.reset(nullptr);
    application->Close(document);

    //BBS: no valid shape from the step, delete the new object as well
    if (new_object->volumes.size() == 0) {
        model->delete_object(new_object);
        return false;
    }

    return true;
}

}; // namespace Slic3r
