"""MCP support for the standalone Trytr polynomial identity checker."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from typing import Any


MAX_PROOF_SOURCE_BYTES = 64 * 1024
PROOF_TIMEOUT_SECONDS = 5

REVIEW_TOOL = {
    "name": "review_trytr_math",
    "title": "Review Trytr algebra steps",
    "description": (
        "Check each equality in a LaTeX-like derivation as a polynomial identity "
        "over exact rational coefficients. Returns verified, not an identity, or "
        "unable to check per equation. This does not verify prose, assumptions, "
        "inequalities, limits, or a complete theorem; unsupported claims remain "
        "unchecked and require human or stronger proof review. Use one equation per "
        "line, optionally inside a LaTeX align environment."
    ),
    "inputSchema": {
        "type": "object",
        "properties": {
            "source": {
                "type": "string",
                "description": "Equation steps, one equality per line, up to 64 KiB.",
                "maxLength": MAX_PROOF_SOURCE_BYTES,
            }
        },
        "required": ["source"],
        "additionalProperties": False,
    },
    "annotations": {
        "readOnlyHint": True,
        "destructiveHint": False,
        "idempotentHint": True,
        "openWorldHint": False,
    },
    "outputSchema": {
        "type": "object",
        "properties": {
            "ok": {"type": "boolean"},
            "status": {"type": "string"},
            "message": {"type": "string"},
            "results": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "line": {"type": "integer"},
                        "status": {"type": "string"},
                        "message": {"type": "string"},
                    },
                    "required": ["line", "status", "message"],
                    "additionalProperties": False,
                },
            },
        },
        "required": ["ok", "status", "message", "results"],
        "additionalProperties": False,
    },
}


def resolve_proof_executable(explicit: str | None, root: Path) -> str | None:
    configured = explicit or os.environ.get("TRYTR_PROOF_EXECUTABLE")
    if configured:
        path = Path(configured).expanduser().resolve()
        if path.is_file() and os.access(path, os.X_OK):
            return str(path)
        raise FileNotFoundError(f"Trytr proof executable is not runnable: {path}")

    candidates = (
        root / "proof/build/trytr-proof",
        root / "build/debug/proof/trytr-proof",
        root / "build/release/proof/trytr-proof",
        root / "build/proof/trytr-proof",
    )
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return shutil.which("trytr-proof")


def review_math(source: Any, executable: str | None) -> dict[str, Any]:
    def failure(message: str) -> dict[str, Any]:
        result = {
            "ok": False,
            "status": "unable_to_check",
            "message": message,
            "results": [],
        }
        return {
            "content": [{"type": "text", "text": message}],
            "structuredContent": result,
            "isError": True,
        }

    if not isinstance(source, str):
        return failure("Math review source must be a string.")
    try:
        source_bytes = source.encode("utf-8", errors="strict")
    except UnicodeEncodeError:
        return failure("Math review source must contain valid Unicode text.")
    if len(source_bytes) > MAX_PROOF_SOURCE_BYTES:
        return failure("Math review source exceeds the 64 KiB input limit.")
    if b"\0" in source_bytes:
        return failure("Math review source must not contain NUL bytes.")
    if not source.strip():
        return failure("Math review requires at least one equation.")
    if executable is None:
        return failure(
            "The Trytr proof checker is unavailable. Build the proof module or set "
            "TRYTR_PROOF_EXECUTABLE."
        )

    try:
        with tempfile.TemporaryDirectory(prefix="trytr-math-review-") as directory:
            path = Path(directory) / "review.trmath"
            path.write_bytes(source_bytes)
            completed = subprocess.run(
                [executable, str(path)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=PROOF_TIMEOUT_SECONDS,
                check=False,
            )
    except subprocess.TimeoutExpired:
        return failure("Math review exceeded its 5 second time limit.")
    except OSError:
        return failure("The Trytr proof checker could not be started.")

    output = completed.stdout.decode("utf-8", errors="replace")
    diagnostics = completed.stderr.decode("utf-8", errors="replace")
    results: list[dict[str, Any]] = []
    for channel, default_status in ((output, "verified"), (diagnostics, "unable_to_check")):
        for row in channel.splitlines():
            match = re.match(r"^.*?:(\d+):\s*(.*)$", row)
            if match is None:
                continue
            message = match.group(2)
            status = default_status
            for prefix, candidate in (
                ("not verified:", "not_an_identity"),
                ("unable to check:", "unable_to_check"),
            ):
                if message.startswith(prefix):
                    status = candidate
                    message = message[len(prefix):].strip()
                    break
            results.append({"line": int(match.group(1)), "status": status, "message": message})
    results.sort(key=lambda result: result["line"])

    if not results:
        return failure("No checkable equation rows were found.")
    statuses = {result["status"] for result in results}
    if statuses == {"verified"}:
        overall = "verified"
        message = f"Verified {len(results)} polynomial equation(s)."
        ok = True
    elif "not_an_identity" in statuses:
        overall = "needs_review"
        message = "At least one equation is not a polynomial identity."
        ok = False
    else:
        overall = "partially_checked"
        message = "Some equation rows could not be checked by the supported polynomial checker."
        ok = False
    result = {"ok": ok, "status": overall, "message": message, "results": results}
    content_text = "\n".join(
        [message]
        + [f"Line {row['line']} ({row['status']}): {row['message']}" for row in results]
    )
    return {
        "content": [{"type": "text", "text": content_text}],
        "structuredContent": result,
        "isError": False,
    }
