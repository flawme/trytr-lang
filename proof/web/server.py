#!/usr/bin/env python3
"""Loopback-only OpenRouter chat bridge and Trytr MathML renderer."""

from __future__ import annotations

import json
import os
import re
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[2]
PAGE = Path(__file__).resolve().with_name("index.html")
STATIC_FILES = {
    "/app.css": ("text/css; charset=utf-8", Path(__file__).resolve().with_name("app.css")),
    "/app.js": ("text/javascript; charset=utf-8", Path(__file__).resolve().with_name("app.js")),
}
OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"
MAX_BODY_BYTES = 12_000
MAX_PROMPT_CHARS = 4_000
MAX_MATH_CHARS = 1_000
MAX_MATH_ROWS = 12
MATH_CHARACTERS = re.compile(r"^[A-Za-z0-9\s+*/=^_{}(),.<>|\\!≤≥≠≈×÷−√π∞∑∫∂]+$")
MODEL_NAME = re.compile(r"^[A-Za-z0-9_./:-]{1,200}$")
MATHML_START = b'<math xmlns="http://www.w3.org/1998/Math/MathML"'


class RequestError(Exception):
    pass


def trytr_executable() -> str:
    override = os.environ.get("TRYTR_EXECUTABLE")
    if override:
        return override
    candidates = (
        ROOT / "build/debug/trytr",
        ROOT / "build/trytr",
        ROOT / "build/Release/trytr",
    )
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    raise RequestError("Trytr executable not found; build the project first.")


def validate_math(expression: str) -> str:
    expression = expression.strip()
    if not expression or len(expression) > MAX_MATH_CHARS:
        raise RequestError("Math must contain 1 to 1000 characters.")
    if MATH_CHARACTERS.fullmatch(expression) is None:
        raise RequestError("Math contains characters outside Trytr's notation subset.")
    depth = 0
    for character in expression:
        if character == "{":
            depth += 1
            if depth > 32:
                raise RequestError("Math grouping is nested too deeply.")
        elif character == "}":
            depth -= 1
            if depth < 0:
                raise RequestError("Math braces are unbalanced.")
    if depth != 0:
        raise RequestError("Math braces are unbalanced.")
    return expression


