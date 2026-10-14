Json mcp_device_summary(MachineObject& machine, bool selected) {
    const bool connected = machine.is_connected();
    return {{"deviceId", machine.get_dev_id()}, {"name", machine.get_dev_name()},
        {"model", machine.printer_type}, {"connectionType", machine.connection_type()},
        {"selected", selected}, {"online", machine.m_is_online}, {"connected", connected},
        {"printStatus", machine.print_status}, {"progressPercent", machine.mc_print_percent},
        {"activeTaskId", !machine.subtask_id_.empty() ? machine.subtask_id_ :
            (!machine.task_id_.empty() ? machine.task_id_ : machine.job_id_)},
        {"remainingSeconds", machine.mc_left_time}, {"currentLayer", machine.curr_layer},
        {"totalLayers", machine.total_layers},
        {"canPause", connected && machine.can_pause()},
        {"canResume", connected && machine.can_resume() && machine.jobState_ <= 1},
        {"canStop", connected && machine.can_abort()},
        {"calibrationSupported", machine.is_support_command_calibration()}};
}

void mcp_device_fields(const Json& params, std::initializer_list<const char*> allowed) {
    require(params.is_object(), "Device parameters must be an object");
    std::set<std::string> names;
    for (const auto* name : allowed) names.insert(name);
    for (auto it = params.begin(); it != params.end(); ++it)
        require(names.count(it.key()) != 0, "Unknown parameter for this device method");
}

int mcp_device_bounded_int(const Json& value, int minimum, int maximum) {
    require(value.is_number_integer(), "Device numeric value must be an integer");
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        require(number >= static_cast<std::uint64_t>(minimum) && number <= static_cast<std::uint64_t>(maximum),
            "Device numeric value is outside supported bounds");
        return static_cast<int>(number);
    }
    const auto number = value.get<std::int64_t>();
    require(number >= minimum && number <= maximum, "Device numeric value is outside supported bounds");
    return static_cast<int>(number);
}

DeviceManager& mcp_device_manager() {
    auto* manager = wxGetApp().getDeviceManager();
    if (!manager) throw RpcError("UNAVAILABLE", "Native device manager is unavailable");
    return *manager;
}

MachineObject& mcp_device(const Json& params, bool require_selected = false) {
    auto& manager = mcp_device_manager();
    const auto device_id = text(params, "deviceId");
    require(device_id.size() <= 128, "Device ID is too long");
    auto* machine = manager.get_my_machine(device_id);
    if (!machine) throw RpcError("STALE_REFERENCE", "Device is not in the current authorized native device list; list devices again");
    if (require_selected) {
        auto* selected = manager.get_selected_machine();
        if (selected != machine) throw RpcError("STALE_REFERENCE", "Selected device changed; select and inspect the intended device again");
    }
    return *machine;
}

void mcp_device_operation_guard(MachineObject& machine, const Json& params) {
    auto* agent = wxGetApp().getAgent();
    if (!agent || !agent->get_network_agent()) throw RpcError("UNAVAILABLE", "Native networking plugin is unavailable");
    const char* send_symbol = machine.is_lan_mode_printer() ? "bambu_network_send_message_to_printer" : "bambu_network_send_message";
    if (!NetworkAgent::get_network_function(send_symbol)) throw RpcError("UNAVAILABLE", "Native device command transport is unavailable");
    if (!machine.is_connected()) throw RpcError("DEVICE_OFFLINE", "Device has no current native connection");
    if (machine.print_status != text(params, "expectedPrintStatus"))
        throw RpcError("STALE_REFERENCE", "Device print status changed; inspect it again before issuing a command");
}

Json mcp_device_command(const Json& params, const std::string& operation) {
    auto& machine = mcp_device(params, true);
    mcp_device_operation_guard(machine, params);
    const auto active_task_id = !machine.subtask_id_.empty() ? machine.subtask_id_ :
        (!machine.task_id_.empty() ? machine.task_id_ : machine.job_id_);
    if (active_task_id.empty() || active_task_id != text(params, "expectedTaskId"))
        throw RpcError("STALE_REFERENCE", "Active print task changed or has no native identity; inspect the device again");
    int result = -1;
    if (operation == "pause") {
        if (!machine.can_pause()) throw RpcError("INVALID_STATE", "Native device cannot pause in its current state");
        result = machine.command_task_pause();
    } else if (operation == "resume") {
        if (!machine.can_resume()) throw RpcError("INVALID_STATE", "Native device cannot resume in its current state");
        if (machine.jobState_ > 1) throw RpcError("INTERACTION_REQUIRED", "This device operation can only resume at the printer");
        result = machine.command_task_resume();
    } else {
        if (text(params, "confirm") != "STOP_PRINT") throw RpcError("INVALID_PARAMS", "Confirm STOP_PRINT to stop the current print");
        if (!machine.can_abort()) throw RpcError("INVALID_STATE", "Native device has no stoppable print");
        result = machine.command_task_abort();
    }
    if (result != 0) throw RpcError("NATIVE_ERROR", "Native device command was not accepted");
    return {{"deviceId", machine.get_dev_id()}, {"operation", operation}, {"accepted", true},
        {"completionObserved", false}, {"statusMethod", "device.get_state"}};
}

