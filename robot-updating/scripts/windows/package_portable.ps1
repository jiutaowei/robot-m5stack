# 把米宝一号服务端打包成「可整体拷走、双击启动」的 Windows 应用目录。
#
#   pwsh -File robot-updating\scripts\windows\package_portable.ps1
#   pwsh -File robot-updating\scripts\windows\package_portable.ps1 -WithFfmpeg   # 附带 ffmpeg（+约 212MB）
#   pwsh -File robot-updating\scripts\windows\package_portable.ps1 -NoTrim      # 不做依赖裁剪（+约 1.1GB）
#
# 产物（默认 D:\mibao\dist\MibaoServer）：
#   runtime\      CPython 3.10 运行时（自包含，不依赖目标机装 Python）
#   runtime\Lib\site-packages\  依赖（默认裁掉 torch 等未使用的大包）
#   app\          服务端源码 + 配置 + 密钥 + 模型
#   tools\ffmpeg\ 可选：放 ffmpeg.exe（本地音频文件播放才需要）
#   启动米宝服务端.cmd      一键启动（双击即可）
#   首次运行-防火墙（管理员）.cmd  首次在本机放行 8001/8003/8004
#   使用说明.txt
#
# 目标机要求：Windows 10/11 x64；无需装 Python；首次运行需允许防火墙弹窗。

