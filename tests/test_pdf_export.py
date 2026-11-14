#!/usr/bin/env python3
"""End-to-end checks for Trytr's optional vector PDF export."""

from __future__ import annotations

import re
import pathlib
import subprocess
import sys
import tempfile
import zlib


def run(*arguments: str, input_bytes: bytes | None = None) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        arguments,
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=45,
    )


def require_pdf(result: subprocess.CompletedProcess[bytes], label: str) -> bytes:
    assert result.returncode == 0, (
        f"{label} failed ({result.returncode}): "
        f"{result.stderr.decode(errors='replace')}"
    )
    data = result.stdout
    assert data.startswith(b"%PDF-"), f"{label} did not write a PDF header"
    assert b"%%EOF" in data[-1024:], f"{label} did not write a complete PDF trailer"
    assert len(data) > 1000, f"{label} produced an unexpectedly small PDF"
    return data


def page_content_stream(data: bytes) -> bytes:
    """Return the first decompressed vector page stream, skipping embedded fonts."""
    for match in re.finditer(rb"stream\r?\n(.*?)\r?\nendstream", data, re.DOTALL):
        try:
            decoded = zlib.decompress(match.group(1))
        except zlib.error:
            continue
        if b" cm\n" in decoded and b"BT\n" in decoded:
            return decoded
    raise AssertionError("PDF has no readable page content stream")


def main() -> None:
    executable = sys.argv[1]
    proof_source = pathlib.Path(sys.argv[2])
    parameter_source = pathlib.Path(sys.argv[3])

    with tempfile.TemporaryDirectory(prefix="trytr-pdf-test-") as directory:
        spaced_source = pathlib.Path(directory) / "proof with spaces.trytr"
        spaced_source.write_bytes(proof_source.read_bytes())

        file_pdf = require_pdf(
            run(executable, "--render-pdf", str(spaced_source)), "file input export"
        )
        stdin_pdf = require_pdf(
            run(
                executable,
                "--render-pdf",
                "-",
                input_bytes=proof_source.read_bytes(),
            ),
            "stdin export",
        )
        assert page_content_stream(file_pdf) == page_content_stream(stdin_pdf), (
            "file and stdin rendering differ unexpectedly"
        )

        default_pdf = require_pdf(
            run(executable, "--render-pdf", str(parameter_source)),
            "default parameter export",
        )
        changed_pdf = require_pdf(
            run(executable, "--set", "peak=38", "--render-pdf", str(parameter_source)),
            "parameter override export",
        )
        assert page_content_stream(default_pdf) != page_content_stream(changed_pdf), (
            "parameter override did not change the rendered PDF page"
        )

        invalid_source = pathlib.Path(directory) / "invalid.trytr"
        invalid_source.write_text("@document(version: 1) { @not-a-feature }\n", encoding="utf-8")
        invalid = run(executable, "--render-pdf", str(invalid_source))
        assert invalid.returncode != 0, "invalid source unexpectedly compiled"
        assert not invalid.stdout, "invalid source emitted partial output"

        conflicting_modes = run(
            executable,
            "--render-pdf",
            "--render-svg",
            str(proof_source),
        )
        assert conflicting_modes.returncode == 2, "conflicting output modes were accepted"
        assert not conflicting_modes.stdout, "conflicting modes wrote partial output"


if __name__ == "__main__":
    main()
