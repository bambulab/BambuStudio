#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Bambu Studio profile 预设检查脚本。

从 slicer_patch_builder._inherit_all_type_config / _inherit_config 提取只读检查逻辑，
用于在 patch build 之前尽早发现 profile 配置问题。

用法（在 bamboo_slicer 仓库根目录）:
    python scripts/check_profiles.py
    python scripts/check_profiles.py --vendor BBL
    python scripts/check_profiles.py --profiles-path resources/profiles

也可从 bamboo_slicer_build 仓库调用:
    python build_script/check_profiles.py --slicer-root ../bamboo_slicer
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from typing import Any


UTF8_BOM = b"\xef\xbb\xbf"


class ProfileCheckError(Exception):
    """单条检查失败。"""


@dataclass
class ProfileChecker:
    profiles_path: str
    vendor: str = "BBL"
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    @property
    def vendor_path(self) -> str:
        return os.path.join(self.profiles_path, self.vendor)

    @property
    def vendor_json_path(self) -> str:
        return os.path.join(self.profiles_path, f"{self.vendor}.json")

    def add_error(self, message: str) -> None:
        if message in self.errors:
            return
        self.errors.append(message)
        logging.error(message)

    def add_warning(self, message: str) -> None:
        self.warnings.append(message)
        logging.warning(message)

    def run(self) -> bool:
        logging.info("profiles path: %s", self.profiles_path)
        logging.info("vendor: %s", self.vendor)

        if not os.path.isdir(self.profiles_path):
            self.add_error(f"profiles 目录不存在: {self.profiles_path}")
            return False

        if not os.path.isfile(self.vendor_json_path):
            self.add_error(f"厂商总配置不存在: {self.vendor_json_path}")
            return False

        try:
            data = self._load_json(self.vendor_json_path)
        except ProfileCheckError as exc:
            self.add_error(str(exc))
            return False

        self._check_vendor_json_structure(data)
        self._check_all_json_bom_and_parse()
        self._check_setting_id_unique()
        self._check_list_entries(data)

        machine = self._collect_machine_info(data)
        nozzle_diameter = self._collect_nozzle_diameter(data)

        self._check_config_list(data.get("machine_list", []), "machine", machine, nozzle_diameter)
        self._check_config_list(data.get("process_list", []), "process", machine, nozzle_diameter)
        self._check_config_list(data.get("filament_list", []), "filament", machine, nozzle_diameter)

        logging.info("检查完成: %d 个错误, %d 个警告", len(self.errors), len(self.warnings))
        return len(self.errors) == 0

    def _load_json(self, json_path: str) -> dict[str, Any]:
        with open(json_path, "rb") as fin:
            raw = fin.read()

        if raw.startswith(UTF8_BOM):
            raise ProfileCheckError(f"UTF-8 BOM: {json_path}")

        try:
            return json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise ProfileCheckError(f"JSON 解析失败: {json_path}: {exc}") from exc

    def _check_vendor_json_structure(self, data: dict[str, Any]) -> None:
        for key in ("version", "machine_list", "process_list", "filament_list"):
            if key not in data:
                self.add_error(f"{self.vendor_json_path} 缺少字段: {key}")

        if "version" in data:
            parts = data["version"].split(".")
            if len(parts) != 4:
                self.add_error(
                    f"{self.vendor_json_path} version 格式应为 aa.bb.cc.dd: {data['version']}"
                )

    def _iter_vendor_json_files(self) -> list[str]:
        files: list[str] = []
        if not os.path.isdir(self.vendor_path):
            return files

        for root, _, names in os.walk(self.vendor_path):
            if "_full" in root:
                continue
            for name in names:
                if name.endswith(".json"):
                    files.append(os.path.join(root, name))
        return files

    def _check_all_json_bom_and_parse(self) -> None:
        for json_file in self._iter_vendor_json_files():
            try:
                self._load_json(json_file)
            except ProfileCheckError as exc:
                self.add_error(str(exc))

    def _check_setting_id_unique(self) -> None:
        setting_id_to_files: dict[str, list[str]] = defaultdict(list)

        for json_file in self._iter_vendor_json_files():
            try:
                data = self._load_json(json_file)
            except ProfileCheckError:
                continue

            setting_id = data.get("setting_id")
            if setting_id:
                setting_id_to_files[setting_id].append(json_file)

        for setting_id, files in sorted(setting_id_to_files.items()):
            if len(files) == 1:
                logging.info("setting id: %s, count: 1", setting_id)
                continue

            self.add_error(
                "setting_id 重复: "
                f"{setting_id} (count={len(files)})\n  "
                + "\n  ".join(files)
            )

    def _check_list_entries(self, data: dict[str, Any]) -> None:
        for list_name in ("machine_list", "process_list", "filament_list"):
            for entry in data.get(list_name, []):
                name = entry.get("name", "<unknown>")
                sub_path = entry.get("sub_path", "")
                if not sub_path:
                    self.add_error(f"{list_name} 条目缺少 sub_path: {name}")
                    continue

                full_path = os.path.join(self.profiles_path, self.vendor, sub_path)
                if not os.path.isfile(full_path):
                    self.add_error(f"{list_name} 引用的文件不存在: {name} -> {full_path}")

    def _collect_machine_info(self, data: dict[str, Any]) -> dict[str, str]:
        machine: dict[str, str] = {}
        for entry in data.get("machine_list", []):
            full_path = os.path.join(self.profiles_path, self.vendor, entry["sub_path"])
            if not os.path.isfile(full_path):
                continue
            try:
                config = self._load_json(full_path)
            except ProfileCheckError as exc:
                self.add_error(str(exc))
                continue

            if config.get("instantiation", "false") != "true":
                continue

            printer_model = config.get("printer_model")
            if not printer_model:
                self.add_error(
                    f"实例化 machine 缺少 printer_model: {entry['name']} ({full_path})"
                )
                continue

            machine[entry["name"]] = printer_model

        logging.info("machine models: %d", len(machine))
        return machine

    def _collect_nozzle_diameter(self, data: dict[str, Any]) -> dict[str, str]:
        nozzle_diameter: dict[str, str] = {}
        for entry in data.get("machine_list", []):
            full_path = os.path.join(self.profiles_path, self.vendor, entry["sub_path"])
            if not os.path.isfile(full_path):
                continue
            try:
                config = self._load_json(full_path)
            except ProfileCheckError:
                continue

            if config.get("instantiation", "false") != "true":
                continue

            diameter_value = config.get("nozzle_diameter")
            if diameter_value is None:
                self.add_error(
                    f"实例化 machine 缺少 nozzle_diameter: {entry['name']} ({full_path})"
                )
                continue

            if isinstance(diameter_value, list):
                diameter = diameter_value[0]
            else:
                diameter = diameter_value

            nozzle_diameter[entry["name"]] = str(diameter)

        logging.info("nozzle diameter: %d", len(nozzle_diameter))
        return nozzle_diameter

    def _build_config_name_path(self, config_list: list[dict[str, str]]) -> dict[str, str]:
        config_name_path: dict[str, str] = {}
        for entry in config_list:
            name = entry.get("name", "")
            if not name:
                continue
            if name in config_name_path:
                self.add_error(
                    f"配置名重复注册: {name}\n"
                    f"  已有: {config_name_path[name]}\n"
                    f"  重复: {entry.get('sub_path', '')}"
                )
            config_name_path[name] = entry["sub_path"]
        return config_name_path

    def _resolve_config_path(self, root_path: str, sub_path: str) -> str:
        return os.path.join(root_path, sub_path)

    def _check_inherits_chain(
        self,
        root_path: str,
        config_name_path: dict[str, str],
        inherits_name: str,
        context: str,
        visited: set[str] | None = None,
    ) -> None:
        if visited is None:
            visited = set()

        if inherits_name in visited:
            self.add_error(f"{context}: inherits 继承链存在环: {inherits_name}")
            return

        if inherits_name not in config_name_path:
            self.add_error(
                f"{context}: inherits 未在列表中注册: {inherits_name}"
            )
            return

        inherits_sub_path = config_name_path[inherits_name]
        inherits_path = self._resolve_config_path(root_path, inherits_sub_path)
        if not os.path.isfile(inherits_path):
            self.add_error(f"{context}: inherits 文件不存在: {inherits_path}")
            return

        try:
            config = self._load_json(inherits_path)
        except ProfileCheckError as exc:
            self.add_error(f"{context}: {exc}")
            return

        parent_inherits = config.get("inherits", "")
        if parent_inherits:
            self._check_inherits_chain(
                root_path,
                config_name_path,
                parent_inherits,
                context,
                visited | {inherits_name},
            )

    def _check_includes_chain(
        self,
        root_path: str,
        config_name_path: dict[str, str],
        include_name: str,
        context: str,
        visited: set[str] | None = None,
    ) -> None:
        if visited is None:
            visited = set()

        if include_name in visited:
            self.add_error(f"{context}: include 引用链存在环: {include_name}")
            return

        if include_name not in config_name_path:
            self.add_error(
                f"{context}: include 未在列表中注册: {include_name}"
            )
            return

        include_sub_path = config_name_path[include_name]
        include_path = self._resolve_config_path(root_path, include_sub_path)
        if not os.path.isfile(include_path):
            self.add_error(f"{context}: include 文件不存在: {include_path}")
            return

        try:
            config = self._load_json(include_path)
        except ProfileCheckError as exc:
            self.add_error(f"{context}: {exc}")
            return

        includes = config.get("include", "")
        if not includes:
            return

        if isinstance(includes, str):
            includes = [includes]

        for inc in includes:
            self._check_includes_chain(
                root_path,
                config_name_path,
                inc,
                context,
                visited | {include_name},
            )

    def _check_config_list(
        self,
        config_list: list[dict[str, str]],
        config_type: str,
        machine: dict[str, str],
        nozzle_diameter: dict[str, str],
    ) -> None:
        logging.info("")
        logging.info("检查 %s list (%d 项)", config_type, len(config_list))

        config_name_path = self._build_config_name_path(config_list)
        root_path = os.path.join(self.profiles_path, self.vendor)

        for entry in config_list:
            name = entry.get("name", "<unknown>")
            sub_path = entry.get("sub_path", "")
            full_path = os.path.join(self.profiles_path, self.vendor, sub_path)
            context = f"{config_type}/{name}"

            if not os.path.isfile(full_path):
                self.add_warning(f"{context}: 文件不存在，跳过继承检查 ({full_path})")
                continue

            try:
                config = self._load_json(full_path)
            except ProfileCheckError as exc:
                self.add_error(f"{context}: {exc}")
                continue

            file_name = config.get("name", "")
            if file_name and file_name != name:
                self.add_warning(
                    f"{context}: 文件名与 JSON name 不一致: list={name}, json={file_name}"
                )

            if config.get("instantiation", "false") == "false":
                continue

            inherits_name = config.get("inherits", "")
            if inherits_name:
                self._check_inherits_chain(
                    root_path, config_name_path, inherits_name, context
                )

            includes = config.get("include", "")
            if includes:
                if isinstance(includes, str):
                    includes = [includes]
                for include_name in includes:
                    self._check_includes_chain(
                        root_path, config_name_path, include_name, context
                    )

            if not config.get("setting_id", ""):
                self.add_error(f"{context}: instantiation=true 但缺少 setting_id")

            if config.get("type") in ("filament", "process"):
                for printer in config.get("compatible_printers", []):
                    if printer not in machine:
                        self.add_error(
                            f"{context}: compatible_printers 引用未知机型: {printer}"
                        )
                    elif printer not in nozzle_diameter:
                        self.add_error(
                            f"{context}: compatible_printers 机型缺少 nozzle_diameter: {printer}"
                        )

            level = sub_path.split("/")
            if len(level) < 2:
                self.add_error(f"{context}: sub_path 格式无效: {sub_path}")


