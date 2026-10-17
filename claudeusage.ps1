#Requires -Version 5.1
<#
  ClaudeUsageTray – zeigt die claude.ai-Nutzungslimits (Sitzung / Woche) im Infobereich der Taskleiste.

  Datenquelle (INOFFIZIELL, kann sich ohne Ankündigung ändern):
    GET https://claude.ai/api/organizations/{orgId}/usage     Auth: Cookie "sessionKey"

  Start:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -STA -File .\ClaudeUsageTray.ps1

  Konfiguration: %APPDATA%\ClaudeUsageTray\config.json  (sessionKey/Cookies per DPAPI an den Windows-User gebunden verschlüsselt)
  Design folgt dem Windows-App-Modus (hell/dunkel), wird beim Öffnen jedes Fensters neu gelesen.
  WICHTIG: Datei als UTF-8 MIT BOM gespeichert lassen, sonst zeigt PS 5.1 Umlaute falsch an.
#>

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing, System.Net.Http
[System.Windows.Forms.Application]::EnableVisualStyles()

# ---------- Eigene Controls (C#) ----------
if (-not ('CUT.UsageBar' -as [type])) {
    Add-Type -ReferencedAssemblies System.Windows.Forms, System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace CUT
{
    public static class Native
    {
        [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr hIcon);
        [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

        // Dunkle Titelleiste (Win10 20H1+/Win11: Attribut 20, ältere Builds: 19)
        public static void SetDarkTitleBar(IntPtr hwnd, bool dark)
        {
            int v = dark ? 1 : 0;
            try { if (DwmSetWindowAttribute(hwnd, 20, ref v, 4) != 0) DwmSetWindowAttribute(hwnd, 19, ref v, 4); } catch { }
        }

        public static GraphicsPath RoundRect(RectangleF r, float radius)
        {
            GraphicsPath p = new GraphicsPath();
            float d = Math.Min(radius * 2f, Math.Min(r.Width, r.Height));
            if (d <= 0f) { p.AddRectangle(r); return p; }
            p.AddArc(r.X, r.Y, d, d, 180, 90);
            p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            p.CloseFigure();
            return p;
        }
    }

    // Abgerundeter Fortschrittsbalken
    public class UsageBar : Control
    {
        private float _value;
        private Color _track = Color.FromArgb(60, 60, 60);
        private Color _fill = Color.FromArgb(59, 130, 246);

        public UsageBar()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            Height = 8;
        }
        public float Value { get { return _value; } set { _value = Math.Max(0f, Math.Min(100f, value)); Invalidate(); } }
        public Color TrackColor { get { return _track; } set { _track = value; Invalidate(); } }
        public Color FillColor { get { return _fill; } set { _fill = value; Invalidate(); } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(BackColor);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            RectangleF r = new RectangleF(0.5f, 0.5f, Width - 1f, Height - 1f);
            float rad = r.Height / 2f;
            using (GraphicsPath p = Native.RoundRect(r, rad))
            using (SolidBrush b = new SolidBrush(_track)) g.FillPath(b, p);
            float w = r.Width * _value / 100f;
            if (w > 0f)
            {
                if (w < r.Height) w = r.Height;
                using (GraphicsPath p = Native.RoundRect(new RectangleF(r.X, r.Y, w, r.Height), rad))
                using (SolidBrush b = new SolidBrush(_fill)) g.FillPath(b, p);
            }
        }
    }

    // Ring-Anzeige mit Prozentwert in der Mitte
    public class RingGauge : Control
    {
        private float _value;
        private string _center = "";
        private string _caption = "";
        private Color _track = Color.Gray, _fill = Color.DodgerBlue, _text = Color.White, _sub = Color.Silver;
        private float _thick = 7f;

        public RingGauge()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            Size = new Size(76, 76);
        }
        public float Value { get { return _value; } set { _value = Math.Max(0f, Math.Min(100f, value)); Invalidate(); } }
        public string CenterText { get { return _center; } set { _center = value ?? ""; Invalidate(); } }
        public string Caption { get { return _caption; } set { _caption = value ?? ""; Invalidate(); } }
        public Color TrackColor { get { return _track; } set { _track = value; Invalidate(); } }
        public Color FillColor { get { return _fill; } set { _fill = value; Invalidate(); } }
        public Color TextColor { get { return _text; } set { _text = value; Invalidate(); } }
        public Color SubColor { get { return _sub; } set { _sub = value; Invalidate(); } }
        public float Thickness { get { return _thick; } set { _thick = value; Invalidate(); } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(BackColor);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
            float t = _thick;
            RectangleF r = new RectangleF(t / 2f + 1f, t / 2f + 1f, Width - t - 2f, Height - t - 2f);
            using (Pen p = new Pen(_track, t)) g.DrawEllipse(p, r);
            if (_value > 0f)
            {
                using (Pen p = new Pen(_fill, t))
                {
                    p.StartCap = LineCap.Round; p.EndCap = LineCap.Round;
                    g.DrawArc(p, r, -90f, 360f * _value / 100f);
                }
            }
            using (StringFormat sf = new StringFormat())
            {
                sf.Alignment = StringAlignment.Center; sf.LineAlignment = StringAlignment.Center;
                bool hasCap = _caption.Length > 0;
                using (Font f = new Font(Font.FontFamily, Width / 4.8f, FontStyle.Bold, GraphicsUnit.Pixel))
                using (SolidBrush b = new SolidBrush(_text))
                    g.DrawString(_center, f, b, new RectangleF(0f, hasCap ? -Height / 12f : 0f, Width, Height), sf);
                if (hasCap)
                {
                    using (Font f2 = new Font(Font.FontFamily, Width / 8.5f, FontStyle.Regular, GraphicsUnit.Pixel))
                    using (SolidBrush b2 = new SolidBrush(_sub))
                        g.DrawString(_caption, f2, b2, new RectangleF(0f, Height / 5.5f, Width, Height), sf);
                }
            }
        }
    }

    // Karte mit abgerundeten Ecken (vertikaler Stapel)
    public class RoundPanel : FlowLayoutPanel
    {
        public Color OuterColor = Color.Black;
        public Color BorderColor = Color.Gray;
        public float Radius = 10f;

        public RoundPanel()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            FlowDirection = FlowDirection.TopDown;
            WrapContents = false;
        }
        protected override void OnPaintBackground(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(OuterColor);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            RectangleF r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
            using (GraphicsPath p = Native.RoundRect(r, Radius))
            {
                using (SolidBrush b = new SolidBrush(BackColor)) g.FillPath(b, p);
                using (Pen pen = new Pen(BorderColor)) g.DrawPath(pen, p);
            }
        }
    }

    // Farben für das Kontextmenü
    public class ThemeColors : ProfessionalColorTable
    {
        private Color _bg, _hover, _border;
        public ThemeColors(Color bg, Color hover, Color border) { _bg = bg; _hover = hover; _border = border; UseSystemColors = false; }
        public override Color ToolStripDropDownBackground { get { return _bg; } }
        public override Color ImageMarginGradientBegin { get { return _bg; } }
        public override Color ImageMarginGradientMiddle { get { return _bg; } }
        public override Color ImageMarginGradientEnd { get { return _bg; } }
        public override Color MenuBorder { get { return _border; } }
        public override Color MenuItemBorder { get { return _hover; } }
        public override Color MenuItemSelected { get { return _hover; } }
        public override Color MenuItemSelectedGradientBegin { get { return _hover; } }
        public override Color MenuItemSelectedGradientEnd { get { return _hover; } }
        public override Color SeparatorDark { get { return _border; } }
        public override Color SeparatorLight { get { return _border; } }
    }

    public class ThemeRenderer : ToolStripProfessionalRenderer
    {
        private Color _text, _sub;
        public ThemeRenderer(ThemeColors colors, Color text, Color sub) : base(colors) { _text = text; _sub = sub; RoundedEdges = false; }
        protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
        {
            e.TextColor = e.Item.Enabled ? _text : _sub;
            base.OnRenderItemText(e);
        }
    }
}
'@
}