Json mcp_calibration_state(MachineObject& machine) {
    return {{"deviceId", machine.get_dev_id()}, {"supported", machine.is_support_command_calibration()},
        {"running", machine.is_calibration_running()}, {"inCalibration", machine.is_in_calibration()},
        {"doneReported", machine.is_calibration_done()}, {"printStatus", machine.print_status},
        {"progressPercent", machine.mc_print_percent}};
}

NetworkAgent& mcp_cloud_agent(const char* symbol) {
    auto* agent = wxGetApp().getAgent();
    if (!agent || !agent->get_network_agent() || !NetworkAgent::get_network_function(symbol))
        throw RpcError("UNAVAILABLE", "Native cloud provider is unavailable");
    if (!agent->is_user_login()) throw RpcError("ACCOUNT_REQUIRED", "Sign in through the application first");
    return *agent;
}

Json mcp_task_summary(const Json& hit) {
    Json result = Json::object();
    if (!hit.is_object()) return result;
    for (const auto* key : {"id", "status"}) {
        auto it = hit.find(key);
        if (it != hit.end() && it->is_number_integer()) result[key] = *it;
    }
    for (const auto* key : {"title", "designTitle", "deviceName", "startTime", "endTime"}) {
        auto it = hit.find(key);
        if (it != hit.end() && it->is_string()) {
            const auto value = it->get<std::string>();
            if (value.size() <= 512) result[key] = value;
        }
    }
    return result;
}

struct McpUploadPlan {
    BBL::PrintParams native;
    std::string source, source_identity, slice_result_id, remote_name;
    std::uintmax_t bytes = 0;
    int plate_index = -1;
};

McpUploadPlan mcp_upload_plan(Plater& plater, const Json& params) {
    auto& machine = mcp_device(params, true);
    auto* agent = wxGetApp().getAgent();
    if (!agent || !agent->get_network_agent() ||
        !NetworkAgent::get_network_function("bambu_network_start_send_gcode_to_sdcard"))
        throw RpcError("UNAVAILABLE", "Native SD-card upload provider is unavailable");
    if (!machine.is_connected() || !machine.is_lan_mode_printer() || !machine.is_support_send_to_sdcard)
        throw RpcError("UNSUPPORTED", "Selected native device has no connected LAN SD-card upload path");
    if (machine.print_status != text(params, "expectedPrintStatus"))
        throw RpcError("STALE_REFERENCE", "Device state changed; inspect it again before upload");
    current_slice_plate(plater, params);
    reconcile_slices(plater);
    if (!result_current(plater) || text(params, "sliceResultId") != slice_result.id)
        throw RpcError("STALE_REFERENCE", "Slice result is no longer current");
    const auto remote_name = text(params, "remoteName");
    require(remote_name.size() >= 7 && remote_name.size() <= 128 &&
        remote_name.substr(remote_name.size() - 6) == ".gcode" &&
        remote_name.front() != '.' &&
        std::all_of(remote_name.begin(), remote_name.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        }), "Remote name must be a plain .gcode basename");
    const std::string source = slice_result.source;
    const auto identity = mcp_gcode_identity(source);
    require(!identity.empty() && identity == slice_result.file_identity, "Native G-code changed after slicing");
    std::error_code error;
    const auto bytes = std::filesystem::file_size(source, error);
    require(!error && bytes > 0 && bytes <= 1024ULL * 1024 * 1024, "G-code file size is invalid");
    BBL::PrintParams native{};
    native.dev_id = machine.get_dev_id();
    native.dev_name = machine.get_dev_name();
    native.dev_ip = machine.get_dev_ip();
    native.password = machine.get_access_code();
    native.username = "bblp";
    native.connection_type = "lan";
    native.use_ssl_for_ftp = machine.local_use_ssl_for_ftp;
    native.use_ssl_for_mqtt = machine.local_use_ssl_for_mqtt;
    native.ftp_folder = machine.get_ftp_folder();
    native.project_name = remote_name;
    native.plate_index = plater.get_partplate_list().get_curr_plate_index() + 1;
    require(!native.dev_ip.empty() && !native.password.empty(), "Selected LAN device has no native connection credentials");
    return {std::move(native), source, identity, slice_result.id, remote_name, bytes, slice_result.plate_index};
}

