"""Check the advertised README example and version/license metadata offline."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
executable = sys.argv[1]
build = Path(sys.argv[2])
header = (ROOT / "include/trytr/trytr.h").read_text()
version = re.search(r'#define TRYTR_VERSION "([^"]+)"', header).group(1)
actual = subprocess.check_output([executable, "--version"], text=True).strip()
assert actual == f"trytr {version}", actual
assert f'"version": "{version}"' in (ROOT / "mcp/trytr_mcp.py").read_text()
assert f'version: "{version}"' in (ROOT / "mcp/ui/preview.html").read_text()
assert f'set(CPACK_PACKAGE_VERSION "{version}")' in (build / "CPackConfig.cmake").read_text()
license_text = (ROOT / "LICENSE").read_text()
assert "Apache License" in license_text and "Version 2.0, January 2004" in license_text
assert "END OF TERMS AND CONDITIONS" in license_text and len(license_text) > 10000
assert "flawme and Trytr contributors" in (ROOT / "NOTICE").read_text()

readme = (ROOT / "README.md").read_text()
example = re.search(r"```trytr\n(.*?)\n```", readme, re.S).group(1)
outputs = []
for override in ([], ["--set", "rate=3"]):
    result = subprocess.run([executable, *override, "--render-html", "-"],
                            input=example, capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stderr
    assert '<math xmlns=' in result.stdout and "<svg" in result.stdout
    assert "<script" not in result.stdout and "<img" not in result.stdout
    outputs.append(result.stdout)
assert outputs[0] != outputs[1], "README parameter override did not affect rendering"
for link in re.findall(r"\]\(([^)]+)\)", readme):
    if not link.startswith(("https://", "http://", "#")):
        assert (ROOT / link.split("#", 1)[0]).exists(), link
print(f"README example, licenses, and {version} metadata passed")
