const GCodeProcessorResult& mcp_preview_result(Plater& plater, const Json& params) {
    check_plate_revision(plater, params);
    const int plate_index = index(params, "plateIndex", plater.get_partplate_list().get_plate_count());
    require(plate_index == plater.get_partplate_list().get_curr_plate_index(), "Select the requested plate first");
    const auto id = text(params, "sliceResultId");
    if (!result_current(plater) || id != slice_result.id)
        throw RpcError("STALE_REFERENCE", "Completed slice result changed; start a fresh tracked slice");
    auto* result = plater.get_partplate_list().get_curr_plate()->get_slice_result();
    if (!result || result->moves.empty()) throw RpcError("STALE_REFERENCE", "Native preview is unavailable");
    return *result;
}

std::size_t mcp_preview_number(const Json& params, const char* key, std::size_t fallback, std::size_t maximum) {
    if (!params.contains(key)) return fallback;
    require(params.at(key).is_number_unsigned() || params.at(key).is_number_integer(), "Preview pagination must use integers");
    if (params.at(key).is_number_unsigned()) {
        const auto value = params.at(key).get<unsigned long long>();
        require(value <= maximum, "Preview pagination is outside the supported range");
        return static_cast<std::size_t>(value);
    }
    const auto value = params.at(key).get<long long>();
    require(value >= 0 && static_cast<unsigned long long>(value) <= maximum, "Preview pagination is outside the supported range");
    return static_cast<std::size_t>(value);
}

bool mcp_support_move(const GCodeProcessorResult::MoveVertex& move) {
    return move.type == EMoveType::Extrude &&
        (move.extrusion_role == erSupportMaterial || move.extrusion_role == erSupportMaterialInterface);
}

Json mcp_preview_summary(Plater& plater, const Json& params) {
    const auto& result = mcp_preview_result(plater, params);
    std::size_t extrusions = 0, supports = 0, travels = 0;
    for (const auto& move : result.moves) {
        if (move.type == EMoveType::Extrude) ++extrusions;
        if (move.type == EMoveType::Travel) ++travels;
        if (mcp_support_move(move)) ++supports;
    }
    Json warnings = Json::array();
    for (const auto& warning : result.warnings) {
        if (warnings.size() >= 100) break;
        warnings.push_back({{"level", warning.level}, {"code", warning.error_code}, {"message", warning.msg}});
    }
    return {{"sliceResultId", slice_result.id}, {"plateIndex", slice_result.plate_index},
        {"moveCount", result.moves.size()}, {"extrusionMoveCount", extrusions},
        {"travelMoveCount", travels}, {"supportMoveCount", supports},
        {"filamentCount", result.filaments_count}, {"toolpathOutside", result.toolpath_outside},
        {"warnings", warnings}};
}

Json mcp_preview_layers(Plater& plater, const Json& params) {
    const auto& result = mcp_preview_result(plater, params);
    const auto offset = mcp_preview_number(params, "offset", 0, 1000000);
    const auto limit = mcp_preview_number(params, "limit", 50, 100);
    require(limit > 0, "Preview page limit must be positive");
    struct Layer { std::size_t extrusions = 0, supports = 0; };
    std::map<float, Layer> layers;
    for (const auto& move : result.moves) {
        if (move.type != EMoveType::Extrude || !std::isfinite(move.print_z)) continue;
        auto& layer = layers[move.print_z];
        ++layer.extrusions;
        if (mcp_support_move(move)) ++layer.supports;
    }
    Json page = Json::array();
    std::size_t i = 0;
    for (const auto& [z, layer] : layers) {
        if (i >= offset && i < offset + limit)
            page.push_back({{"index", i}, {"z", z}, {"extrusionMoveCount", layer.extrusions},
                {"supportMoveCount", layer.supports}});
        ++i;
    }
    return {{"sliceResultId", slice_result.id}, {"plateIndex", slice_result.plate_index},
        {"offset", offset}, {"limit", limit}, {"total", layers.size()}, {"layers", page}};
}

Json mcp_preview_toolpaths(Plater& plater, const Json& params) {
    const auto& result = mcp_preview_result(plater, params);
    const auto offset = mcp_preview_number(params, "offset", 0, 100000000);
    const auto limit = mcp_preview_number(params, "limit", 200, 500);
    require(limit > 0, "Preview page limit must be positive");
    Json moves = Json::array();
    for (std::size_t i = offset; i < result.moves.size() && i - offset < limit; ++i) {
        const auto& move = result.moves[i];
        Json item = {{"index", i}, {"type", static_cast<int>(move.type)},
            {"extrusionRole", static_cast<int>(move.extrusion_role)}, {"extruderId", move.extruder_id},
            {"support", mcp_support_move(move)}};
        if (move.position.allFinite()) item["position"] = {move.position.x(), move.position.y(), move.position.z()};
        if (std::isfinite(move.print_z)) item["printZ"] = move.print_z;
        if (std::isfinite(move.width)) item["width"] = move.width;
        if (std::isfinite(move.height)) item["height"] = move.height;
        if (std::isfinite(move.feedrate)) item["feedrate"] = move.feedrate;
        moves.push_back(std::move(item));
    }
    return {{"sliceResultId", slice_result.id}, {"plateIndex", slice_result.plate_index},
        {"offset", offset}, {"limit", limit}, {"total", result.moves.size()}, {"moves", moves}};
}

void register_preview_handlers(std::map<std::string, Handler>& handlers) {
    handlers["preview.get_summary"] = [](Plater& plater, const Json& params) { return mcp_preview_summary(plater, params); };
    handlers["preview.get_layers"] = [](Plater& plater, const Json& params) { return mcp_preview_layers(plater, params); };
    handlers["preview.get_toolpaths"] = [](Plater& plater, const Json& params) { return mcp_preview_toolpaths(plater, params); };
}
