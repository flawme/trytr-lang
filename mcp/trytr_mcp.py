#!/usr/bin/env python3
"""Expose Trytr rendering and its optional embedded MCP Apps view."""

from __future__ import annotations

import argparse
from collections import OrderedDict
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import time
from typing import Any

from proof_review import (
    CHECK_CERTIFICATE_TOOL,
    REVIEW_TOOL,
    check_certificate,
    review_math,
    resolve_proof_executable,
)


ROOT = Path(__file__).resolve().parents[1]
MODERN_VERSION = "2026-07-28"
LEGACY_VERSIONS = {
    "2025-11-25",
    "2025-06-18",
    "2025-03-26",
    "2024-11-05",
}
MAX_SOURCE_BYTES = 1024 * 1024
MAX_OUTPUT_BYTES = 2 * 1024 * 1024
MAX_MESSAGE_BYTES = 8 * MAX_SOURCE_BYTES + 64 * 1024
MAX_DIAGNOSTIC_BYTES = 16 * 1024
MAX_PARAMETER_COUNT = 128
RENDER_TIMEOUT_SECONDS = 10
UI_EXTENSION = "io.modelcontextprotocol/ui"
UI_MIME_TYPE = "text/html;profile=mcp-app"
UI_RESOURCE_URI = "ui://trytr/preview-v1"
UI_RESOURCE = ROOT / "mcp" / "ui" / "preview.html"
MAX_CACHED_PREVIEWS = 4
PREVIEW_TTL_SECONDS = 10 * 60
PREVIEW_CACHE: OrderedDict[str, tuple[float, tuple[str, str]]] = OrderedDict()
SERVER_STARTED_AT = time.monotonic()
MAX_REQUEST_LOG_BYTES = 256 * 1024
MAX_LOG_READ_ENTRIES = 100

SERVER_INFO = {"name": "trytr-renderer", "version": "0.1.0-alpha.1"}
SERVER_INSTRUCTIONS = (
    "Render vector documents written in Trytr. @math expressions are typeset as "
    "native MathML in the embedded preview. Prefer render_trytr for a smooth, "
    "code-rendered vector preview when the client supports MCP Apps. Use "
    "render_trytr_text as the fallback for clients without MCP Apps; text characters "
    "occupy a discrete grid and cannot show a mathematically continuous curve. "
    "Plots accept either decimal data points or y = scalar-expression blocks with "
    "bounded x ranges and sample counts. Scalar functions are available in @expr "
    "and formula plots. Declared @param values are returned as preview controls; "
    "pass integer overrides in the parameters object to re-render the document. "
    "Both modes validate and render locally after receiving the complete source. "
    "review_trytr_math checks polynomial and conditional rational identities; it does not prove "
    "prose, assumptions, or unsupported mathematics. check_trytr_certificate runs "
    "the small built-in checker for supported formal certificate formats; it is "
    "not a general theorem prover. Use health_check and read_server_log to "
    "diagnose server availability and recent request handling; request logs never "
    "include document or tool argument contents."
)
SERVER_META = {"io.modelcontextprotocol/serverInfo": SERVER_INFO}

