"""Render the actual firmware form with an HTTP-chunk-aware test server."""
from html.parser import HTMLParser
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "src/main.cpp").read_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


harness = r'''
#include <string>
#include <iostream>
#include <cassert>
#define F(value) value
struct String : std::string {
  using std::string::string;
  String() = default;
  String(int value) : std::string(std::to_string(value)) {}
};
struct {
  bool ended = false;
  std::string html;
  void sendContent(const std::string &text) {
    if (ended) return;
    if (text.empty()) ended = true;
    else html += text;
  }
} web;
struct {
  struct {
    bool enabled = false, insecure = false;
    int port = 443, intervalSec = 60;
    String host, username, token, oauthClientId, fingerprint;
  } trueConf;
} config;
struct { String state; } trueConfResult;
'''
harness += "\n".join(function(name) for name in (
    "String htmlEscape(", "void sendHtmlEscaped(", "void sendTrueConfSettings("))
harness += r'''
int main(int argc, char **argv) {
  if (argc > 1) {
    config.trueConf.enabled = config.trueConf.insecure = true;
    config.trueConf.host = argv[1];
  }
  sendTrueConfSettings();
  assert(!web.ended);
  web.sendContent("<p id='after'>AFTER</p>");
  std::cout << web.html;
}
'''


class Form(HTMLParser):
    def __init__(self):
        super().__init__()
        self.inputs = {}
        self.scripts = 0
        self.forms = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "input":
            self.inputs[attrs["name"]] = attrs
        if tag == "script":
            self.scripts += 1
        if tag == "form":
            self.forms.append(attrs.get("action"))


with tempfile.TemporaryDirectory(prefix="claudedisplay-html-") as directory:
    cpp = Path(directory) / "form.cpp"
    binary = Path(directory) / "form"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", str(cpp), "-o", str(binary)], check=True)
    for args in ([], ["server'\"><script>unexpected</script>"]):
        html = subprocess.check_output([str(binary), *args], text=True)
        form = Form()
        form.feed(html)
        assert set(form.inputs) == {"enabled", "host", "port", "username", "password", "token",
                                    "clearToken", "clearPassword", "oauthClientId", "fingerprint",
                                    "insecure", "intervalSec"}
        assert form.inputs["host"].get("value") == (args[0] if args else "")
        assert ("checked" in form.inputs["enabled"]) == bool(args)
        assert ("checked" in form.inputs["insecure"]) == bool(args)
        assert form.scripts == 0
        assert "/settings/trueconf/test" in form.forms
        assert "AFTER" in html
print("TrueConf HTML: empty values, all fields, checkboxes and escaping OK")
