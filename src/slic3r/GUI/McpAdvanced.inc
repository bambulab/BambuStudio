struct McpMeshObservation {
    std::string revision, volume_id;
    std::weak_ptr<const TriangleMesh> mesh;
};

std::deque<McpMeshObservation> mcp_mesh_observations;

void advanced_fields(const Json& args, std::initializer_list<const char*> allowed) {
    for (auto it = args.begin(); it != args.end(); ++it) {
        bool found = false;
        for (const auto* key : allowed) if (it.key() == key) found = true;
        require(found, "Unknown parameter for this method");
    }
}

ModelVolume& advanced_volume(ModelObject& object, const Json& args) {
    const auto id = text(args, "volumeId");
    for (auto* volume : object.volumes)
        if (reference_id(volume->id().id) == id) return *volume;
    throw RpcError("STALE_REFERENCE", "Volume no longer exists in this object; list the model again");
}

void advanced_unlocked(Plater& plater, int object_index) {
    const auto& object = *plater.model().objects[object_index];
    auto& plates = plater.get_partplate_list();
    for (size_t i = 0; i < object.instances.size(); ++i) {
        const int plate = plates.find_instance(object_index, int(i));
        if (plate >= 0) require(!plates.get_plate(plate)->is_locked(), "Unlock every affected instance plate before editing this object");
    }
}

void advanced_budget(const ModelObject& object, size_t limit = 100000) {
    size_t faces = 0;
    for (const auto* volume : object.volumes) {
        faces += volume->mesh().its.indices.size();
        require(faces <= limit, "Mesh exceeds the bounded synchronous operation triangle limit");
    }
}

std::string advanced_revision(const ModelVolume& volume) {
    const auto id = reference_id(volume.id().id);
    for (const auto& observation : mcp_mesh_observations)
        if (observation.volume_id == id && observation.mesh.lock() == volume.get_mesh_shared_ptr()) return observation.revision;
    const auto revision = session_id() + ":mesh:" + boost::uuids::to_string(boost::uuids::random_generator()());
    mcp_mesh_observations.push_back({revision, id, volume.get_mesh_shared_ptr()});
    if (mcp_mesh_observations.size() > 64) mcp_mesh_observations.pop_front();
    return revision;
}

void advanced_check_mesh(const ModelVolume& volume, const Json& args) {
    const auto revision = text(args, "meshRevision");
    for (const auto& observation : mcp_mesh_observations)
        if (observation.revision == revision && observation.volume_id == reference_id(volume.id().id) &&
            observation.mesh.lock() == volume.get_mesh_shared_ptr()) return;
    throw RpcError("STALE_REFERENCE", "Mesh changed or its observation expired; call geometry.faces again");
}

Json advanced_stats(const TriangleMesh& mesh) {
    const auto& stats = mesh.stats();
    return {{"triangles", mesh.its.indices.size()}, {"vertices", mesh.its.vertices.size()},
        {"openEdges", stats.open_edges}, {"nonManifoldEdges", stats.non_manifold_edges},
        {"nonManifoldVertices", stats.non_manifold_vertices}, {"reversedFaces", stats.has_reversed_faces}};
}

ModelVolumeType advanced_volume_type(const Json& args) {
    const auto type = text(args, "type");
    if (type == "part") return ModelVolumeType::MODEL_PART;
    if (type == "negative") return ModelVolumeType::NEGATIVE_VOLUME;
    if (type == "modifier") return ModelVolumeType::PARAMETER_MODIFIER;
    if (type == "support_enforcer") return ModelVolumeType::SUPPORT_ENFORCER;
    if (type == "support_blocker") return ModelVolumeType::SUPPORT_BLOCKER;
    throw RpcError("INVALID_PARAMS", "Unknown volume type");
}

void advanced_refresh(Plater& plater, int object_index) {
    auto& object = *plater.model().objects[object_index];
    object.invalidate_bounding_box();
    for (size_t i = 0; i < object.instances.size(); ++i)
        plater.get_partplate_list().notify_instance_update(object_index, int(i));
    refresh(plater);
}

void advanced_preflight_load(Plater& plater, const ModelObjectPtrs& objects) {
    const Vec2d bed = plater.bed().build_volume().bounding_volume2d().size() - 2.0 * Vec2d::Ones();
    require(bed.minCoeff() > 0, "Printer bed dimensions are unavailable");
    for (const auto* object : objects) {
        const auto size = object->bounding_box().size();
        require(std::isfinite(size.x()) && std::isfinite(size.y()) && std::max(size.x() / bed.x(), size.y() / bed.y()) <= 10,
            "Result exceeds native noninteractive import dimensions; original object was not changed");
        for (size_t i = 0; i < object->instances.size(); ++i) {
            const auto instance_size = object->instance_bounding_box(i).size();
            require(std::isfinite(instance_size.x()) && std::isfinite(instance_size.y()) &&
                std::max(instance_size.x() / bed.x(), instance_size.y() / bed.y()) <= 10,
                "Result instance would require a native scaling decision; original object was not changed");
        }
    }
}