param(
    [string]$OutDir = 'D:\mibao\dist\MibaoServer',
    [switch]$WithFfmpeg,
    [switch]$NoTrim
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$srv  = Join-Path $repo 'robot-updating\xiaozhi-server'
$py   = Join-Path $repo '.tools\uv-python\cpython-3.10-windows-x86_64-none'

if (-not (Test-Path $py)) { throw "找不到自包含 Python 运行时：$py" }
if (-not (Test-Path (Join-Path $srv 'app.py'))) { throw "找不到服务端：$srv\app.py" }

# 未使用的大包（依据：服务端源码中无人 import；provider 是按需 importlib 加载的）
# torch 只被 silero_vad 包引用，而活跃 VAD 是 onnxruntime 版 core/providers/vad/silero.py
$RemovePackages = @(
    'torch', 'torchaudio', 'torchvision', 'silero_vad',
    'pandas', 'scipy', 'sklearn', 'numba', 'llvmlite', 'sympy', 'networkx',
    'google', 'googleapiclient', 'google_auth_httplib2', 'google_auth_oauthlib',
    'googleapis_common_protos', 'proto_plus', 'matplotlib', 'transformers',
    'jieba', 'mutagen', 'pygame', 'sherpa_onnx', 'tensorboard', 'tensorboard_data_server',
    'funasr', 'modelscope', 'datasets', 'huggingface_hub', 'tokenizers'
) | Where-Object { -not $NoTrim }

function Copy-Tree([string]$src, [string]$dst, [string[]]$excludeDirs = @(), [string[]]$excludeFiles = @()) {
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    $args = @($src, $dst, '/E', '/NFL', '/NDL', '/NJH', '/NJS', '/R:1', '/W:1')
    foreach ($d in $excludeDirs)  { $args += @('/XD', $d) }
    foreach ($f in $excludeFiles) { $args += @('/XF', $f) }
    & robocopy @args | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy 失败($LASTEXITCODE): $src -> $dst" }
}

Write-Host "== 组装到 $OutDir =="
if (Test-Path $OutDir) {
    try {
        Remove-Item $OutDir -Recurse -Force -ErrorAction Stop
    } catch {
        throw "无法清空 $OutDir ：$($_.Exception.Message)`n  多半是服务端还在运行，或有 cmd 窗口的工作目录停在包里——关掉那些窗口再重跑。"
    }
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host "[1/6] runtime（自包含 Python 3.10）"
Copy-Tree $py (Join-Path $OutDir 'runtime') @('__pycache__', 'Lib\test', 'Lib\idlelib')

Write-Host "[2/6] 依赖 -> runtime\Lib\site-packages"
$sp    = Join-Path $srv '.venv\Lib\site-packages'
$spDst = Join-Path $OutDir 'runtime\Lib\site-packages'
# 必须装进运行时自己的 site-packages：只有 site 目录里的 .pth 会被执行，
# 而 pywin32 正是靠 pywin32.pth（加 win32\lib 到 sys.path + 引导 DLL）才能被导入。
# 若放到独立目录再用 PYTHONPATH 指向它，.pth 不生效 -> import mcp 时
# ModuleNotFoundError: No module named 'pywintypes'（2026-10 实测踩过）。
New-Item -ItemType Directory -Force -Path $spDst | Out-Null
$xd = @('__pycache__')
foreach ($p in $RemovePackages) {
    $xd += (Join-Path $sp $p)
    $xd += (Join-Path $sp "$p.libs")
}
Copy-Tree $sp $spDst $xd

# 被裁掉的包会留下失效的 .pth（google 的 nspkg 命名空间钩子等），
# site 处理时报 AttributeError 刷屏，这里按目标路径是否存在清掉。
Get-ChildItem $spDst -Filter '*.pth' -File -ErrorAction SilentlyContinue | ForEach-Object {
    $pth = $_
    $drop = $false
    if ($pth.Name -like '*-nspkg.pth') {
        $ns = [regex]::Matches((Get-Content $pth.FullName -Raw), "\*\('([^']+)'") |
              ForEach-Object { $_.Groups[1].Value }
        if ($ns.Count -gt 0 -and -not (Test-Path (Join-Path $spDst ($ns -join '\')))) { $drop = $true }
    } else {
        foreach ($line in (Get-Content $pth.FullName -ErrorAction SilentlyContinue)) {
            $t = ($line -split '#')[0].Trim()
            if (-not $t -or $t.StartsWith('import ')) { continue }
            if (-not (Test-Path (Join-Path $spDst $t))) { $drop = $true }
        }
    }
    if ($drop) { Write-Host "      清理失效的 .pth：$($pth.Name)"; Remove-Item $pth.FullName -Force }
}

Write-Host "[3/6] app（源码+配置+密钥+模型）"
Copy-Tree $srv (Join-Path $OutDir 'app') @('tmp', '.venv', '__pycache__', 'logs') @('*.pyc')

Write-Host "[4/6] 启动器与说明"
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir 'tools\ffmpeg') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir 'lib') | Out-Null

# opus.dll：opuslib_next 用 ctypes 按 PATH 搜索 libopus，Windows 没有系统版，
# 必须随包携带并加进 PATH（开发环境是 .tools\mibao-server-start.ps1 干的这件事）
$opus = Join-Path $repo '.tools\opus\bin\opus.dll'
if (Test-Path $opus) {
    Copy-Item $opus (Join-Path $OutDir 'lib\opus.dll') -Force
    Write-Host "      已附带 opus.dll（音频编解码必需）"
} else {
    throw "缺少 $opus —— 便携版没有它无法启动（先跑 .tools\fetch-opus.ps1）"
}

# 启动器本身只用 ASCII：chcp 65001 之后，GBK 编码的中文会被 cmd 解析成乱码。
# 中文提示放在 banner.txt（UTF-8），由 type 输出（此时控制台已是 65001，显示正常）。
$banner = @'
============================================================
  米宝一号 AI 服务端
  对话端口 8001 / OTA+接口 8003 / 设备发现 8004
  机器人连到同一个 WiFi 或手机热点后会自动发现本机
  日志：app\tmp\server.log      关闭本窗口 = 停止服务
============================================================
'@
Set-Content -Path (Join-Path $OutDir 'banner.txt') -Value $banner -Encoding utf8

$launcher = @'
@echo off
chcp 65001 >nul
setlocal
title Mibao-1 AI Server
cd /d "%~dp0app"
set "PATH=%~dp0lib;%~dp0runtime;%~dp0runtime\Scripts;%~dp0tools\ffmpeg;%PATH%"
set PYTHONUTF8=1
set PYTHONIOENCODING=utf-8
if exist "%~dp0tools\ffmpeg\ffmpeg.exe" (set "MIBAO_ALLOW_NO_FFMPEG=") else (set "MIBAO_ALLOW_NO_FFMPEG=1")
type "%~dp0banner.txt"
"%~dp0runtime\python.exe" app.py
echo.
echo [Server stopped] Press any key to close this window...
pause >nul
'@
Set-Content -Path (Join-Path $OutDir '启动米宝服务端.cmd') -Value $launcher -Encoding ascii

# 真·可执行程序：一个极小的 .NET exe（WinExe，双击不闪黑框），内部拉起上面的 .cmd，
# 保留控制台窗口和日志。目标机不需要装 Python，也不需要额外装运行库
# （.NET Framework 4.x 是 Windows 10/11 自带的）。
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (Test-Path $csc) {
    $csSrc = @'
using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

static class MibaoLauncher
{
    [STAThread]
    static void Main()
    {
        string dir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
        string bat = Path.Combine(dir, "启动米宝服务端.cmd");
        if (!File.Exists(bat))
        {
            MessageBox.Show("缺少启动脚本：" + Environment.NewLine + bat,
                            "米宝一号 AI 服务端", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        var psi = new ProcessStartInfo("cmd.exe", "/c \"\"" + bat + "\"\"");
        psi.UseShellExecute = true;
        psi.WorkingDirectory = dir;
        Process.Start(psi);
    }
}
'@
    $csPath  = Join-Path $env:TEMP 'mibao_launcher.cs'
    $exeTemp = Join-Path $env:TEMP 'mibao_launcher.exe'
    # 带 BOM 写源码：csc 才按 UTF-8 解析里面的中文
    [System.IO.File]::WriteAllText($csPath, $csSrc, (New-Object System.Text.UTF8Encoding $true))
    # 先编译到纯 ASCII 临时路径，再拷成中文名（csc 命令行对非 ANSI 路径不稳）
    & $csc /nologo /target:winexe /utf8output /r:System.Windows.Forms.dll "/out:$exeTemp" $csPath | Out-Null
    if ($LASTEXITCODE -eq 0 -and (Test-Path $exeTemp)) {
        Copy-Item $exeTemp (Join-Path $OutDir '米宝服务端.exe') -Force
        Write-Host "      已生成 米宝服务端.exe（双击即启动）"
    } else {
        Write-Host "      ⚠️ exe 启动器编译失败，双击 启动米宝服务端.cmd 即可"
    }
    Remove-Item $csPath, $exeTemp -Force -ErrorAction SilentlyContinue
} else {
    Write-Host "      ⚠️ 未找到 csc.exe，跳过 exe 启动器（双击 启动米宝服务端.cmd 即可）"
}

$firewall = @'
@echo off
chcp 65001 >nul
title Allow Mibao ports in firewall (administrator required)
net session >nul 2>&1
if errorlevel 1 (
  echo [FAILED] Administrator rights required.
  echo          Right-click this file -^> "Run as administrator"
  pause & exit /b 1
)
set PY=%~dp0runtime\python.exe
netsh advfirewall firewall delete rule name="MibaoServer TCP" >nul 2>&1
netsh advfirewall firewall delete rule name="MibaoServer UDP" >nul 2>&1
netsh advfirewall firewall add rule name="MibaoServer TCP" dir=in action=allow program="%PY%" protocol=TCP localport=8001,8003 enable=yes profile=any
netsh advfirewall firewall add rule name="MibaoServer UDP" dir=in action=allow program="%PY%" protocol=UDP localport=8004 enable=yes profile=any
echo [OK] Ports 8001/8003 (TCP) and 8004 (UDP) are allowed.
pause
'@
Set-Content -Path (Join-Path $OutDir '首次运行-防火墙（管理员）.cmd') -Value $firewall -Encoding ascii

$readme = @'
米宝一号 AI 服务端（便携版）使用说明
=====================================

一、启动
  1) 把整个 MibaoServer 文件夹拷到目标 Windows 电脑（任意位置，路径别含中文也可）
  2) 首次使用：右键「首次运行-防火墙（管理员）.cmd」→ 以管理员身份运行（放行 8001/8003/8004）
     若跳过这步，第一次启动时 Windows 会弹窗询问，勾选「专用网络+公用网络」并允许即可
  3) 双击「米宝服务端.exe」（或「启动米宝服务端.cmd」）→ 出现黑色窗口并打印服务地址即为启动成功
     ⚠️ 该窗口不能关闭，关闭即停止服务
     提示：exe 与 cmd 是同一个东西（exe 就是替你双击 cmd），任选一个即可；
           .cmd 内容可读，排错时想看清每一步就用它。

二、网络（手机热点 / 无 WiFi 场景）
  - 电脑连上手机热点；机器人也连同一个热点
  - 机器人通过 UDP 广播自动发现本机（无需改配置）
  - 若热点开了「客户端隔离」，广播可能不通；此时在机器人屏幕的
    设置 → 米宝 URL 里填 http://<本机在热点里的IP>:8003/xiaozhi/ota/
    本机 IP 在启动窗口的日志里能看到（OTA接口是 http://x.x.x.x:8003/...）

三、日志与排错
  - 运行日志：app\tmp\server.log
  - 窗口里出现「Websocket地址是 ws://...」即代表已就绪
  - 常见问题：
    · 机器人连不上 → 先确认同一网段、防火墙已放行；再看日志有没有「请求发现」
    · 提示缺少 ffmpeg → 只影响播放本地音频文件；把 ffmpeg.exe 放进 tools\ffmpeg\ 再重启

四、目录说明
  runtime\     自带 Python 运行时（目标机不需要装 Python）
  runtime\Lib\site-packages\  依赖库（就地安装，不要移动）
  app\         服务端源码与配置（含 data\.config.yaml 与 .keys\ 私钥，请勿外传）
  tools\ffmpeg\ 可选：放 ffmpeg.exe
'@
Set-Content -Path (Join-Path $OutDir '使用说明.txt') -Value $readme -Encoding utf8

# 自检：模拟启动器的环境（无 PYTHONPATH、只有 PATH 指向 lib/runtime），
# 提前暴露「依赖被裁掉」这类只在目标机才会炸的问题。
Write-Host "[5/6] 自检（无 PYTHONPATH 导入关键依赖 + 语法编译）"
$savedPath = $env:PATH
$env:PATH = "$(Join-Path $OutDir 'lib');$(Join-Path $OutDir 'runtime');$(Join-Path $OutDir 'runtime\Scripts');$env:PATH"
Remove-Item Env:PYTHONPATH -ErrorAction SilentlyContinue
$env:PYTHONUTF8 = '1'
$check = & (Join-Path $OutDir 'runtime\python.exe') -c @'
import importlib, sys
mods = ['pywintypes', 'mcp', 'opuslib_next', 'onnxruntime', 'edge_tts', 'httpx',
        'websockets', 'aiohttp', 'pydub', 'yaml', 'loguru', 'cryptography', 'requests', 'numpy']
bad = []
for m in mods:
    try:
        importlib.import_module(m)
    except Exception as e:
        bad.append(f'{m}: {type(e).__name__}: {e}')
if bad:
    print('FAILED:' + '; '.join(bad)); sys.exit(1)
import compileall
ok = compileall.compile_dir(sys.argv[1], quiet=2, force=True)
print('IMPORTS_OK; COMPILE_OK' if ok else 'IMPORTS_OK; COMPILE_FAILED')
sys.exit(0 if ok else 1)
'@ (Join-Path $OutDir 'app')
$env:PATH = $savedPath
if ($LASTEXITCODE -ne 0) {
    Write-Host $check
    throw "自检未通过：$OutDir 的依赖不完整，先别分发"
}
Write-Host "      $check"

if ($WithFfmpeg) {
    $ff = Get-ChildItem 'C:\Users\*\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg*' -Recurse -Filter 'ffmpeg.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ff) {
        Copy-Item $ff.FullName (Join-Path $OutDir 'tools\ffmpeg\ffmpeg.exe') -Force
        Write-Host "      已附带 ffmpeg: $($ff.FullName)"
    } else { Write-Host "      ⚠️ 未找到 ffmpeg.exe（可自行放入 tools\ffmpeg\）" }
}

Write-Host "[6/6] 体积统计"
foreach ($d in @('runtime', 'app', 'tools')) {
    $p = Join-Path $OutDir $d
    if (Test-Path $p) {
        $sz = (Get-ChildItem $p -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
        Write-Host ("      {0,-9} {1,8:N1} MB" -f $d, ($sz / 1MB))
    }
}
$total = (Get-ChildItem $OutDir -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("      合计      {0,8:N1} MB" -f ($total / 1MB))
Write-Host "== 完成：$OutDir =="
