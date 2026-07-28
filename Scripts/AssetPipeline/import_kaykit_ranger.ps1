#requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter()]
    [string]$UnrealEditorCmd =
        'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe',

    [Parameter()]
    [switch]$ValidateOnly,

    [Parameter()]
    [switch]$RepairMaterial,

    [Parameter()]
    [switch]$SyncAnimations
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (@($ValidateOnly, $RepairMaterial, $SyncAnimations).Where({ $_ }).Count -gt 1) {
    throw '-ValidateOnly, -RepairMaterial, and -SyncAnimations cannot be used together.'
}

$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path -Path $PSScriptRoot -ChildPath '..\..')
)
$projectFile = Join-Path -Path $repositoryRoot -ChildPath 'ScrollPeddler.uproject'
$importScript = Join-Path -Path $PSScriptRoot -ChildPath 'unreal\import_kaykit_character.py'
$manifest = Join-Path -Path $PSScriptRoot -ChildPath 'assets\kaykit_ranger_pilot.json'

foreach ($requiredFile in @($UnrealEditorCmd, $projectFile, $importScript, $manifest)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file does not exist: $requiredFile"
    }
}

$engineRoot = Split-Path -Path (
    Split-Path -Path (
        Split-Path -Path $UnrealEditorCmd -Parent
    ) -Parent
) -Parent
$buildVersionPath = Join-Path -Path $engineRoot -ChildPath 'Build\Build.version'
$buildVersion = Get-Content -Raw -LiteralPath $buildVersionPath | ConvertFrom-Json
if ([int]$buildVersion.MajorVersion -ne 5 -or [int]$buildVersion.MinorVersion -ne 8) {
    throw "Unreal Engine 5.8 is required; found $($buildVersion.MajorVersion).$($buildVersion.MinorVersion)."
}

$arguments = @(
    $projectFile,
    '-unattended',
    '-nop4',
    '-nosplash',
    '-nullrhi',
    '-NoSound',
    '-stdout',
    '-FullStdOutLogOutput',
    '-run=PythonScript',
    "-script=$importScript",
    "-SPKayKitManifest=$manifest"
)
if ($ValidateOnly) {
    $arguments += '-SPValidateOnly'
}
elseif ($RepairMaterial) {
    $arguments += '-SPRepairMaterial'
}
elseif ($SyncAnimations) {
    $arguments += '-SPSyncAnimations'
}

Push-Location -LiteralPath $repositoryRoot
try {
    & $UnrealEditorCmd @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "KayKit Unreal import failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}

$mode = if ($ValidateOnly) {
    'validate-only'
}
elseif ($RepairMaterial) {
    'repair-material'
}
elseif ($SyncAnimations) {
    'sync-animations'
}
else {
    'import'
}
Write-Host "[SP_KAYKIT_IMPORT] Completed mode=$mode manifest=$manifest"
