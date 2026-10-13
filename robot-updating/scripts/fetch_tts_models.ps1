# 下载离线 TTS 所需模型 + 搭建 Kokoro 独立环境（模型是几百 MB，不入 git）。
#
#   pwsh -File robot-updating\scripts\fetch_tts_models.ps1
#   pwsh -File robot-updating\scripts\fetch_tts_models.ps1 -SkipVenv    # 只下模型
#
# 产物：
#   robot-updating\xiaozhi-server\models\piper\zh_CN-huayan-medium.onnx(.json)   约 60MB
#   robot-updating\xiaozhi-server\models\kokoro\kokoro-v1.0.onnx                 约 310MB
#   robot-updating\xiaozhi-server\models\kokoro\voices.json                      约 8MB（由单音色 .bin 合成）
#   .tools\kokoro-venv\                                                          约 575MB（隔离环境）
#
# 说明：HuggingFace 主站在大陆不可达，这里走 hf-mirror.com；PyPI 走阿里云镜像。

param(
    [switch]$SkipVenv
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$srv  = Join-Path $repo 'robot-updating\xiaozhi-server'
$models = Join-Path $srv 'models'
$venv = Join-Path $repo '.tools\kokoro-venv'
$HF = 'https://hf-mirror.com'
$PIP = 'https://mirrors.aliyun.com/pypi/simple/'

function Get-File([string]$url, [string]$dst) {
    if (Test-Path $dst) { Write-Host "  已存在 $(Split-Path -Leaf $dst)"; return }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Invoke-WebRequest -Uri $url -OutFile $dst -TimeoutSec 1800
    $sw.Stop()
    Write-Host ("  {0,-34} {1,7:N1} MB  {2:N0}s" -f (Split-Path -Leaf $dst), ((Get-Item $dst).Length / 1MB), $sw.Elapsed.TotalSeconds)
}

Write-Host "== 1/4 Piper 中文模型（轻量兜底） =="
$p = "$HF/rhasspy/piper-voices/resolve/main/zh/zh_CN/huayan/medium"
Get-File "$p/zh_CN-huayan-medium.onnx" "$models\piper\zh_CN-huayan-medium.onnx"
Get-File "$p/zh_CN-huayan-medium.onnx.json" "$models\piper\zh_CN-huayan-medium.onnx.json"

Write-Host "== 2/4 Kokoro 模型与中文音色 =="
$k = "$HF/onnx-community/Kokoro-82M-v1.0-ONNX/resolve/main"
Get-File "$k/onnx/model.onnx" "$models\kokoro\kokoro-v1.0.onnx"
foreach ($v in 'zf_xiaobei', 'zf_xiaoxiao', 'zm_yunxi') {
    Get-File "$k/voices/$v.bin" "$models\kokoro\$v.bin"
}

if ($SkipVenv) {
    Write-Host "== 跳过独立环境搭建（-SkipVenv） =="
} else {
    Write-Host "== 3/4 Kokoro 独立环境（kokoro-onnx 依赖 librosa/numba/scipy，与主环境隔离） =="
    $uv = (Get-Command uv -ErrorAction SilentlyContinue).Source
    if (-not $uv) { $uv = Join-Path $env:USERPROFILE '.uv\bin\uv.exe' }
    if (-not (Test-Path $uv)) { throw "找不到 uv，请先安装（https://docs.astral.sh/uv/）" }

    if (-not (Test-Path "$venv\Scripts\python.exe")) {
        & $uv venv $venv --python 3.10
    }
    & $uv pip install --python "$venv\Scripts\python.exe" --index-url $PIP `
        kokoro-onnx "misaki[zh]" "numpy<2" "onnxruntime==1.20.1"
    if ($LASTEXITCODE -ne 0) { throw "kokoro 依赖安装失败" }
}

Write-Host "== 4/4 由单音色 .bin 生成 kokoro-onnx 需要的 voices.json =="
$py = if (Test-Path "$venv\Scripts\python.exe") { "$venv\Scripts\python.exe" } else { 'python' }
$script = Join-Path $PSScriptRoot '_build_kokoro_voices.py'
@'
import json, sys
import numpy as np
from pathlib import Path

d = Path(sys.argv[1])
out = d / "voices.json"
voices = {}
for f in sorted(d.glob("*.bin")):
    arr = np.fromfile(f, dtype=np.float32)
    if arr.size % 256:
        raise SystemExit(f"{f.name} 大小异常: {arr.size}")
    arr = arr.reshape(-1, 1, 256)
    if arr.shape[0] < 511:                      # 官方是 511 行，补一行防越界
        arr = np.concatenate([arr, arr[-1:]], axis=0)
    voices[f.stem] = arr.tolist()
    print(f"  {f.stem}: {arr.shape}")
out.write_text(json.dumps(voices), encoding="utf-8")
print(f"  已写 {out} ({out.stat().st_size/1e6:.1f} MB)")
'@ | Set-Content -Path $script -Encoding utf8
& $py $script (Join-Path $models 'kokoro')
Remove-Item $script -Force -ErrorAction SilentlyContinue

Write-Host @"

== 完成 ==
Kokoro 环境: $venv\Scripts\python.exe
在 data/.config.yaml 里选用：selected_module.TTS = KokoroTTS（配置模板见 .config.yaml.template）
"@