# ---------- Einzelinstanz ----------
$createdNew = $false
$script:Mutex = New-Object System.Threading.Mutex($true, 'Local\ClaudeUsageTray', [ref]$createdNew)
if (-not $createdNew) {
    [void][System.Windows.Forms.MessageBox]::Show('ClaudeUsageTray läuft bereits.', 'Claude Usage')
    return
}

# ---------- Konstanten ----------
$ScriptPath = $PSCommandPath
$AppDir     = Join-Path $env:APPDATA 'ClaudeUsageTray'
$CfgFile    = Join-Path $AppDir 'config.json'
$RunKey     = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$BaseUrl    = 'https://claude.ai/api/organizations'
$WarnRest   = 50   # verbleibend <= 50 % -> gelb
$CritRest   = 20   # verbleibend <= 20 % -> rot

$BucketInfo = [ordered]@{
    five_hour        = @{ Label = 'Aktuelle Sitzung (5 Std.)'; Short = 'Sitzung' }
    seven_day        = @{ Label = 'Alle Modelle';              Short = 'Woche'   }
    seven_day_opus   = @{ Label = 'Opus';                      Short = 'Opus'    }
    seven_day_sonnet = @{ Label = 'Sonnet';                    Short = 'Sonnet'  }
    extra_usage      = @{ Label = 'Zusatznutzung (Credits)';   Short = 'Extra'   }
}

$DefaultCfg = [ordered]@{
    OrgId           = ''
    SessionKeyEnc   = ''
    ExtraCookiesEnc = ''
    UserAgent       = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36'
    IntervalSec     = 120
    IconMetric      = 'max'        # Taskleisten-Symbol: max | five_hour | seven_day
    RingMetric      = 'max'        # Ring im Detailfenster: max | five_hour | seven_day
    IconMode        = 'remaining'  # remaining | used
    Autostart       = $false
}

# ---------- Theme / Skalierung ----------
$gTmp = [System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero)
$script:Scale = $gTmp.DpiX / 96.0
$gTmp.Dispose()
function Px([double]$v) { [int][Math]::Round($v * $script:Scale) }
function Pt([double]$x, [double]$y) { New-Object System.Drawing.Point (Px $x), (Px $y) }
function Sz([double]$w, [double]$h) { New-Object System.Drawing.Size (Px $w), (Px $h) }
function Pad([double]$l, [double]$t, [double]$r, [double]$b) { New-Object System.Windows.Forms.Padding (Px $l), (Px $t), (Px $r), (Px $b) }

