#!/usr/bin/env python3
"""Validate the repository's GitHub issue forms without contacting GitHub."""

from pathlib import Path
import re

import yaml

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE_DIR = ROOT / ".github" / "ISSUE_TEMPLATE"
FIELD_TYPES = {"markdown", "input", "textarea", "dropdown", "checkboxes"}
ID = re.compile(r"^[A-Za-z0-9_-]+$")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def mapping(value, label):
    require(isinstance(value, dict), f"{label} must be a mapping")
    return value


def validate_form(path):
    data = mapping(yaml.safe_load(path.read_text(encoding="utf-8")), str(path))
    for key in ("name", "description", "body"):
        require(key in data, f"{path}: missing {key}")
    require(isinstance(data["name"], str) and len(data["name"]) > 3, f"{path}: invalid name")
    require(isinstance(data["description"], str) and data["description"].strip(),
            f"{path}: invalid description")
    if "title" in data:
        require(isinstance(data["title"], str), f"{path}: title must be text")
    if "labels" in data:
        require(isinstance(data["labels"], list) and all(isinstance(label, str) for label in data["labels"]),
                f"{path}: labels must be a text list")
    require(isinstance(data["body"], list) and data["body"], f"{path}: body must be a non-empty list")

    ids = set()
    for index, item in enumerate(data["body"]):
        where = f"{path}: body item {index}"
        item = mapping(item, where)
        field_type = item.get("type")
        require(field_type in FIELD_TYPES, f"{where}: unsupported type {field_type!r}")
        attributes = mapping(item.get("attributes"), f"{where}: attributes")
        if field_type == "markdown":
            require(isinstance(attributes.get("value"), str) and attributes["value"].strip(),
                    f"{where}: markdown needs value text")
            continue

        field_id = item.get("id")
        require(isinstance(field_id, str) and ID.fullmatch(field_id), f"{where}: invalid id")
        require(field_id not in ids, f"{where}: duplicate id {field_id}")
        ids.add(field_id)
        require(isinstance(attributes.get("label"), str) and attributes["label"].strip(),
                f"{where}: missing label")
        validations = mapping(item.get("validations"), f"{where}: validations")
        require(isinstance(validations.get("required"), bool), f"{where}: required must be boolean")

        if field_type == "dropdown":
            options = attributes.get("options")
            require(isinstance(options, list) and options and all(isinstance(option, str) for option in options),
                    f"{where}: dropdown options must be a non-empty text list")
            if "default" in attributes:
                default = attributes["default"]
                require(isinstance(default, int) and 0 <= default < len(options),
                        f"{where}: invalid dropdown default")
        elif field_type == "checkboxes":
            options = attributes.get("options")
            require(isinstance(options, list) and options, f"{where}: checkbox options missing")
            for option in options:
                option = mapping(option, f"{where}: checkbox option")
                require(isinstance(option.get("label"), str) and option["label"].strip(),
                        f"{where}: checkbox option needs label")
                if "required" in option:
                    require(isinstance(option["required"], bool), f"{where}: checkbox required must be boolean")
    return data["name"]


def validate_config(path):
    data = mapping(yaml.safe_load(path.read_text(encoding="utf-8")), str(path))
    require(isinstance(data.get("blank_issues_enabled"), bool), "config: blank_issues_enabled must be boolean")
    links = data.get("contact_links", [])
    require(isinstance(links, list), "config: contact_links must be a list")
    for index, link in enumerate(links):
        link = mapping(link, f"config: contact link {index}")
        for key in ("name", "url", "about"):
            require(isinstance(link.get(key), str) and link[key].strip(), f"config: link {index} missing {key}")
        require(link["url"].startswith("https://"), f"config: link {index} must use HTTPS")
    return len(links)


def main():
    forms = sorted(TEMPLATE_DIR.glob("*.yml"))
    form_paths = [path for path in forms if path.name != "config.yml"]
    require(form_paths, "no issue forms found")
    names = [validate_form(path) for path in form_paths]
    require(len(names) == len(set(names)), "issue form names must be unique")
    link_count = validate_config(TEMPLATE_DIR / "config.yml")
    for path, name in zip(form_paths, names):
        print(f"Validated {path.name}: {name}")
    print(f"Validated issue chooser configuration ({link_count} links)")


if __name__ == "__main__":
    main()
