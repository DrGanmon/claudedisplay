"""Exercise the flash-served browser script with local API fixtures, if Chrome exists."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/web_ui.h').read_text()
js = re.search(r'R"COZYJS\((.*?)\)COZYJS"', source, re.S)[1]
css = re.search(r'R"COZYCSS\((.*?)\)COZYCSS"', source, re.S)[1]
assert 'localStorage.setItem(\'cozy-theme\'' in js
assert '@media(max-width:480px)' in css
chrome = shutil.which('chromium') or shutil.which('google-chrome')
if not chrome and Path('/opt/google/chrome/chrome').exists():
    chrome = '/opt/google/chrome/chrome'
if not chrome:
    print('Web UI: flash assets OK; browser check skipped (Chrome unavailable)')
else:
    fixture = '''<!doctype html><html><head><meta charset="utf-8"><style>CSS</style></head><body>
    <button id="themeToggle"></button><nav><a href="/">Start</a></nav>
    <time id="deviceClock" data-unix="1792500000"></time>
    <main id="dashboard"><span id="liveState"></span><span id="claudeBadge"></span>
    <div class="gauge" id="sessionGauge"></div><div id="weeklyGauge"></div>
    <span id="sessionReset" data-reset=""></span><span id="weeklyReset" data-reset=""></span>
    <span data-live="weather.location"></span><span data-live="free_heap" data-format="kib"></span>
    <span id="weatherSymbol"></span><span id="displayPage"></span></main>
    <form action="http://device/settings" method="post"><label>Secret<input name="secret" type="password"></label><button type="submit">Speichern</button></form>
    <div id="toast" hidden></div>
    <script>
    let posted=false;
    window.fetch=async(url,options={})=>{
      if(options.method==='POST'){posted=String(options.body)==='secret=demo';return new Response('<p role="status">Gespeichert &lt;ok&gt;</p>');}
      if(String(url).includes('/api/time'))return new Response(JSON.stringify({clock_synced:true,unix_time:1792500000}));
      return new Response(JSON.stringify({five_hour:{remaining:84},seven_day:{remaining:50},claude_state:'Verbunden',free_heap:32768,weather:{location:'<img src=x onerror=alert(1)>',code:0},display_page:'weather'}));
    };
    </script><script>JS</script><script>
    setTimeout(()=>{
      document.querySelector('#themeToggle').click();
      document.querySelector('.password-toggle').click();
      document.querySelector('input').value='demo';
      document.querySelector('form').requestSubmit();
    },1200);
    setTimeout(()=>{
      const checks=[
        document.documentElement.dataset.theme==='light',
        document.querySelector('input').type==='text',posted,
        document.querySelector('.form-result').textContent==='Gespeichert <ok>',
        document.querySelector('[data-live="weather.location"]').textContent==='<img src=x onerror=alert(1)>',
        !document.querySelector('img'),
        document.querySelector('[data-live="free_heap"]').textContent==='32',
        Math.abs(parseFloat(document.querySelector('#sessionGauge').style.getPropertyValue('--value'))-302.4)<0.001,
        document.querySelector('#displayPage').textContent==='Wetter',
        !document.querySelector('button[type=submit]').disabled
      ];
      document.body.dataset.test=checks.every(Boolean)?'passed':JSON.stringify(checks);
    },2200);
    </script></body></html>'''.replace('<style>CSS</style>', '<style>' + css + '</style>').replace('<script>JS</script>', '<script>' + js + '</script>')
    with tempfile.TemporaryDirectory(prefix='cozy-browser-') as directory:
        html = Path(directory) / 'index.html'
        html.write_text(fixture)
        result = subprocess.run([chrome, '--headless', '--no-sandbox', '--disable-gpu',
                                 '--disable-dev-shm-usage', '--no-first-run',
                                 '--user-data-dir=' + directory + '/profile',
                                 '--virtual-time-budget=3500', '--dump-dom', html.as_uri()],
                                capture_output=True, text=True, timeout=40, check=True)
        assert 'data-test="passed"' in result.stdout, result.stdout[-6000:]
    print('Web UI: live values, gauges, theme, password toggle, form feedback and escaping OK')
