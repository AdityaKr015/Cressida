from pathlib import Path
import unittest

from check_pr_template import CHECKLIST, CHANGE_TYPES, validate


TEMPLATE = Path(__file__).resolve().parents[2] / "PULL_REQUEST_TEMPLATE.md"


def ready_body():
    return "\n".join([
        "## Summary", "Fix channel loading.", "## Motivation", "Avoid redundant requests.",
        "## Type of Change", "- [x] Bug fix", "## Testing", "Offline tests passed.",
        "## Checklist", *(f"- [x] {item}" for item in CHECKLIST),
    ])


class TemplateTests(unittest.TestCase):
    def test_supplied_template_is_valid_draft(self):
        self.assertEqual(validate(TEMPLATE.read_text(), draft=True), [])

    def test_unfilled_template_is_not_ready(self):
        self.assertTrue(validate(TEMPLATE.read_text()))

    def test_complete_ready_pr_and_optional_sections(self):
        self.assertEqual(validate(ready_body()), [])
        self.assertEqual(validate(ready_body() + "\n## Related Issue\nCloses #1"), [])

    def test_every_required_item_is_checked(self):
        for item in CHECKLIST:
            body = ready_body().replace(f"- [x] {item}", f"- [ ] {item}")
            self.assertTrue(validate(body), item)
            self.assertEqual(validate(body, draft=True), [])

    def test_every_change_type_supported(self):
        for kind in CHANGE_TYPES:
            self.assertEqual(validate(ready_body().replace("- [x] Bug fix", f"- [X] {kind}")), [])

    def test_comments_and_examples_do_not_satisfy_requirements(self):
        for wrapper in ("<!--\n{}\n-->", "```markdown\n{}\n```", "~~~~\n{}\n~~~~"):
            self.assertTrue(validate(wrapper.format(ready_body()), draft=True))
        self.assertTrue(validate(ready_body().replace("Fix channel loading.", "<!-- later -->")))

    def test_checkboxes_in_wrong_section_are_rejected(self):
        body = ready_body().replace("## Checklist", "## Other") + "\n## Checklist"
        self.assertTrue(validate(body, draft=True))

    def test_fenced_testing_commands_are_valid_but_fenced_checklists_are_not(self):
        body = ready_body().replace("Offline tests passed.", "```sh\njust test\n```")
        self.assertEqual(validate(body), [])
        before, checklist = ready_body().split("## Checklist\n")
        self.assertTrue(validate(before + "## Checklist\n```\n" + checklist + "\n```"))

    def test_null_or_empty_body(self):
        self.assertTrue(validate(None))
        self.assertTrue(validate(""))

    def test_heading_in_blockquote_is_not_heading(self):
        self.assertTrue(validate(ready_body().replace("## Summary", "> ## Summary")))


if __name__ == "__main__":
    unittest.main()
