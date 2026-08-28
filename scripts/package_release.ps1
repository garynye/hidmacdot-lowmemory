[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [string]$ExecutablePath,

    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Path $PSScriptRoot -Parent
if (-not $ExecutablePath) {
    $ExecutablePath = Join-Path $repositoryRoot "x64\Release\DotHiderNative.exe"
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot ("out\v" + $Version)
}

$requiredFiles = @{
    Executable = $ExecutablePath
    Readme = Join-Path $repositoryRoot "README.md"
    License = Join-Path $repositoryRoot "LICENSE"
}
foreach ($item in $requiredFiles.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $item.Value -PathType Leaf)) {
        throw "$($item.Key) file not found: $($item.Value)"
    }
}

$resolvedExecutable = (Resolve-Path -LiteralPath $ExecutablePath).Path
$resolvedOutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
[void](New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force)

$assetBaseName = "DotHiderNative-v$Version-windows-x64"
$directExecutable = Join-Path $resolvedOutputDirectory ($assetBaseName + ".exe")
$zipPath = Join-Path $resolvedOutputDirectory ($assetBaseName + ".zip")
$checksumsPath = Join-Path $resolvedOutputDirectory "SHA256SUMS.txt"
$stagingDirectory = Join-Path $resolvedOutputDirectory ($assetBaseName + "-package")

if (Test-Path -LiteralPath $stagingDirectory) {
    Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
}

try {
    Copy-Item -LiteralPath $resolvedExecutable -Destination $directExecutable -Force
    [void](New-Item -ItemType Directory -Path $stagingDirectory)
    Copy-Item -LiteralPath $resolvedExecutable -Destination (Join-Path $stagingDirectory "DotHiderNative.exe")
    Copy-Item -LiteralPath $requiredFiles.Readme -Destination (Join-Path $stagingDirectory "README.md")
    Copy-Item -LiteralPath $requiredFiles.License -Destination (Join-Path $stagingDirectory "LICENSE")

    Compress-Archive -Path (Join-Path $stagingDirectory "*") -DestinationPath $zipPath -CompressionLevel Optimal -Force

    $checksumLines = foreach ($assetPath in @($directExecutable, $zipPath)) {
        $hash = Get-FileHash -LiteralPath $assetPath -Algorithm SHA256
        "{0}  {1}" -f $hash.Hash.ToLowerInvariant(), [IO.Path]::GetFileName($assetPath)
    }
    $checksumLines | Set-Content -LiteralPath $checksumsPath -Encoding ASCII
}
finally {
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }
}

Get-Item -LiteralPath $directExecutable, $zipPath, $checksumsPath |
    Select-Object Name, Length, LastWriteTime