TOOLS = [
    {
        "name": "render_trytr",
        "title": "Render Trytr vector document",
        "description": (
            "Preferred graph and drawing renderer. Validate the complete Trytr source and "
            "render it as a smooth, scalable vector in the embedded Trytr Preview when "
            "the chat client supports MCP Apps. The final geometry is drawn by Trytr "
            "code after generation, so model token streaming does not create jagged "
            "intermediate strokes. @math expressions are typeset as native MathML "
            "beside the vector preview. Clients without MCP Apps receive a text-grid fallback. "
            "This is a local vector view, not image generation. "
            "Declared @param values appear as controls in MCP Apps previews and can "
            "be overridden with the optional parameters object. Formula plots use "
            "y = expression blocks with x-min, x-max, and samples options; the "
            "baseline is refined where sampled curvature exceeds half a screen pixel. "
            "On invalid source, return parser or validation diagnostics."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "source": {
                    "type": "string",
                    "description": "A complete Trytr document beginning with @document.",
                    "maxLength": MAX_SOURCE_BYTES,
                },
                "parameters": {
                    "type": "object",
                    "description": "Optional values for declared interactive parameters.",
                    "maxProperties": MAX_PARAMETER_COUNT,
                    "additionalProperties": {"type": "integer"},
                },
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
                "previewId": {"type": "string"},
                "message": {"type": "string"},
                "parameters": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "id": {"type": "string"},
                            "label": {"type": "string"},
                            "min": {"type": "integer"},
                            "max": {"type": "integer"},
                            "default": {"type": "integer"},
                            "value": {"type": "integer"},
                            "step": {"type": "integer"},
                        },
                        "required": ["id", "label", "min", "max", "default", "value", "step"],
                        "additionalProperties": False,
                    },
                },
            },
            "required": ["ok", "message"],
            "additionalProperties": False,
        },
    },
    {
        "name": "render_trytr_text",
        "title": "Render Trytr as inline text",
        "description": (
            "Fallback for chat clients that do not support MCP Apps. Validate Trytr source "
            "and return its text blocks and a character-grid drawing directly in chat. "
            "When called by an MCP Apps client, open the same vector and native-math preview "
            "as render_trytr so the final graph stays smooth. Use this when literal "
            "plain-text output is needed or the client has no embedded preview."
            " Parameter values can be supplied in the optional parameters object."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "source": {
                    "type": "string",
                    "description": "A complete Trytr document beginning with @document.",
                    "maxLength": MAX_SOURCE_BYTES,
                },
                "parameters": {
                    "type": "object",
                    "description": "Optional values for declared interactive parameters.",
                    "maxProperties": MAX_PARAMETER_COUNT,
                    "additionalProperties": {"type": "integer"},
                },
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
                "previewId": {"type": "string"},
                "message": {"type": "string"},
                "parameters": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "id": {"type": "string"},
                            "label": {"type": "string"},
                            "min": {"type": "integer"},
                            "max": {"type": "integer"},
                            "default": {"type": "integer"},
                            "value": {"type": "integer"},
                            "step": {"type": "integer"},
                        },
                        "required": ["id", "label", "min", "max", "default", "value", "step"],
                        "additionalProperties": False,
                    },
                },
            },
            "required": ["ok", "message"],
            "additionalProperties": False,
        },
    }
]
TOOLS.append(REVIEW_TOOL)
TOOLS.append(CHECK_CERTIFICATE_TOOL)
TOOLS.extend([
    {
        "name": "health_check",
        "title": "Check Trytr MCP health",
        "description": (
            "Return server protocol version, process ID, uptime, project and state paths, "
            "and whether the renderer and proof checker are available."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {},
            "additionalProperties": False,
        },
        "outputSchema": {
            "type": "object",
            "properties": {
                "ok": {"type": "boolean"},
                "protocolVersion": {
                    "type": "integer",
                    "description": "Health payload schema version, currently 1.",
                },
                "serverPid": {"type": "integer"},
                "projectRoot": {"type": "string"},
                "stateDirectory": {"type": "string"},
                "uptimeMs": {"type": "integer"},
                "rendererAvailable": {"type": "boolean"},
                "proofCheckerAvailable": {"type": "boolean"},
            },
            "required": [
                "ok", "protocolVersion", "serverPid", "projectRoot", "stateDirectory",
                "uptimeMs", "rendererAvailable", "proofCheckerAvailable",
            ],
            "additionalProperties": False,
        },
        "annotations": {
            "readOnlyHint": True,
            "destructiveHint": False,
            "idempotentHint": True,
            "openWorldHint": False,
        },
    },
    {
        "name": "read_server_log",
        "title": "Read recent Trytr MCP requests",
        "description": (
            "Read recent request IDs, method/tool names, project, durations, and safe error "
            "categories. Logs never contain source documents or tool argument values."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {"limit": {"type": "integer", "minimum": 1, "maximum": 100}},
            "additionalProperties": False,
        },
        "outputSchema": {
            "type": "object",
            "properties": {
                "ok": {"type": "boolean"},
                "entries": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "timestamp": {"type": "string"},
                            "requestId": {"type": "string"},
                            "method": {"type": "string"},
                            "tool": {"type": "string"},
                            "project": {"type": "string"},
                            "durationMs": {"type": "number"},
                            "error": {
                                "anyOf": [
                                    {
                                        "type": "object",
                                        "properties": {
                                            "type": {"type": "string"},
                                            "code": {
                                                "anyOf": [
                                                    {"type": "integer"},
                                                    {"type": "null"},
                                                ]
                                            },
                                            "status": {"type": "string"},
                                        },
                                        "required": ["type", "code", "status"],
                                        "additionalProperties": False,
                                    },
                                    {"type": "null"},
                                ]
                            },
                        },
                        "required": [
                            "timestamp", "requestId", "method", "tool", "project",
                            "durationMs", "error",
                        ],
                        "additionalProperties": False,
                    },
                },
            },
            "required": ["ok", "entries"],
            "additionalProperties": False,
        },
        "annotations": {
            "readOnlyHint": True,
            "destructiveHint": False,
            "idempotentHint": True,
            "openWorldHint": False,
        },
    },
])
APP_TOOLS = [
    {
        "name": "get_trytr_preview",
        "title": "Load rendered Trytr preview",
            "description": "Load vector and native MathML data for a recent successful render into the embedded Trytr Preview.",
        "inputSchema": {
            "type": "object",
            "properties": {"previewId": {"type": "string", "maxLength": 64}},
            "required": ["previewId"],
            "additionalProperties": False,
        },
        "outputSchema": {
            "type": "object",
            "properties": {
                "ok": {"type": "boolean"},
                "message": {"type": "string"},
                "mathml": {"type": "string"},
            },
            "required": ["ok", "message"],
            "additionalProperties": False,
        },
        "annotations": {
            "readOnlyHint": True,
            "destructiveHint": False,
            "idempotentHint": True,
            "openWorldHint": False,
        },
        "_meta": {"ui": {"visibility": ["app"]}},
    }
]


