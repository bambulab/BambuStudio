import argparse
import configparser
import json
import sys
from pathlib import Path


DEFAULT_TARGETS = (
    "resources/profiles",
    "resources/printers",
    "resources/info",
    "resources/data",
)

STRING_FIELDS = {
    "from",
    "inherits",
    "instantiation",
    "model_id",
    "name",
    "printer_model",
    "printer_type",
    "setting_id",
    "type",
}

STRING_LIST_FIELDS = {
    "compatible_printers",
    "include",
}

STRING_OR_STRING_LIST_FIELDS = {
    "nozzle_diameter",
}


def iter_target_files(targets):
    seen = set()
    for raw_target in targets:
        target = Path(raw_target)
        if target.is_file():
            candidates = [target]
        elif target.is_dir():
            candidates = sorted(
                path
                for pattern in ("*.json", "*.ini")
                for path in target.rglob(pattern)
            )
        else:
            continue

        for path in candidates:
            suffix = path.suffix.lower()
            if suffix not in {".json", ".ini"}:
                continue
            resolved = path.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            yield path


def validate_non_empty_string(data, field_name, path, errors):
    if field_name not in data:
        return
    value = data[field_name]
    if not isinstance(value, str) or not value.strip():
        errors.append(f"{path}: {field_name} must be a non-empty string")


def validate_string_list(data, field_name, path, errors):
    if field_name not in data:
        return
    value = data[field_name]
    if not isinstance(value, list):
        errors.append(f"{path}: {field_name} must be a list")
        return
    for index, item in enumerate(value):
        if not isinstance(item, str) or not item.strip():
            errors.append(
                f"{path}: {field_name}[{index}] must be a non-empty string"
            )


def validate_string_or_string_list(data, field_name, path, errors):
    if field_name not in data:
        return
    value = data[field_name]
    if isinstance(value, str):
        if not value.strip():
            errors.append(f"{path}: {field_name} must be a non-empty string")
        return
    if isinstance(value, list):
        for index, item in enumerate(value):
            if not isinstance(item, str) or not item.strip():
                errors.append(
                    f"{path}: {field_name}[{index}] must be a non-empty string"
                )
        return
    errors.append(f"{path}: {field_name} must be a string or list of strings")


def validate_json_file(path):
    errors = []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"{path}: invalid JSON ({exc.msg} at line {exc.lineno}, column {exc.colno})"]

    if not isinstance(data, dict):
        return errors

    for field_name in sorted(STRING_FIELDS):
        validate_non_empty_string(data, field_name, path, errors)
    for field_name in sorted(STRING_LIST_FIELDS):
        validate_string_list(data, field_name, path, errors)
    for field_name in sorted(STRING_OR_STRING_LIST_FIELDS):
        validate_string_or_string_list(data, field_name, path, errors)
    return errors


def validate_ini_file(path):
    parser = configparser.ConfigParser(strict=False)
    payload = "[root]\n" + path.read_text(encoding="utf-8")
    try:
        parser.read_string(payload, source=str(path))
    except configparser.Error as exc:
        return [f"{path}: invalid INI ({exc})"]
    return []


def validate_paths(paths):
    errors = []
    for path in paths:
        suffix = Path(path).suffix.lower()
        if suffix == ".json":
            errors.extend(validate_json_file(Path(path)))
        elif suffix == ".ini":
            errors.extend(validate_ini_file(Path(path)))
    return errors


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Validate BambuStudio profile/config JSON and INI assets."
    )
    parser.add_argument(
        "targets",
        nargs="*",
        default=list(DEFAULT_TARGETS),
        help="Files or directories to validate recursively.",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    targets = list(iter_target_files(args.targets))
    errors = validate_paths(targets)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        print(
            f"Validation failed: {len(errors)} issue(s) across {len(targets)} file(s).",
            file=sys.stderr,
        )
        return 1

    print(f"Validation passed: {len(targets)} file(s) checked.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
