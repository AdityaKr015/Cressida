"""Validate PR metadata without executing PR code or making API mutations."""

import json
from pathlib import Path
import re
import sys


HEADINGS = ("Summary", "Motivation", "Type of Change", "Testing", "Checklist")
CHANGE_TYPES = (
    "Bug fix", "New feature", "Breaking change", "Refactoring",
    "Build / packaging", "Documentation",
)
CHECKLIST = (
    "I read and followed `CONTRIBUTING.md`.",
    "I ran `just lint`, or explained why it was not run.",
    "I ran the relevant build and tests, or explained why they were not run.",
    "I self-reviewed the changes and checked for new warnings or errors.",
    "I updated the relevant documentation, or no documentation changes are needed.",
    "I kept credentials and private account data out of code, logs and fixtures.",
)


def sections(body):
    # Comments and fenced examples must not satisfy the actual template.
    body = re.sub(r"<!--.*?(?:-->|\Z)", "", body or "", flags=re.S)
    result = {}
    current = None
    fence = None
    for line in body.splitlines():
        marker = re.match(r"^\s{0,3}(`{3,}|~{3,})", line)
        if marker:
            token = marker[1]
            if fence is None:
                fence = token
            elif token[0] == fence[0] and len(token) >= len(fence):
                fence = None
            continue
        if fence:
            if current and line.strip():
                result[current].append("> " + line)
            continue
        heading = re.fullmatch(r"##\s+(.+?)\s*", line)
        if heading:
            current = heading[1]
            result.setdefault(current, [])
        elif current:
            result[current].append(line)
    return {name: "\n".join(lines).strip() for name, lines in result.items()}


def checklist_items(text):
    return {
        re.sub(r"\s+", " ", match[2]).strip(): match[1].lower() == "x"
        for match in re.finditer(r"^\s*[-*] \[([ xX])\]\s+(.+?)\s*$", text, re.M)
    }


def validate(body, draft=False):
    parsed = sections(body)
    errors = [f"Missing required heading: ## {name}" for name in HEADINGS if name not in parsed]
    items = checklist_items(parsed.get("Checklist", ""))
    for item in CHECKLIST:
        if item not in items:
            errors.append(f"Missing Checklist item: {item}")
        elif not draft and not items[item]:
            errors.append(f"Complete the Checklist item or keep the PR as Draft: {item}")
    if not draft:
        for name in ("Summary", "Motivation", "Testing"):
            if name in parsed and not parsed[name]:
                errors.append(f"Fill in ## {name} or keep the PR as Draft.")
        changes = checklist_items(parsed.get("Type of Change", ""))
        if not any(changes.get(kind, False) for kind in CHANGE_TYPES):
            errors.append("Select at least one Type of Change or keep the PR as Draft.")
    return errors


def main():
    event = json.loads(Path(sys.argv[1]).read_text())
    pr = event["pull_request"]
    errors = validate(pr.get("body"), pr.get("draft", False))
    for error in errors:
        # Do not interpret contributor text as GitHub workflow commands.
        print(f"PR description: {error}")
    if errors:
        print("Use .github/PULL_REQUEST_TEMPLATE.md; incomplete work may remain Draft.")
        return 1
    print("PR description meets the template requirements.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