struct McpPrintPlan {
    BBL::PrintParams native;
    std::string route, remote_name;
    int plate_index = -1;
};

Json mcp_ams_inventory(MachineObject& machine) {
    Json units = Json::array();
    auto system = machine.GetFilaSystem();
    if (!system) return Json{{"deviceId", machine.get_dev_id()}, {"units", units}};
    for (const auto& [id, ams] : system->GetAmsList()) {
        if (!ams || !ams->IsExist()) continue;
        const auto type = ams->GetAmsType();
        const bool known_type = type == DevAmsType::AMS || type == DevAmsType::AMS_LITE ||
            type == DevAmsType::N3F || type == DevAmsType::N3S || type == DevAmsType::AMS_LITE_MIXED;
        Json slots = Json::array();
        for (const auto& [slot_id, tray] : ams->GetTrays()) {
            if (!tray || slots.size() >= 32) continue;
            int slot_number = -1, global_id = -1;
            try { slot_number = std::stoi(slot_id); if (known_type && slot_number >= 0 && slot_number <= 31) global_id = ams->GetTrayId(slot_number); }
            catch (...) {}
            slots.push_back({{"slotId", slot_id}, {"globalTrayId", global_id},
                {"exists", tray->is_exists}, {"filamentType", tray->get_filament_type()},
                {"color", tray->color}, {"remainingPercent", tray->remain}});
        }
        units.push_back({{"amsId", id}, {"slots", slots}});
        if (units.size() >= 16) break;
    }
    Json filaments = Json::array();
    auto* bundle = wxGetApp().preset_bundle;
    if (bundle) for (std::size_t i = 0; i < bundle->filament_presets.size() && i < 32; ++i) {
        auto* preset = bundle->filaments.find_preset(bundle->filament_presets[i]);
        if (!preset) continue;
        std::string display;
        filaments.push_back({{"filamentIndex", i}, {"name", preset->name},
            {"type", preset->get_filament_type(display)}});
    }
    return Json{{"deviceId", machine.get_dev_id()}, {"units", units}, {"slicedFilaments", filaments}};
}