function Get-Theme {
    $dark = $true
    try { $dark = ((Get-ItemPropertyValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize' -Name AppsUseLightTheme) -eq 0) } catch { }
    $c = { param($h) [System.Drawing.ColorTranslator]::FromHtml($h) }
    if ($dark) {
        [pscustomobject]@{
            Dark = $true; Bg = & $c '#1B1B1D'; Surface = & $c '#252528'; Input = & $c '#1B1B1D'; Hover = & $c '#323236'
            Border = & $c '#36363A'; Text = & $c '#EDEDED'; Sub = & $c '#9C9CA3'; Accent = & $c '#3B82F6'; Track = & $c '#38383D'
            Ok = & $c '#22C55E'; Warn = & $c '#F59E0B'; Crit = & $c '#EF4444'
        }
    } else {
        [pscustomobject]@{
            Dark = $false; Bg = & $c '#F4F4F6'; Surface = & $c '#FFFFFF'; Input = & $c '#F7F7F9'; Hover = & $c '#ECECF0'
            Border = & $c '#E2E2E7'; Text = & $c '#1B1B1D'; Sub = & $c '#6B6B73'; Accent = & $c '#2563EB'; Track = & $c '#E5E7EB'
            Ok = & $c '#16A34A'; Warn = & $c '#D97706'; Crit = & $c '#DC2626'
        }
    }
}

function Get-LevelColor([double]$rest, $normal) {
    if ($rest -le $CritRest) { $script:Theme.Crit } elseif ($rest -le $WarnRest) { $script:Theme.Warn } else { $normal }
}

function Get-StateColor {
    switch ($script:State) {
        'ok'      { $script:Theme.Ok; break }
        'error'   { $script:Theme.Crit; break }
        'offline' { $script:Theme.Warn; break }
        default   { $script:Theme.Sub }
    }
}

# ---------- UI-Bausteine ----------
function New-Label([string]$text, [single]$size = 9, [string]$family = 'Segoe UI', $color = $null) {
    $l = New-Object System.Windows.Forms.Label
    $l.AutoSize = $true; $l.Text = $text
    $l.Font = New-Object System.Drawing.Font($family, $size)
    $l.ForeColor = $(if ($null -ne $color) { $color } else { $script:Theme.Text })
    $l.Margin = New-Object System.Windows.Forms.Padding 0
    $l
}

function New-Button([string]$text, [bool]$primary = $false) {
    $th = $script:Theme
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $text; $b.AutoSize = $true; $b.FlatStyle = 'Flat'; $b.UseVisualStyleBackColor = $false
    $b.Cursor = [System.Windows.Forms.Cursors]::Hand
    $b.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 9)
    $b.Padding = Pad 10 3 10 3
    if ($primary) {
        $b.BackColor = $th.Accent; $b.ForeColor = [System.Drawing.Color]::White
        $b.FlatAppearance.BorderSize = 0
        $b.FlatAppearance.MouseOverBackColor = [System.Windows.Forms.ControlPaint]::Light($th.Accent, 0.15)
    } else {
        $b.BackColor = $th.Surface; $b.ForeColor = $th.Text
        $b.FlatAppearance.BorderSize = 1; $b.FlatAppearance.BorderColor = $th.Border
        $b.FlatAppearance.MouseOverBackColor = $th.Hover
    }
    $b
}

function New-Stack {
    $p = New-Object System.Windows.Forms.FlowLayoutPanel
    $p.FlowDirection = 'TopDown'; $p.WrapContents = $false; $p.AutoSize = $true; $p.AutoSizeMode = 'GrowAndShrink'
    $p.Margin = New-Object System.Windows.Forms.Padding 0
    $p
}

function New-Row {
    $p = New-Object System.Windows.Forms.FlowLayoutPanel
    $p.FlowDirection = 'LeftToRight'; $p.WrapContents = $false; $p.AutoSize = $true; $p.AutoSizeMode = 'GrowAndShrink'
    $p.Margin = New-Object System.Windows.Forms.Padding 0
    $p
}

function New-ButtonBar([int]$width) {
    $p = New-Object System.Windows.Forms.FlowLayoutPanel
    $p.FlowDirection = 'RightToLeft'; $p.WrapContents = $false; $p.AutoSize = $true; $p.AutoSizeMode = 'GrowAndShrink'
    $p.MinimumSize = New-Object System.Drawing.Size $width, 0
    $p.Margin = Pad 0 6 0 0
    $p
}

function New-Card([string]$title) {
    $th = $script:Theme
    $card = New-Object CUT.RoundPanel
    $card.OuterColor = $th.Bg; $card.BorderColor = $th.Border; $card.BackColor = $th.Surface; $card.Radius = (Px 10)
    $card.AutoSize = $true; $card.AutoSizeMode = 'GrowAndShrink'
    $card.Padding = Pad 16 12 16 14
    $card.Margin = Pad 0 0 0 10
    if ($title) {
        $t = New-Label $title 10.5 'Segoe UI Semibold'
        $t.Margin = Pad 0 0 0 2
        $card.Controls.Add($t)
    }
    $card
}

function New-ThemedForm([string]$title) {
    $th = $script:Theme
    $f = New-Object System.Windows.Forms.Form
    $f.Text = $title; $f.FormBorderStyle = 'FixedDialog'; $f.MaximizeBox = $false; $f.MinimizeBox = $false; $f.ShowIcon = $false
    $f.StartPosition = 'CenterScreen'; $f.AutoSize = $true; $f.AutoSizeMode = 'GrowAndShrink'; $f.ShowInTaskbar = $false
    $f.BackColor = $th.Bg; $f.ForeColor = $th.Text
    $f.Font = New-Object System.Drawing.Font('Segoe UI', 9)
    $f.Add_HandleCreated({ param($s, $e) [CUT.Native]::SetDarkTitleBar($s.Handle, [bool]$script:Theme.Dark) })
    $f
}

function Set-InputStyle($c) {
    $th = $script:Theme
    $c.BackColor = $th.Input; $c.ForeColor = $th.Text
    $c.Font = New-Object System.Drawing.Font('Segoe UI', 9.5)
    if ($c -is [System.Windows.Forms.TextBox] -or $c -is [System.Windows.Forms.NumericUpDown]) { $c.BorderStyle = 'FixedSingle' }
    if ($c -is [System.Windows.Forms.ComboBox]) { $c.FlatStyle = 'Flat' }
}

function Add-Field($card, [string]$label, $ctl, [string]$hint, [int]$iw) {
    $th = $script:Theme
    $l = New-Label $label 8.5 'Segoe UI Semibold' $th.Sub
    $l.Margin = Pad 0 10 0 4
    $card.Controls.Add($l)
    if ($ctl -is [System.Windows.Forms.TextBox] -or $ctl -is [System.Windows.Forms.ComboBox] -or $ctl -is [System.Windows.Forms.NumericUpDown]) { Set-InputStyle $ctl }
    $ctl.Margin = New-Object System.Windows.Forms.Padding 0
    $card.Controls.Add($ctl)
    if ($hint) {
        $h = New-Label $hint 8.5 'Segoe UI' $th.Sub
        $h.MaximumSize = New-Object System.Drawing.Size $iw, 0
        $h.Margin = Pad 0 4 0 0
        $card.Controls.Add($h)
    }
}

# ---------- Konfiguration ----------
function Protect-Text([string]$t) {
    if ([string]::IsNullOrEmpty($t)) { return '' }
    ConvertTo-SecureString $t -AsPlainText -Force | ConvertFrom-SecureString
}

function Unprotect-Text([string]$e) {
    if ([string]::IsNullOrEmpty($e)) { return '' }
    try {
        $ss = ConvertTo-SecureString $e
        $b  = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($ss)
        try { [Runtime.InteropServices.Marshal]::PtrToStringBSTR($b) }
        finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b) }
    } catch { '' }
}

function Get-Config {
    $cfg = [pscustomobject]$DefaultCfg
    if (Test-Path $CfgFile) {
        try {
            $j = Get-Content $CfgFile -Raw -Encoding UTF8 | ConvertFrom-Json
            foreach ($p in $j.PSObject.Properties) {
                if ($cfg.PSObject.Properties[$p.Name]) { $cfg.($p.Name) = $p.Value }
            }
        } catch { }
    }
    $cfg
}

function Save-Config($cfg) {
    if (-not (Test-Path $AppDir)) { New-Item -ItemType Directory -Path $AppDir | Out-Null }
    $cfg | ConvertTo-Json | Set-Content -Path $CfgFile -Encoding UTF8
}

function Test-Configured { [bool]($script:Cfg.OrgId -and $script:Cfg.SessionKeyEnc) }

function Set-Autostart([bool]$on) {
    if ($on) {
        $cmd = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -STA -File "{0}"' -f $ScriptPath
        Set-ItemProperty -Path $RunKey -Name 'ClaudeUsageTray' -Value $cmd
    } else {
        Remove-ItemProperty -Path $RunKey -Name 'ClaudeUsageTray' -ErrorAction SilentlyContinue
    }
}

# ---------- HTTP ----------
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$handler = New-Object System.Net.Http.HttpClientHandler
$handler.UseCookies = $false
$handler.AutomaticDecompression = [System.Net.DecompressionMethods]::GZip -bor [System.Net.DecompressionMethods]::Deflate
$script:Http = New-Object System.Net.Http.HttpClient($handler)
$script:Http.Timeout = [TimeSpan]::FromSeconds(20)