def _response(request_id: Any, result: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def _error(request_id: Any, code: int, message: str, data: Any = None) -> dict[str, Any]:
    error: dict[str, Any] = {"code": code, "message": message}
    if data is not None:
        error["data"] = data
    return {"jsonrpc": "2.0", "id": request_id, "error": error}


def _modern_result(payload: dict[str, Any]) -> dict[str, Any]:
    return {"resultType": "complete", **payload, "_meta": SERVER_META}


def _state_directory() -> Path:
    configured = os.environ.get("TRYTR_MCP_STATE_DIR")
    if configured:
        return Path(configured).expanduser().resolve()
    state_root = os.environ.get("XDG_STATE_HOME")
    if state_root:
        return (Path(state_root).expanduser() / "trytr-mcp").resolve()
    return (Path.home() / ".local" / "state" / "trytr-mcp").resolve()


def _request_log_path() -> Path:
    return _state_directory() / "requests.jsonl"


def _safe_log_text(value: str, limit: int) -> str:
    return value.encode("utf-8", errors="replace").decode("utf-8")[:limit]


def _append_request_log(record: dict[str, Any]) -> None:
    path = _request_log_path()
    try:
        path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        line = json.dumps(record, ensure_ascii=True, separators=(",", ":")).encode("ascii") + b"\n"
        if len(line) > 4096:
            return
        if path.exists() and path.stat().st_size + len(line) > MAX_REQUEST_LOG_BYTES:
            rotated = path.with_suffix(".jsonl.1")
            rotated.unlink(missing_ok=True)
            os.replace(path, rotated)
        flags = os.O_WRONLY | os.O_CREAT | os.O_APPEND | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags, 0o600)
        if hasattr(os, "fchmod"):
            os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "ab") as log_file:
            log_file.write(line)
    except OSError:
        # Logging must never prevent a tool response from reaching its client.
        return


