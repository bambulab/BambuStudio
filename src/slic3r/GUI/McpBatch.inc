struct SliceBatchJob {
    std::string id, status = "running", error, input_fingerprint;
    std::vector<int> plate_indices;
    Json results = Json::array();
    std::size_t next_index = 0;
    std::uint64_t current_run = 0;
    int stopped_plate_index = -1;
    bool cancel_requested = false, completion_observed = false;
};

std::deque<SliceBatchJob> slice_batches;
std::uint64_t next_batch_run = 0;

SliceBatchJob* find_batch_job(const std::string& id) {
    for (auto& job : slice_batches) if (job.id == id) return &job;
    return nullptr;
}

bool batch_active() {
    return std::any_of(slice_batches.begin(), slice_batches.end(),
        [](const SliceBatchJob& job) { return !job.completion_observed; });
}

Json batch_job_state(const SliceBatchJob& job) {
    Json error = job.error.empty() ? Json(nullptr) : Json{{"code", job.status == "stale" ? "STALE_REFERENCE" : "NATIVE_ERROR"}, {"message", job.error}};
    Json current = nullptr;
    if (job.current_run) if (auto* child = slice_jobs.find(job.current_run)) current = child->id;
    return {{"jobId", job.id}, {"kind", "slice_batch"}, {"status", job.status},
        {"plateIndices", job.plate_indices}, {"completedPlateCount", job.results.size()},
        {"stoppedPlateIndex", job.stopped_plate_index < 0 ? Json(nullptr) : Json(job.stopped_plate_index)},
        {"results", job.results}, {"currentJobId", current}, {"cancelRequested", job.cancel_requested},
        {"completionObserved", job.completion_observed}, {"error", error}};
}

void finish_batch(SliceBatchJob& job, std::string status, std::string error = {}) {
    job.status = std::move(status);
    job.error = std::move(error);
    job.current_run = 0;
    job.completion_observed = true;
}

void advance_batch(Plater& plater, const std::string& id) {
    auto* batch = find_batch_job(id);
    if (!batch || batch->completion_observed) return;
    if (batch->next_index >= batch->plate_indices.size()) { finish_batch(*batch, "succeeded"); return; }
    if (batch->cancel_requested) {
        batch->stopped_plate_index = batch->plate_indices[batch->next_index];
        finish_batch(*batch, "cancelled");
        return;
    }
    if (slice_fingerprint(plater, true) != batch->input_fingerprint) {
        batch->stopped_plate_index = batch->plate_indices[batch->next_index];
        finish_batch(*batch, "stale", "Native slice inputs changed during the plate queue");
        return;
    }
    while (batch->next_index < batch->plate_indices.size()) {
        const int plate_index = batch->plate_indices[batch->next_index];
        if (plate_index >= plater.get_partplate_list().get_plate_count()) {
            batch->stopped_plate_index = plate_index;
            finish_batch(*batch, "stale", "Plate list changed during the slice queue");
            return;
        }
        try {
            if (plater.get_partplate_list().get_curr_plate_index() != plate_index &&
                plater.select_plate(plate_index, false) != 0)
                throw RpcError("NATIVE_ERROR", "Native plate selection failed");
            const auto revision = plate_state(plater)["plateRevision"].get<std::string>();
            const Json request = {{"plateIndex", plate_index}, {"expectedPlateRevision", revision}};
            const auto child_state = start_slice(plater, request);
            const auto child_id = child_state["jobId"].get<std::string>();
            auto* child = slice_jobs.find(child_id);
            if (!child) throw RpcError("NATIVE_ERROR", "Native slice job was not recorded");
            batch->current_run = child->run;
            if (child->worker_pending) return;
            if (child->state != "succeeded") throw RpcError("NATIVE_ERROR", "Native plate slice failed to start");
            batch->results.push_back({{"plateIndex", plate_index}, {"sliceResultId", child->result_id}, {"reused", child->reused}});
            ++batch->next_index;
            batch->current_run = 0;
            if (batch->cancel_requested) {
                if (batch->next_index >= batch->plate_indices.size()) finish_batch(*batch, "succeeded");
                else {
                    batch->stopped_plate_index = batch->plate_indices[batch->next_index];
                    finish_batch(*batch, "cancelled");
                }
                return;
            }
        } catch (const std::exception& error) {
            batch->stopped_plate_index = plate_index;
            finish_batch(*batch, "failed", error.what());
            return;
        }
    }
    finish_batch(*batch, "succeeded");
}

void batch_child_completed(const McpSliceJob& child) {
    for (auto& batch : slice_batches) {
        if (batch.completion_observed || batch.current_run != child.run) continue;
        batch.current_run = 0;
        if (child.state == "succeeded") {
            batch.results.push_back({{"plateIndex", child.plate_index}, {"sliceResultId", child.result_id}, {"reused", child.reused}});
            ++batch.next_index;
            if (batch.next_index >= batch.plate_indices.size()) { finish_batch(batch, "succeeded"); return; }
            if (batch.cancel_requested) {
                batch.stopped_plate_index = batch.plate_indices[batch.next_index];
                finish_batch(batch, "cancelled");
                return;
            }
        } else {
            batch.stopped_plate_index = child.plate_index;
            if (child.state == "cancelled") { finish_batch(batch, "cancelled"); return; }
            finish_batch(batch, child.state == "stale" ? "stale" : "failed",
                child.error_message.empty() ? "Native plate slice failed" : child.error_message);
            return;
        }
        const auto id = batch.id;
        const auto generation = mcp_domain_generation.load();
        wxGetApp().CallAfter([id, generation] {
            if (!mcp_publication_enabled.load() || mcp_domain_generation.load() != generation) return;
            if (auto* plater = wxGetApp().plater()) advance_batch(*plater, id);
        });
        return;
    }
}

Json start_batch(Plater& plater, const Json& params) {
    check_plate_revision(plater, params);
    require(!batch_active(), "A plate slice queue is already active");
    require(!slice_jobs.active() && !plater.background_process().running() && !plater.background_process().mcp_orphans_running(),
        "Native slicing is already active");
    const int plate_count = plater.get_partplate_list().get_plate_count();
    std::vector<int> indices;
    if (params.contains("plateIndices")) {
        require(params["plateIndices"].is_array() && !params["plateIndices"].empty() && params["plateIndices"].size() <= 32,
            "plateIndices must contain 1 to 32 indices");
        std::set<int> seen;
        for (const auto& value : params["plateIndices"]) {
            require(value.is_number_integer(), "Plate indices must be integers");
            const auto number = value.get<long long>();
            require(number >= 0 && number < plate_count, "Plate index is outside current state");
            require(seen.insert(static_cast<int>(number)).second, "Plate indices must be unique");
            indices.push_back(static_cast<int>(number));
        }
    } else {
        require(plate_count <= 32, "Select at most 32 plates for one queue");
        for (int i = 0; i < plate_count; ++i) indices.push_back(i);
    }
    while (slice_batches.size() >= 16) {
        if (!slice_batches.front().completion_observed) throw RpcError("BUSY", "Plate queue history is full");
        slice_batches.pop_front();
    }
    SliceBatchJob batch;
    batch.id = session_id() + ":slice_batch:" + std::to_string(++next_batch_run);
    batch.plate_indices = std::move(indices);
    batch.input_fingerprint = slice_fingerprint(plater, true);
    slice_batches.push_back(std::move(batch));
    const auto id = slice_batches.back().id;
    advance_batch(plater, id);
    return batch_job_state(*find_batch_job(id));
}