function New-ClaudeRequest([string]$url, $cfg) {
    $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, $url)
    $cookie = 'sessionKey=' + (Unprotect-Text $cfg.SessionKeyEnc)
    $extra  = Unprotect-Text $cfg.ExtraCookiesEnc
    if ($extra) { $cookie += '; ' + $extra.Trim().TrimEnd(';') }
    [void]$req.Headers.TryAddWithoutValidation('Cookie', $cookie)
    [void]$req.Headers.TryAddWithoutValidation('User-Agent', $cfg.UserAgent)
    [void]$req.Headers.TryAddWithoutValidation('Accept', 'application/json')
    [void]$req.Headers.TryAddWithoutValidation('Referer', 'https://claude.ai/settings/usage')
    $req
}

# Synchron (nur für Einstellungsdialog)
function Invoke-ClaudeGet([string]$url, $cfg) {
    $resp = $script:Http.SendAsync((New-ClaudeRequest $url $cfg)).GetAwaiter().GetResult()
    try {
        [pscustomobject]@{
            Code = [int]$resp.StatusCode
            Body = $resp.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            Cf   = $resp.Headers.Contains('cf-mitigated')
        }
    } finally { $resp.Dispose() }
}

function Get-HttpErrorText([int]$code, [bool]$cf) {
    switch ($code) {
        401     { 'Session abgelaufen oder ungültig (401)'; break }
        403     { if ($cf) { 'Cloudflare-Challenge (403) – siehe "Weitere Cookies"' } else { 'Zugriff verweigert (403) – sessionKey/Org-ID prüfen' }; break }
        404     { 'Org-ID nicht gefunden (404)'; break }
        429     { 'Zu viele Anfragen (429) – Intervall erhöhen'; break }
        default { "HTTP-Fehler $code" }
    }
}

# ---------- Daten ----------
function Get-Buckets($data) {
    $order = @($BucketInfo.Keys)
    $list = foreach ($p in $data.PSObject.Properties) {
        $v = $p.Value
        if ($null -eq $v -or -not $v.PSObject.Properties['utilization'] -or $null -eq $v.utilization) { continue }

        $reset = $null
        $ra = $v.PSObject.Properties['resets_at']
        if ($ra -and $ra.Value) {
            if ($ra.Value -is [datetime]) { $reset = $ra.Value.ToLocalTime() }
            else { try { $reset = [DateTimeOffset]::Parse([string]$ra.Value, [Globalization.CultureInfo]::InvariantCulture).LocalDateTime } catch { } }
        }
        $known = $BucketInfo.Contains($p.Name)
        $idx   = [array]::IndexOf($order, $p.Name)
        [pscustomobject]@{
            Key      = $p.Name
            Label    = $(if ($known) { $BucketInfo[$p.Name].Label } else { $p.Name })
            Short    = $(if ($known) { $BucketInfo[$p.Name].Short } else { $p.Name })
            Used     = [double]$v.utilization
            ResetsAt = $reset
            Order    = $(if ($idx -ge 0) { $idx } else { 99 })
        }
    }
    @($list | Sort-Object Order)
}

function Format-Reset($b) {
    if (-not $b.ResetsAt) { return '' }
    $span = $b.ResetsAt - (Get-Date)
    if ($span.TotalSeconds -le 0) { return 'Zurückgesetzt' }
    if ($span.TotalHours -lt 24) {
        return ('Zurücksetzung in {0} Std. {1} Min.' -f [int][Math]::Floor($span.TotalHours), $span.Minutes)
    }
    'Zurücksetzung am ' + $b.ResetsAt.ToString('ddd., HH:mm', [Globalization.CultureInfo]'de-DE')
}

function Get-MetricBucket([string]$metric) {
    $b = $null
    if ($metric -and $metric -ne 'max') {
        $b = $script:Buckets | Where-Object { $_.Key -eq $metric } | Select-Object -First 1
    }
    # 'max' bzw. Fallback, falls der gewählte Wert in der Antwort fehlt.
    # Zusatznutzung (Credits) greift erst nach den Plan-Limits -> nicht heranziehen
    if (-not $b) { $b = $script:Buckets | Where-Object { $_.Key -ne 'extra_usage' } | Sort-Object Used -Descending | Select-Object -First 1 }
    if (-not $b) { $b = $script:Buckets | Sort-Object Used -Descending | Select-Object -First 1 }
    $b
}

# ---------- Tray-Icon ----------
function New-TrayIcon([string]$text, [System.Drawing.Color]$bg, [System.Drawing.Color]$fg) {
    $size = [Math]::Max(16, [System.Windows.Forms.SystemInformation]::SmallIconSize.Width)
    $bmp  = New-Object System.Drawing.Bitmap $size, $size
    $g    = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)

    $path = [CUT.Native]::RoundRect((New-Object System.Drawing.RectangleF 0, 0, ($size - 1), ($size - 1)), [single]([Math]::Max(2, $size / 4.5)))
    $bgBrush = New-Object System.Drawing.SolidBrush $bg
    $g.FillPath($bgBrush, $path)

    $sf = New-Object System.Drawing.StringFormat ([System.Drawing.StringFormat]::GenericTypographic)
    $sf.Alignment = 'Center'; $sf.LineAlignment = 'Center'
    $fs = [int]($size * 0.8)
    while ($true) {
        $font = New-Object System.Drawing.Font('Segoe UI', [single]$fs, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
        $w = $g.MeasureString($text, $font, 1000, $sf).Width
        if ($w -le ($size - 1) -or $fs -le 7) { break }
        $font.Dispose(); $fs--
    }
    $fgBrush = New-Object System.Drawing.SolidBrush $fg
    $g.DrawString($text, $font, $fgBrush, (New-Object System.Drawing.RectangleF 0, 1, $size, $size), $sf)

    $h    = $bmp.GetHicon()
    $icon = [System.Drawing.Icon]::FromHandle($h).Clone()
    [void][CUT.Native]::DestroyIcon($h)
    $font.Dispose(); $fgBrush.Dispose(); $bgBrush.Dispose(); $sf.Dispose(); $path.Dispose(); $g.Dispose(); $bmp.Dispose()
    $icon
}

# ---------- UI-Status ----------
function Set-State([string]$state, [string]$msg) {
    $script:State = $state; $script:StateMsg = $msg
    Update-Ui
}

