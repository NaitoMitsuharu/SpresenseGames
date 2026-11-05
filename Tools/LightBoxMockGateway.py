"""Loopback-only fixture. Does not contact OpenClaw or an AI provider."""
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Gateway(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass  # Do not log headers, instructions, or credentials.

    def do_POST(self):
        if self.path != "/v1/chat/completions":
            self.send_error(404)
            return
        if self.headers.get("Authorization") != "Bearer lightbox-fixture-only":
            self.send_error(401)
            return
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert body["model"] == "openclaw/default" and body["stream"] is False
        assert body["messages"][0]["role"] == "system"
        words = body["messages"][1]["content"]
        if words == "delayed-scene":
            time.sleep(32)
        if words == "slow":
            time.sleep(1)
        style = dict(title="青い海", color="#2277ff", accent="#66ffcc", motion=.6,
                     viscosity=.7, swirl=.3, attraction=.3, tilt=1, glow=1,
                     trail=.8, shakeThreshold=1.7, shake="scatter")
        if words == "repair-once" and len(body["messages"]) == 4:
            style["title"] = "修正された景色"
        content = "not scene json" if words == "bad-json" or (words == "repair-once" and len(body["messages"]) == 2) else json.dumps(style, ensure_ascii=False)
        data = json.dumps({"choices": [{"finish_reason": "stop", "message": {"role": "assistant", "content": content}}]}, ensure_ascii=False).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Expected when testing cancellation.


if __name__ == "__main__":
    print("Light Box test fixture listening on 127.0.0.1:18879", flush=True)
    ThreadingHTTPServer(("127.0.0.1", 18879), Gateway).serve_forever()