Json mcp_configure_ams(MachineObject& machine, const GCodeProcessorResult& sliced,
    const Json& params, BBL::PrintParams& native) {
    std::set<int> used;
    for (const auto& filament : sliced.used_filaments) used.insert(filament.filament_id);
    if (used.empty()) for (const auto& move : sliced.moves)
        if (move.type == EMoveType::Extrude) used.insert(move.extruder_id);
    require(!used.empty() && used.size() <= 32, "Slice has no bounded filament set");
    const auto mapping = params.value("amsMapping", Json::array());
    require(mapping.is_array() && mapping.size() <= 32, "AMS mapping must be a bounded array");
    if (mapping.empty()) {
        if (used.size() != 1) throw RpcError("INTERACTION_REQUIRED", "Multi-filament print requires explicit AMS mapping");
        native.task_use_ams = false;
        return Json{{"useAms", false}, {"usedFilamentIndices", used}};
    }
    if (mapping.size() != used.size()) throw RpcError("INVALID_PARAMS", "Map every used sliced filament exactly once");
    auto system = machine.GetFilaSystem();
    if (!system || !system->HasAms()) throw RpcError("INVALID_STATE", "Selected device has no current AMS inventory");
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle || bundle->filament_presets.empty() || bundle->filament_presets.size() > 32)
        throw RpcError("INVALID_STATE", "Current filament preset inventory is unavailable");
    Json legacy = Json::array(), pairs = Json::array(), info = Json::array();
    for (std::size_t i = 0; i < bundle->filament_presets.size(); ++i) {
        legacy.push_back(-1);
        pairs.push_back({{"ams_id", 255}, {"slot_id", 255}});
        info.push_back({{"ams", -1}, {"targetColor", ""}, {"filamentId", ""},
            {"filamentType", ""}, {"sourceColor", ""}});
    }
    std::set<int> mapped, trays;
    std::vector<FilamentInfo> native_mapping;
    Json chosen = Json::array();
    for (const auto& item : mapping) {
        mcp_device_fields(item, {"filamentIndex", "amsId", "slotId"});
        require(item.is_object() && item.contains("filamentIndex") && item.contains("amsId") && item.contains("slotId") &&
            item["filamentIndex"].is_number_integer() && item["amsId"].is_number_integer() && item["slotId"].is_number_integer(),
            "AMS mapping entries require integer filament, AMS and slot IDs");
        const int filament = mcp_device_bounded_int(item["filamentIndex"], 0, 31),
            ams_id = mcp_device_bounded_int(item["amsId"], 0, 255),
            slot_id = mcp_device_bounded_int(item["slotId"], 0, 31);
        require(used.count(filament) && mapped.insert(filament).second && filament >= 0 &&
            static_cast<std::size_t>(filament) < bundle->filament_presets.size(), "AMS mapping filament is unused or duplicated");
        require(ams_id >= 0 && ams_id <= 255 && slot_id >= 0 && slot_id <= 31, "AMS or slot ID is outside native bounds");
        auto* ams = system->GetAmsById(std::to_string(ams_id));
        if (!ams || !ams->IsExist()) throw RpcError("STALE_REFERENCE", "Selected AMS unit is no longer present");
        const auto type = ams->GetAmsType();
        if (type != DevAmsType::AMS && type != DevAmsType::AMS_LITE && type != DevAmsType::N3F &&
            type != DevAmsType::N3S && type != DevAmsType::AMS_LITE_MIXED)
            throw RpcError("UNSUPPORTED", "Selected AMS unit type has no safe native mapping");
        auto* tray = ams->GetTray(std::to_string(slot_id));
        if (!tray || !tray->is_exists || tray->remain == 0)
            throw RpcError("STALE_REFERENCE", "Selected AMS slot is absent or reported empty");
        const int global_id = ams->GetTrayId(slot_id);
        require(global_id >= 0 && trays.insert(global_id).second, "Selected AMS slot is invalid or reused");
        auto* preset = bundle->filaments.find_preset(bundle->filament_presets[filament]);
        if (!preset) throw RpcError("STALE_REFERENCE", "Sliced filament preset is no longer available");
        std::string displayed;
        const auto filament_type = preset->get_filament_type(displayed);
        if (filament_type.empty() || filament_type != tray->get_filament_type())
            throw RpcError("INVALID_STATE", "AMS slot filament type does not match the sliced filament");
        FilamentInfo match;
        match.id = filament;
        match.type = filament_type;
        match.tray_id = global_id;
        match.ams_id = std::to_string(ams_id);
        match.slot_id = std::to_string(slot_id);
        native_mapping.push_back(match);
        legacy[filament] = global_id;
        pairs[filament] = {{"ams_id", ams_id}, {"slot_id", slot_id}};
        info[filament] = {{"ams", global_id}, {"targetColor", tray->color},
            {"filamentId", preset->filament_id}, {"filamentType", filament_type},
            {"sourceColor", ""}};
        chosen.push_back({{"filamentIndex", filament}, {"amsId", ams_id}, {"slotId", slot_id},
            {"globalTrayId", global_id}, {"filamentType", filament_type}});
    }
    if (!DevMappingUtil::is_valid_mapping_result(&machine, native_mapping, true))
        throw RpcError("STALE_REFERENCE", "Native AMS mapping validation failed; inspect slots again");
    native.task_use_ams = true;
    native.ams_mapping = legacy.dump();
    native.ams_mapping2 = pairs.dump();
    native.ams_mapping_info = info.dump();
    return Json{{"useAms", true}, {"assignments", chosen}, {"usedFilamentIndices", used}};
}

