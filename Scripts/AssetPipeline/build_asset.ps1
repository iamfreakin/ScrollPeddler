#requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter()]
    [string]$Manifest = 'Scripts/AssetPipeline/assets/scroll_pickup_test_blockout.json',

    [Parameter()]
    [string]$BlenderExecutable,

    [Parameter()]
    [string]$UnrealEditorCmd,

    [Parameter()]
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:RepositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path -Path $PSScriptRoot -ChildPath '..\..')
).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
$script:RepositoryPrefix = $script:RepositoryRoot + [System.IO.Path]::DirectorySeparatorChar
$script:ProjectFile = Join-Path -Path $script:RepositoryRoot -ChildPath 'ScrollPeddler.uproject'
$script:BlenderExportScript = Join-Path -Path $PSScriptRoot -ChildPath 'blender\export_static_mesh.py'
$script:UnrealImportScript = Join-Path -Path $PSScriptRoot -ChildPath 'unreal\import_static_mesh.py'

function Test-IsRepositoryPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return $Path.Equals(
        $script:RepositoryRoot,
        [System.StringComparison]::OrdinalIgnoreCase
    ) -or $Path.StartsWith(
        $script:RepositoryPrefix,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function Assert-NoRepositoryReparsePoint {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-IsRepositoryPath -Path $Path)) {
        throw "Path is outside the repository: $Path"
    }

    $relative = $Path.Substring($script:RepositoryRoot.Length).TrimStart(
        [char[]]@([char]'\', [char]'/')
    )
    $current = $script:RepositoryRoot
    foreach ($part in ($relative -split '[\\/]' | Where-Object { $_ })) {
        $current = Join-Path -Path $current -ChildPath $part
        if (-not (Test-Path -LiteralPath $current)) {
            break
        }
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Repository asset paths cannot traverse a reparse point: $current"
        }
    }
}

function Resolve-RepositoryPath {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Field,
        [Parameter()][switch]$MustExist,
        [Parameter()][switch]$File
    )

    $controlCharacters = [char[]]@([char]13, [char]10, [char]0)
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value.IndexOfAny($controlCharacters) -ge 0) {
        throw "Manifest field '$Field' must be a non-empty path without control characters."
    }

    if ([System.IO.Path]::IsPathRooted($Value)) {
        $fullPath = [System.IO.Path]::GetFullPath($Value)
    }
    else {
        $fullPath = [System.IO.Path]::GetFullPath(
            (Join-Path -Path $script:RepositoryRoot -ChildPath $Value)
        )
    }

    if (-not (Test-IsRepositoryPath -Path $fullPath)) {
        throw "Manifest field '$Field' escapes the repository: $fullPath"
    }
    Assert-NoRepositoryReparsePoint -Path $fullPath

    if ($MustExist -and -not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Required file for '$Field' does not exist: $fullPath"
    }
    if ($File -and (Test-Path -LiteralPath $fullPath) -and
        -not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Manifest field '$Field' must point to a file: $fullPath"
    }
    return $fullPath
}

function Resolve-ToolExecutable {
    param(
        [Parameter()][AllowEmptyString()][string]$ExplicitPath,
        [Parameter(Mandatory = $true)][string[]]$Candidates,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $paths = @()
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $controlCharacters = [char[]]@([char]13, [char]10, [char]0)
        if ($ExplicitPath.IndexOfAny($controlCharacters) -ge 0) {
            throw "$Label executable path contains a control character."
        }
        $paths += [System.IO.Path]::GetFullPath($ExplicitPath)
    }
    else {
        $paths += $Candidates
    }

    foreach ($candidate in $paths) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Get-Item -LiteralPath $candidate).FullName
        }
    }
    throw "$Label executable was not found. Checked: $($paths -join ', ')"
}

function Get-RequiredProperty {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Field
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value -or
        [string]::IsNullOrWhiteSpace([string]$property.Value)) {
        throw "Manifest field '$Field' is required."
    }
    return $property.Value
}