function Update-Ui {
    $ct = [System.Drawing.ColorTranslator]
    switch ($script:State) {
        'ok' {
            $b    = Get-MetricBucket $script:Cfg.IconMetric
            $used = [Math]::Min(100, [Math]::Max(0, $b.Used))
            $rest = 100 - $used
            $val  = if ($script:Cfg.IconMode -eq 'used') { $used } else { $rest }
            $text = '{0:0}' -f [Math]::Round($val)
            if     ($rest -le $CritRest) { $bg = '#DC2626'; $fg = '#FFFFFF' }
            elseif ($rest -le $WarnRest) { $bg = '#F59E0B'; $fg = '#111111' }
            else                         { $bg = '#16A34A'; $fg = '#FFFFFF' }
        }
        'connecting' { $bg = '#6B7280'; $fg = '#FFFFFF'; $text = '…' }
        'offline'    { $bg = '#4B5563'; $fg = '#FFFFFF'; $text = '–' }
        'error'      { $bg = '#374151'; $fg = '#F59E0B'; $text = '!' }
        default      { $bg = '#9CA3AF'; $fg = '#FFFFFF'; $text = '?' }
    }

    $tip = $script:StateMsg
    if ($script:Buckets.Count -gt 0) {
        $tip += "`n" + (($script:Buckets | Select-Object -First 2 | ForEach-Object { '{0} {1:0} %' -f $_.Short, $_.Used }) -join ' · ') + ' verwendet'
    }
    if ($tip.Length -gt 63) { $tip = $tip.Substring(0, 62) + '…' }   # Limit NotifyIcon.Text (.NET Framework)

    $old = $script:Tray.Icon
    $script:Tray.Icon = New-TrayIcon $text ($ct::FromHtml($bg)) ($ct::FromHtml($fg))
    if ($old) { $old.Dispose() }
    $script:Tray.Text = $tip
    Update-Details   # offenes Detailfenster mitziehen
}

# ---------- Abruf (asynchron) ----------
function Start-Refresh {
    if ($script:Pending) { return }
    if (-not (Test-Configured)) { Set-State 'unconfigured' 'Nicht konfiguriert'; return }
    try {
        $url = '{0}/{1}/usage' -f $BaseUrl, $script:Cfg.OrgId.Trim()
        $script:Pending = $script:Http.SendAsync((New-ClaudeRequest $url $script:Cfg))
    } catch {
        Set-State 'error' ('Fehler: ' + $_.Exception.Message)
    }
}

function Complete-Refresh($task) {
    if ($task.IsCanceled) { Set-State 'offline' 'Nicht verbunden: Zeitüberschreitung'; return }
    if ($task.IsFaulted)  { Set-State 'offline' ('Nicht verbunden: ' + $task.Exception.GetBaseException().Message); return }
    $resp = $task.Result
    try {
        $code = [int]$resp.StatusCode
        $body = $resp.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        if ($code -eq 200) {
            $script:RawJson = $body
            $buckets = Get-Buckets ($body | ConvertFrom-Json)
            if ($buckets.Count -eq 0) { Set-State 'error' 'Fehler: Antwort enthält keine Nutzungsdaten'; return }
            $script:Buckets    = $buckets
            $script:LastUpdate = Get-Date
            Set-State 'ok' 'Verbunden'
        } else {
            Set-State 'error' ('Fehler: ' + (Get-HttpErrorText $code $resp.Headers.Contains('cf-mitigated')))
        }
    } catch {
        Set-State 'error' ('Fehler: ' + $_.Exception.Message)
    } finally { $resp.Dispose() }
}

# ---------- Kontextmenü ----------
function Add-InfoItem([string]$text) {
    $i = $script:Menu.Items.Add($text); $i.Enabled = $false
}

function Build-Menu {
    $script:Theme = Get-Theme
    $th = $script:Theme
    $mn = $script:Menu
    $colors = New-Object CUT.ThemeColors -ArgumentList $th.Surface, $th.Hover, $th.Border
    $mn.Renderer = New-Object CUT.ThemeRenderer -ArgumentList $colors, $th.Text, $th.Sub
    $mn.BackColor = $th.Surface; $mn.ForeColor = $th.Text; $mn.ShowImageMargin = $false
    $mn.Font = New-Object System.Drawing.Font('Segoe UI', 9.5)

    $mn.Items.Clear()
    Add-InfoItem ('Status: ' + $script:StateMsg + $(if ($script:LastUpdate) { ' (' + $script:LastUpdate.ToString('HH:mm') + ')' } else { '' }))
    if ($script:Buckets.Count -gt 0) {
        [void]$mn.Items.Add('-')
        foreach ($b in $script:Buckets) {
            $r = Format-Reset $b
            Add-InfoItem ('{0}: {1:0} % verwendet{2}' -f $b.Short, $b.Used, $(if ($r) { ' – ' + $r } else { '' }))
        }
    }
    [void]$mn.Items.Add('-')
    [void]$mn.Items.Add('Details anzeigen…', $null, { Show-Details })
    [void]$mn.Items.Add('Jetzt aktualisieren', $null, { Start-Refresh })
    [void]$mn.Items.Add('Einstellungen…', $null, { Show-Settings })
    [void]$mn.Items.Add('Rohdaten (JSON) kopieren', $null, {
        if ($script:RawJson) { [System.Windows.Forms.Clipboard]::SetText($script:RawJson) }
    })
    [void]$mn.Items.Add('-')
    [void]$mn.Items.Add('Beenden', $null, { Stop-App })
}

function Stop-App {
    $script:PollTimer.Stop(); $script:CheckTimer.Stop()
    $script:Tray.Visible = $false
    $script:Ctx.ExitThread()
}

# ---------- Detailfenster ----------
function New-UsageRow($b, [int]$w) {
    $th = $script:Theme
    $row = New-Object System.Windows.Forms.Panel
    $row.Size = New-Object System.Drawing.Size $w, (Px 58)
    $row.Margin = Pad 0 8 0 0

    $title = New-Label $b.Label 9.5 'Segoe UI Semibold'
    $title.Location = Pt 0 0

    $pct = New-Label ('{0:0} % verwendet' -f $b.Used) 9 'Segoe UI' $th.Sub
    $pct.AutoSize = $false; $pct.TextAlign = 'TopRight'
    $pct.Size = Sz 150 20
    $pct.Location = New-Object System.Drawing.Point ($w - (Px 150)), (Px 1)

    $bar = New-Object CUT.UsageBar
    $bar.Location = Pt 0 26
    $bar.Size = New-Object System.Drawing.Size $w, (Px 8)
    $bar.TrackColor = $th.Track
    $bar.FillColor  = Get-LevelColor (100 - $b.Used) $th.Accent
    $bar.Value      = [single]$b.Used

    $reset = New-Label (Format-Reset $b) 8.5 'Segoe UI' $th.Sub
    $reset.Location = Pt 0 40

    $row.Controls.AddRange(@($title, $pct, $bar, $reset))
    $row
}