def _find_repo_root(start: str) -> str | None:
    """向上查找包含 resources/profiles 的仓库根目录。"""
    cur = os.path.abspath(start)
    for _ in range(8):
        if os.path.isdir(os.path.join(cur, "resources", "profiles")):
            return cur
        parent = os.path.dirname(cur)
        if parent == cur:
            break
        cur = parent
    return None


def _detect_slicer_root() -> str:
    """
    自动探测 bamboo_slicer 仓库根目录。

    依次从当前工作目录、脚本所在目录及其父目录向上查找 resources/profiles。
    支持在仓库根目录、scripts 目录或 IDE 直接运行脚本。
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    for start in (os.getcwd(), script_dir, os.path.dirname(script_dir)):
        root = _find_repo_root(start)
        if root:
            return root
    return os.path.dirname(script_dir)


def _detect_profiles_path(slicer_root: str) -> str:
    candidates = [
        os.path.join(slicer_root, "resources", "profiles"),
        os.path.join(slicer_root, "install_dir", "Release", "resources", "profiles"),
    ]
    for path in candidates:
        if os.path.isdir(path):
            return path
    return candidates[0]


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="检查 Bambu Studio profile 预设配置（对应 patch build 中的预设检查阶段）"
    )
    parser.add_argument(
        "--slicer-root",
        default=None,
        help="bamboo_slicer 仓库根目录，默认自动探测（从 cwd 或脚本位置向上查找）",
    )
    parser.add_argument(
        "--profiles-path",
        default="",
        help="profiles 目录，默认根据 slicer-root 自动探测 resources/profiles",
    )
    parser.add_argument(
        "--vendor",
        default="BBL",
        help="厂商目录名，默认 BBL",
    )
    parser.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        help="仅输出错误和警告",
    )
    return parser.parse_args()


def main() -> int:
    args = _parse_args()

    logging.basicConfig(
        level=logging.WARNING if args.quiet else logging.INFO,
        format="%(levelname)-8s: %(message)s",
    )

    slicer_root = (
        os.path.abspath(args.slicer_root)
        if args.slicer_root
        else _detect_slicer_root()
    )
    profiles_path = (
        os.path.abspath(args.profiles_path)
        if args.profiles_path
        else _detect_profiles_path(slicer_root)
    )

    logging.info("slicer root: %s", slicer_root)

    checker = ProfileChecker(profiles_path=profiles_path, vendor=args.vendor)
    ok = checker.run()

    if checker.errors:
        logging.error("=== 发现 %d 个错误 ===", len(checker.errors))
        for idx, err in enumerate(checker.errors, 1):
            print(f"[ERROR {idx}] {err}", file=sys.stderr)

    if checker.warnings:
        logging.warning("=== 发现 %d 个警告 ===", len(checker.warnings))
        for idx, warn in enumerate(checker.warnings, 1):
            print(f"[WARN {idx}] {warn}", file=sys.stderr)

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