function Assert-BlenderVersion {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$ExpectedVersion
    )

    $versionOutput = & $Executable --version 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Blender version probe failed with exit code $LASTEXITCODE."
    }
    $firstLine = [string]($versionOutput | Select-Object -First 1)
    if ($firstLine -notmatch ('^Blender\s+' + [regex]::Escape($ExpectedVersion) + '(?:\s|$)')) {
        throw "Blender version mismatch. Expected $ExpectedVersion, got '$firstLine'."
    }
}

function Assert-UnrealVersion {
    param([Parameter(Mandatory = $true)][string]$Executable)

    $win64Directory = Split-Path -Path $Executable -Parent
    $binariesDirectory = Split-Path -Path $win64Directory -Parent
    $engineDirectory = Split-Path -Path $binariesDirectory -Parent
    $buildVersionPath = Join-Path -Path $engineDirectory -ChildPath 'Build\Build.version'
    if (-not (Test-Path -LiteralPath $buildVersionPath -PathType Leaf)) {
        throw "Could not find Unreal Build.version beside the selected executable: $buildVersionPath"
    }

    $buildVersion = Get-Content -Raw -LiteralPath $buildVersionPath | ConvertFrom-Json
    if ([int]$buildVersion.MajorVersion -ne 5 -or [int]$buildVersion.MinorVersion -ne 8) {
        throw "Unreal version mismatch. Expected 5.8, got $($buildVersion.MajorVersion).$($buildVersion.MinorVersion)."
    }
}

function Invoke-CheckedNative {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Stage
    )

    Write-Host "[$Stage] $Executable"
    & $Executable @Arguments
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$Stage failed with exit code $exitCode."
    }
}

if (-not (Test-Path -LiteralPath $script:ProjectFile -PathType Leaf)) {
    throw "Unreal project file is missing: $script:ProjectFile"
}
if (-not (Test-Path -LiteralPath $script:BlenderExportScript -PathType Leaf)) {
    throw "Blender export script is missing: $script:BlenderExportScript"
}
if (-not (Test-Path -LiteralPath $script:UnrealImportScript -PathType Leaf)) {
    throw "Unreal import script is missing: $script:UnrealImportScript"
}

$manifestPath = Resolve-RepositoryPath -Value $Manifest -Field 'manifest' -MustExist -File
try {
    $manifestData = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
}
catch {
    throw "Could not parse asset manifest '$manifestPath': $($_.Exception.Message)"
}
if ([int](Get-RequiredProperty -Object $manifestData -Name 'schemaVersion' -Field 'schemaVersion') -ne 1) {
    throw 'Only asset manifest schemaVersion 1 is supported.'
}

$pipeline = Get-RequiredProperty -Object $manifestData -Name 'pipeline' -Field 'pipeline'
$paths = Get-RequiredProperty -Object $manifestData -Name 'paths' -Field 'paths'
$expectedBlenderVersion = [string](
    Get-RequiredProperty -Object $pipeline -Name 'blenderVersion' -Field 'pipeline.blenderVersion'
)
$sourceBlend = Resolve-RepositoryPath -Value ([string](
    Get-RequiredProperty -Object $paths -Name 'sourceBlend' -Field 'paths.sourceBlend'
)) -Field 'paths.sourceBlend' -MustExist -File
$fbxPath = Resolve-RepositoryPath -Value ([string](
    Get-RequiredProperty -Object $paths -Name 'fbx' -Field 'paths.fbx'
)) -Field 'paths.fbx' -File
$reportPath = Resolve-RepositoryPath -Value ([string](
    Get-RequiredProperty -Object $paths -Name 'report' -Field 'paths.report'
)) -Field 'paths.report' -File