# Baut den Inhalt des Detailfensters neu auf (nach jedem Abruf und alle 30 s)
function Update-Details {
    $body = $script:DetailsBody
    if (-not $body -or $body.IsDisposed) { return }
    $th = $script:Theme
    $W  = Px 380
    $IW = $W - (Px 32)

    $body.SuspendLayout()
    try {
        foreach ($c in @($body.Controls)) { $c.Dispose() }
        $body.Controls.Clear()

        # Kopfbereich: Ring + Titel + Status
        $hdr = New-Object System.Windows.Forms.Panel
        $hdr.Size = New-Object System.Drawing.Size $W, (Px 84)
        $hdr.Margin = Pad 0 0 0 12

        $ring = New-Object CUT.RingGauge
        $ring.Size = Sz 76 76; $ring.Location = Pt 0 4; $ring.Thickness = [single](Px 7)
        $ring.TrackColor = $th.Track; $ring.TextColor = $th.Text; $ring.SubColor = $th.Sub
        $ib = Get-MetricBucket $script:Cfg.RingMetric
        $ringTip = 'Keine Daten'
        if ($ib) {
            $used = [Math]::Min(100, [Math]::Max(0, $ib.Used)); $rest = 100 - $used
            $show = if ($script:Cfg.IconMode -eq 'used') { $used } else { $rest }
            $ring.Value      = [single]$show
            $ring.CenterText = ('{0:0} %' -f $show)
            $ring.Caption    = $ib.Short
            $ringTip = '{0} ({1}): {2:0} % {3}' -f $ib.Short, $ib.Label, $show, $(if ($script:Cfg.IconMode -eq 'used') { 'verwendet' } else { 'übrig' })
            $ring.FillColor  = Get-LevelColor $rest $th.Accent
        } else {
            $ring.Value = 0; $ring.CenterText = '–'; $ring.Caption = ''
        }

        if ($script:DetailsTip) { $script:DetailsTip.SetToolTip($ring, $ringTip) }

        $t1 = New-Label 'Nutzungslimits' 15 'Segoe UI Semibold'
        $t1.Location = Pt 92 12
        $dot = New-Label '●' 10 'Segoe UI' (Get-StateColor)
        $dot.Location = Pt 92 47
        $st = New-Label $script:StateMsg 9 'Segoe UI' $th.Sub
        $st.Location = Pt 110 49
        $st.MaximumSize = New-Object System.Drawing.Size ($W - (Px 110)), 0
        $hdr.Controls.AddRange(@($ring, $t1, $dot, $st))
        $body.Controls.Add($hdr)

        # Karten
        $groups = @(
            @{ Title = 'Aktuelle Sitzung';    Items = @($script:Buckets | Where-Object { $_.Key -eq 'five_hour' }) }
            @{ Title = 'Wöchentliche Limits'; Items = @($script:Buckets | Where-Object { $_.Key -like 'seven_day*' }) }
            @{ Title = 'Weitere';             Items = @($script:Buckets | Where-Object { $_.Key -ne 'five_hour' -and $_.Key -notlike 'seven_day*' }) }
        )
        foreach ($grp in $groups) {
            if ($grp.Items.Count -eq 0) { continue }
            $card = New-Card $grp.Title
            foreach ($b in $grp.Items) {
                $r = New-UsageRow $b $IW
                if ($grp.Title -eq 'Aktuelle Sitzung') { $r.Controls[0].Text = '5-Stunden-Fenster' }
                $card.Controls.Add($r)
            }
            $body.Controls.Add($card)
        }
        if ($script:Buckets.Count -eq 0) {
            $card = New-Card ''
            $n = New-Label 'Keine Daten verfügbar.' 9.5 'Segoe UI' $th.Sub
            $n.MinimumSize = New-Object System.Drawing.Size $IW, 0
            $card.Controls.Add($n)
            $body.Controls.Add($card)
        }

        if ($script:LastUpdate) {
            $lu = New-Label ('Zuletzt aktualisiert: ' + $script:LastUpdate.ToString('HH:mm:ss')) 8.5 'Segoe UI' $th.Sub
            $lu.Margin = Pad 2 0 0 0
            $body.Controls.Add($lu)
        }
    } finally { $body.ResumeLayout($true) }
}

function Show-Details {
    if ($script:DialogOpen) {
        if ($script:DetailsForm -and -not $script:DetailsForm.IsDisposed) { $script:DetailsForm.Activate() }
        return
    }
    $script:DialogOpen = $true
    $tick = $null
    try {
        $script:Theme = Get-Theme
        $W = Px 380

        $f = New-ThemedForm 'Claude Usage'
        $outer = New-Stack
        $outer.Padding = Pad 20 18 20 18
        $body = New-Stack

        $btnClose    = New-Button 'Schließen' $true; $btnClose.DialogResult = 'Cancel'
        $btnRefresh  = New-Button 'Aktualisieren'
        $btnSettings = New-Button 'Einstellungen'
        $btnRefresh.Add_Click({ Start-Refresh })
        $btnSettings.Add_Click({ $script:OpenSettingsAfter = $true; $f.Close() })
        foreach ($b in @($btnClose, $btnRefresh, $btnSettings)) { $b.Margin = Pad 8 0 0 0 }
        $bar = New-ButtonBar $W
        $bar.Margin = Pad 0 12 0 0
        $bar.Controls.AddRange(@($btnClose, $btnRefresh, $btnSettings))

        $outer.Controls.AddRange(@($body, $bar))
        $f.Controls.Add($outer)
        $f.CancelButton = $btnClose

        $script:DetailsForm = $f
        $script:DetailsBody = $body
        $script:DetailsTip  = New-Object System.Windows.Forms.ToolTip
        Update-Details

        # Countdown auch zwischen zwei Abrufen aktuell halten
        $tick = New-Object System.Windows.Forms.Timer
        $tick.Interval = 30000
        $tick.Add_Tick({ Update-Details })
        $tick.Start()

        [void]$f.ShowDialog()
        $f.Dispose()
    } finally {
        if ($tick) { $tick.Stop(); $tick.Dispose() }
        if ($script:DetailsTip) { $script:DetailsTip.Dispose() }
        $script:DetailsForm = $null; $script:DetailsBody = $null; $script:DetailsTip = $null
        $script:DialogOpen = $false
    }
    if ($script:OpenSettingsAfter) { $script:OpenSettingsAfter = $false; Show-Settings }
}