def render_math(expressions: list[str]) -> str:
    if not expressions or len(expressions) > MAX_MATH_ROWS:
        raise RequestError(f"Provide between 1 and {MAX_MATH_ROWS} equations.")
    checked = [validate_math(expression) for expression in expressions]
    body = "\n".join(
        f"    @math #equation{index} {{{expression}}}"
        for index, expression in enumerate(checked)
    )
    source = f"@document(version: 1) {{\n{body}\n}}\n"
    try:
        completed = subprocess.run(
            [trytr_executable(), "--render-mathml", "-"],
            input=source.encode("utf-8"),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise RequestError("Trytr math rendering exceeded its time limit.") from error
    except OSError as error:
        raise RequestError("Trytr could not be started.") from error
    if completed.returncode != 0:
        raise RequestError("Trytr rejected the generated math document.")
    if len(completed.stdout) > 128_000:
        raise RequestError("Trytr MathML output exceeded the size limit.")
    if not completed.stdout.startswith(MATHML_START) or not completed.stdout.rstrip().endswith(b"</math>"):
        raise RequestError("Trytr returned invalid MathML.")
    try:
        return completed.stdout.decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        raise RequestError("Trytr returned non-UTF-8 MathML.") from error


def extract_math_line(response: str) -> tuple[str, str]:
    lines = response.strip().splitlines()
    if not lines or not lines[-1].startswith("MATH:"):
        raise RequestError("The model did not finish with a Trytr MATH: line.")
    if sum(line.startswith("MATH:") for line in lines) != 1:
        raise RequestError("The model returned more than one MATH: line.")
    expression = validate_math(lines[-1][len("MATH:"):])
    prose = "\n".join(lines[:-1]).strip()
    return prose, expression


class TrytrWebHandler(BaseHTTPRequestHandler):
    server_version = "TrytrProofDemo/0.1"

    def log_message(self, format: str, *args: object) -> None:
        # Paths and response codes only; never log headers, bodies, or API keys.
        super().log_message(format, *args)

    def _set_common_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")

    def _send_json(self, status: int, value: dict[str, object]) -> None:
        body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self._set_common_headers()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _check_local_origin(self) -> bool:
        host = self.headers.get("Host", "")
        allowed_hosts = {f"127.0.0.1:{self.server.server_port}",
                         f"localhost:{self.server.server_port}"}
        if host not in allowed_hosts:
            return False
        origin = self.headers.get("Origin")
        return origin is None or origin in {
            f"http://127.0.0.1:{self.server.server_port}",
            f"http://localhost:{self.server.server_port}",
        }

    def _read_json(self) -> dict[str, object]:
        if self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower() != "application/json":
            raise RequestError("Send a JSON request body.")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as error:
            raise RequestError("Invalid Content-Length.") from error
        if length <= 0 or length > MAX_BODY_BYTES:
            raise RequestError("Request body is empty or too large.")
        try:
            value = json.loads(self.rfile.read(length))
        except (json.JSONDecodeError, UnicodeDecodeError) as error:
            raise RequestError("Request body is not valid JSON.") from error
        if not isinstance(value, dict):
            raise RequestError("Request body must be a JSON object.")
        return value

    def do_GET(self) -> None:
        if self.path == "/health":
            self._send_json(200, {"ok": True, "renderer": "Trytr MathML"})
            return
        if self.path in STATIC_FILES:
            content_type, path = STATIC_FILES[self.path]
            try:
                body = path.read_bytes()
            except OSError:
                self._send_json(500, {"error": "Static asset could not be read"})
                return
            self.send_response(200)
            self._set_common_headers()
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path not in ("/", "/index.html"):
            self._send_json(404, {"error": "Not found"})
            return
        try:
            body = PAGE.read_bytes()
        except OSError:
            self._send_json(500, {"error": "Web page could not be read"})
            return
        self.send_response(200)
        self._set_common_headers()
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self) -> None:
        if not self._check_local_origin():
            self._send_json(403, {"error": "This demo only accepts same-origin loopback requests."})
            return
        if self.path == "/api/render":
            self._render_request()
        elif self.path == "/api/chat":
            self._chat_request()
        else:
            self._send_json(404, {"error": "Not found"})

    def _render_request(self) -> None:
        try:
            payload = self._read_json()
            raw_expressions = payload.get("expressions")
            if isinstance(raw_expressions, list) and all(isinstance(item, str) for item in raw_expressions):
                expressions = raw_expressions
            elif isinstance(payload.get("expression"), str):
                expressions = [payload["expression"]]
            else:
                raise RequestError("Provide an expression or list of expressions.")
            mathml = render_math(expressions)
        except RequestError as error:
            self._send_json(400, {"error": str(error)})
            return
        self._send_json(200, {"mathml": mathml})

    def _begin_sse(self) -> None:
        self.send_response(200)
        self._set_common_headers()
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Connection", "close")
        self.end_headers()

    def _send_event(self, name: str, value: dict[str, object]) -> None:
        data = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        self.wfile.write(f"event: {name}\ndata: {data}\n\n".encode("utf-8"))
        self.wfile.flush()
        if name in ("done", "error"):
            self.close_connection = True

    def _chat_request(self) -> None:
        try:
            payload = self._read_json()
            api_key = payload.get("apiKey")
            model = payload.get("model")
            prompt = payload.get("prompt")
            if not isinstance(api_key, str) or not 10 <= len(api_key) <= 512 or any(ord(char) < 33 for char in api_key):
                raise RequestError("Enter a valid OpenRouter API key.")
            if not isinstance(model, str) or MODEL_NAME.fullmatch(model.strip()) is None:
                raise RequestError("Enter a model ID such as openai/gpt-4o-mini.")
            if not isinstance(prompt, str) or not prompt.strip() or len(prompt) > MAX_PROMPT_CHARS:
                raise RequestError("Prompt must contain 1 to 4000 characters.")
        except RequestError as error:
            self._send_json(400, {"error": str(error)})
            return

        self._begin_sse()
        self._send_event("status", {"message": "Connecting to OpenRouter…"})
        request_body = {
            "model": model.strip(),
            "stream": True,
            "temperature": 0.2,
            "max_tokens": 700,
            "messages": [
                {
                    "role": "system",
                    "content": (
                        "Answer the user's math request briefly and carefully. "
                        "Use plain text for the explanation. End with exactly one final line "
                        "of the form MATH: expression, where expression uses Trytr math notation. "
                        "Use one single-line expression only; do not use Markdown fences. "
                        "For a proof, the final equation should summarize the result."
                    ),
                },
                {"role": "user", "content": prompt.strip()},
            ],
        }
        request = Request(
            OPENROUTER_URL,
            data=json.dumps(request_body).encode("utf-8"),
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
                "Accept": "text/event-stream",
                "HTTP-Referer": f"http://127.0.0.1:{self.server.server_port}/",
                "X-OpenRouter-Title": "Trytr Proof Chat Demo",
            },
            method="POST",
        )
        del api_key
        response_text: list[str] = []
        try:
            with urlopen(request, timeout=120) as response:
                for raw_line in response:
                    line = raw_line.decode("utf-8", errors="replace").strip()
                    if not line.startswith("data:"):
                        continue
                    data = line[5:].strip()
                    if data == "[DONE]":
                        break
                    try:
                        event = json.loads(data)
                    except json.JSONDecodeError:
                        continue
                    choices = event.get("choices", [])
                    if not choices:
                        continue
                    delta = choices[0].get("delta", {}).get("content")
                    if isinstance(delta, str) and delta:
                        if sum(len(part) for part in response_text) + len(delta) > 16_000:
                            raise RequestError("Model response exceeded the text limit.")
                        response_text.append(delta)
                        self._send_event("delta", {"text": delta})
        except HTTPError as error:
            self._send_event("error", {"message": f"OpenRouter returned HTTP {error.code}. Check the key, model, and account balance."})
            return
        except URLError:
            self._send_event("error", {"message": "Could not connect to OpenRouter."})
            return
        except (BrokenPipeError, ConnectionResetError):
            return
        except RequestError as error:
            self._send_event("error", {"message": str(error)})
            return
        except Exception:
            self._send_event("error", {"message": "The OpenRouter stream ended unexpectedly."})
            return

        try:
            prose, expression = extract_math_line("".join(response_text))
            mathml = render_math([expression])
        except RequestError as error:
            self._send_event("done", {
                "text": "".join(response_text).strip(),
                "mathml": "",
                "notice": str(error),
            })
            return
        self._send_event("done", {"text": prose, "mathml": mathml, "expression": expression})


def main() -> None:
    port = int(os.environ.get("TRYTR_WEB_PORT", "8765"))
    server = ThreadingHTTPServer(("127.0.0.1", port), TrytrWebHandler)
    print(f"Trytr proof chat: http://127.0.0.1:{port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping Trytr proof chat.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
