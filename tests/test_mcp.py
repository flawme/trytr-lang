import json
from pathlib import Path
import subprocess
import sys
import unittest


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
        if len(sys.argv) != 2:
            raise RuntimeError("expected path to the Trytr executable")
        cls.trytr_executable = sys.argv.pop()

    def run_server(self, requests):
        input_text = "".join(json.dumps(request) + "\n" for request in requests)
        completed = subprocess.run(
            [sys.executable, str(SERVER), "--trytr", self.trytr_executable],
            input=input_text,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
            timeout=30,
        )
        self.assertEqual(0, completed.returncode, completed.stderr)
        return [json.loads(line) for line in completed.stdout.splitlines()]

    def start_server(self):
        process = subprocess.Popen(
            [sys.executable, str(SERVER), "--trytr", self.trytr_executable],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
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
            ["render_trytr", "render_trytr_text"],
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
            svg = fetched["content"][0]["text"]
            self.assertTrue(svg.startswith("<svg "))
            self.assertIn(" C ", svg)
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
                ["render_trytr", "render_trytr_text", "get_trytr_preview"],
                [tool["name"] for tool in tools],
            )
            self.assertEqual(["app"], tools[2]["_meta"]["ui"]["visibility"])

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
            [sys.executable, str(SERVER), "--trytr", self.trytr_executable],
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
