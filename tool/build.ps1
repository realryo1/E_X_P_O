param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Clean,
    [switch]$Run
)

# Find VS Installation Path using vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null

if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
}

$msbuild = $null
if ($vsPath) {
    $msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuild)) {
        $msbuild = Join-Path $vsPath "MSBuild\15.0\Bin\MSBuild.exe"
    }
}

# Fallback paths
if (-not $msbuild -or -not (Test-Path $msbuild)) {
    $fallbackPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($path in $fallbackPaths) {
        if (Test-Path $path) {
            $msbuild = $path
            break
        }
    }
}

if (-not $msbuild -or -not (Test-Path $msbuild)) {
    Write-Error "MSBuild.exe was not found. Please install Visual Studio or check MSBuild path."
    exit 1
}

Write-Host "Using MSBuild: $msbuild" -ForegroundColor Cyan
Write-Host "Configuration: $Configuration" -ForegroundColor Cyan

# Clean only
if ($Clean) {
    Write-Host "Cleaning solution..." -ForegroundColor Yellow
    & $msbuild "expogame.sln" /t:Clean /p:Configuration=$Configuration /p:Platform="x64"
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0) {
        Write-Host "Clean Succeeded!" -ForegroundColor Green
    } else {
        Write-Error "Clean Failed with exit code $exitCode"
        exit $exitCode
    }
    exit 0
}

# Build
Write-Host "Building solution..." -ForegroundColor Yellow
& $msbuild "expogame.sln" /p:Configuration=$Configuration /p:Platform="x64" /m /v:minimal

$exitCode = $LASTEXITCODE
if ($exitCode -eq 0) {
    Write-Host "Build Succeeded!" -ForegroundColor Green
    if ($Run) {
        Write-Host "Starting application..." -ForegroundColor Cyan
        $exePath = "x64/$Configuration/expogame.exe"
        if (Test-Path $exePath) {
            $fullExe = (Resolve-Path $exePath).Path
            $workDir = (Get-Item .).FullName
            $proc = Start-Process -FilePath $fullExe -WorkingDirectory $workDir -PassThru -Wait
            exit $proc.ExitCode
        } else {
            Write-Error "Executable not found at $exePath"
        }
    }
} else {
    Write-Error "Build Failed with exit code $exitCode"
    exit $exitCode
}
