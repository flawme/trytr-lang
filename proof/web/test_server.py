import http.client
import json
from http.server import ThreadingHTTPServer
from threading import Thread
import unittest
from unittest.mock import patch

from server import (
    RequestError,
    TrytrWebHandler,
    extract_math_line,
    render_math,
    validate_math,
)


class TrytrWebValidationTests(unittest.TestCase):
    def test_accepts_supported_equation(self):
        self.assertEqual(validate_math(r"\frac{a_1+b_1}{c} = \sqrt{x^2}"),
                         r"\frac{a_1+b_1}{c} = \sqrt{x^2}")

    def test_rejects_directive_injection(self):
        with self.assertRaises(RequestError):
            validate_math("x} @text #inject {no}")

    def test_rejects_unbalanced_braces(self):
        with self.assertRaises(RequestError):
            validate_math(r"\frac{x}{y")

    def test_rejects_excessive_grouping(self):
        with self.assertRaises(RequestError):
            validate_math("{" * 33 + "x" + "}" * 33)

    def test_extracts_final_trytr_expression(self):
        prose, expression = extract_math_line(
            "Use the recursive addition rule.\nMATH: 1 + 1 = 2"
        )
        self.assertEqual(prose, "Use the recursive addition rule.")
        self.assertEqual(expression, "1 + 1 = 2")

    def test_requires_one_final_math_line(self):
        with self.assertRaises(RequestError):
            extract_math_line("MATH: x = x\nMore text")
        with self.assertRaises(RequestError):
            extract_math_line("MATH: x = x\nMATH: y = y")

    def test_uses_trytr_core_mathml_renderer(self):
        output = render_math(["x^2 + 1 = 5"])
        self.assertTrue(output.startswith('<math xmlns="http://www.w3.org/1998/Math/MathML"'))
        self.assertIn("<msup>", output)
        self.assertNotIn("<svg", output)

    def test_streamed_openrouter_answer_flows_through_trytr(self):
        chunks = [
            "From the recursive rules, reduce each side.\nMATH: ",
            "1 + 1 = 2",
        ]
        encoded = b"".join(
            b"data: " + json.dumps({"choices": [{"delta": {"content": chunk}}]}).encode() + b"\n\n"
            for chunk in chunks
        ) + b"data: [DONE]\n\n"
        server = ThreadingHTTPServer(("127.0.0.1", 0), TrytrWebHandler)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        captured_authorization = []

        def fake_open(request, timeout):
            del timeout
            captured_authorization.append(request.get_header("Authorization"))
            import io
            return io.BytesIO(encoded)

        try:
            body = json.dumps({
                "apiKey": "sk-or-test-key",
                "model": "test/provider-model",
                "prompt": "Prove one plus one equals two.",
            })
            with patch("server.urlopen", side_effect=fake_open):
                connection = http.client.HTTPConnection("127.0.0.1", server.server_port)
                connection.request(
                    "POST",
                    "/api/chat",
                    body=body,
                    headers={"Content-Type": "application/json"},
                )
                response = connection.getresponse()
                output = response.read().decode("utf-8")
                connection.close()
            self.assertEqual(response.status, 200)
            self.assertIn("event: delta", output)
            self.assertIn("event: done", output)
            self.assertEqual(captured_authorization, ["Bearer sk-or-test-key"])
            self.assertIn("From the recursive rules, reduce each side.", output)
            self.assertIn("<math xmlns=", output)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