McpPrintPlan mcp_print_plan(Plater& plater, const Json& params) {
    auto& machine = mcp_device(params, true);
    auto* agent = wxGetApp().getAgent();
    if (!agent || !agent->get_network_agent()) throw RpcError("UNAVAILABLE", "Native print provider is unavailable");
    const auto route = text(params, "route");
    require(route == "lan" || route == "cloud", "Print route must be lan or cloud");
    const auto* symbol = route == "lan" ? "bambu_network_start_local_print" : "bambu_network_start_print";
    if (!NetworkAgent::get_network_function(symbol)) throw RpcError("UNAVAILABLE", "Selected native print route is unavailable");
    if (!machine.is_connected() || (route == "lan" ? !machine.is_lan_mode_printer() : !machine.is_cloud_mode_printer()))
        throw RpcError("UNSUPPORTED", "Selected printer is not connected through the requested native route");
    if (route == "cloud" && !agent->is_user_login()) throw RpcError("ACCOUNT_REQUIRED", "Sign in through the application first");
    if (machine.print_status != text(params, "expectedPrintStatus"))
        throw RpcError("STALE_REFERENCE", "Device print state changed; inspect it again");
    if (machine.print_status != "IDLE" && machine.print_status != "FINISH")
        throw RpcError("INVALID_STATE", "Selected printer is not idle or finished");
    if (machine.is_enable_np) throw RpcError("UNSUPPORTED", "Multi-nozzle print mapping requires the native UI workflow");
    if (!plater.is_same_printer_for_connected_and_selected(false))
        throw RpcError("INVALID_STATE", "Selected printer is incompatible with the sliced printer preset");
    auto& plate = current_slice_plate(plater, params);
    reconcile_slices(plater);
    if (!result_current(plater) || text(params, "sliceResultId") != slice_result.id)
        throw RpcError("STALE_REFERENCE", "Slice result is no longer current");
    const auto* sliced = plate.get_slice_result();
    if (!sliced || sliced->toolpath_outside)
        throw RpcError("UNSUPPORTED", "Printing requires toolpaths inside the selected plate");
    const auto remote_name = text(params, "remoteName");
    require(remote_name.size() >= 11 && remote_name.size() <= 128 &&
        remote_name.substr(remote_name.size() - 10) == ".gcode.3mf" && remote_name.front() != '.' &&
        std::all_of(remote_name.begin(), remote_name.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        }), "Remote name must be a plain .gcode.3mf basename");
    const bool bed_level = flag(params, "bedLeveling"), flow = flag(params, "flowCalibration");
    require(params.contains("bedLeveling") && params.contains("flowCalibration"),
        "Print bed-leveling and flow-calibration choices are required");
    if (bed_level && machine.is_support_bed_leveling == 0)
        throw RpcError("UNSUPPORTED", "Selected printer does not support the requested bed-leveling option");
    if (flow && !machine.is_support_flow_calibration)
        throw RpcError("UNSUPPORTED", "Selected printer does not support the requested flow calibration option");
    BBL::PrintParams native{};
    native.dev_id = machine.get_dev_id();
    native.dev_name = machine.get_dev_name();
    native.dev_ip = machine.get_dev_ip();
    native.password = machine.get_access_code();
    native.username = "bblp";
    native.connection_type = route;
    native.use_ssl_for_ftp = machine.local_use_ssl_for_ftp;
    native.use_ssl_for_mqtt = machine.local_use_ssl_for_mqtt;
    native.ftp_folder = machine.get_ftp_folder();
    native.project_name = remote_name;
    native.task_name = remote_name;
    native.preset_name = wxGetApp().preset_bundle->prints.get_selected_preset_name();
    native.plate_index = plater.get_partplate_list().get_curr_plate_index() + 1;
    native.task_bed_type = bed_type_to_gcode_string(plate.get_bed_type(true));
    native.task_bed_leveling = bed_level;
    native.task_flow_cali = flow;
    native.task_vibration_cali = false;
    native.task_layer_inspect = false;
    native.task_record_timelapse = false;
    native.task_timelapse_use_internal = false;
    native.print_type = "from_normal";
    mcp_configure_ams(machine, *sliced, params, native);
    if (route == "lan" && (native.dev_ip.empty() || native.password.empty()))
        throw RpcError("UNAVAILABLE", "Selected LAN printer has no native connection credentials");
    return {std::move(native), route, remote_name, slice_result.plate_index};
}