# ---------- Einstellungen ----------
function Show-Settings {
    if ($script:DialogOpen) { return }
    $script:DialogOpen = $true
    try {
        $script:Theme = Get-Theme
        $th  = $script:Theme
        $W   = Px 440
        $IW  = $W - (Px 32)
        $cfg = $script:Cfg

        $f = New-ThemedForm 'Claude Usage – Einstellungen'
        $outer = New-Stack
        $outer.Padding = Pad 20 18 20 18
        $f.Controls.Add($outer)

        $h = New-Label 'Einstellungen' 15 'Segoe UI Semibold'
        $h.Margin = Pad 0 0 0 12
        $outer.Controls.Add($h)

        # --- Verbindung ---
        $cConn = New-Card 'Verbindung'
        $tbKey = New-Object System.Windows.Forms.TextBox
        $tbKey.Width = $IW; $tbKey.UseSystemPasswordChar = $true; $tbKey.Text = Unprotect-Text $cfg.SessionKeyEnc
        Add-Field $cConn 'sessionKey' $tbKey 'claude.ai im Browser öffnen → F12 → Anwendung/Speicher → Cookies → https://claude.ai → Wert von "sessionKey". Entspricht deinem Login – nicht weitergeben.' $IW

        $cbOrg = New-Object System.Windows.Forms.ComboBox
        $cbOrg.DropDownStyle = 'DropDown'; $cbOrg.Width = $IW - (Px 108); $cbOrg.Text = $cfg.OrgId
        Set-InputStyle $cbOrg
        $cbOrg.Margin = Pad 0 0 8 0
        $btnOrg = New-Button 'Ermitteln'
        $btnOrg.AutoSize = $false; $btnOrg.Size = Sz 100 27; $btnOrg.Margin = New-Object System.Windows.Forms.Padding 0
        $pOrg = New-Row
        $pOrg.Controls.AddRange(@($cbOrg, $btnOrg))
        Add-Field $cConn 'Organisations-ID' $pOrg $null $IW

        $btnTest = New-Button 'Verbindung testen'
        $btnTest.Margin = New-Object System.Windows.Forms.Padding 0
        $lblTest = New-Label '' 9 'Segoe UI' $th.Sub
        $lblTest.MaximumSize = New-Object System.Drawing.Size ($IW - (Px 150)), 0
        $lblTest.Margin = Pad 10 6 0 0
        $pTest = New-Row
        $pTest.Margin = Pad 0 12 0 0
        $pTest.Controls.AddRange(@($btnTest, $lblTest))
        $cConn.Controls.Add($pTest)
        $outer.Controls.Add($cConn)

        # --- Anzeige ---
        $cView = New-Card 'Anzeige'
        $metricKeys  = @('five_hour', 'seven_day', 'max')
        $metricNames = @('Aktuelle Sitzung (5 Std.)', 'Wöchentliches Limit (alle Modelle)', 'Kritischster Wert (höchste Auslastung)')

        $cbMetric = New-Object System.Windows.Forms.ComboBox
        $cbMetric.DropDownStyle = 'DropDownList'; $cbMetric.Width = $IW
        [void]$cbMetric.Items.AddRange($metricNames)
        $i = [array]::IndexOf($metricKeys, [string]$cfg.IconMetric); if ($i -lt 0) { $i = 2 }
        $cbMetric.SelectedIndex = $i
        Add-Field $cView 'Taskleisten-Symbol zeigt' $cbMetric $null $IW

        $cbRing = New-Object System.Windows.Forms.ComboBox
        $cbRing.DropDownStyle = 'DropDownList'; $cbRing.Width = $IW
        [void]$cbRing.Items.AddRange($metricNames)
        $i = [array]::IndexOf($metricKeys, [string]$cfg.RingMetric); if ($i -lt 0) { $i = 2 }
        $cbRing.SelectedIndex = $i
        Add-Field $cView 'Ring im Detailfenster zeigt' $cbRing $null $IW

        $modeKeys = @('remaining', 'used')
        $cbMode = New-Object System.Windows.Forms.ComboBox
        $cbMode.DropDownStyle = 'DropDownList'; $cbMode.Width = $IW
        [void]$cbMode.Items.AddRange(@('Verbleibend (%)', 'Verwendet (%)'))
        $cbMode.SelectedIndex = [Math]::Max(0, [array]::IndexOf($modeKeys, [string]$cfg.IconMode))
        Add-Field $cView 'Werte anzeigen als' $cbMode 'Gilt für Taskleisten-Symbol und Ring.' $IW

        $numInt = New-Object System.Windows.Forms.NumericUpDown
        $numInt.Minimum = 60; $numInt.Maximum = 3600; $numInt.Increment = 30; $numInt.Width = Px 120
        $numInt.Value = [Math]::Min(3600, [Math]::Max(60, [int]$cfg.IntervalSec))
        Add-Field $cView 'Abfrageintervall (Sekunden)' $numInt 'Zu kurze Intervalle können zu Fehler 429 führen.' $IW

        $chkAuto = New-Object System.Windows.Forms.CheckBox
        $chkAuto.Text = 'Mit Windows starten'; $chkAuto.AutoSize = $true; $chkAuto.Checked = [bool]$cfg.Autostart
        $chkAuto.FlatStyle = 'Flat'; $chkAuto.FlatAppearance.BorderColor = $th.Border; $chkAuto.ForeColor = $th.Text
        $chkAuto.Font = New-Object System.Drawing.Font('Segoe UI', 9.5)
        $chkAuto.Margin = Pad 0 12 0 0
        $cView.Controls.Add($chkAuto)
        $outer.Controls.Add($cView)

        # --- Erweitert ---
        $cAdv = New-Card 'Erweitert'
        $tbCookies = New-Object System.Windows.Forms.TextBox
        $tbCookies.Width = $IW; $tbCookies.UseSystemPasswordChar = $true; $tbCookies.Text = Unprotect-Text $cfg.ExtraCookiesEnc
        Add-Field $cAdv 'Weitere Cookies (optional)' $tbCookies 'Nur bei Cloudflare-Fehler (403), z. B. "cf_clearance=…". Dann muss der User-Agent exakt dem Browser entsprechen.' $IW
        $tbUA = New-Object System.Windows.Forms.TextBox
        $tbUA.Width = $IW; $tbUA.Text = $cfg.UserAgent
        Add-Field $cAdv 'User-Agent' $tbUA $null $IW
        $outer.Controls.Add($cAdv)

        # --- Buttons ---
        $btnSave   = New-Button 'Speichern' $true; $btnSave.DialogResult = 'OK'
        $btnCancel = New-Button 'Abbrechen';       $btnCancel.DialogResult = 'Cancel'
        foreach ($b in @($btnSave, $btnCancel)) { $b.Margin = Pad 8 0 0 0 }
        $pBtn = New-ButtonBar $W
        $pBtn.Controls.AddRange(@($btnSave, $btnCancel))
        $outer.Controls.Add($pBtn)
        $f.AcceptButton = $btnSave; $f.CancelButton = $btnCancel

        $collect = {
            $c = $script:Cfg | ConvertTo-Json | ConvertFrom-Json
            $m = [regex]::Match($cbOrg.Text, '[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}')
            $c.OrgId           = $(if ($m.Success) { $m.Value } else { $cbOrg.Text.Trim() })
            $c.SessionKeyEnc   = Protect-Text ($tbKey.Text.Trim() -replace '^sessionKey=', '')
            $c.ExtraCookiesEnc = Protect-Text $tbCookies.Text.Trim()
            $c.UserAgent       = $tbUA.Text.Trim()
            $c.IntervalSec     = [int]$numInt.Value
            $c.IconMetric      = $metricKeys[$cbMetric.SelectedIndex]
            $c.RingMetric      = $metricKeys[$cbRing.SelectedIndex]
            $c.IconMode        = $modeKeys[$cbMode.SelectedIndex]
            $c.Autostart       = $chkAuto.Checked
            $c
        }
        $setMsg = {
            param([string]$msg, [bool]$ok)
            $lblTest.Text = $msg
            $lblTest.ForeColor = $(if ($ok) { $th.Ok } else { $th.Crit })
        }

        # Org-ID ermitteln – Annahme: GET /api/organizations liefert ein Array mit "uuid" und "name"
        $btnOrg.Add_Click({
            if (-not $tbKey.Text.Trim()) { & $setMsg 'Bitte zuerst den sessionKey eintragen.' $false; return }
            $c = & $collect
            try {
                $f.Cursor = [System.Windows.Forms.Cursors]::WaitCursor
                $r = Invoke-ClaudeGet $BaseUrl $c
                if ($r.Code -ne 200) { & $setMsg ('Abruf fehlgeschlagen: ' + (Get-HttpErrorText $r.Code $r.Cf)) $false; return }
                $orgs = $r.Body | ConvertFrom-Json
                $cbOrg.Items.Clear()
                foreach ($o in $orgs) { [void]$cbOrg.Items.Add(('{0}  [{1}]' -f $o.name, $o.uuid)) }
                if ($cbOrg.Items.Count -gt 0) {
                    $cbOrg.SelectedIndex = 0; $cbOrg.DroppedDown = $true
                    & $setMsg ('{0} Organisation(en) gefunden' -f $cbOrg.Items.Count) $true
                } else { & $setMsg 'Keine Organisationen gefunden.' $false }
            } catch {
                & $setMsg ('Fehler: ' + $_.Exception.GetBaseException().Message) $false
            } finally { $f.Cursor = [System.Windows.Forms.Cursors]::Default }
        })

        $btnTest.Add_Click({
            $c = & $collect
            try {
                $f.Cursor = [System.Windows.Forms.Cursors]::WaitCursor
                $r = Invoke-ClaudeGet ('{0}/{1}/usage' -f $BaseUrl, $c.OrgId) $c
                if ($r.Code -eq 200) {
                    $bs = Get-Buckets ($r.Body | ConvertFrom-Json)
                    $txt = ($bs | ForEach-Object { '{0} {1:0} %' -f $_.Short, $_.Used }) -join ' · '
                    & $setMsg ("Verbunden – $txt verwendet") $true
                } else {
                    & $setMsg ('Fehler: ' + (Get-HttpErrorText $r.Code $r.Cf)) $false
                }
            } catch {
                & $setMsg ('Nicht verbunden: ' + $_.Exception.GetBaseException().Message) $false
            } finally { $f.Cursor = [System.Windows.Forms.Cursors]::Default }
        })

        if ($f.ShowDialog() -eq 'OK') {
            $new = & $collect
            $script:Cfg = $new
            Save-Config $new
            try { Set-Autostart ([bool]$new.Autostart) } catch { }
            $script:PollTimer.Interval = [int]$new.IntervalSec * 1000
            $script:Buckets = @()
            if (Test-Configured) { Set-State 'connecting' 'Verbinde …' } else { Set-State 'unconfigured' 'Nicht konfiguriert' }
            Start-Refresh
        }
        $f.Dispose()
    } finally { $script:DialogOpen = $false }
}

