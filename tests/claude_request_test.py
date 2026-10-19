"""Exercise the actual HTTP test handler: queue TLS work after the response."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/main.cpp').read_text()
start = source.index('void handleTestSessionKey() {')
handler = source[start:source.index('\n}', start) + 2]
harness = r'''
#include "display_pages.h"
#include <cassert>
#include <string>
#define F(value) value
bool authenticated = true, forceClaudeRefresh = false;
DisplayPages pages;
bool responded = false, closed = false;
bool requireSettingsAuth() { return authenticated; }
void settingsResult(const char *) { responded = true; }
struct Client { void stop() { assert(responded); closed = true; } };
struct { Client client() { return Client(); } } web;
'''
harness += handler
harness += r'''
int main() {
  authenticated = false;
  handleTestSessionKey();
  assert(!forceClaudeRefresh && !responded && !closed);
  authenticated = true;
  pages.update(TouchInput::Event::SHORT_PRESS, false, 0);
  pages.update(TouchInput::Event::NONE, false, 350);
  handleTestSessionKey();
  assert(forceClaudeRefresh && responded && !closed);
  forceClaudeRefresh = responded = false;
  pages.update(TouchInput::Event::SHORT_PRESS, false, 500);
  pages.update(TouchInput::Event::NONE, false, 850);
  handleTestSessionKey();
  assert(forceClaudeRefresh && responded && closed);
}
'''
# No fetchClaudeUsage stub: an accidental synchronous fetch fails compilation.
with tempfile.TemporaryDirectory(prefix='claudedisplay-request-') as directory:
    cpp = Path(directory) / 'request.cpp'
    binary = Path(directory) / 'request'
    cpp.write_text(harness)
    subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-I', str(root / 'src'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Claude test: authentication, weather pause and deferred request OK')
