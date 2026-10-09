#!/usr/bin/env python3
"""Bounded, isolated acceptance checks for a running native Bambu Studio MCP server."""

import argparse
import http.client
import json
import math
import socket
import stat
import time
import zipfile
from pathlib import Path


VERSION = "2026-07-28"
ORACLE = json.loads((Path(__file__).parent / "legacy_tools_oracle.json").read_text(encoding="utf-8"))
TOOLS = {item["name"] for item in ORACLE}


def check(value, message):
    if not value:
        raise AssertionError(message)
    print(f"PASS {message}", flush=True)


def reachable(port):
    with socket.socket() as connection:
        connection.settimeout(2)
        return connection.connect_ex(("127.0.0.1", port)) == 0


def exchange(port, token, method, params=None):
    params = dict(params or {})
    params["_meta"] = {
        "io.modelcontextprotocol/protocolVersion": VERSION,
        "io.modelcontextprotocol/clientCapabilities": {},
    }
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params})
    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
        "MCP-Protocol-Version": VERSION,
        "Mcp-Method": method,
    }
    if method == "tools/call":
        headers["Mcp-Name"] = params["name"]
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=8)
    try:
        connection.request("POST", "/mcp", body=body, headers=headers)
        response = connection.getresponse()
        payload = response.read()
        return response.status, json.loads(payload) if payload else {}
    finally:
        connection.close()


def request(port, token, method, params=None):
    status, payload = exchange(port, token, method, params)
    if status != 200 or "result" not in payload:
        raise AssertionError(f"{method}: HTTP {status}, JSON-RPC error {payload.get('error', {}).get('code')}")
    return payload["result"]


def tool_result(port, token, name, arguments=None):
    return exchange(port, token, "tools/call", {"name": name, "arguments": arguments or {}})


def tool(port, token, name, arguments=None):
    status, payload = tool_result(port, token, name, arguments)
    answer = payload.get("result", {})
    if status != 200 or "error" in payload or "error" in answer:
        raise AssertionError(f"{name}: HTTP {status}, response {payload}")
    if answer.get("isError"):
        data = answer.get("structuredContent", {})
        raise AssertionError(f"{name}: {data.get('code', 'error')}: {data.get('message', '')}")
    return answer["structuredContent"]


def error_code(port, token, name, arguments):
    _, payload = tool_result(port, token, name, arguments)
    answer = payload.get("result", {})
    if "error" in payload:
        return payload["error"]["code"]
    if "error" in answer:
        return answer["error"]["code"]
    if answer.get("isError"):
        return answer.get("structuredContent", {}).get("code")
    raise AssertionError(f"{name} unexpectedly succeeded")


def invalid(port, token, name, arguments):
    check(error_code(port, token, name, arguments) in (-32602, "INVALID_PARAMS"),
          f"{name} rejects invalid arguments")


def token_from(profile):
    path = profile / "mcp.token"
    check(path.is_file() and not path.is_symlink(), "isolated profile has a regular token file")
    check(stat.S_IMODE(path.stat().st_mode) == 0o600, "token file is owner-only (0600)")
    token = path.read_text(encoding="ascii").strip()
    check(len(token) == 64 and all(c in "0123456789abcdef" for c in token), "token format is valid")
    return token