if ([System.IO.Path]::GetExtension($sourceBlend) -ne '.blend') {
    throw "paths.sourceBlend must use the .blend extension: $sourceBlend"
}
if ([System.IO.Path]::GetExtension($fbxPath) -ne '.fbx') {
    throw "paths.fbx must use the .fbx extension: $fbxPath"
}
if ([System.IO.Path]::GetExtension($reportPath) -ne '.json') {
    throw "paths.report must use the .json extension: $reportPath"
}
$distinctAssetPaths = @($sourceBlend, $fbxPath, $reportPath) |
    Sort-Object -Unique
if ($distinctAssetPaths.Count -ne 3) {
    throw 'paths.sourceBlend, paths.fbx, and paths.report must be distinct files.'
}

$blender = Resolve-ToolExecutable `
    -ExplicitPath $BlenderExecutable `
    -Candidates @('C:\Program Files (x86)\Steam\steamapps\common\Blender\blender.exe') `
    -Label 'Steam Blender'
$unreal = Resolve-ToolExecutable `
    -ExplicitPath $UnrealEditorCmd `
    -Candidates @('C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe') `
    -Label 'UnrealEditor-Cmd 5.8'

Assert-BlenderVersion -Executable $blender -ExpectedVersion $expectedBlenderVersion
Assert-UnrealVersion -Executable $unreal

$blenderArguments = @(
    '--factory-startup',
    '--disable-autoexec',
    '--background',
    '--python-use-system-env',
    $sourceBlend,
    '--python',
    $script:BlenderExportScript,
    '--',
    '--manifest',
    $manifestPath
)
if ($ValidateOnly) {
    $blenderArguments += '--validate-only'
}
else {
    foreach ($outputDirectory in @(
        (Split-Path -Path $fbxPath -Parent),
        (Split-Path -Path $reportPath -Parent)
    ) | Select-Object -Unique) {
        if (-not (Test-Path -LiteralPath $outputDirectory)) {
            New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
        }
    }
}

$pythonEnvironmentNames = @(
    'PYTHONHASHSEED',
    'PYTHONNOUSERSITE',
    'PYTHONHOME',
    'PYTHONPATH'
)
$previousPythonEnvironment = @{}
foreach ($name in $pythonEnvironmentNames) {
    $previousPythonEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    # Blender's FBX exporter derives object UUIDs through Python hash(). Pin the
    # child-process seed so identical source data produces identical FBX bytes.
    # --python-use-system-env is required for Blender's embedded Python to honor it,
    # so remove host Python injection paths and keep user site-packages disabled.
    [Environment]::SetEnvironmentVariable('PYTHONHASHSEED', '0', 'Process')
    [Environment]::SetEnvironmentVariable('PYTHONNOUSERSITE', '1', 'Process')
    [Environment]::SetEnvironmentVariable('PYTHONHOME', $null, 'Process')
    [Environment]::SetEnvironmentVariable('PYTHONPATH', $null, 'Process')
    Invoke-CheckedNative -Executable $blender -Arguments $blenderArguments -Stage 'Blender export'
}
finally {
    foreach ($name in $pythonEnvironmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $previousPythonEnvironment[$name],
            'Process'
        )
    }
}

if (-not $ValidateOnly) {
    foreach ($output in @($fbxPath, $reportPath)) {
        if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
            throw "Blender completed without producing required output: $output"
        }
    }
}

$unrealArguments = @(
    $script:ProjectFile,
    '-unattended',
    '-nop4',
    '-nosplash',
    '-nullrhi',
    '-NoSound',
    '-stdout',
    '-FullStdOutLogOutput',
    '-run=PythonScript',
    "-script=$script:UnrealImportScript",
    "-SPAssetManifest=$manifestPath"
)
if ($ValidateOnly) {
    $unrealArguments += '-SPValidateOnly'
}

Invoke-CheckedNative -Executable $unreal -Arguments $unrealArguments -Stage 'Unreal import'

$mode = if ($ValidateOnly) { 'validate-only' } else { 'build' }
Write-Host "[SP_ASSET_PIPELINE] Completed mode=$mode manifest=$manifestPath"