std::map<std::string, Handler>& register_devices_handlers(std::map<std::string, Handler>& handlers) {
    handlers["device.list"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {});
        auto& manager = mcp_device_manager();
        auto* agent = wxGetApp().getAgent();
        auto* selected = manager.get_selected_machine();
        Json devices = Json::array();
        for (const auto& entry : manager.get_my_machine_list())
            if (entry.second) devices.push_back(mcp_device_summary(*entry.second, entry.second == selected));
        return Json{{"pluginAvailable", agent && agent->get_network_agent()}, {"devices", devices}};
    };
    handlers["device.get_state"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {"deviceId"});
        auto& machine = mcp_device(params);
        auto* selected = mcp_device_manager().get_selected_machine();
        auto result = mcp_device_summary(machine, &machine == selected);
        auto* storage = machine.GetStorage();
        if (storage) result["storage"] = {{"sdCardState", static_cast<int>(storage->get_sdcard_state())}};
        return result;
    };
    handlers["device.select"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {"deviceId"});
        auto& machine = mcp_device(params);
        auto& manager = mcp_device_manager();
        if (!manager.set_selected_machine(machine.get_dev_id())) throw RpcError("NATIVE_ERROR", "Native device selection failed");
        auto* selected = manager.get_selected_machine();
        if (selected != &machine) throw RpcError("NATIVE_ERROR", "Native device selection did not persist");
        return mcp_device_summary(machine, true);
    };
    handlers["device.pause"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {"deviceId", "expectedPrintStatus", "expectedTaskId"}); return mcp_device_command(params, "pause"); };
    handlers["device.resume"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {"deviceId", "expectedPrintStatus", "expectedTaskId"}); return mcp_device_command(params, "resume"); };
    handlers["device.stop"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {"deviceId", "expectedPrintStatus", "expectedTaskId", "confirm"}); return mcp_device_command(params, "stop"); };
    handlers["calibration.get_state"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {"deviceId"});
        return mcp_calibration_state(mcp_device(params));
    };
    handlers["calibration.start"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {"deviceId", "expectedPrintStatus", "confirm", "vibration", "bedLeveling",
            "camera", "motorNoise", "nozzle", "bedCalibration", "clumpPosition"});
        auto& machine = mcp_device(params, true);
        mcp_device_operation_guard(machine, params);
        if (text(params, "confirm") != "START_CALIBRATION") throw RpcError("INVALID_PARAMS", "Confirm START_CALIBRATION for the intended device");
        if (machine.can_abort() || machine.is_in_calibration()) throw RpcError("BUSY", "Device is printing or already calibrating");
        if (!machine.is_support_command_calibration()) throw RpcError("UNSUPPORTED", "This firmware uses a script fallback; semantic calibration is unavailable");
        const bool vibration = flag(params, "vibration"), bed = flag(params, "bedLeveling"), camera = flag(params, "camera"),
            motor = flag(params, "motorNoise"), nozzle = flag(params, "nozzle"), bed_cali = flag(params, "bedCalibration"),
            clump = flag(params, "clumpPosition");
        require(vibration || bed || camera || motor || nozzle || bed_cali || clump, "Select at least one calibration routine");
        if (machine.command_start_calibration(vibration, bed, camera, motor, nozzle, bed_cali, clump) != 0)
            throw RpcError("NATIVE_ERROR", "Native calibration command was not accepted");
        return Json{{"deviceId", machine.get_dev_id()}, {"accepted", true}, {"completionObserved", false},
            {"statusMethod", "calibration.get_state"}};
    };
    handlers["account.get_state"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {});
        auto* agent = wxGetApp().getAgent();
        const bool plugin = agent && agent->get_network_agent();
        return Json{{"pluginAvailable", plugin}, {"loggedIn", plugin && agent->is_user_login()},
            {"serverConnected", plugin && agent->is_server_connected()}};
    };
    handlers["account.tasks.list"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {"offset", "limit"});
        auto& agent = mcp_cloud_agent("bambu_network_get_user_tasks");
        BBL::TaskQueryParams query;
        query.offset = params.contains("offset") ? mcp_device_bounded_int(params["offset"], 0, 10000) : 0;
        query.limit = params.contains("limit") ? mcp_device_bounded_int(params["limit"], 1, 50) : 20;
        auto job = start_external_job("account_tasks", -1, [&agent, query](const std::shared_ptr<ExternalJob>& job) {
            try {
            std::string body;
            const int code = agent.get_user_tasks(query, &body);
            if (code != 0) { external_finish(job, "failed", "Native cloud task query failed"); return; }
            if (body.size() > 2 * 1024 * 1024) { external_finish(job, "failed", "Cloud task response exceeded the bounded size"); return; }
            const auto parsed = Json::parse(body, nullptr, false);
            if (!parsed.is_object() || !parsed.contains("hits") || !parsed["hits"].is_array()) {
                external_finish(job, "failed", "Native cloud task response was malformed"); return;
            }
            Json tasks = Json::array();
            for (const auto& hit : parsed["hits"]) {
                if (tasks.size() >= static_cast<std::size_t>(query.limit)) break;
                auto summary = mcp_task_summary(hit);
                if (!summary.empty()) tasks.push_back(std::move(summary));
            }
            Json result = {{"offset", query.offset}, {"limit", query.limit}, {"tasks", tasks}};
            auto total = parsed.find("total");
            if (total != parsed.end() && total->is_number_integer()) result["total"] = *total;
            external_finish(job, "succeeded", {}, std::move(result));
            } catch (...) { external_finish(job, "failed", "Native cloud task query failed"); }
        });
        return external_job_state(job);
    };
    handlers["account.presets.list"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {});
        auto& agent = mcp_cloud_agent("bambu_network_get_user_presets");
        auto job = start_external_job("account_presets", -1, [&agent](const std::shared_ptr<ExternalJob>& job) {
            try {
            std::map<std::string, std::map<std::string, std::string>> presets;
            const int code = agent.get_user_presets(&presets);
            if (code != 0) { external_finish(job, "failed", "Native cloud preset query failed"); return; }
            if (presets.size() > 1000) { external_finish(job, "failed", "Cloud preset response exceeded the bounded count"); return; }
            Json names = Json::array();
            for (const auto& entry : presets) if (entry.first.size() <= 512) names.push_back(entry.first);
            external_finish(job, "succeeded", {}, Json{{"names", names}});
            } catch (...) { external_finish(job, "failed", "Native cloud preset query failed"); }
        });
        return external_job_state(job);
    };
    handlers["device.upload.plan"] = [](Plater& plater, const Json& params) {
        mcp_device_fields(params, {"deviceId", "expectedPrintStatus", "plateIndex", "expectedPlateRevision", "sliceResultId", "remoteName"});
        const auto plan = mcp_upload_plan(plater, params);
        return Json{{"deviceId", plan.native.dev_id}, {"plateIndex", plan.plate_index},
            {"providerPlateNumber", plan.native.plate_index},
            {"remoteName", plan.remote_name}, {"bytes", plan.bytes}, {"ready", true},
            {"effect", "Upload selected plate G-code to SD card; does not start printing"}};
    };
    handlers["device.upload.start"] = [](Plater& plater, const Json& params) {
        mcp_device_fields(params, {"deviceId", "expectedPrintStatus", "plateIndex", "expectedPlateRevision", "sliceResultId", "remoteName", "confirm"});
        if (text(params, "confirm") != "UPLOAD_TO_SDCARD")
            throw RpcError("INVALID_PARAMS", "Confirm UPLOAD_TO_SDCARD for the selected device");
        auto plan = mcp_upload_plan(plater, params);
        const auto directory = std::filesystem::temp_directory_path() /
            ("bambu-mcp-upload-" + boost::uuids::to_string(boost::uuids::random_generator()()));
        std::error_code error;
        if (!std::filesystem::create_directory(directory, error) || error)
            throw RpcError("NATIVE_ERROR", "Cannot create private upload staging directory");
        std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace, error);
        if (error) { std::filesystem::remove(directory); throw RpcError("NATIVE_ERROR", "Cannot protect upload staging directory"); }
        const auto staged = directory / plan.remote_name;
        const auto temporary = directory / "copy.tmp";
        try {
            mcp_copy_gcode(plan.source, temporary.string(), staged.string(), false,
                [&plater, &plan] { return result_current(plater) &&
                    slice_result.id == plan.slice_result_id &&
                    mcp_gcode_identity(plan.source) == plan.source_identity; });
            plan = mcp_upload_plan(plater, params);
            plan.native.filename = staged.string();
            auto* agent = wxGetApp().getAgent();
            auto job = start_external_job("device_upload", plan.plate_index,
                [agent, native = std::move(plan.native), directory, remote_name = plan.remote_name]
                (const std::shared_ptr<ExternalJob>& job) mutable {
                    const auto cleanup = [&] { std::error_code ignored; std::filesystem::remove_all(directory, ignored); };
                    try {
                        auto progress = [job](int, int, std::string) { external_progress(job, 0, "Native upload in progress"); };
                        auto cancelled = [job] { return job->cancel_requested.load(); };
                        const int code = agent->start_send_gcode_to_sdcard(native, progress, cancelled, nullptr);
                        cleanup();
                        if (code == 0) external_finish(job, "succeeded", {},
                            Json{{"deviceId", native.dev_id}, {"remoteName", remote_name},
                                {"acceptedByProvider", true}, {"printingStarted", false}});
                        else if (code == BAMBU_NETWORK_ERR_CANCELED) external_finish(job, "cancelled");
                        else external_finish(job, "failed", "Native SD-card upload failed (code " + std::to_string(code) + ")");
                    } catch (...) { cleanup(); external_finish(job, "failed", "Native SD-card upload failed"); }
                });
            return external_job_state(job);
        } catch (...) {
            std::filesystem::remove_all(directory, error);
            throw;
        }
    };
    handlers["device.print.plan"] = [](Plater& plater, const Json& params) {
        mcp_device_fields(params, {"deviceId", "expectedPrintStatus", "plateIndex", "expectedPlateRevision", "sliceResultId", "remoteName",
            "route", "bedLeveling", "flowCalibration", "amsMapping"});
        const auto plan = mcp_print_plan(plater, params);
        return Json{{"deviceId", plan.native.dev_id}, {"plateIndex", plan.plate_index},
            {"providerPlateNumber", plan.native.plate_index},
            {"remoteName", plan.remote_name}, {"route", plan.route},
            {"bedLeveling", plan.native.task_bed_leveling}, {"flowCalibration", plan.native.task_flow_cali},
            {"useAms", plan.native.task_use_ams}, {"ready", true},
            {"effect", "Submit the selected sliced plate to start a physical print"}};
    };
    handlers["ams.inventory"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {"deviceId"});
        return mcp_ams_inventory(mcp_device(params));
    };
    handlers["ams.mapping.plan"] = [](Plater& plater, const Json& params) {
        mcp_device_fields(params, {"deviceId", "expectedPrintStatus", "plateIndex", "expectedPlateRevision", "sliceResultId", "remoteName",
            "route", "bedLeveling", "flowCalibration", "amsMapping"});
        const auto plan = mcp_print_plan(plater, params);
        if (!plan.native.task_use_ams) throw RpcError("INVALID_PARAMS", "Provide explicit AMS assignments to plan mapping");
        return Json{{"deviceId", plan.native.dev_id}, {"plateIndex", plan.plate_index},
            {"route", plan.route}, {"assignments", Json::parse(plan.native.ams_mapping2)},
            {"ready", true}, {"effect", "Mapping is used only when device.print.start submits this sliced plate"}};
    };
    handlers["device.print.start"] = [](Plater& plater, const Json& params) {
        mcp_device_fields(params, {"deviceId", "expectedPrintStatus", "plateIndex", "expectedPlateRevision", "sliceResultId", "remoteName",
            "route", "bedLeveling", "flowCalibration", "amsMapping", "confirm"});
        if (text(params, "confirm") != "START_PRINT")
            throw RpcError("INVALID_PARAMS", "Confirm START_PRINT for the selected printer and sliced plate");
        auto plan = mcp_print_plan(plater, params);
        const auto staged = mcp_stage_sliced_plate(plater, params, true);
        try { plan = mcp_print_plan(plater, params); }
        catch (...) { std::error_code ignored; std::filesystem::remove_all(std::filesystem::path(staged).parent_path(), ignored); throw; }
        plan.native.filename = staged;
        plan.native.config_filename = staged;
        auto* agent = wxGetApp().getAgent();
        try {
            auto job = start_external_job("device_print", plan.plate_index,
                [agent, native = std::move(plan.native), route = plan.route, staged]
                (const std::shared_ptr<ExternalJob>& job) mutable {
                    const auto cleanup = [&] { std::error_code ignored; std::filesystem::remove_all(std::filesystem::path(staged).parent_path(), ignored); };
                    try {
                        auto progress = [job](int, int, std::string) { external_progress(job, 0, "Native print submission in progress"); };
                        auto cancelled = [job] { return job->cancel_requested.load(); };
                        const int code = route == "lan"
                            ? agent->start_local_print(native, progress, cancelled)
                            : agent->start_print(native, progress, cancelled,
                                [](int, std::string) { return false; });
                        cleanup();
                        if (code == 0) external_finish(job, "succeeded", {},
                            Json{{"deviceId", native.dev_id}, {"submittedToProvider", true},
                                {"physicalPrintObserved", false}, {"statusMethod", "device.get_state"}});
                        else if (code == BAMBU_NETWORK_ERR_CANCELED) external_finish(job, "cancelled");
                        else external_finish(job, "failed", "Native print submission failed (code " + std::to_string(code) + ")");
                    } catch (...) { cleanup(); external_finish(job, "failed", "Native print submission failed"); }
                });
            return external_job_state(job);
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove_all(std::filesystem::path(staged).parent_path(), ignored);
            throw;
        }
    };
    handlers["preferences.get"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {});
        auto* config = wxGetApp().app_config;
        if (!config) throw RpcError("UNAVAILABLE", "Application preferences are unavailable");
        Json values = Json::object();
        for (const auto* key : {"show_3d_navigator", "use_12h_time_format", "auto_calculate_flush", "backup_switch"})
            values[key] = config->get_bool(key);
        return Json{{"values", values}, {"scope", "application profile"}};
    };
    handlers["preferences.set"] = [](Plater&, const Json& params) {
        mcp_device_fields(params, {"key", "value"});
        auto* config = wxGetApp().app_config;
        if (!config) throw RpcError("UNAVAILABLE", "Application preferences are unavailable");
        const auto key = text(params, "key");
        if (key != "show_3d_navigator" && key != "use_12h_time_format" &&
            key != "auto_calculate_flush" && key != "backup_switch")
            throw RpcError("INVALID_PARAMS", "Preference key is outside the supported whitelist");
        const bool value = flag(params, "value");
        const auto previous = config->get(key);
        config->set("app", key, value ? "true" : "false");
        try { config->save(); }
        catch (...) { config->set("app", key, previous); throw; }
        return Json{{"key", key}, {"value", config->get_bool(key)}, {"saved", true},
            {"requiresRestartOrUiRefresh", true}};
    };
    return handlers;
}
