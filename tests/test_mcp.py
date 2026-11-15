import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from xml.etree import ElementTree


ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "mcp" / "trytr_mcp.py"
MODERN_VERSION = "2026-07-28"
SERVER_META = {
    "io.modelcontextprotocol/protocolVersion": MODERN_VERSION,
    "io.modelcontextprotocol/clientCapabilities": {},
}
UI_EXTENSION = "io.modelcontextprotocol/ui"
UI_MIME = "text/html;profile=mcp-app"
APP_CAPABILITIES = {"extensions": {UI_EXTENSION: {"mimeTypes": [UI_MIME]}}}
APP_SERVER_META = {
    "io.modelcontextprotocol/protocolVersion": MODERN_VERSION,
    "io.modelcontextprotocol/clientCapabilities": APP_CAPABILITIES,
}


class TrytrMcpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        arguments = sys.argv[1:]
        if not arguments or len(arguments) > 2:
            raise RuntimeError("expected Trytr executable and optional proof executable")
        cls.trytr_executable = arguments[0]
        cls.proof_executable = arguments[1] if len(arguments) == 2 else None

    def server_command(self):
        command = [sys.executable, str(SERVER), "--trytr", self.trytr_executable]
        if self.proof_executable:
            command.extend(["--trytr-proof", self.proof_executable])
        return command

    def run_server(self, requests):
        input_text = "".join(json.dumps(request) + "\n" for request in requests)
        with tempfile.TemporaryDirectory(prefix="trytr-mcp-test-state-") as state_directory:
            environment = os.environ.copy()
            environment["TRYTR_MCP_STATE_DIR"] = state_directory
            completed = subprocess.run(
                self.server_command(),
                input=input_text,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
                timeout=30,
                env=environment,
            )
        self.assertEqual(0, completed.returncode, completed.stderr)
        return [json.loads(line) for line in completed.stdout.splitlines()]

    def start_server(self):
        state_directory = tempfile.TemporaryDirectory(prefix="trytr-mcp-test-state-")
        self.addCleanup(state_directory.cleanup)
        environment = os.environ.copy()
        environment["TRYTR_MCP_STATE_DIR"] = state_directory.name
        process = subprocess.Popen(
            self.server_command(),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            env=environment,
        )

        def exchange(request):
            process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
            process.stdin.flush()
            return json.loads(process.stdout.readline())

        return process, exchange

    def test_legacy_handshake_lists_app_and_reads_preview_resource(self):
        requests = [
            {
                "jsonrpc": "2.0",
                "id": "init",
                "method": "initialize",
                "params": {
                    "protocolVersion": "2025-11-25",
                    "capabilities": APP_CAPABILITIES,
                },
            },
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 1, "method": "tools/list"},
            {
                "jsonrpc": "2.0",
                "id": "preview",
                "method": "resources/read",
                "params": {"uri": "ui://trytr/preview-v1"},
            },
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/call",
                "params": {
                    "name": "render_trytr",
                    "arguments": {
                        "source": (
                            "@document(version:1){"
                            "@heading #title{Native vector preview}"
                            "@roundrect #card(10,20,80,40,10)}"
                        )
                    },
                },
            },
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {
                    "name": "render_trytr",
                    "arguments": {"source": "@document(version:1){@roundrect(0,0,20,20,11)}"},
                },
            },
        ]
        responses = self.run_server(requests)
        self.assertEqual(5, len(responses))
        self.assertEqual("2025-11-25", responses[0]["result"]["protocolVersion"])
        self.assertEqual(
            [UI_MIME],
            responses[0]["result"]["capabilities"]["extensions"][UI_EXTENSION]["mimeTypes"],
        )
        tool = responses[1]["result"]["tools"][0]
        self.assertEqual("render_trytr", tool["name"])
        self.assertEqual("render_trytr_text", responses[1]["result"]["tools"][1]["name"])
        self.assertEqual("ui://trytr/preview-v1", tool["_meta"]["ui"]["resourceUri"])
        self.assertEqual(["model", "app"], tool["_meta"]["ui"]["visibility"])
        self.assertEqual(
            "ui://trytr/preview-v1",
            responses[1]["result"]["tools"][1]["_meta"]["ui"]["resourceUri"],
        )

        resource = responses[2]["result"]["contents"][0]
        self.assertEqual(UI_MIME, resource["mimeType"])
        self.assertIn("ui/notifications/tool-result", resource["text"])
        self.assertIn('id="text-output"', resource["text"])
        self.assertIn('id="math-canvas"', resource["text"])
        self.assertIn("createElementNS(mathmlNamespace", resource["text"])
        self.assertIn("block-sextant", resource["text"])

        rendered = responses[3]["result"]
        self.assertFalse(rendered["isError"])
        self.assertEqual("text", rendered["content"][0]["type"])
        self.assertIn("rendered successfully", rendered["content"][0]["text"])
        self.assertIn("previewId", rendered["structuredContent"])
        self.assertNotIn("svg", rendered["structuredContent"])

        invalid = responses[4]["result"]
        self.assertTrue(invalid["isError"])
        self.assertIn("radius", invalid["content"][0]["text"].lower())
        self.assertFalse(invalid["structuredContent"]["ok"])

    def test_modern_discovery_and_render(self):
        requests = [
            {
                "jsonrpc": "2.0",
                "id": "discover",
                "method": "server/discover",
                "params": {"_meta": SERVER_META},
            },
            {
                "jsonrpc": "2.0",
                "id": "call",
                "method": "tools/call",
                "params": {
                    "_meta": SERVER_META,
                    "name": "render_trytr",
                    "arguments": {
                        "source": "@document(version:1){@text{Hello from chat}}"
                    },
                },
            },
        ]
        discovery, call = self.run_server(requests)
        discovery_result = discovery["result"]
        self.assertEqual("complete", discovery_result["resultType"])
        self.assertEqual([MODERN_VERSION], discovery_result["supportedVersions"])
        self.assertEqual(
            [UI_MIME],
            discovery_result["capabilities"]["extensions"][UI_EXTENSION]["mimeTypes"],
        )
        self.assertEqual(
            "trytr-renderer",
            discovery_result["_meta"]["io.modelcontextprotocol/serverInfo"]["name"],
        )
        self.assertEqual("complete", call["result"]["resultType"])
        self.assertFalse(call["result"]["isError"])
        self.assertNotIn("structuredContent", call["result"])
        self.assertEqual("text", call["result"]["content"][0]["type"])
        self.assertIn("Hello from chat", call["result"]["content"][0]["text"])

    def test_text_tool_returns_graph_as_plain_chat_content(self):
        source = (
            "@document(version:1,width:100,height:50){"
            "@heading{Revenue}@line(0,40,100,40)@line(0,0,0,40)"
            "@path{M 5 35 L 25 30 L 50 20 C 60 12 75 18 95 5}}"
        )
        tools, result = self.run_server([
            {
                "jsonrpc": "2.0",
                "id": "tools",
                "method": "tools/list",
                "params": {"_meta": SERVER_META},
            },
            {
                "jsonrpc": "2.0",
                "id": "render-text",
                "method": "tools/call",
                "params": {
                    "_meta": SERVER_META,
                    "name": "render_trytr_text",
                    "arguments": {"source": source},
                },
            },
        ])
        self.assertEqual(
            [
                "render_trytr",
                "render_trytr_text",
                "review_trytr_math",
                "check_trytr_certificate",
                "health_check",
                "read_server_log",
            ],
            [tool["name"] for tool in tools["result"]["tools"]],
        )
        result = result["result"]
        self.assertFalse(result["isError"])
        rendered = result["content"][0]["text"]
        self.assertIn("# Revenue", rendered)
        self.assertTrue(any(0x1FB00 <= ord(character) <= 0x1FB3B
                            for character in rendered))
        self.assertNotIn("<svg", rendered)
        self.assertNotIn("previewId", result.get("structuredContent", {}))

    def test_modern_apps_negotiation_returns_preview_component(self):
        requests = [
            {
                "jsonrpc": "2.0",
                "id": "tools",
                "method": "tools/list",
                "params": {"_meta": APP_SERVER_META},
            },
            {
                "jsonrpc": "2.0",
                "id": "resource",
                "method": "resources/read",
                "params": {"_meta": APP_SERVER_META, "uri": "ui://trytr/preview-v1"},
            },
            {
                "jsonrpc": "2.0",
                "id": "render",
                "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "render_trytr",
                    "arguments": {"source": "@document(version:1){@text{Hello from chat — नमस्ते}}"},
                },
            },
            {
                "jsonrpc": "2.0",
                "id": "render-text",
                "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "render_trytr_text",
                    "arguments": {
                        "source": (
                            "@document(version:1,width:40,height:20){"
                            "@path{M 1 2 C 10 12 20 8 38 18}}"
                        )
                    },
                },
            },
        ]
        tools, resource, rendered, rendered_text = self.run_server(requests)
        self.assertEqual(
            "ui://trytr/preview-v1",
            tools["result"]["tools"][0]["_meta"]["ui"]["resourceUri"],
        )
        self.assertEqual(UI_MIME, resource["result"]["contents"][0]["mimeType"])
        self.assertEqual("complete", rendered["result"]["resultType"])
        self.assertTrue(rendered["result"]["structuredContent"]["ok"])
        self.assertIn("previewId", rendered["result"]["structuredContent"])
        self.assertNotIn("svg", rendered["result"]["structuredContent"])
        self.assertEqual(
            "ui://trytr/preview-v1",
            tools["result"]["tools"][1]["_meta"]["ui"]["resourceUri"],
        )
        self.assertTrue(rendered_text["result"]["structuredContent"]["ok"])
        self.assertIn("previewId", rendered_text["result"]["structuredContent"])
        self.assertNotIn("\U0001fb00", rendered_text["result"]["content"][0]["text"])
        self.assertIn("Preview ID:", rendered_text["result"]["content"][0]["text"])

    def test_declared_parameters_become_host_controls_and_change_formula_plot(self):
        source = (
            "@document(version:1,width:100,height:60){"
            "@param #gain(min:1,max:5,value:2,step:1){Curve gain}"
            "@plot #response(5,5,90,45)(x-min:-2,x-max:2,samples:17,"
            "grid:false,markers:false){y=#gain*sin(x)}}"
        )
        process, exchange = self.start_server()
        try:
            tools = exchange({
                "jsonrpc": "2.0", "id": 1, "method": "tools/list",
                "params": {"_meta": APP_SERVER_META},
            })["result"]["tools"]
            parameter_schema = tools[0]["inputSchema"]["properties"]["parameters"]
            self.assertEqual("object", parameter_schema["type"])
            self.assertEqual(128, parameter_schema["maxProperties"])

            baseline = exchange({
                "jsonrpc": "2.0", "id": 2, "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "render_trytr",
                    "arguments": {"source": source},
                },
            })["result"]
            baseline_parameter = baseline["structuredContent"]["parameters"][0]
            self.assertEqual({
                "id": "gain", "label": "Curve gain", "min": 1, "max": 5,
                "default": 2, "value": 2, "step": 1,
            }, baseline_parameter)

            updated = exchange({
                "jsonrpc": "2.0", "id": 3, "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "render_trytr",
                    "arguments": {"source": source, "parameters": {"gain": 4}},
                },
            })["result"]
            self.assertFalse(updated["isError"])
            self.assertEqual(4, updated["structuredContent"]["parameters"][0]["value"])
            previews = []
            for rendered in (baseline, updated):
                previews.append(exchange({
                    "jsonrpc": "2.0", "id": 4 + len(previews), "method": "tools/call",
                    "params": {
                        "_meta": APP_SERVER_META,
                        "name": "get_trytr_preview",
                        "arguments": {
                            "previewId": rendered["structuredContent"]["previewId"]
                        },
                    },
                })["result"]["content"][0]["text"])
            self.assertNotEqual(previews[0], previews[1])

            resource = exchange({
                "jsonrpc": "2.0", "id": 7, "method": "resources/read",
                "params": {"uri": "ui://trytr/preview-v1", "_meta": APP_SERVER_META},
            })["result"]["contents"][0]["text"]
            self.assertIn('type = "range"', resource)
            self.assertIn('name: "render_trytr"', resource)
        finally:
            process.stdin.close()
            process.wait(timeout=10)
            self.assertEqual(0, process.returncode, process.stderr.read())

    def test_parameter_override_rejects_non_integer_values(self):
        response = self.run_server([{
            "jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {
                "_meta": APP_SERVER_META,
                "name": "render_trytr",
                "arguments": {
                    "source": "@document(version:1){@text{safe}}",
                    "parameters": {"invalid;--render-svg": True},
                },
            },
        }])[0]["result"]
        self.assertTrue(response["isError"])
        self.assertFalse(response["structuredContent"]["ok"])

    def test_apps_text_fallback_loads_smooth_svg_preview(self):
        process, exchange = self.start_server()
        try:
            rendered = exchange({
                "jsonrpc": "2.0",
                "id": "render-text",
                "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "render_trytr_text",
                    "arguments": {
                        "source": (
                            "@document(version:1,width:100,height:50){"
                            "@plot(5,5,90,40){0,0 1,4 2,3 3,10}}"
                        )
                    },
                },
            })["result"]
            self.assertTrue(rendered["structuredContent"]["ok"])
            self.assertIn("previewId", rendered["structuredContent"])
            self.assertFalse(any(
                0x1FB00 <= ord(character) <= 0x1FB3B
                for character in rendered["content"][0]["text"]
            ))

            fetched = exchange({
                "jsonrpc": "2.0",
                "id": "load-vector",
                "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "get_trytr_preview",
                    "arguments": {
                        "previewId": rendered["structuredContent"]["previewId"]
                    },
                },
            })["result"]
            self.assertTrue(fetched["structuredContent"]["ok"])
            self.assertEqual("", fetched["structuredContent"]["mathml"])
            svg = fetched["content"][0]["text"]
            self.assertTrue(svg.startswith("<svg "))
            self.assertIn(" C ", svg)
        finally:
            process.stdin.close()
            process.wait(timeout=10)
            self.assertEqual(0, process.returncode, process.stderr.read())

    def test_math_expressions_are_returned_as_native_mathml(self):
        source = (
            "@document(version:1){"
            "@math #formula{\\frac{\\alpha+x_1}{\\sqrt{y}} < z_i^2}}"
        )
        process, exchange = self.start_server()
        try:
            rendered = exchange({
                "jsonrpc": "2.0", "id": "render", "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "render_trytr",
                    "arguments": {"source": source},
                },
            })["result"]
            self.assertTrue(rendered["structuredContent"]["ok"])
            fetched = exchange({
                "jsonrpc": "2.0", "id": "preview", "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "get_trytr_preview",
                    "arguments": {
                        "previewId": rendered["structuredContent"]["previewId"]
                    },
                },
            })["result"]
            mathml = fetched["structuredContent"]["mathml"]
            svg = fetched["content"][0]["text"]
            self.assertNotIn("alpha", svg)
            math_root = ElementTree.fromstring(mathml)
            self.assertEqual(
                "{http://www.w3.org/1998/Math/MathML}math", math_root.tag
            )
            self.assertTrue(mathml.startswith(
                '<math xmlns="http://www.w3.org/1998/Math/MathML"'
            ))
            self.assertIn("<mfrac>", mathml)
            self.assertIn("<mi>α</mi>", mathml)
            self.assertIn("<msqrt>", mathml)
            self.assertIn("<msubsup>", mathml)
            self.assertIn("<mo>&lt;</mo>", mathml)
            self.assertTrue(mathml.endswith("</math>"))
        finally:
            process.stdin.close()
            process.wait(timeout=10)
            self.assertEqual(0, process.returncode, process.stderr.read())

    def test_app_only_preview_fetch_returns_vector_data(self):
        process, exchange = self.start_server()
        try:
            discovery = exchange({
                "jsonrpc": "2.0",
                "id": "discover",
                "method": "server/discover",
                "params": {"_meta": APP_SERVER_META},
            })
            self.assertEqual("complete", discovery["result"]["resultType"])
            tools = exchange({
                "jsonrpc": "2.0",
                "id": 1,
                "method": "tools/list",
                "params": {"_meta": APP_SERVER_META},
            })["result"]["tools"]
            self.assertEqual(
                [
                    "render_trytr",
                    "render_trytr_text",
                    "review_trytr_math",
                    "check_trytr_certificate",
                    "health_check",
                    "read_server_log",
                    "get_trytr_preview",
                ],
                [tool["name"] for tool in tools],
            )
            self.assertEqual(["app"], tools[6]["_meta"]["ui"]["visibility"])

            rendered = exchange({
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "render_trytr",
                    "arguments": {
                        "source": "@document(version:1){@text{Hello from chat — नमस्ते}}"
                    },
                },
            })["result"]
            preview_id = rendered["structuredContent"]["previewId"]
            fetched = exchange({
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "get_trytr_preview",
                    "arguments": {"previewId": preview_id},
                },
            })["result"]
            self.assertTrue(fetched["structuredContent"]["ok"])
            self.assertEqual("", fetched["structuredContent"]["mathml"])
            svg = fetched["content"][0]["text"]
            self.assertIn("Hello from chat — नमस्ते", svg)
            self.assertTrue(svg.startswith("<svg "))

            newest_ids = [preview_id]
            for index in range(4):
                next_render = exchange({
                    "jsonrpc": "2.0",
                    "id": 10 + index,
                    "method": "tools/call",
                    "params": {
                        "_meta": APP_SERVER_META,
                        "name": "render_trytr",
                        "arguments": {
                            "source": f"@document(version:1){{@text{{Drawing {index}}}}}"
                        },
                    },
                })["result"]
                newest_ids.append(next_render["structuredContent"]["previewId"])

            evicted = exchange({
                "jsonrpc": "2.0",
                "id": 20,
                "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "get_trytr_preview",
                    "arguments": {"previewId": newest_ids[0]},
                },
            })["result"]
            self.assertTrue(evicted["isError"])
            newest = exchange({
                "jsonrpc": "2.0",
                "id": 21,
                "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "get_trytr_preview",
                    "arguments": {"previewId": newest_ids[-1]},
                },
            })["result"]
            self.assertTrue(newest["structuredContent"]["ok"])

            expired = exchange({
                "jsonrpc": "2.0",
                "id": 4,
                "method": "tools/call",
                "params": {
                    "_meta": APP_SERVER_META,
                    "name": "get_trytr_preview",
                    "arguments": {"previewId": "missing-preview"},
                },
            })["result"]
            self.assertTrue(expired["isError"])
            self.assertIn("expired or was not found", expired["structuredContent"]["message"])
        finally:
            process.stdin.close()
            process.wait(timeout=10)
            self.assertEqual(0, process.returncode, process.stderr.read())

    def test_parse_errors_are_reported_without_stopping_server(self):
        initialize = {
            "jsonrpc": "2.0",
            "id": "init",
            "method": "initialize",
            "params": {"protocolVersion": "2025-11-25", "capabilities": {}},
        }
        list_tools = {"jsonrpc": "2.0", "id": 1, "method": "tools/list"}
        raw_input = json.dumps(initialize) + "\nthis is not json\n" + json.dumps(list_tools) + "\n"
        completed = subprocess.run(
            self.server_command(),
            input=raw_input,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
            timeout=30,
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        responses = [json.loads(line) for line in completed.stdout.splitlines()]
        self.assertEqual(3, len(responses))
        self.assertEqual(-32700, responses[1]["error"]["code"])
        tool = responses[2]["result"]["tools"][0]
        self.assertEqual("render_trytr", tool["name"])
        self.assertNotIn("_meta", tool)

    def test_math_review_reports_identity_counterexample_and_unsupported_row(self):
        if self.proof_executable is None:
            self.skipTest("proof executable is not configured")
        source = (
            "(x+y)^2 = x^2+2xy+y^2\n"
            "x^2 = x\n"
            "\\sin(x) = x\n"
        )
        responses = self.run_server([
            {
                "jsonrpc": "2.0",
                "id": "init",
                "method": "initialize",
                "params": {"protocolVersion": "2025-11-25", "capabilities": {}},
            },
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "tools/call",
                "params": {
                    "name": "review_trytr_math",
                    "arguments": {"source": source},
                },
            },
        ])
        result = responses[1]["result"]["structuredContent"]
        self.assertFalse(responses[1]["result"]["isError"])
        self.assertIn("Line 1 (verified)", responses[1]["result"]["content"][0]["text"])
        self.assertFalse(result["ok"])
        self.assertEqual("needs_review", result["status"])
        self.assertEqual(
            ["verified", "not_an_identity", "unable_to_check"],
            [row["status"] for row in result["results"]],
        )
        self.assertEqual([1, 2, 3], [row["line"] for row in result["results"]])

    def test_math_review_rejects_oversized_input(self):
        responses = self.run_server([
            {
                "jsonrpc": "2.0",
                "id": "init",
                "method": "initialize",
                "params": {"protocolVersion": "2025-11-25", "capabilities": {}},
            },
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "tools/call",
                "params": {
                    "name": "review_trytr_math",
                    "arguments": {"source": "x=x\n" + (" " * (64 * 1024))},
                },
            },
        ])
        result = responses[1]["result"]["structuredContent"]
        self.assertTrue(responses[1]["result"]["isError"])
        self.assertEqual("unable_to_check", result["status"])
        self.assertIn("64 KiB", result["message"])

    def test_certificate_tool_checks_natural_and_limit_certificates(self):
        if self.proof_executable is None:
            self.skipTest("proof executable is not configured")
        natural_certificate = (
            "theorem one_plus_one: add(succ(0), succ(0)) = succ(succ(0)) {\n"
            "    rewrite add_succ;\n"
            "    rewrite add_zero;\n"
            "}\n"
        )
        limit_certificate = (
            "\\begin{limitproof}\n"
            "\\lim_{x\\to 2}(x^2+1)=5\n"
            "\\delta=\\min\\left(1,\\frac{\\varepsilon}{5}\\right)\n"
            "\\end{limitproof}\n"
        )
        false_certificate = natural_certificate.replace(
            "succ(succ(0)) {", "succ(succ(succ(0))) {"
        )
        malformed_certificate = "\\begin{limitproof}\nnot a valid certificate\n"
        calls = [
            ("natural_equality", natural_certificate),
            ("polynomial_limit", limit_certificate),
            ("natural_equality", false_certificate),
            ("polynomial_limit", malformed_certificate),
            ("polynomial_limit", natural_certificate),
            ("natural_equality", limit_certificate),
        ]
        requests = [
            {
                "jsonrpc": "2.0",
                "id": "init",
                "method": "initialize",
                "params": {"protocolVersion": "2025-11-25", "capabilities": {}},
            }
        ]
        requests.extend(
            {
                "jsonrpc": "2.0",
                "id": index,
                "method": "tools/call",
                "params": {
                    "name": "check_trytr_certificate",
                    "arguments": {"kind": kind, "source": source},
                },
            }
            for index, (kind, source) in enumerate(calls, start=1)
        )
        responses = self.run_server(requests)
        results = [response["result"] for response in responses[1:]]
        payloads = [result["structuredContent"] for result in results]
        self.assertEqual(
            ["verified", "verified", "rejected", "rejected", "rejected", "rejected"],
            [payload["status"] for payload in payloads],
        )
        self.assertEqual(
            [True, True, False, False, False, False],
            [payload["ok"] for payload in payloads],
        )
        self.assertFalse(any(result["isError"] for result in results))
        self.assertIn("checker", payloads[0]["message"])
        self.assertIn("did not validate", payloads[2]["message"])
        self.assertIn("does not match kind", payloads[4]["message"])
        self.assertIn("does not match kind", payloads[5]["message"])
        self.assertNotIn(
            "trytr-certificate-",
            " ".join(payload["message"] for payload in payloads),
        )

    def test_certificate_tool_bounds_input_and_rejects_unknown_kind(self):
        requests = [
            {
                "jsonrpc": "2.0",
                "id": "init",
                "method": "initialize",
                "params": {"protocolVersion": "2025-11-25", "capabilities": {}},
            },
            {
                "jsonrpc": "2.0",
                "id": "large",
                "method": "tools/call",
                "params": {
                    "name": "check_trytr_certificate",
                    "arguments": {
                        "kind": "natural_equality",
                        "source": "x" * (64 * 1024 + 1),
                    },
                },
            },
            {
                "jsonrpc": "2.0",
                "id": "kind",
                "method": "tools/call",
                "params": {
                    "name": "check_trytr_certificate",
                    "arguments": {"kind": "lean", "source": "anything"},
                },
            },
            {
                "jsonrpc": "2.0",
                "id": "extra",
                "method": "tools/call",
                "params": {
                    "name": "check_trytr_certificate",
                    "arguments": {
                        "kind": "natural_equality",
                        "source": "anything",
                        "path": "/tmp/input",
                    },
                },
            },
        ]
        requests.extend(
            {
                "jsonrpc": "2.0",
                "id": f"bad-source-{index}",
                "method": "tools/call",
                "params": {
                    "name": "check_trytr_certificate",
                    "arguments": {"kind": "natural_equality", "source": source},
                },
            }
            for index, source in enumerate((42, "\0", "   ", "\ud800"))
        )
        responses = self.run_server(requests)
        oversized = responses[1]["result"]
        self.assertTrue(oversized["isError"])
        self.assertEqual("unable_to_check", oversized["structuredContent"]["status"])
        self.assertIn("64 KiB", oversized["structuredContent"]["message"])
        unknown_kind = responses[2]["result"]
        self.assertTrue(unknown_kind["isError"])
        self.assertEqual("unable_to_check", unknown_kind["structuredContent"]["status"])
        self.assertEqual(-32602, responses[3]["error"]["code"])
        invalid_source_results = [response["result"] for response in responses[4:]]
        self.assertEqual(4, len(invalid_source_results))
        self.assertTrue(all(result["isError"] for result in invalid_source_results))
        self.assertTrue(
            all(
                result["structuredContent"]["status"] == "unable_to_check"
                for result in invalid_source_results
            )
        )

    def test_health_and_request_log_expose_metadata_without_document_source(self):
        sentinel = "PRIVATE_SOURCE_SENTINEL_2af1d9"
        responses = self.run_server([
            {
                "jsonrpc": "2.0",
                "id": "init",
                "method": "initialize",
                "params": {"protocolVersion": "2025-11-25", "capabilities": {}},
            },
            {
                "jsonrpc": "2.0",
                "id": "health-id",
                "method": "tools/call",
                "params": {"name": "health_check"},
            },
            {
                "jsonrpc": "2.0",
                "id": "render-id",
                "method": "tools/call",
                "params": {
                    "name": "render_trytr",
                    "arguments": {
                        "source": "@document(version:1){@text{" + sentinel + "}}"
                    },
                },
            },
            {
                "jsonrpc": "2.0",
                "id": "error-id",
                "method": "tools/call",
                "params": {"name": "unknown_tool", "arguments": {}},
            },
            {
                "jsonrpc": "2.0",
                "id": "log-id",
                "method": "tools/call",
                "params": {
                    "name": "read_server_log",
                    "arguments": {"limit": 3},
                },
            },
        ])
        health = responses[1]["result"]["structuredContent"]
        self.assertTrue(health["ok"])
        self.assertEqual(1, health["protocolVersion"])
        self.assertEqual(str(ROOT), health["projectRoot"])
        self.assertTrue(health["rendererAvailable"])
        self.assertGreaterEqual(health["uptimeMs"], 0)

        error_response = responses[3]
        self.assertEqual(-32602, error_response["error"]["code"])
        log_result = responses[4]["result"]
        self.assertFalse(log_result["isError"])
        entries = log_result["structuredContent"]["entries"]
        self.assertEqual(
            ["health-id", "render-id", "error-id"],
            [entry["requestId"] for entry in entries],
        )
        self.assertEqual(
            ["health_check", "render_trytr", "unknown_tool"],
            [entry["tool"] for entry in entries],
        )
        self.assertTrue(all(entry["project"] == str(ROOT) for entry in entries))
        self.assertTrue(all(entry["durationMs"] >= 0 for entry in entries))
        self.assertEqual(
            {"type": "jsonrpc", "code": -32602, "status": "request_error"},
            entries[2]["error"],
        )
        self.assertNotIn(sentinel, json.dumps(log_result))

    def test_request_log_rejects_unbounded_or_unknown_arguments(self):
        responses = self.run_server([
            {
                "jsonrpc": "2.0",
                "id": "init",
                "method": "initialize",
                "params": {"protocolVersion": "2025-11-25", "capabilities": {}},
            },
            {
                "jsonrpc": "2.0",
                "id": "wrong-limit",
                "method": "tools/call",
                "params": {"name": "read_server_log", "arguments": {"limit": True}},
            },
            {
                "jsonrpc": "2.0",
                "id": "extra-arg",
                "method": "tools/call",
                "params": {"name": "health_check", "arguments": {"path": "/tmp"}},
            },
        ])
        self.assertEqual(-32602, responses[1]["error"]["code"])
        self.assertEqual(-32602, responses[2]["error"]["code"])

    def test_oversized_vector_preview_is_rejected(self):
        source = "@document(version:1){@text{" + ("<" * 600_000) + "}}"
        responses = self.run_server([
            {
                "jsonrpc": "2.0",
                "id": "init",
                "method": "initialize",
                "params": {
                    "protocolVersion": "2025-11-25",
                    "capabilities": APP_CAPABILITIES,
                },
            },
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "tools/call",
                "params": {"name": "render_trytr", "arguments": {"source": source}},
            },
        ])
        result = responses[1]["result"]
        self.assertTrue(result["isError"])
        self.assertIn("2 MiB MCP content limit", result["content"][0]["text"])
        self.assertFalse(result["structuredContent"]["ok"])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
