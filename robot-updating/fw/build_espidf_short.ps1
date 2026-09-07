param(
    [string]$MirrorRoot = "D:\zhuomian\金禾实习\智能机器人\fw",
    [string]$ComponentCache = "D:\zhuomian\金禾实习\智能机器人\.idf-component-cache",
    [string]$IdfProfile = "C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1",
    [string]$BuildDir = "build_noccache",
    [string]$ShortDrive = "R:",
    [string]$ShortIdfDrive = "I:",
    [switch]$SkipSync
)

$ErrorActionPreference = "Stop"

$source = (Resolve-Path $PSScriptRoot).Path
$mirror = $MirrorRoot

if (-not (Test-Path -LiteralPath $IdfProfile)) {
    throw "ESP-IDF PowerShell profile not found: $IdfProfile"
}

Write-Host "[build_espidf_short] source: $source"
Write-Host "[build_espidf_short] mirror: $mirror"
Write-Host "[build_espidf_short] build dir: $BuildDir"

if (-not $SkipSync) {
    if (-not (Test-Path -LiteralPath $mirror)) {
        New-Item -ItemType Directory -Path $mirror -Force | Out-Null
    }

    $excludedDirectories = @(
        "build",
        "build_*",
        ".component-cache*",
        ".component-quarantine"
    )

    $robocopyArgs = @(
        $source,
        $mirror,
        "/MIR",
        "/R:1",
        "/W:1",
        "/XD"
    ) + $excludedDirectories + @(
        "/NFL",
        "/NDL",
        "/NJH",
        "/NJS",
        "/NP"
    )

    Write-Host "[build_espidf_short] syncing firmware to short path..."
    & robocopy @robocopyArgs | Out-Host
    $robocopyExit = $LASTEXITCODE
    if ($robocopyExit -ge 8) {
        throw "robocopy failed with exit code $robocopyExit"
    }
    Write-Host "[build_espidf_short] robocopy exit code: $robocopyExit"
} else {
    Write-Host "[build_espidf_short] sync skipped"
}

Write-Host "[build_espidf_short] loading ESP-IDF profile..."
. $IdfProfile

# Some Windows shells expose both PATH and Path. ESP-IDF updates PATH, while
# CMake/Ninja may inherit Path. Keep only the ESP-IDF-enriched value.
$toolPath = $env:PATH
Remove-Item Env:Path -ErrorAction SilentlyContinue
$env:Path = $toolPath

$env:IDF_COMPONENT_CACHE_PATH = $ComponentCache
$env:IDF_CCACHE_ENABLE = "0"
$env:CCACHE_DISABLE = "1"
$profileIdfPath = $env:IDF_PATH

$shortIdfDriveName = $ShortIdfDrive.TrimEnd("\")
if ($shortIdfDriveName -notmatch "^[A-Za-z]:$") {
    throw "ShortIdfDrive must be a drive name like I:, got: $ShortIdfDrive"
}
if ($shortIdfDriveName -ieq $ShortDrive.TrimEnd("\")) {
    throw "ShortIdfDrive must be different from ShortDrive"
}

$resolvedIdfPath = (Resolve-Path -LiteralPath $env:IDF_PATH).Path.TrimEnd("\")
$existingIdfSubst = (& subst.exe $shortIdfDriveName 2>$null) -join "`n"
if ($LASTEXITCODE -eq 0 -and $existingIdfSubst) {
    if ($existingIdfSubst -notmatch [regex]::Escape($resolvedIdfPath)) {
        throw "$shortIdfDriveName is already mapped to another path: $existingIdfSubst"
    }
} else {
    Write-Host "[build_espidf_short] mapping $shortIdfDriveName to IDF path: $resolvedIdfPath"
    & subst.exe $shortIdfDriveName $resolvedIdfPath
    if ($LASTEXITCODE -ne 0) {
        throw "subst failed for $shortIdfDriveName -> $resolvedIdfPath"
    }
}
$env:IDF_PATH = $shortIdfDriveName + "\"

$workspaceRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $MirrorRoot)).Path
$shortDriveName = $ShortDrive.TrimEnd("\")
if ($shortDriveName -notmatch "^[A-Za-z]:$") {
    throw "ShortDrive must be a drive name like R:, got: $ShortDrive"
}

$existingSubst = (& subst.exe $shortDriveName 2>$null) -join "`n"
if ($LASTEXITCODE -eq 0 -and $existingSubst) {
    if ($existingSubst -notmatch [regex]::Escape($workspaceRoot)) {
        throw "$shortDriveName is already mapped to another path: $existingSubst"
    }
} else {
    Write-Host "[build_espidf_short] mapping $shortDriveName to workspace root: $workspaceRoot"
    & subst.exe $shortDriveName $workspaceRoot
    if ($LASTEXITCODE -ne 0) {
        throw "subst failed for $shortDriveName -> $workspaceRoot"
    }
}

$shortRoot = $shortDriveName + "\"
$rootWithSlash = $workspaceRoot.TrimEnd("\") + "\"
if ($env:IDF_PATH.StartsWith($rootWithSlash, [System.StringComparison]::OrdinalIgnoreCase)) {
    $relativeIdfPath = $env:IDF_PATH.Substring($rootWithSlash.Length)
    $env:IDF_PATH = Join-Path $shortRoot $relativeIdfPath
}
$mirror = Join-Path $shortRoot (Split-Path -Leaf $MirrorRoot)
$env:IDF_COMPONENT_CACHE_PATH = Join-Path $shortRoot (Split-Path -Leaf $ComponentCache)
Write-Host "[build_espidf_short] active mirror: $mirror"
Write-Host "[build_espidf_short] active IDF_PATH: $env:IDF_PATH"

Set-Location $mirror
$buildPath = Join-Path $mirror $BuildDir
$cachePath = Join-Path $buildPath "CMakeCache.txt"
if (Test-Path -LiteralPath $cachePath) {
    $staleCcacheCache = Select-String -LiteralPath $cachePath -Pattern "^CCACHE_ENABLE:.*=(1|ON|TRUE|YES)$" -Quiet
    $staleLongPathCache = Select-String -LiteralPath $cachePath -SimpleMatch -Pattern $profileIdfPath, ($profileIdfPath -replace "\\", "/") -Quiet
    if ($staleCcacheCache -or $staleLongPathCache) {
        $resolvedMirror = (Resolve-Path -LiteralPath $mirror).Path.TrimEnd("\")
        $resolvedBuildPath = (Resolve-Path -LiteralPath $buildPath).Path
        if (-not $resolvedBuildPath.StartsWith($resolvedMirror + "\", [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove build directory outside mirror: $resolvedBuildPath"
        }
        Write-Host "[build_espidf_short] removing stale build dir: $resolvedBuildPath"
        Remove-Item -LiteralPath $resolvedBuildPath -Recurse -Force
    }
}

Write-Host "[build_espidf_short] running idf.py build..."
idf.py --no-ccache -B $BuildDir -DCCACHE_ENABLE=0 build
exit $LASTEXITCODE
