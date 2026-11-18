"""A failing browser must be drained without leaking unbounded logs or stdout."""
import subprocess
import sys
import time

completed = subprocess.run([sys.argv[1]], capture_output=True, timeout=5)
assert completed.returncode == 0, completed.stderr
assert completed.stdout == b"", completed.stdout
prefix = b"trytr: Chromium PDF export failed:\n"
assert completed.stderr.startswith(prefix + b"browser setup failed: test diagnostic\n")
assert len(completed.stderr) == len(prefix) + 4096, len(completed.stderr)

for path in ("/hang", "/descendant", "/flood"):
    start = time.monotonic()
    result = subprocess.run([sys.argv[1], path], capture_output=True, timeout=2)
    assert result.returncode == 0, (path, result.stderr)
    assert result.stdout == b"", path
    assert time.monotonic() - start < 1.5, path
    assert len(result.stderr) <= len(prefix) + 4096 + 50, path
    if path != "/descendant":
        assert b"timed out" in result.stderr, (path, result.stderr)