def _read_request_log(limit: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for path in (_request_log_path().with_suffix(".jsonl.1"), _request_log_path()):
        try:
            if path.is_symlink() or path.stat().st_size > MAX_REQUEST_LOG_BYTES + 4096:
                continue
            lines = path.read_bytes().splitlines()
        except OSError:
            continue
        for line in lines:
            if len(line) > 4096:
                continue
            try:
                entry = json.loads(line)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue
            if not isinstance(entry, dict):
                continue
            if not all(
                isinstance(entry.get(field), str)
                for field in ("timestamp", "requestId", "method", "tool", "project")
            ):
                continue
            duration = entry.get("durationMs")
            if (
                not isinstance(duration, (int, float))
                or isinstance(duration, bool)
                or not math.isfinite(duration)
            ):
                continue
            error = entry.get("error")
            if error is not None:
                if not isinstance(error, dict):
                    continue
                if (
                    not isinstance(error.get("type"), str)
                    or (
                        error.get("code") is not None
                        and (
                            not isinstance(error.get("code"), int)
                            or isinstance(error.get("code"), bool)
                        )
                    )
                    or not isinstance(error.get("status"), str)
                ):
                    continue
                error = {
                    "type": _safe_log_text(error["type"], 32),
                    "code": error.get("code"),
                    "status": _safe_log_text(error["status"], 64),
                }
            rows.append({
                "timestamp": _safe_log_text(entry["timestamp"], 64),
                "requestId": _safe_log_text(entry["requestId"], 128),
                "method": _safe_log_text(entry["method"], 128),
                "tool": _safe_log_text(entry["tool"], 128),
                "project": _safe_log_text(entry["project"], 512),
                "durationMs": float(duration),
                "error": error,
            })
    return rows[-limit:]


def _health_result(executable: str, proof_executable: str | None) -> dict[str, Any]:
    state_directory = _state_directory()
    payload = {
        "ok": True,
        "protocolVersion": 1,
        "serverPid": os.getpid(),
        "projectRoot": str(ROOT),
        "stateDirectory": str(state_directory),
        "uptimeMs": max(0, int((time.monotonic() - SERVER_STARTED_AT) * 1000)),
        "rendererAvailable": Path(executable).is_file() and os.access(executable, os.X_OK),
        "proofCheckerAvailable": (
            proof_executable is not None
            and Path(proof_executable).is_file()
            and os.access(proof_executable, os.X_OK)
        ),
    }
    return {
        "content": [{"type": "text", "text": "Trytr MCP process is healthy."}],
        "structuredContent": payload,
        "isError": False,
    }


def _server_log_result(limit: int) -> dict[str, Any]:
    entries = _read_request_log(limit)
    payload = {"ok": True, "entries": entries}
    return {
        "content": [{"type": "text", "text": json.dumps(entries, ensure_ascii=False)}],
        "structuredContent": payload,
        "isError": False,
    }


def _request_log_record(
    message: Any,
    response: dict[str, Any] | None,
    duration_ms: float,
) -> dict[str, Any]:
    request_id = message.get("id") if isinstance(message, dict) else None
    method = message.get("method") if isinstance(message, dict) else None
    params = message.get("params") if isinstance(message, dict) else None
    tool = params.get("name") if method == "tools/call" and isinstance(params, dict) else ""
    error: dict[str, Any] | None = None
    if isinstance(response, dict) and isinstance(response.get("error"), dict):
        jsonrpc_error = response["error"]
        code = jsonrpc_error.get("code")
        error = {
            "type": "jsonrpc",
            "code": code if isinstance(code, int) and not isinstance(code, bool) else None,
            "status": "request_error",
        }
    elif isinstance(response, dict):
        result = response.get("result")
        if isinstance(result, dict) and result.get("isError") is True:
            structured = result.get("structuredContent")
            status = structured.get("status") if isinstance(structured, dict) else "tool_error"
            if not isinstance(status, str):
                status = "tool_error"
            error = {"type": "tool", "code": None, "status": status[:64]}

    return {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "requestId": _safe_log_text(str(request_id), 128) if request_id is not None else "",
        "method": _safe_log_text(method, 128) if isinstance(method, str) else "",
        "tool": _safe_log_text(tool, 128) if isinstance(tool, str) else "",
        "project": str(ROOT),
        "durationMs": round(max(duration_ms, 0.0), 3),
        "error": error,
    }


def _supports_apps(capabilities: Any) -> bool:
    if not isinstance(capabilities, dict):
        return False
    extensions = capabilities.get("extensions")
    if not isinstance(extensions, dict):
        return False
    app_capability = extensions.get(UI_EXTENSION)
    mime_types = app_capability.get("mimeTypes") if isinstance(app_capability, dict) else None
    return (
        isinstance(mime_types, list)
        and UI_MIME_TYPE in mime_types
    )


def _tools_for_client(apps_supported: bool) -> list[dict[str, Any]]:
    tools = json.loads(json.dumps(TOOLS))
    if apps_supported:
        tools[0]["_meta"] = {
            "ui": {
                "resourceUri": UI_RESOURCE_URI,
                "visibility": ["model", "app"],
            }
        }
        tools[1]["_meta"] = {
            "ui": {
                "resourceUri": UI_RESOURCE_URI,
                "visibility": ["model", "app"],
            }
        }
        tools.extend(json.loads(json.dumps(APP_TOOLS)))
    return tools


def _store_preview(svg: str, mathml: str) -> str:
    now = time.monotonic()
    expired = [
        preview_id
        for preview_id, (created_at, _) in PREVIEW_CACHE.items()
        if now - created_at > PREVIEW_TTL_SECONDS
    ]
    for preview_id in expired:
        del PREVIEW_CACHE[preview_id]
    while len(PREVIEW_CACHE) >= MAX_CACHED_PREVIEWS:
        PREVIEW_CACHE.popitem(last=False)
    preview_id = secrets.token_urlsafe(18)
    PREVIEW_CACHE[preview_id] = (now, (svg, mathml))
    return preview_id


def _read_preview(preview_id: str) -> dict[str, Any]:
    cached = PREVIEW_CACHE.get(preview_id)
    if cached is not None and time.monotonic() - cached[0] > PREVIEW_TTL_SECONDS:
        del PREVIEW_CACHE[preview_id]
        cached = None
    if cached is None:
        return {
            "content": [{"type": "text", "text": "Preview expired or was not found. Render the Trytr source again."}],
            "structuredContent": {
                "ok": False,
                "message": "Preview expired or was not found. Render the Trytr source again.",
            },
            "isError": True,
        }
    _, preview = cached
    svg, mathml = preview
    return {
        "content": [{"type": "text", "text": svg}],
        "structuredContent": {
            "ok": True,
            "message": "Trytr preview data loaded.",
            "mathml": mathml,
        },
        "isError": False,
    }


def _ui_resource() -> dict[str, Any]:
    try:
        html = UI_RESOURCE.read_text(encoding="utf-8")
    except OSError:
        html = (
            "<!doctype html><html><body><p>Trytr Preview resource is unavailable. "
            "Reinstall or restore mcp/ui/preview.html.</p></body></html>"
        )
    return {
        "contents": [{
            "uri": UI_RESOURCE_URI,
            "mimeType": UI_MIME_TYPE,
            "text": html,
            "_meta": {"ui": {"csp": {}, "prefersBorder": True}},
        }]
    }


def _check_modern_meta(
    request_id: Any, params: dict[str, Any]
) -> tuple[dict[str, Any] | None, dict[str, Any] | None]:
    meta = params.get("_meta")
    if not isinstance(meta, dict):
        return None, _error(request_id, -32602, "Missing required MCP request metadata")

    version = meta.get("io.modelcontextprotocol/protocolVersion")
    capabilities = meta.get("io.modelcontextprotocol/clientCapabilities")
    if not isinstance(version, str) or not isinstance(capabilities, dict):
        return None, _error(request_id, -32602, "Invalid MCP request metadata")
    if version != MODERN_VERSION:
        return None, _error(
            request_id,
            -32022,
            "Unsupported protocol version",
            {"supported": [MODERN_VERSION], "requested": version},
        )
    return meta, None


def _render_document(
    source: str, executable: str, apps_supported: bool,
    parameter_values: dict[str, int] | None = None,
) -> dict[str, Any]:
    # Apps clients can show vectors even when a model selected the text tool.
    # Every non-App client gets useful inline text from either render tool.
    render_format = "svg" if apps_supported else "text"
    try:
        source_bytes = source.encode("utf-8", errors="strict")
    except UnicodeEncodeError:
        return {
            "content": [{"type": "text", "text": "Trytr source must contain valid Unicode text."}],
            "structuredContent": {"ok": False, "message": "Trytr source must contain valid Unicode text."},
            "isError": True,
        }
    if len(source_bytes) > MAX_SOURCE_BYTES:
        return {
            "content": [{"type": "text", "text": "Trytr source exceeds the 1 MiB input limit."}],
            "structuredContent": {"ok": False, "message": "Trytr source exceeds the 1 MiB input limit."},
            "isError": True,
        }

    parameter_values = parameter_values or {}
    if len(parameter_values) > MAX_PARAMETER_COUNT or any(
        not isinstance(parameter_id, str)
        or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_-]*", parameter_id) is None
        or not isinstance(value, int)
        or isinstance(value, bool)
        or value < -(2**63)
        or value > 2**63 - 1
        for parameter_id, value in parameter_values.items()
    ):
        message = "Parameter overrides must be a bounded object of integer values keyed by declared IDs."
        return {
            "content": [{"type": "text", "text": message}],
            "structuredContent": {"ok": False, "message": message},
            "isError": True,
        }

    override_args = [argument for parameter_id, value in parameter_values.items()
                     for argument in ("--set", f"{parameter_id}={value}")]
    try:
        metadata = subprocess.run(
            [executable, *override_args, "--dump-parameters", "-"],
            input=source_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=RENDER_TIMEOUT_SECONDS,
            check=False,
        )
    except subprocess.TimeoutExpired:
        metadata = None
    except OSError:
        metadata = None
    if metadata is None:
        message = "Trytr parameter metadata could not be loaded."
        return {
            "content": [{"type": "text", "text": message}],
            "structuredContent": {"ok": False, "message": message},
            "isError": True,
        }
    if metadata.returncode != 0:
        diagnostic = metadata.stderr[:MAX_DIAGNOSTIC_BYTES].decode(
            "utf-8", errors="replace"
        ).strip()
        message = diagnostic or "Trytr rejected the document while reading its parameters."
        return {
            "content": [{"type": "text", "text": message}],
            "structuredContent": {"ok": False, "message": message},
            "isError": True,
        }
    try:
        parameters = json.loads(metadata.stdout.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        parameters = None
    if (not isinstance(parameters, list) or len(parameters) > MAX_PARAMETER_COUNT
            or any(not isinstance(parameter, dict) for parameter in parameters)):
        message = "Trytr returned invalid parameter metadata."
        return {
            "content": [{"type": "text", "text": message}],
            "structuredContent": {"ok": False, "message": message},
            "isError": True,
        }

    try:
        render_option = (
            "--render-text" if render_format == "text" else
            "--render-svg-no-math" if apps_supported else
            "--render-svg"
        )
        completed = subprocess.run(
            [executable, *override_args, render_option, "-"],
            input=source_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=RENDER_TIMEOUT_SECONDS,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return {
            "content": [{"type": "text", "text": "Trytr rendering exceeded its 10 second limit."}],
            "structuredContent": {"ok": False, "message": "Trytr rendering exceeded its 10 second limit."},
            "isError": True,
        }
    except OSError:
        return {
            "content": [{
                "type": "text",
                "text": "The Trytr executable could not be started. Build the project or set TRYTR_EXECUTABLE.",
            }],
            "structuredContent": {
                "ok": False,
                "message": "The Trytr executable could not be started. Build the project or set TRYTR_EXECUTABLE.",
            },
            "isError": True,
        }

    if completed.returncode != 0:
        diagnostic = completed.stderr[:MAX_DIAGNOSTIC_BYTES].decode("utf-8", errors="replace").strip()
        return {
            "content": [{
                "type": "text",
                "text": diagnostic or "Trytr rejected the document without a diagnostic.",
            }],
            "structuredContent": {
                "ok": False,
                "message": diagnostic or "Trytr rejected the document without a diagnostic.",
            },
            "isError": True,
        }
    if len(completed.stdout) > MAX_OUTPUT_BYTES:
        return {
            "content": [{
                "type": "text",
                "text": f"Trytr text output exceeds the 2 MiB MCP content limit."
                if render_format == "text" else
                "Trytr returned invalid SVG or the preview exceeds the 2 MiB MCP content limit.",
            }],
            "structuredContent": {
                "ok": False,
                "message": "Trytr text output exceeds the 2 MiB MCP content limit."
                if render_format == "text" else
                "Trytr returned invalid SVG or the preview exceeds the 2 MiB MCP content limit.",
            },
            "isError": True,
        }

    if render_format == "text":
        rendered_text = completed.stdout.decode("utf-8", errors="replace")
        return {
            "content": [{"type": "text", "text": rendered_text}],
            "structuredContent": {
                "ok": True,
                "message": "Trytr text rendered successfully.",
                "parameters": parameters,
            },
            "isError": False,
        }

    if (
        not completed.stdout.startswith(b"<svg ")
        or not completed.stdout.rstrip().endswith(b"</svg>")
    ):
        return {
            "content": [{
                "type": "text",
                "text": "Trytr returned invalid SVG.",
            }],
            "structuredContent": {"ok": False, "message": "Trytr returned invalid SVG."},
            "isError": True,
        }

    svg = completed.stdout.decode("utf-8")
    mathml = ""
    if apps_supported:
        try:
            math_completed = subprocess.run(
                [executable, *override_args, "--render-mathml", "-"],
                input=source_bytes,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=RENDER_TIMEOUT_SECONDS,
                check=False,
            )
        except subprocess.TimeoutExpired:
            message = "Trytr math rendering exceeded its 10 second limit."
            return {
                "content": [{"type": "text", "text": message}],
                "structuredContent": {"ok": False, "message": message},
                "isError": True,
            }
        except OSError:
            message = "The Trytr executable could not render native math."
            return {
                "content": [{"type": "text", "text": message}],
                "structuredContent": {"ok": False, "message": message},
                "isError": True,
            }
        if math_completed.returncode != 0:
            diagnostic = math_completed.stderr[:MAX_DIAGNOSTIC_BYTES].decode(
                "utf-8", errors="replace"
            ).strip()
            message = diagnostic or "Trytr could not render the document's math expressions."
            return {
                "content": [{"type": "text", "text": message}],
                "structuredContent": {"ok": False, "message": message},
                "isError": True,
            }
        if len(math_completed.stdout) > MAX_OUTPUT_BYTES:
            message = "Trytr MathML exceeds the 2 MiB MCP content limit."
            return {
                "content": [{"type": "text", "text": message}],
                "structuredContent": {"ok": False, "message": message},
                "isError": True,
            }
        try:
            rendered_mathml = math_completed.stdout.decode("utf-8", errors="strict")
        except UnicodeDecodeError:
            rendered_mathml = None
        if rendered_mathml is None or (rendered_mathml and (
            not rendered_mathml.startswith('<math xmlns="http://www.w3.org/1998/Math/MathML"')
            or not rendered_mathml.endswith("</math>")
        )):
            message = "Trytr returned invalid MathML."
            return {
                "content": [{"type": "text", "text": message}],
                "structuredContent": {"ok": False, "message": message},
                "isError": True,
            }
        mathml = rendered_mathml
    structured_content: dict[str, Any] = {
        "ok": True,
        "message": "Trytr document rendered successfully.",
        "parameters": parameters,
    }
    content_text = "Trytr document rendered successfully."
    if apps_supported:
        preview_id = _store_preview(svg, mathml)
        structured_content["previewId"] = preview_id
        content_text += f" Preview ID: {preview_id}"
    return {
        "content": [
            {"type": "text", "text": content_text},
        ],
        "structuredContent": structured_content,
        "isError": False,
    }


def handle_request(
    message: Any,
    executable: str,
    proof_executable: str | None = None,
    legacy_initialized: bool = False,
    legacy_apps_supported: bool = False,
) -> dict[str, Any] | None:
    if not isinstance(message, dict) or message.get("jsonrpc") != "2.0":
        request_id = message.get("id") if isinstance(message, dict) else None
        return _error(request_id, -32600, "Invalid JSON-RPC request")

    method = message.get("method")
    request_id = message.get("id")
    if not isinstance(method, str):
        return _error(request_id, -32600, "Invalid JSON-RPC request")
    if "id" not in message:
        return None
    if not isinstance(request_id, (str, int)) or isinstance(request_id, bool):
        return _error(None, -32600, "Request id must be a string or integer")

    params = message.get("params", {})
    if not isinstance(params, dict):
        return _error(request_id, -32602, "Request params must be an object")

    if method == "server/discover":
        _, error = _check_modern_meta(request_id, params)
        if error is not None:
            return error
        return _response(
            request_id,
            _modern_result({
                "supportedVersions": [MODERN_VERSION],
                "capabilities": {
                    "tools": {},
                    "resources": {},
                    "extensions": {UI_EXTENSION: {"mimeTypes": [UI_MIME_TYPE]}},
                },
                "instructions": SERVER_INSTRUCTIONS,
                "ttlMs": 3_600_000,
                "cacheScope": "public",
            }),
        )

    if method == "initialize":
        requested_version = params.get("protocolVersion")
        if requested_version not in LEGACY_VERSIONS:
            return _error(request_id, -32602, "Unsupported legacy protocol version")
        return _response(request_id, {
            "protocolVersion": requested_version,
            "capabilities": {
                "tools": {"listChanged": False},
                "resources": {"listChanged": False},
                "extensions": {UI_EXTENSION: {"mimeTypes": [UI_MIME_TYPE]}},
            },
            "serverInfo": SERVER_INFO,
            "instructions": SERVER_INSTRUCTIONS,
        })

    if method.startswith("notifications/"):
        return None

    is_modern = "_meta" in params
    apps_supported = legacy_apps_supported
    if is_modern:
        request_meta, error = _check_modern_meta(request_id, params)
        if error is not None:
            return error
        apps_supported = _supports_apps(request_meta.get("io.modelcontextprotocol/clientCapabilities"))
    elif not legacy_initialized:
        return _error(request_id, -32600, "Legacy requests require the initialize handshake")

    if method == "ping" and not is_modern:
        return _response(request_id, {})
    if method == "tools/list":
        payload = {"tools": _tools_for_client(apps_supported)}
        if is_modern:
            payload.update({"ttlMs": 3_600_000, "cacheScope": "public"})
            return _response(request_id, _modern_result(payload))
        return _response(request_id, payload)
    if method == "resources/list":
        payload = {
            "resources": [{
                "uri": UI_RESOURCE_URI,
                "name": "Trytr Preview",
                "description": "Embedded chat view for successfully rendered Trytr documents.",
                "mimeType": UI_MIME_TYPE,
            }]
        }
        if is_modern:
            payload.update({"ttlMs": 3_600_000, "cacheScope": "public"})
            return _response(request_id, _modern_result(payload))
        return _response(request_id, payload)
    if method == "resources/read":
        if params.get("uri") != UI_RESOURCE_URI:
            return _error(request_id, -32602, "Unknown resource URI")
        resource = _ui_resource()
        if is_modern:
            return _response(request_id, _modern_result(resource))
        return _response(request_id, resource)
    if method == "tools/call":
        tool_name = params.get("name")
        if tool_name == "health_check":
            arguments = params.get("arguments", {})
            if not isinstance(arguments, dict) or arguments:
                return _error(request_id, -32602, "health_check accepts no arguments")
            result = _health_result(executable, proof_executable)
            if is_modern:
                return _response(request_id, _modern_result(result))
            return _response(request_id, result)
        if tool_name == "read_server_log":
            arguments = params.get("arguments", {})
            if not isinstance(arguments, dict) or set(arguments) - {"limit"}:
                return _error(request_id, -32602, "read_server_log accepts only an optional 'limit'")
            limit = arguments.get("limit", 50)
            if type(limit) is not int or not 1 <= limit <= MAX_LOG_READ_ENTRIES:
                return _error(request_id, -32602, "read_server_log limit must be an integer from 1 to 100")
            result = _server_log_result(limit)
            if is_modern:
                return _response(request_id, _modern_result(result))
            return _response(request_id, result)
        if tool_name == "review_trytr_math":
            arguments = params.get("arguments")
            if not isinstance(arguments, dict) or set(arguments) != {"source"}:
                return _error(
                    request_id,
                    -32602,
                    "review_trytr_math requires only a string 'source' argument",
                )
            result = review_math(arguments.get("source"), proof_executable)
            if is_modern:
                return _response(request_id, _modern_result(result))
            return _response(request_id, result)
        if tool_name == "check_trytr_certificate":
            arguments = params.get("arguments")
            if not isinstance(arguments, dict) or set(arguments) != {"kind", "source"}:
                return _error(
                    request_id,
                    -32602,
                    "check_trytr_certificate requires only 'kind' and 'source' arguments",
                )
            result = check_certificate(
                arguments.get("kind"), arguments.get("source"), proof_executable
            )
            if is_modern:
                return _response(request_id, _modern_result(result))
            return _response(request_id, result)
        if tool_name == "get_trytr_preview":
            if not apps_supported:
                return _error(request_id, -32602, "This MCP client did not negotiate MCP Apps support")
            arguments = params.get("arguments")
            if not isinstance(arguments, dict) or not isinstance(arguments.get("previewId"), str):
                return _error(request_id, -32602, "get_trytr_preview requires a string 'previewId' argument")
            result = _read_preview(arguments["previewId"])
            if is_modern:
                return _response(request_id, _modern_result(result))
            return _response(request_id, result)
        if tool_name not in ("render_trytr", "render_trytr_text"):
            return _error(request_id, -32602, "Unknown tool")
        arguments = params.get("arguments")
        if (not isinstance(arguments, dict)
                or set(arguments) - {"source", "parameters"}
                or not isinstance(arguments.get("source"), str)):
            return _error(request_id, -32602, f"{tool_name} requires a string 'source' argument")
        parameter_values = arguments.get("parameters", {})
        if not isinstance(parameter_values, dict):
            return _error(request_id, -32602, "'parameters' must be an object of integer values")
        result = _render_document(arguments["source"], executable, apps_supported,
                                  parameter_values)
        if not apps_supported:
            result.pop("structuredContent", None)
        if is_modern:
            return _response(request_id, _modern_result(result))
        return _response(request_id, result)

    return _error(request_id, -32601, "Method not found")


def _write_message(message: dict[str, Any]) -> None:
    encoded = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    sys.stdout.buffer.write(encoded + b"\n")
    sys.stdout.buffer.flush()


def serve(executable: str, proof_executable: str | None = None) -> int:
    legacy_initialized = False
    legacy_apps_supported = False
    while True:
        raw_line = sys.stdin.buffer.readline(MAX_MESSAGE_BYTES + 1)
        if not raw_line:
            return 0
        if len(raw_line) > MAX_MESSAGE_BYTES:
            while raw_line and not raw_line.endswith(b"\n"):
                raw_line = sys.stdin.buffer.readline(MAX_MESSAGE_BYTES + 1)
            _write_message(_error(None, -32700, "MCP message exceeds the input limit"))
            continue
        try:
            message = json.loads(raw_line)
        except (json.JSONDecodeError, UnicodeDecodeError):
            _write_message(_error(None, -32700, "Parse error"))
            continue
        method = message.get("method") if isinstance(message, dict) else None
        if method == "initialize":
            initialize_params = message.get("params", {})
            version = initialize_params.get("protocolVersion") if isinstance(initialize_params, dict) else None
            legacy_initialized = version in LEGACY_VERSIONS
            client_caps = initialize_params.get("capabilities") if isinstance(initialize_params, dict) else None
            legacy_apps_supported = _supports_apps(client_caps)
        request_started = time.perf_counter()
        response = handle_request(
            message,
            executable,
            proof_executable,
            legacy_initialized,
            legacy_apps_supported,
        )
        if isinstance(message, dict) and "id" in message:
            duration_ms = (time.perf_counter() - request_started) * 1000.0
            _append_request_log(_request_log_record(message, response, duration_ms))
        if response is not None:
            _write_message(response)


def resolve_executable(explicit: str | None) -> str:
    configured = explicit or os.environ.get("TRYTR_EXECUTABLE")
    if configured:
        path = Path(configured).expanduser().resolve()
        if path.is_file() and os.access(path, os.X_OK):
            return str(path)
        raise FileNotFoundError(f"Trytr executable is not runnable: {path}")

    for candidate in (ROOT / "build/debug/trytr", ROOT / "build/release/trytr"):
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    located = shutil.which("trytr")
    if located:
        return located
    raise FileNotFoundError(
        "Trytr executable not found; build the project or set TRYTR_EXECUTABLE"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trytr", help="path to the Trytr executable")
    parser.add_argument("--trytr-proof", help="path to the Trytr math proof checker")
    parser.add_argument("--state-dir", help="directory for bounded MCP request logs")
    arguments = parser.parse_args()
    try:
        executable = resolve_executable(arguments.trytr)
    except FileNotFoundError as error:
        print(f"trytr-mcp: {error}", file=sys.stderr)
        return 2
    try:
        proof_executable = resolve_proof_executable(arguments.trytr_proof, ROOT)
    except FileNotFoundError as error:
        print(f"trytr-mcp: {error}", file=sys.stderr)
        return 2
    if arguments.state_dir:
        os.environ["TRYTR_MCP_STATE_DIR"] = arguments.state_dir
    return serve(executable, proof_executable)


if __name__ == "__main__":
    raise SystemExit(main())