# ---------- Start ----------
$script:Theme             = Get-Theme
$script:Cfg               = Get-Config
$script:Buckets           = @()
$script:State             = 'unconfigured'
$script:StateMsg          = ''
$script:RawJson           = $null
$script:LastUpdate        = $null
$script:Pending           = $null
$script:DialogOpen        = $false
$script:OpenSettingsAfter = $false
$script:DetailsForm       = $null
$script:DetailsBody       = $null
$script:DetailsTip        = $null

$script:Menu = New-Object System.Windows.Forms.ContextMenuStrip
$script:Menu.Add_Opening({ param($s, $e) Build-Menu; $e.Cancel = $false })

$script:Tray = New-Object System.Windows.Forms.NotifyIcon
$script:Tray.ContextMenuStrip = $script:Menu
$script:Tray.Add_MouseClick({ param($s, $e) if ($e.Button -eq [System.Windows.Forms.MouseButtons]::Left) { Show-Details } })

$script:PollTimer = New-Object System.Windows.Forms.Timer
$script:PollTimer.Interval = [Math]::Max(60, [int]$script:Cfg.IntervalSec) * 1000
$script:PollTimer.Add_Tick({ Start-Refresh })

$script:CheckTimer = New-Object System.Windows.Forms.Timer
$script:CheckTimer.Interval = 500
$script:CheckTimer.Add_Tick({
    $task = $script:Pending
    if (-not $task -or -not $task.IsCompleted) { return }
    $script:Pending = $null
    Complete-Refresh $task
})

if (Test-Configured) { Set-State 'connecting' 'Verbinde …' } else { Set-State 'unconfigured' 'Nicht konfiguriert' }
Build-Menu
$script:Tray.Visible = $true
if (-not (Test-Configured)) { Show-Settings }

Start-Refresh
$script:PollTimer.Start()
$script:CheckTimer.Start()

$script:Ctx = New-Object System.Windows.Forms.ApplicationContext
try {
    [System.Windows.Forms.Application]::Run($script:Ctx)
} finally {
    $script:Tray.Visible = $false
    $script:Tray.Dispose()
    $script:Http.Dispose()
    $script:Mutex.ReleaseMutex()
    $script:Mutex.Dispose()
}
