"""A failing browser must be drained without leaking unbounded logs or stdout."""
import subprocess
import sys

completed = subprocess.run([sys.argv[1]], capture_output=True, timeout=5)
assert completed.returncode == 0, completed.stderr
assert completed.stdout == b"", completed.stdout
prefix = b"trytr: Chromium PDF export failed:\n"
assert completed.stderr.startswith(prefix + b"browser setup failed: test diagnostic\n")
assert len(completed.stderr) == len(prefix) + 4096, len(completed.stderr)