void register_advanced_handlers(std::map<std::string, Handler>& methods) {
    methods.emplace("model.import_multipart", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"paths", "name", "plateIndex", "expectedPlateRevision"});
        check_plate_revision(p, a);
        const int pi = index(a, "plateIndex", p.get_partplate_list().get_plate_count());
        auto* plate = p.get_partplate_list().get_plate(pi);
        require(pi == p.get_partplate_list().get_curr_plate_index() && !plate->is_locked(), "Select the intended unlocked plate first");
        require(wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF, "Multipart import requires an FFF project");
        require(a.contains("paths") && a["paths"].is_array() && !a["paths"].empty() && a["paths"].size() <= 100, "Provide 1 to 100 STL/OBJ paths");
        std::vector<std::string> paths;
        std::uintmax_t bytes = 0;
        for (const auto& value : a["paths"]) {
            require(value.is_string(), "Paths must be strings");
            const auto path = input_path(value.get<std::string>());
            auto extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            require(extension == ".stl" || extension == ".obj", "Multipart import supports STL and untextured OBJ geometry");
            bytes += std::filesystem::file_size(path.string());
            require(bytes <= 32 * 1024 * 1024, "Multipart input exceeds the 32 MiB synchronous limit");
            paths.push_back(path.string());
        }
        const std::string name = a.contains("name") ? text(a, "name") : "Multipart model";
        require(!name.empty() && name.size() <= 1024, "Name must contain 1 to 1024 bytes");
        Model prepared;
        auto* target = prepared.add_object();
        target->name = name;
        size_t triangles = 0;
        for (const auto& path : paths) {
            auto source = Model::read_from_file(path);
            require(!source.texture_mesh, "Textured or coloured OBJ needs native material mapping; import it through the application");
            for (const auto* object : source.objects) {
                require(object->instances.size() <= 1, "Multipart mesh files must contain at most one instance per object");
                require(object->layer_height_profile.get().empty() && object->layer_config_ranges.empty(), "Multipart import cannot preserve object-specific layer profiles");
                const size_t count = std::max<size_t>(1, object->instances.size());
                for (size_t ii = 0; ii < count; ++ii) for (const auto* volume : object->volumes) {
                    require(target->volumes.size() < 1000, "Multipart import exceeds 1000 parts");
                    triangles += volume->mesh().its.indices.size();
                    require(triangles <= 200000 && !volume->mesh().empty(), "Multipart import requires nonempty meshes within 200000 triangles");
                    auto* part = target->add_volume(*volume);
                    const Transform3d transform = (object->instances.empty() ? Transform3d::Identity() : object->instances[ii]->get_matrix()) * volume->get_matrix();
                    require(transform.matrix().allFinite(), "Part transform is not finite");
                    part->set_transformation(Geometry::Transformation(transform));
                    auto config = object->config.get();
                    config.apply(volume->config.get(), true);
                    part->config.assign_config(std::move(config));
                    if (part->name.empty()) part->name = std::filesystem::path(path).stem().string();
                    if (const auto* material = volume->material()) {
                        const auto id = "mcp-material-" + boost::uuids::to_string(boost::uuids::random_generator()());
                        part->set_material(id, *material);
                    }
                }
            }
        }
        require(!target->volumes.empty(), "Files contained no importable parts");
        auto* instance = target->add_instance();
        const auto bounds = target->instance_bounding_box(0);
        require(bounds.defined && bounds.min.allFinite() && bounds.max.allFinite(), "Imported bounds are invalid");
        const auto bed_bounds = plate->get_bounding_box();
        instance->set_offset(Vec3d(bed_bounds.center().x() - bounds.center().x(), bed_bounds.center().y() - bounds.center().y(), bed_bounds.min.z() - bounds.min.z()));
        target->invalidate_bounding_box();
        require(plate->contains(target->instance_bounding_box(0)), "Multipart assembly does not fit the selected plate");
        advanced_preflight_load(p, prepared.objects);
        p.take_snapshot("MCP import multipart model");
        const auto before_count = p.model().objects.size();
        std::vector<std::string> materials;
        try {
            std::set<std::string> used_materials;
            for (const auto* part : target->volumes) if (!part->material_id().empty()) used_materials.insert(part->material_id());
            for (const auto& id : used_materials) {
                materials.push_back(id);
                p.model().add_material(id, *prepared.get_material(id));
            }
            ImportScope scope;
            const auto loaded = p.mcp_load_model_objects(prepared.objects);
            require(loaded.size() == 1 && p.model().objects.size() == before_count + 1, "Native multipart import returned an unexpected object count");
            refresh(p);
            return {{"mode", "multipart"}, {"frame", "shared-input-coordinates"}, {"importedIndices", loaded},
                {"objectId", reference_id(p.model().objects[loaded.front()]->id().id)}, {"partCount", target->volumes.size()}, {"triangles", triangles}, {"model", model_state(p)}};
        } catch (...) {
            while (p.model().objects.size() > before_count) p.remove(p.model().objects.size() - 1);
            for (const auto& id : materials) p.model().delete_material(id);
            refresh(p);
            throw;
        }
    });
    methods.emplace("view.capture", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"plateIndex", "expectedPlateRevision", "path", "width", "height", "view", "overwrite"});
        check_plate_revision(p, a);
        const int pi = index(a, "plateIndex", p.get_partplate_list().get_plate_count());
        auto dimension = [&](const char* key) {
            if (!a.contains(key)) return 512;
            require(a[key].is_number_integer(), "Image dimensions must be integers");
            const auto value = a[key].get<long long>();
            require(value >= 64 && value <= 1024, "Image dimensions must be 64 to 1024 pixels");
            return int(value);
        };
        const int width = dimension("width"), height = dimension("height");
        const std::string view = a.contains("view") ? text(a, "view") : "iso";
        const std::map<std::string, Camera::ViewAngleType> views = {{"iso", Camera::ViewAngleType::Iso},
            {"top", Camera::ViewAngleType::Top}, {"front", Camera::ViewAngleType::Front}, {"right", Camera::ViewAngleType::Right}};
        require(views.count(view), "View must be iso, top, front or right");
        const std::filesystem::path destination(text(a, "path"));
        require(destination.is_absolute() && destination.extension() == ".png" && std::filesystem::is_directory(destination.parent_path()),
            "Capture requires an absolute .png path in an existing directory");
        const bool overwrite = flag(a, "overwrite");
        require(overwrite || !std::filesystem::exists(destination), "Destination exists; set overwrite=true explicitly");
        auto* canvas = p.get_view3D_canvas3D();
        require(canvas && canvas->make_current_for_postinit(), "Native rendering context is unavailable");
        ThumbnailData thumbnail;
        const ThumbnailsParams params = {{}, false, true, true, false, pi};
        canvas->render_thumbnail(thumbnail, width, height, params, Camera::EType::Ortho, views.at(view));
        if (!thumbnail.is_valid() || thumbnail.width != unsigned(width) || thumbnail.height != unsigned(height) ||
            thumbnail.pixels.size() != size_t(width) * height * 4)
            throw RpcError("NATIVE_ERROR", "Native renderer did not produce the requested image");
        wxImage image(width, height, false);
        auto* pixels = image.GetData();
        require(pixels != nullptr, "Cannot allocate PNG image");
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto source = (size_t(height - y - 1) * width + x) * 4;
            const auto target = (size_t(y) * width + x) * 3;
            for (size_t channel = 0; channel < 3; ++channel) pixels[target + channel] = thumbnail.pixels[source + channel];
        }
        const auto directory = destination.parent_path() / (".bambu-mcp-image-" + boost::uuids::to_string(boost::uuids::random_generator()()));
        require(std::filesystem::create_directory(directory), "Cannot create owned image directory");
        std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
        struct Cleanup { std::filesystem::path directory; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); } } cleanup{directory};
        const auto source = directory / "view.png";
        if (!image.SaveFile(wxString::FromUTF8(source.string()), wxBITMAP_TYPE_PNG)) throw RpcError("NATIVE_ERROR", "Native PNG encoder failed");
        const auto bytes = mcp_copy_gcode(source.string(), (directory / "publish.tmp").string(), destination.string(), overwrite, [] { return true; });
        return {{"path", destination.string()}, {"mimeType", "image/png"}, {"bytes", bytes}, {"width", width}, {"height", height},
            {"view", view}, {"projection", "orthographic"}, {"content", "native-prepare-plate-render"}, {"plateIndex", pi}};
    });
    methods.emplace("geometry.faces", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "volumeId", "offset", "limit"});
        auto& object = *p.model().objects[object_index(p, a)];
        auto& volume = advanced_volume(object, a);
        const auto& mesh = volume.mesh().its;
        size_t offset = 0, limit = 100;
        if (a.contains("offset")) {
            require(a["offset"].is_number_integer() && a["offset"].get<long long>() >= 0, "offset must be a nonnegative integer");
            offset = a["offset"].get<size_t>();
        }
        if (a.contains("limit")) {
            require(a["limit"].is_number_integer() && a["limit"].get<long long>() > 0 && a["limit"].get<long long>() <= 500, "limit must be 1 to 500");
            limit = a["limit"].get<size_t>();
        }
        require(offset <= mesh.indices.size(), "Face offset is outside the mesh");
        Json faces = Json::array();
        for (size_t i = offset; i < std::min(mesh.indices.size(), offset + limit); ++i) {
            const auto& indices = mesh.indices[i];
            const Vec3d v0 = mesh.vertices[indices[0]].cast<double>();
            const Vec3d v1 = mesh.vertices[indices[1]].cast<double>();
            const Vec3d v2 = mesh.vertices[indices[2]].cast<double>();
            Vec3d normal = (v1 - v0).cross(v2 - v0);
            const double area = normal.norm() / 2;
            if (area > 0) normal.normalize(); else normal.setZero();
            faces.push_back({{"faceIndex", i}, {"vertices", Json::array({vector_json(v0), vector_json(v1), vector_json(v2)})},
                {"normal", vector_json(normal)}, {"area", area}});
        }
        return {{"objectId", text(a, "objectId")}, {"volumeId", text(a, "volumeId")}, {"meshRevision", advanced_revision(volume)},
            {"frame", "volume-local"}, {"total", mesh.indices.size()}, {"offset", offset}, {"faces", faces}};
    });
    methods.emplace("geometry.analyze", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "instanceId", "plateIndex", "expectedPlateRevision", "overhangAngle", "contactTolerance"});
        check_plate_revision(p, a);
        const int oi = object_index(p, a);
        auto& object = *p.model().objects[oi];
        const int ii = instance_index(object, a);
        const int pi = index(a, "plateIndex", p.get_partplate_list().get_plate_count());
        require(p.get_partplate_list().find_instance(oi, ii) == pi, "Instance is not on the specified plate");
        advanced_budget(object, 200000);
        auto number = [&](const char* key, double fallback, double lower, double upper) {
            if (!a.contains(key)) return fallback;
            require(a[key].is_number(), "Analysis parameters must be numbers");
            const double value = a[key].get<double>();
            require(std::isfinite(value) && value >= lower && value <= upper, "Analysis parameter is outside its supported range");
            return value;
        };
        const double angle = number("overhangAngle", 45, 0, 90);
        const double tolerance = number("contactTolerance", 0.05, 1e-9, 1);
        auto* plate = p.get_partplate_list().get_plate(pi);
        const double bed_z = plate->get_bounding_box().min.z();
        const double threshold = std::cos(angle * PI / 180);
        size_t triangles = 0, contact_faces = 0, overhang_faces = 0, degenerate = 0;
        double surface_area = 0, contact_area = 0, overhang_area = 0;
        double minimum_z = std::numeric_limits<double>::max();
        Json samples = Json::array();
        for (const auto* part : object.volumes) if (part->is_model_part()) {
            const Transform3d transform = object.instances[ii]->get_matrix() * part->get_matrix();
            const auto& mesh = part->mesh().its;
            for (size_t face = 0; face < mesh.indices.size(); ++face) {
                const auto& ids = mesh.indices[face];
                const Vec3d p0 = transform * mesh.vertices[ids[0]].cast<double>();
                const Vec3d p1 = transform * mesh.vertices[ids[1]].cast<double>();
                const Vec3d p2 = transform * mesh.vertices[ids[2]].cast<double>();
                minimum_z = std::min({minimum_z, p0.z(), p1.z(), p2.z()});
                Vec3d normal = (p1 - p0).cross(p2 - p0);
                if (transform.linear().determinant() < 0) normal = -normal;
                const double area = normal.norm() / 2;
                ++triangles;
                if (area <= 1e-12) { ++degenerate; continue; }
                surface_area += area;
                normal.normalize();
                const bool contact = std::max({std::abs(p0.z() - bed_z), std::abs(p1.z() - bed_z), std::abs(p2.z() - bed_z)}) <= tolerance;
                const bool overhang = !contact && normal.z() < -1e-9 && -normal.z() + 1e-9 >= threshold;
                if (contact) { ++contact_faces; contact_area += area; }
                if (overhang) {
                    ++overhang_faces; overhang_area += area;
                    if (samples.size() < 100) samples.push_back({{"volumeId", reference_id(part->id().id)},
                        {"meshRevision", advanced_revision(*part)}, {"faceIndex", face}, {"area", area}});
                }
            }
        }
        require(triangles > 0 && std::isfinite(minimum_z), "Object has no printable surface to analyze");
        const double clearance = minimum_z - bed_z;
        return {{"objectId", text(a, "objectId")}, {"instanceId", text(a, "instanceId")}, {"plateIndex", pi}, {"frame", "world"},
            {"analysis", "raw-printable-triangle-surfaces"}, {"triangles", triangles}, {"degenerateTriangles", degenerate}, {"surfaceArea", surface_area},
            {"contactTolerance", tolerance}, {"minimumBedClearance", clearance}, {"touchesBed", std::abs(clearance) <= tolerance},
            {"penetratesBed", clearance < -tolerance}, {"contactFaces", contact_faces}, {"contactArea", contact_area},
            {"fitsPlateBounds", plate->contains(object.instance_bounding_box(ii))}, {"overhangAngleFromDownwardNormal", angle},
            {"overhangFaces", overhang_faces}, {"overhangArea", overhang_area}, {"overhangSamples", samples},
            {"samplesTruncated", overhang_faces > samples.size()}, {"limitations", "Counts raw printable part surfaces, including internal overlaps; does not subtract negative volumes or predict generated supports or adhesion."}};
    });
    methods.emplace("object.lay_on_face", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "instanceId", "volumeId", "meshRevision", "faceIndex"});
        const int oi = object_index(p, a);
        auto& object = *p.model().objects[oi];
        const int ii = instance_index(object, a);
        auto& volume = advanced_volume(object, a);
        advanced_check_mesh(volume, a);
        require(volume.is_model_part(), "Choose a face from a printable part");
        const int face = index(a, "faceIndex", volume.mesh().its.indices.size());
        const int pi = p.get_partplate_list().find_instance(oi, ii);
        require(pi >= 0 && !p.get_partplate_list().get_plate(pi)->is_locked(), "Instance must be on an unlocked plate");
        auto* instance = object.instances[ii];
        const Transform3d world = instance->get_matrix() * volume.get_matrix();
        const auto& indices = volume.mesh().its.indices[face];
        const Vec3d v0 = world * volume.mesh().its.vertices[indices[0]].cast<double>();
        const Vec3d v1 = world * volume.mesh().its.vertices[indices[1]].cast<double>();
        const Vec3d v2 = world * volume.mesh().its.vertices[indices[2]].cast<double>();
        Vec3d normal = (v1 - v0).cross(v2 - v0);
        if (world.linear().determinant() < 0) normal = -normal;
        require(normal.norm() > 1e-12, "A degenerate face cannot define orientation");
        const Matrix3d rotation = Eigen::Quaterniond::FromTwoVectors(normal.normalized(), -Vec3d::UnitZ()).toRotationMatrix();
        Transform3d transform = instance->get_matrix();
        transform.linear() = rotation * transform.linear();
        BoundingBoxf3 bounds;
        for (const auto* part : object.volumes) if (part->is_model_part())
            bounds.merge(part->mesh().transformed_bounding_box(transform * part->get_matrix()));
        require(bounds.defined, "Object has no printable mesh");
        const double face_z = (transform * volume.get_matrix() * volume.mesh().its.vertices[indices[0]].cast<double>()).z();
        const double contact_tolerance = 1e-5 + bounds.size().maxCoeff() * 1e-7;
        require(std::abs(face_z - bounds.min.z()) <= contact_tolerance,
            "Selected face cannot contact the bed without other printable geometry crossing the bed");
        auto* plate = p.get_partplate_list().get_plate(pi);
        transform.translation().z() += plate->get_bounding_box().min.z() - bounds.min.z();
        bounds.translate(Vec3d(0, 0, plate->get_bounding_box().min.z() - bounds.min.z()));
        require(plate->contains(bounds), "Oriented instance does not fit its plate");
        p.take_snapshot("MCP lay instance on face");
        instance->set_transformation(Geometry::Transformation(transform));
        object.invalidate_bounding_box();
        p.get_partplate_list().notify_instance_update(oi, ii);
        refresh(p);
        return model_state(p);
    });
    methods.emplace("volume.add", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "shape", "size", "position", "type", "name"});
        const int oi = object_index(p, a);
        auto& object = *p.model().objects[oi];
        advanced_unlocked(p, oi);
        require(object.volumes.size() < 1000, "Object volume limit reached");
        const auto type = advanced_volume_type(a);
        const auto shape = text(a, "shape");
        require(a.contains("size") && a.contains("position"), "size and position are required");
        const Vec3d size = vector3(a["size"], true), position = vector3(a["position"]);
        require(size.maxCoeff() <= 10000, "Primitive dimensions must be at most 10000 mm");
        const auto name = a.contains("name") ? text(a, "name") : shape;
        TriangleMesh mesh;
        if (shape == "box") mesh = make_cube(size.x(), size.y(), size.z());
        else if (shape == "cylinder") {
            require(std::abs(size.x() - size.y()) < 1e-9, "Cylinder x/y diameters must match");
            mesh = make_cylinder(size.x() / 2, size.z(), PI / 24);
        } else if (shape == "sphere") {
            require((size.array() - size.x()).abs().maxCoeff() < 1e-9, "Sphere dimensions must match");
            mesh = make_sphere(size.x() / 2, PI / 24);
        } else throw RpcError("INVALID_PARAMS", "Unknown primitive shape");
        mesh.translate((-mesh.bounding_box().center()).cast<float>());
        p.take_snapshot("MCP add volume");
        auto* volume = object.add_volume(std::move(mesh), type, false);
        volume->name = name;
        volume->set_offset(position);
        wxGetApp().obj_list()->add_volumes_to_object_in_list(oi);
        advanced_refresh(p, oi);
        return {{"volumeId", reference_id(volume->id().id)}, {"model", model_state(p)}};
    });
    methods.emplace("volume.set_type", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "volumeId", "type"});
        const int oi = object_index(p, a);
        auto& object = *p.model().objects[oi];
        auto& volume = advanced_volume(object, a);
        advanced_unlocked(p, oi);
        const auto type = advanced_volume_type(a);
        if (volume.is_model_part() && type != ModelVolumeType::MODEL_PART) {
            size_t printable = 0;
            for (const auto* part : object.volumes) if (part->is_model_part()) ++printable;
            require(printable > 1, "Cannot remove the object's last printable part");
        }
        p.take_snapshot("MCP change volume type");
        volume.set_type(type);
        wxGetApp().obj_list()->add_volumes_to_object_in_list(oi);
        advanced_refresh(p, oi);
        return model_state(p);
    });
    methods.emplace("painting.set", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "volumeId", "meshRevision", "kind", "faceIndices", "state"});
        const int oi = object_index(p, a);
        auto& object = *p.model().objects[oi];
        auto& volume = advanced_volume(object, a);
        advanced_unlocked(p, oi);
        advanced_check_mesh(volume, a);
        advanced_budget(object, 200000);
        require(volume.is_model_part(), "Painting requires a printable part");
        const auto kind = text(a, "kind");
        require(kind == "support" || kind == "seam" || kind == "filament", "Unknown painting kind");
        require(a.contains("state") && a["state"].is_number_integer(), "state must be an integer");
        const auto requested_state = a["state"].get<long long>();
        const int maximum = kind == "filament" ? int(std::min<size_t>(32, wxGetApp().preset_bundle->filament_presets.size())) : 2;
        require(requested_state >= 0 && requested_state <= maximum, "Painting state is outside the available range");
        const int state = int(requested_state);
        require(a.contains("faceIndices") && a["faceIndices"].is_array() && !a["faceIndices"].empty() && a["faceIndices"].size() <= 10000, "Provide 1 to 10000 face indices");
        std::set<int> faces;
        for (const auto& face : a["faceIndices"]) {
            require(face.is_number_integer() && face.get<long long>() >= 0 && face.get<unsigned long long>() < volume.mesh().its.indices.size(), "Face index is outside the observed mesh");
            faces.insert(face.get<int>());
        }
        auto& annotation = kind == "support" ? volume.supported_facets : kind == "seam" ? volume.seam_facets : volume.mmu_segmentation_facets;
        TriangleSelector selector(volume.mesh());
        selector.deserialize(annotation.get_data());
        for (int face : faces) selector.set_facet(face, static_cast<EnforcerBlockerType>(state));
        p.take_snapshot("MCP paint facets");
        const bool changed = annotation.set(selector);
        advanced_refresh(p, oi);
        return {{"changed", changed}, {"kind", kind}, {"state", state}, {"faceCount", faces.size()}, {"meshRevision", advanced_revision(volume)}};
    });
    methods.emplace("layers.get", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId"});
        const auto& object = *p.model().objects[object_index(p, a)];
        const auto data = object.layer_height_profile.get();
        Json profile = Json::array();
        for (size_t i = 0; i + 1 < data.size(); i += 2) profile.push_back({data[i], data[i + 1]});
        return {{"objectId", text(a, "objectId")}, {"profile", profile}, {"heightFrame", "object-print-height"}};
    });
    methods.emplace("layers.set", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "profile"});
        const int oi = object_index(p, a);
        auto& object = *p.model().objects[oi];
        advanced_unlocked(p, oi);
        require(a.contains("profile") && a["profile"].is_array() && a["profile"].size() <= 1000, "profile must contain at most 1000 height/thickness pairs");
        const auto config = wxGetApp().preset_bundle->full_config();
        const auto* nozzles = config.option<ConfigOptionFloats>("nozzle_diameter");
        require(nozzles && !nozzles->values.empty(), "Printer nozzle information is unavailable");
        double minimum = 0, maximum = std::numeric_limits<double>::max();
        for (size_t i = 0; i < nozzles->values.size(); ++i) {
            minimum = std::max(minimum, double(Slicing::min_layer_height_from_nozzle(config, int(i + 1))));
            maximum = std::min(maximum, double(Slicing::max_layer_height_from_nozzle(config, int(i + 1))));
        }
        require(!object.instances.empty(), "Object has no instance");
        const double height = object.instance_bounding_box(0).size().z();
        for (size_t i = 1; i < object.instances.size(); ++i)
            require(std::abs(object.instance_bounding_box(i).size().z() - height) < 1e-5, "Variable layers require instances of matching print height");
        std::vector<coordf_t> profile;
        double previous = -1;
        for (const auto& pair : a["profile"]) {
            require(pair.is_array() && pair.size() == 2 && pair[0].is_number() && pair[1].is_number(), "Each profile entry must be [height, thickness]");
            const double z = pair[0].get<double>(), thickness = pair[1].get<double>();
            require(std::isfinite(z) && std::isfinite(thickness) && z >= 0 && z > previous && z <= height + 1e-5,
                "Profile heights must strictly increase from zero through object height");
            require(thickness >= minimum && thickness <= maximum, "Layer thickness is outside native nozzle limits");
            profile.push_back(z); profile.push_back(thickness); previous = z;
        }
        require(profile.empty() || (profile.size() >= 4 && profile.front() == 0 && std::abs(profile[profile.size() - 2] - height) < 1e-5),
            "Nonempty profiles must begin at zero and end at the object's print height");
        p.take_snapshot("MCP variable layer profile");
        object.layer_height_profile.set(std::move(profile));
        advanced_refresh(p, oi);
        return {{"updated", true}, {"profile", a["profile"]}, {"minLayerHeight", minimum}, {"maxLayerHeight", maximum}};
    });
    methods.emplace("mesh.repair", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "volumeId", "meshRevision", "discardPaint"});
        const int oi = object_index(p, a);
        auto& object = *p.model().objects[oi];
        auto& volume = advanced_volume(object, a);
        advanced_unlocked(p, oi); advanced_check_mesh(volume, a); advanced_budget(object, 20000);
        const bool painted = !volume.supported_facets.empty() || !volume.seam_facets.empty() || !volume.mmu_segmentation_facets.empty() || !volume.fuzzy_skin_facets.empty();
        require(!painted || flag(a, "discardPaint"), "Native repair changes face identity; explicitly set discardPaint for painted meshes");
        const auto& mesh = volume.mesh().its;
        const auto before = advanced_stats(volume.mesh());
        stl_file source;
        source.stats.type = inmemory;
        source.stats.number_of_facets = uint32_t(mesh.indices.size());
        source.stats.original_num_facets = int(mesh.indices.size());
        stl_allocate(&source);
        bool first = true;
        for (size_t i = 0; i < mesh.indices.size(); ++i) {
            auto& facet = source.facet_start[i];
            for (int j = 0; j < 3; ++j) facet.vertex[j] = mesh.vertices[mesh.indices[i][j]];
            const Vec3f normal = (facet.vertex[1] - facet.vertex[0]).cross(facet.vertex[2] - facet.vertex[0]);
            facet.normal = normal.norm() > 0 ? Vec3f(normal.normalized()) : Vec3f::Zero();
            stl_facet_stats(&source, facet, first);
        }
        source.stats.size = source.stats.max - source.stats.min;
        source.stats.bounding_diameter = source.stats.size.norm();
        TriangleMesh repaired;
        require(repaired.from_stl(source, true) && !repaired.empty(), "Native repair produced no retained geometry");
        const auto after = advanced_stats(repaired);
        p.take_snapshot("MCP repair mesh");
        p.clear_before_change_mesh(oi);
        volume.set_mesh(std::move(repaired));
        volume.reset_extra_facets();
        volume.calculate_convex_hull();
        volume.invalidate_convex_hull_2d();
        volume.set_new_unique_id();
        p.changed_mesh(oi);
        advanced_refresh(p, oi);
        return {{"reprocessed", true}, {"mode", "native-admesh-import-repair"},
            {"before", before}, {"after", after}, {"volumeId", reference_id(volume.id().id)},
            {"watertight", after["openEdges"] == 0 && after["nonManifoldEdges"] == 0 && after["nonManifoldVertices"] == 0},
            {"meshRevision", advanced_revision(volume)}};
    });
    methods.emplace("mesh.simplify", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "volumeId", "meshRevision", "targetTriangles", "discardPaint"});
        const int oi = object_index(p, a);
        auto& object = *p.model().objects[oi];
        auto& volume = advanced_volume(object, a);
        advanced_unlocked(p, oi); advanced_check_mesh(volume, a); advanced_budget(object, 200000);
        require(a.contains("targetTriangles") && a["targetTriangles"].is_number_integer(), "targetTriangles must be an integer");
        const auto target = a["targetTriangles"].get<long long>();
        require(target >= 4 && size_t(target) < volume.mesh().its.indices.size(), "targetTriangles must be at least four and below the current count");
        const bool painted = !volume.supported_facets.empty() || !volume.seam_facets.empty() || !volume.mmu_segmentation_facets.empty() || !volume.fuzzy_skin_facets.empty();
        require(!painted || flag(a, "discardPaint"), "Simplification changes face identity; explicitly set discardPaint for painted meshes");
        auto mesh = volume.mesh().its;
        const auto before = advanced_stats(volume.mesh());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        its_quadric_edge_collapse(mesh, uint32_t(target), nullptr, [deadline] {
            if (std::chrono::steady_clock::now() > deadline) throw RpcError("RESOURCE_LIMIT", "Simplification exceeded eight seconds; model was not changed");
        });
        require(!mesh.indices.empty(), "Simplification produced empty geometry");
        TriangleMesh simplified(std::move(mesh));
        const auto after = advanced_stats(simplified);
        p.take_snapshot("MCP simplify mesh");
        p.clear_before_change_mesh(oi);
        volume.set_mesh(std::move(simplified));
        volume.reset_extra_facets();
        volume.calculate_convex_hull();
        volume.invalidate_convex_hull_2d();
        volume.set_new_unique_id();
        p.changed_mesh(oi);
        advanced_refresh(p, oi);
        return {{"before", before}, {"after", after}, {"targetReached", volume.mesh().its.indices.size() <= size_t(target)},
            {"volumeId", reference_id(volume.id().id)}, {"meshRevision", advanced_revision(volume)}};
    });
    methods.emplace("object.merge", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectIds"});
        require(a.contains("objectIds") && a["objectIds"].is_array() && a["objectIds"].size() >= 2 && a["objectIds"].size() <= 100, "Provide 2 to 100 object IDs");
        std::vector<size_t> indices;
        std::set<int> unique;
        int plate = -1;
        size_t faces = 0;
        for (const auto& id : a["objectIds"]) {
            require(id.is_string(), "Object IDs must be strings");
            const int oi = object_index(p, Json{{"objectId", id}});
            require(unique.insert(oi).second, "Duplicate object ID");
            advanced_unlocked(p, oi);
            const auto& object = *p.model().objects[oi];
            require(object.instances.size() == 1, "Merge requires one instance per source object");
            require(!object.has_custom_layering(), "Clear custom layer profiles/ranges explicitly before merging");
            const int pi = p.get_partplate_list().find_instance(oi, 0);
            require(pi >= 0 && (plate < 0 || plate == pi), "Merged objects must share an unlocked plate");
            plate = pi;
            faces += object.facets_count();
            require(faces <= 200000, "Merge exceeds 200000 triangles");
            indices.push_back(size_t(oi));
        }
        const auto before = p.model().objects.size();
        wxGetApp().obj_list()->merge_objects(indices);
        if (p.model().objects.size() != before - indices.size() + 1)
            throw RpcError("NATIVE_ERROR", "Native multipart merge did not produce the expected object count; inspect current model");
        refresh(p);
        return {{"mode", "multipart"}, {"model", model_state(p)}};
    });
    methods.emplace("object.split", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId"});
        const int oi = object_index(p, a);
        auto& object = *p.model().objects[oi];
        advanced_unlocked(p, oi); advanced_budget(object, 50000);
        require(!object.has_custom_layering(), "Clear custom layer profiles/ranges explicitly before splitting");
        for (const auto* volume : object.volumes)
            require(volume->is_model_part() && volume->mesh().facets_count() >= 3,
                "Split requires nonempty printable parts only; remove or separate modifiers first");
        Model prepared;
        auto* copy = prepared.add_object(object);
        if (copy->volumes.size() == 1)
            copy->volumes[0]->split(unsigned(std::max<size_t>(1, wxGetApp().preset_bundle->filament_presets.size())));
        ModelObjectPtrs results;
        copy->split(&results);
        require(results.size() > 1 && results.size() <= 100, "Split must produce 2 to 100 printable objects");
        require(results.size() == copy->volumes.size(), "Native split did not retain the expected part correspondence");
        for (size_t i = 0; i < results.size(); ++i) {
            require(results[i]->volumes.size() == 1, "Native split produced an unexpected hierarchy");
            auto& target = *results[i]->volumes[0];
            const auto& source = *copy->volumes[i];
            target.supported_facets.reset(); target.supported_facets.assign(source.supported_facets);
            target.seam_facets.reset(); target.seam_facets.assign(source.seam_facets);
            target.fuzzy_skin_facets.reset(); target.fuzzy_skin_facets.assign(source.fuzzy_skin_facets);
        }
        advanced_preflight_load(p, results);
        p.take_snapshot("MCP split object");
        ImportScope scope;
        p.apply_cut_object_to_model(size_t(oi), results);
        refresh(p);
        return {{"createdObjects", results.size()}, {"model", model_state(p)}};
    });
    methods.emplace("object.cut", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "instanceId", "normal", "offset", "keep", "discardPaint"});
        const int oi = object_index(p, a);
        auto& object = *p.model().objects[oi];
        const int ii = instance_index(object, a);
        advanced_unlocked(p, oi); advanced_budget(object, 50000);
        require(object.instances.size() == 1, "Cut requires a single instance; duplicate separate objects first");
        require(!object.has_custom_layering(), "Clear custom layer profiles/ranges explicitly before cutting");
        require(a.contains("normal") && a.contains("offset") && a["offset"].is_number(), "normal and offset are required");
        Vec3d normal = vector3(a["normal"]);
        require(normal.norm() > 1e-12, "Cut normal must be nonzero");
        normal.normalize();
        const double offset = a["offset"].get<double>();
        require(std::isfinite(offset) && std::abs(offset) <= 1000000, "Invalid plane offset");
        const auto keep = text(a, "keep");
        require(keep == "both" || keep == "upper" || keep == "lower", "keep must be both, upper or lower");
        for (const auto* volume : object.volumes) {
            require(volume->is_model_part(), "Cut currently requires printable volumes only");
            require(flag(a, "discardPaint") || (volume->supported_facets.empty() && volume->seam_facets.empty() && volume->mmu_segmentation_facets.empty() && volume->fuzzy_skin_facets.empty()),
                "Native cut resets facet annotations; explicitly set discardPaint");
        }
        double lowest = std::numeric_limits<double>::max(), highest = std::numeric_limits<double>::lowest();
        for (const auto* volume : object.volumes) {
            const Transform3d transform = object.instances[ii]->get_matrix() * volume->get_matrix();
            for (const auto& vertex : volume->mesh().its.vertices) {
                const double projection = normal.dot(transform * vertex.cast<double>());
                lowest = std::min(lowest, projection); highest = std::max(highest, projection);
            }
        }
        require(offset > lowest + 1e-7 && offset < highest - 1e-7, "Cut plane must pass through the object's geometry extent");
        const Vec3d u = normal.unitOrthogonal(), v = normal.cross(u), origin = normal * offset;
        const std::array<Vec3d, 4> plane = {{origin - u - v, origin + u - v, origin + u + v, origin - u + v}};
        ModelObjectCutAttributes attributes;
        if (keep != "lower") attributes = attributes | ModelObjectCutAttribute::KeepUpper;
        if (keep != "upper") attributes = attributes | ModelObjectCutAttribute::KeepLower;
        Model prepared;
        auto* copy = prepared.add_object(object);
        auto results = copy->cut(size_t(ii), plane, attributes);
        prepared.objects.insert(prepared.objects.end(), results.begin(), results.end());
        require(!results.empty() && results.size() <= 100, "Cut produced no retained geometry");
        advanced_preflight_load(p, results);
        p.take_snapshot("MCP cut object");
        ImportScope scope;
        p.apply_cut_object_to_model(size_t(oi), results);
        refresh(p);
        return {{"createdObjects", results.size()}, {"planeFrame", "world"}, {"model", model_state(p)}};
    });
    methods.emplace("export.geometry", [](Plater& p, const Json& a) -> Json {
        advanced_fields(a, {"objectId", "instanceId", "path", "overwrite"});
        auto& object = *p.model().objects[object_index(p, a)];
        const int ii = instance_index(object, a);
        advanced_budget(object, 200000);
        const std::filesystem::path destination(text(a, "path"));
        require(destination.is_absolute() && std::filesystem::is_directory(destination.parent_path()), "Export requires an absolute path in an existing directory");
        require(destination.extension() == ".stl" || destination.extension() == ".obj", "Use a .stl or .obj destination");
        const bool overwrite = flag(a, "overwrite");
        require(overwrite || !std::filesystem::exists(destination), "Destination exists; set overwrite=true explicitly");
        const auto directory = destination.parent_path() / (".bambu-mcp-geometry-" + boost::uuids::to_string(boost::uuids::random_generator()()));
        require(std::filesystem::create_directory(directory), "Cannot create owned export directory");
        std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
        struct Cleanup { std::filesystem::path directory; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); } } cleanup{directory};
        const auto source = directory / ("geometry" + destination.extension().string());
        auto mesh = Plater::combine_mesh_fff(object, ii, [](const std::string& error) { throw RpcError("NATIVE_ERROR", error); });
        require(!mesh.empty(), "Object has no exportable geometry");
        if (destination.extension() == ".stl") {
            if (!mesh.write_binary(source.c_str())) throw RpcError("NATIVE_ERROR", "Native STL writer failed");
        } else mesh.WriteOBJFile(source.c_str());
        const auto bytes = mcp_copy_gcode(source.string(), (directory / "publish.tmp").string(), destination.string(), overwrite, [] { return true; });
        return {{"path", destination.string()}, {"bytes", bytes}, {"frame", "world"}, {"triangles", mesh.its.indices.size()}};
    });
}
