param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

function Resolve-MsBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = $null

    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath | Out-String
        $installPath = $installPath.Trim()
        if ($installPath) {
            $candidate = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $candidate) {
                $msbuild = $candidate
            }
        }
    }

    if (-not $msbuild) {
        $legacy = Get-Command msbuild.exe -ErrorAction SilentlyContinue
        if ($legacy) {
            $msbuild = $legacy.Source
        }
    }

    if (-not $msbuild) {
        throw "MSBuild not found. Install Visual Studio Build Tools or ensure msbuild.exe is on PATH."
    }

    return $msbuild
}

$project = Join-Path $PSScriptRoot "DotHiderNative.vcxproj"
if (-not (Test-Path $project)) {
    throw "Project file not found: $project"
}

# Normalize environment variable casing to avoid MSBuild's inherited PATH/PATH duplicate bug
$normalizedPath = [System.Environment]::GetEnvironmentVariable("Path", "Process")
if ($null -ne $normalizedPath) {
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:Path = $normalizedPath
}

$msbuild = Resolve-MsBuild
& $msbuild $project /m /p:Configuration=$Configuration /p:Platform=$Platform
