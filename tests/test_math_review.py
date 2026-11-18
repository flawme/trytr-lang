"""Exercise the public MCP review adapter with the real checker executable."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "mcp"))
from proof_review import review_math

CHECKER = sys.argv.pop(1)


class RationalReviewTests(unittest.TestCase):
    def test_conditional_identity_never_becomes_an_unconditional_proof(self):
        result = review_math("(x^2-1)/(x-1)=x+1\nx/x=2\nx=x\n", CHECKER)
        payload = result["structuredContent"]
        self.assertFalse(result["isError"])
        self.assertFalse(payload["ok"])
        self.assertEqual("needs_review", payload["status"])
        self.assertEqual(["conditional", "not_an_identity", "verified"],
                         [row["status"] for row in payload["results"]])
        self.assertIn("(x-1)", payload["results"][0]["message"])
        self.assertIn("!= 0", payload["results"][0]["message"])

    def test_only_conditional_claims_still_require_review(self):
        payload = review_math("0/x=0\n1/(1/x)=x", CHECKER)["structuredContent"]
        self.assertFalse(payload["ok"])
        self.assertEqual("partially_checked", payload["status"])
        self.assertEqual(["conditional", "conditional"], [row["status"] for row in payload["results"]])

    def test_environment_prefix_cannot_hide_an_unchecked_claim(self):
        for wrapper in (r"\begin{align*} x=x+1", r"\end{align*} x=x+1", r"\begin{alignment} x=x+1"):
            with self.subTest(wrapper=wrapper):
                payload = review_math(wrapper + "\nx=x\n", CHECKER)["structuredContent"]
                self.assertFalse(payload["ok"])
                self.assertEqual(["unable_to_check", "verified"], [row["status"] for row in payload["results"]])

    def test_exact_align_wrappers_remain_accepted(self):
        payload = review_math("\\begin{align*}\nx &= x\\\\\n\\end{align*}", CHECKER)["structuredContent"]
        self.assertTrue(payload["ok"])
        self.assertEqual(["verified"], [row["status"] for row in payload["results"]])

    def test_cli_returns_two_for_conditions_and_one_for_a_false_identity(self):
        for source, expected in (("x/x=1", 2), ("x/x=2", 1), ("x=x", 0), ("0/0=0", 2)):
            with self.subTest(source=source), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "claim.trmath"
                path.write_text(source)
                completed = subprocess.run([CHECKER, str(path)], capture_output=True, text=True, timeout=5)
                self.assertEqual(expected, completed.returncode, completed.stderr)


if __name__ == "__main__":
    unittest.main()