def make_cube(path):
    vertices = [(0, 0, 0), (16, 0, 0), (16, 16, 0), (0, 16, 0),
                (0, 0, 12), (16, 0, 12), (16, 16, 12), (0, 16, 12)]
    faces = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7),
             (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
             (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
    with path.open("x", encoding="ascii") as output:
        output.write("solid native_acceptance_cube\n")
        for face in faces:
            output.write(" facet normal 0 0 0\n  outer loop\n")
            for vertex in face:
                output.write("   vertex %s %s %s\n" % vertices[vertex])
            output.write("  endloop\n endfacet\n")
        output.write("endsolid native_acceptance_cube\n")


def wait_job(port, token, job_id, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        job = tool(port, token, "job_get", {"jobId": job_id})
        if job.get("completionObserved"):
            check(job.get("status") == "succeeded", f"job {job_id} completed successfully")
            return job
        time.sleep(1)
    raise AssertionError(f"native job {job_id} remained running beyond the bounded wait")


def wait_idle(port, token, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = tool(port, token, "app_get_state")
        if not state["uiJobRunning"] and not state["slicingRunning"] and not state["modalOpen"]:
            return
        time.sleep(1)
    raise AssertionError("native GUI did not become idle within the bounded wait")


def valid_3mf(path):
    if not path.is_file() or path.stat().st_size == 0:
        return False
    with zipfile.ZipFile(path) as archive:
        return archive.testzip() is None and any(name.endswith(".model") for name in archive.namelist())


def check_contract(port, token):
    listed = request(port, token, "tools/list")["tools"]
    check(len(listed) == 90 and {item["name"] for item in listed} == TOOLS,
          "all 90 legacy names are advertised")
    check(listed == ORACLE, "all 90 descriptions, schemas and annotations match the legacy registry")
    capabilities = tool(port, token, "app_get_capabilities")
    check(isinstance(capabilities.get("sessionId"), str) and capabilities["sessionId"],
          "native session has a stable identity")
    check({method.replace(".", "_") for method in capabilities["methods"]} == TOOLS,
          "all 90 advertised tools have native method dispatch")
    features = capabilities["features"]
    check(features["securityPolicyVersion"] == 1 and not features["deviceActionsEnabled"]
          and not features["accountReadsEnabled"], "printer actions and cloud reads default off")
    for name in ("app_get_state", "printer_get_state", "model_list", "plate_list",
                 "settings_get", "slice_get_state", "preferences_get"):
        tool(port, token, name)
    check(True, "representative application, printer, model, plate, setting and slice reads work")


def check_validation(port, token, workdir):
    invalid(port, token, "model_import", {"paths": ["relative.stl"]})
    invalid(port, token, "model_import_multipart", {"paths": [str(workdir / "part.3mf")],
                                                    "plateIndex": 0, "expectedPlateRevision": "stale"})
    invalid(port, token, "model_import_multipart", {"paths": [str(workdir / "PART.STL")],
                                                    "plateIndex": 0, "expectedPlateRevision": "stale"})
    invalid(port, token, "object_transform", {"objectId": "missing", "instanceId": "missing"})
    invalid(port, token, "plate_delete", {"plateIndex": 0, "expectedPlateRevision": "stale",
                                          "targetPlateIndex": 1})
    invalid(port, token, "settings_effective", {"plateIndex": 0})
    invalid(port, token, "settings_update", {"values": {}})
    invalid(port, token, "settings_override", {"scope": "object", "values": {"wall_loops": "3"}})
    invalid(port, token, "object_cut", {"objectId": "missing", "instanceId": "missing",
                                        "normal": [0, 0, 0], "offset": 0, "keep": "both"})
    invalid(port, token, "object_merge", {"objectIds": ["same", "same"]})
    invalid(port, token, "painting_set", {"objectId": "missing", "volumeId": "missing",
                                           "meshRevision": "stale", "kind": "support",
                                           "faceIndices": [0, 0], "state": 1})
    invalid(port, token, "slice_start_batch", {"expectedPlateRevision": "stale", "plateIndices": [0, 0]})
    invalid(port, token, "view_capture", {"plateIndex": 0, "expectedPlateRevision": "stale",
                                            "path": str(workdir / "wrong.txt")})
    invalid(port, token, "view_capture", {"plateIndex": 0, "expectedPlateRevision": "stale",
                                            "path": str(workdir / "wrong.PNG")})
    invalid(port, token, "export_geometry", {"objectId": "missing", "instanceId": "missing",
                                               "path": str(workdir / "wrong.txt")})
    invalid(port, token, "export_gcode", {"plateIndex": 0, "expectedPlateRevision": "stale",
                                          "sliceResultId": "stale", "path": str(workdir / "wrong.txt")})
    invalid(port, token, "export_gcode", {"plateIndex": 0, "expectedPlateRevision": "stale",
                                          "sliceResultId": "stale", "path": str(workdir / "wrong.GCODE")})
    invalid(port, token, "export_sliced_3mf", {"plateIndex": 0, "expectedPlateRevision": "stale",
                                               "sliceResultId": "stale", "path": str(workdir / "wrong.txt")})
    invalid(port, token, "ams_mapping_plan", {"deviceId": "missing", "expectedPrintStatus": "idle",
                                              "plateIndex": 0, "expectedPlateRevision": "stale",
                                              "sliceResultId": "stale", "remoteName": "sample.gcode.3mf",
                                              "route": "lan", "bedLeveling": False, "flowCalibration": False})
    invalid(port, token, "calibration_start", {"deviceId": "missing", "expectedPrintStatus": "idle",
                                               "confirm": "START_CALIBRATION"})
    invalid(port, token, "preferences_set", {"key": "mcp_allow_device_actions", "value": True})


def check_default_admission(port, token):
    device = {"deviceId": "disposable-no-device", "expectedPrintStatus": "idle", "expectedTaskId": "none"}
    plate = {"plateIndex": 0, "expectedPlateRevision": "stale", "sliceResultId": "stale"}
    selected = {"deviceId": device["deviceId"], "expectedPrintStatus": device["expectedPrintStatus"]}
    upload = {**selected, **plate, "remoteName": "acceptance.gcode", "confirm": "UPLOAD_TO_SDCARD"}
    printing = {**selected, **plate, "remoteName": "acceptance.gcode.3mf", "route": "lan",
                "bedLeveling": False, "flowCalibration": False, "confirm": "START_PRINT"}
    calls = [
        ("device_pause", device), ("device_resume", device),
        ("device_stop", {**device, "confirm": "STOP_PRINT"}),
        ("device_upload_start", upload), ("device_print_start", printing),
        ("calibration_start", {"deviceId": device["deviceId"], "expectedPrintStatus": "idle",
                               "confirm": "START_CALIBRATION", "bedLeveling": True}),
        ("account_tasks_list", {}), ("account_presets_list", {}),
    ]
    for name, arguments in calls:
        check(error_code(port, token, name, arguments) == "PERMISSION_DENIED",
              f"{name} is refused by the default preference guard")


def run_flow(port, token, workdir, do_slice):
    fixture = workdir / "native_acceptance_cube.stl"
    check(fixture.is_file(), "generated STL fixture exists")
    model = tool(port, token, "model_list")
    check(not model["objects"], "isolated scene starts empty")
    tool(port, token, "model_import", {"paths": [str(fixture)]})
    wait_idle(port, token)
    model = tool(port, token, "model_list")
    check(len(model["objects"]) == 1 and isinstance(model["objects"][0]["id"], str),
          "STL import created one object with a stable string ID")
    obj = model["objects"][0]
    instance = obj["instances"][0]
    before = list(instance["position"])
    position = [before[0] + 2, before[1], before[2]]
    tool(port, token, "object_transform", {"objectId": obj["id"], "instanceId": instance["id"],
                                           "position": position})
    model = tool(port, token, "model_list")
    actual = model["objects"][0]["instances"][0]["position"]
    check(all(math.isclose(a, b, abs_tol=1e-5) for a, b in zip(actual, position)),
          "absolute instance transform is visible in the model")
    wait_idle(port, token)
    plates = tool(port, token, "plate_list")
    old_revision = plates["plateRevision"]
    first_plate = plates["selectedIndex"]
    tool(port, token, "plate_create")
    plates = tool(port, token, "plate_list")
    check(len(plates["plates"]) == 2 and plates["plateRevision"] != old_revision,
          "plate creation changed the revision")
    check(error_code(port, token, "plate_select", {"plateIndex": first_plate,
          "expectedPlateRevision": old_revision}) == "STALE_REFERENCE",
          "stale plate revision is rejected")
    tool(port, token, "plate_select", {"plateIndex": first_plate,
                                      "expectedPlateRevision": plates["plateRevision"]})
    wait_idle(port, token)
    plates = tool(port, token, "plate_list")
    check(plates["selectedIndex"] == first_plate, "plate selection respects a fresh revision")
    saved_path = workdir / "saved.3mf"
    saved = tool(port, token, "project_save", {"path": str(saved_path)})
    check(saved.get("path") == str(saved_path) and valid_3mf(saved_path),
          "project save wrote a valid 3MF")
    arranged = tool(port, token, "arrange_start")
    check(isinstance(arranged.get("jobId"), str), "arrangement returned a tracked job ID")
    wait_job(port, token, arranged["jobId"], 90)
    wait_idle(port, token)
    if do_slice:
        plates = tool(port, token, "plate_list")
        reference = {"plateIndex": first_plate, "expectedPlateRevision": plates["plateRevision"]}
        validation = tool(port, token, "slice_validate", reference)
        check(validation.get("valid") is True, "native slice validation passed")
        started = tool(port, token, "slice_start", reference)
        check(isinstance(started.get("jobId"), str), "slicing returned a tracked job ID")
        finished = wait_job(port, token, started["jobId"], 180)
        slice_id = finished.get("result", {}).get("sliceResultId")
        check(isinstance(slice_id, str) and slice_id, "completed job contains a slice result ID")
        plates = tool(port, token, "plate_list")
        reference = {"plateIndex": first_plate, "expectedPlateRevision": plates["plateRevision"],
                     "sliceResultId": slice_id}
        summary = tool(port, token, "preview_get_summary", reference)
        check(summary.get("sliceResultId") == slice_id and summary.get("moveCount", 0) > 0,
              "native toolpath preview matches completed slice")
        gcode_path = workdir / "sliced.gcode"
        exported = tool(port, token, "export_gcode", {**reference, "path": str(gcode_path)})
        if "jobId" in exported:
            wait_job(port, token, exported["jobId"], 90)
        check(gcode_path.is_file() and gcode_path.stat().st_size > 0,
              "G-code export from the current slice exists")
        model = tool(port, token, "model_list")
        instance = model["objects"][0]["instances"][0]
        moved = list(instance["position"])
        moved[0] += 1
        tool(port, token, "object_transform", {"objectId": obj["id"], "instanceId": instance["id"],
                                               "position": moved})
        plates = tool(port, token, "plate_list")
        check(error_code(port, token, "preview_get_summary", {
            "plateIndex": first_plate, "expectedPlateRevision": plates["plateRevision"],
            "sliceResultId": slice_id}) == "STALE_REFERENCE",
            "model edit invalidates the prior slice result")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=["prepare", "off", "on", "flow", "settings", "persisted"])
    parser.add_argument("--port", type=int, default=27183)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--workdir", type=Path, required=True)
    parser.add_argument("--slice", action="store_true", help="Only for flow, with a safe local preset")
    parser.add_argument("--old-port", type=int, help="Previous listener port, for settings phase")
    parser.add_argument("--old-token-file", type=Path, help="Owner-only snapshot of the previous disposable token")
    parser.add_argument("--token-snapshot", type=Path, help="Owner-only token snapshot from this disposable profile")
    args = parser.parse_args()
    check(args.profile.is_absolute() and args.workdir.is_absolute(), "profile and workdir are absolute")
    check(args.profile != args.workdir and args.profile != Path.home(), "profile is isolated")
    check(1024 <= args.port <= 65535, "port is valid")
    if args.phase == "prepare":
        args.profile.mkdir(parents=True, exist_ok=False)
        args.workdir.mkdir(parents=True, exist_ok=False)
        (args.profile / ".native_acceptance_profile").write_text("disposable test profile\n", encoding="ascii")
        make_cube(args.workdir / "native_acceptance_cube.stl")
        check(True, "isolated profile and STL fixture created")
        return
    check(args.profile.is_dir() and args.workdir.is_dir(), "isolated directories exist")
    check((args.profile / ".native_acceptance_profile").is_file(), "profile has the disposable-test marker")
    if args.phase == "off":
        check(not reachable(args.port), "MCP listener is disabled on chosen port")
        return
    token = token_from(args.profile)
    if args.phase == "persisted":
        check(args.token_snapshot is not None and args.token_snapshot.is_file()
              and not args.token_snapshot.is_symlink()
              and args.token_snapshot.parent == args.workdir
              and stat.S_IMODE(args.token_snapshot.stat().st_mode) == 0o600,
              "token snapshot is an owner-only work file")
        check(args.token_snapshot.read_text(encoding="ascii").strip() == token,
              "token is unchanged in the same disposable profile")
        config = json.loads((args.profile / "BambuStudio.conf").read_text(encoding="utf-8"))
        check(config["app"]["mcp_server_enabled"] is True
              and config["app"]["mcp_server_port"] == str(args.port),
              "requested enabled state and port remain saved")
        return
    check(reachable(args.port), "MCP listener is reachable on chosen port")
    if args.phase == "settings":
        check(args.old_port is not None and args.old_port != args.port and 1024 <= args.old_port <= 65535,
              "old and new ports are distinct and valid")
        check(args.old_token_file is not None and args.old_token_file.is_file()
              and not args.old_token_file.is_symlink()
              and args.old_token_file.parent == args.workdir
              and stat.S_IMODE(args.old_token_file.stat().st_mode) == 0o600,
              "previous disposable token is stored in an owner-only work file")
        old_token = args.old_token_file.read_text(encoding="ascii").strip()
        check(len(old_token) == 64 and all(c in "0123456789abcdef" for c in old_token)
              and old_token != token, "token was regenerated")
        check(not reachable(args.old_port), "previous port is closed")
        old_status, _ = exchange(args.port, old_token, "server/discover")
        check(old_status == 401, "previous token is rejected on the new port")
        check(request(args.port, token, "server/discover")["resultType"] == "complete",
              "new token works on the new port")
        config = json.loads((args.profile / "BambuStudio.conf").read_text(encoding="utf-8"))
        check(config["app"]["mcp_server_enabled"] is True
              and config["app"]["mcp_server_port"] == str(args.port),
              "enabled state and new port are saved in the disposable profile")
        return
    wrong_status, _ = exchange(args.port, "0" * 64, "server/discover")
    check(wrong_status == 401, "wrong token is rejected")
    discovery = request(args.port, token, "server/discover")
    check(discovery["resultType"] == "complete" and VERSION in discovery["supportedVersions"],
          "modern discovery succeeds")
    check_contract(args.port, token)
    check_validation(args.port, token, args.workdir)
    check_default_admission(args.port, token)
    if args.phase == "flow":
        run_flow(args.port, token, args.workdir, args.slice)


if __name__ == "__main__":
    main()
