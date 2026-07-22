#requires -Version 5.1

[CmdletBinding()]
param(
    [string] $InstallRoot = (Join-Path $env:LOCALAPPDATA 'ScrollPeddler\Tools\BlenderMCP'),
    [string] $BlenderExecutable = '',
    [switch] $EnableAddon,
    [switch] $ForceAddonOverwrite,
    [switch] $VerifyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RequiredCommandPath {
    param([Parameter(Mandatory)][string] $Name)

    $Command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $Command) {
        throw "Required command '$Name' was not found on PATH."
    }

    return $Command.Source
}

function Resolve-UvExecutable {
    $Command = Get-Command 'uv' -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }

    $WinGetPackages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path -LiteralPath $WinGetPackages -PathType Container) {
        foreach ($PackageDirectory in (Get-ChildItem -LiteralPath $WinGetPackages -Directory |
            Where-Object Name -Like 'astral-sh.uv_*' |
            Sort-Object LastWriteTime -Descending)) {
            $Candidate = Join-Path $PackageDirectory.FullName 'uv.exe'
            if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
                return $Candidate
            }
        }
    }

    throw "Required command 'uv' was not found. Install the pinned version documented in Docs/BLENDER_MCP_SETUP.md."
}

function Invoke-CheckedNative {
    param(
        [Parameter(Mandatory)][string] $Executable,
        [Parameter(Mandatory)][string[]] $Arguments
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Executable $($Arguments -join ' ')"
    }
}

function Resolve-SteamBlenderExecutable {
    param([string] $ExplicitPath)

    if ($ExplicitPath) {
        if (-not (Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            throw "Blender executable was not found: $ExplicitPath"
        }

        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $SteamRoots = [System.Collections.Generic.List[string]]::new()
    foreach ($RegistryPath in @(
        'HKCU:\Software\Valve\Steam',
        'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
        'HKLM:\SOFTWARE\Valve\Steam'
    )) {
        if (-not (Test-Path -LiteralPath $RegistryPath)) {
            continue
        }

        $Properties = Get-ItemProperty -LiteralPath $RegistryPath
        foreach ($PropertyName in @('SteamPath', 'InstallPath')) {
            $Property = $Properties.PSObject.Properties[$PropertyName]
            if ($Property -and $Property.Value) {
                $SteamRoots.Add($Property.Value)
            }
        }
    }

    foreach ($DefaultRoot in @('C:\Program Files (x86)\Steam', 'C:\Program Files\Steam')) {
        if (Test-Path -LiteralPath $DefaultRoot) {
            $SteamRoots.Add($DefaultRoot)
        }
    }

    $LibraryRoots = [System.Collections.Generic.List[string]]::new()
    foreach ($SteamRoot in ($SteamRoots | Sort-Object -Unique)) {
        $LibraryRoots.Add($SteamRoot)
        $LibraryFile = Join-Path $SteamRoot 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $LibraryFile -PathType Leaf)) {
            continue
        }

        $LibraryText = Get-Content -LiteralPath $LibraryFile -Raw
        foreach ($Match in [regex]::Matches($LibraryText, '"path"\s+"([^"]+)"')) {
            $LibraryRoots.Add(($Match.Groups[1].Value -replace '\\\\', '\'))
        }
    }

    foreach ($LibraryRoot in ($LibraryRoots | Sort-Object -Unique)) {
        $Candidate = Join-Path $LibraryRoot 'steamapps\common\Blender\blender.exe'
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }

    throw 'Steam Blender was not found. Pass -BlenderExecutable with an explicit blender.exe path.'
}

function Resolve-SafeInstallRoot {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $RepositoryRoot
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'InstallRoot cannot be empty.'
    }

    $FullPath = [IO.Path]::GetFullPath($Path).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    $VolumeRoot = [IO.Path]::GetPathRoot($FullPath).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    if ($FullPath -eq $VolumeRoot) {
        throw "InstallRoot cannot be a drive root: $FullPath"
    }

    $RepositoryFullPath = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    if ($FullPath.Equals($RepositoryFullPath, [StringComparison]::OrdinalIgnoreCase) -or
        $FullPath.StartsWith(
            $RepositoryFullPath + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase
        )) {
        throw "InstallRoot must remain outside the Git repository: $FullPath"
    }

    if (Test-Path -LiteralPath $FullPath) {
        $Item = Get-Item -LiteralPath $FullPath -Force
        if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "InstallRoot cannot be a reparse point: $FullPath"
        }
    }

    return $FullPath
}

function Assert-PinnedCheckout {
    param(
        [Parameter(Mandatory)][string] $GitExecutable,
        [Parameter(Mandatory)][string] $SourceDirectory,
        [Parameter(Mandatory)][string] $ExpectedRepository,
        [Parameter(Mandatory)][string] $ExpectedRevision
    )

    $ActualRepository = (& $GitExecutable -C $SourceDirectory remote get-url origin).Trim()
    if ($LASTEXITCODE -ne 0 -or
        -not $ActualRepository.Equals($ExpectedRepository, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected Blender MCP origin. Expected $ExpectedRepository, got $ActualRepository."
    }

    $ActualRevision = (& $GitExecutable -C $SourceDirectory rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $ActualRevision -ne $ExpectedRevision) {
        throw "Unexpected Blender MCP revision. Expected $ExpectedRevision, got $ActualRevision."
    }

    $CheckoutChanges = @(& $GitExecutable -C $SourceDirectory status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to inspect Blender MCP checkout: $SourceDirectory"
    }
    if ($CheckoutChanges.Count -gt 0) {
        throw "Blender MCP checkout is not clean:`n$($CheckoutChanges -join [Environment]::NewLine)"
    }

    $PackageDirectory = Join-Path $SourceDirectory 'src\blender_mcp'
    $TrackedPython = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($TrackedPath in @(& $GitExecutable -C $SourceDirectory ls-files -- 'src/blender_mcp')) {
        if ($TrackedPath.EndsWith('.py', [StringComparison]::OrdinalIgnoreCase)) {
            [void] $TrackedPython.Add($TrackedPath.Replace('\', '/'))
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to enumerate tracked Blender MCP Python files: $SourceDirectory"
    }

    $SourcePrefix = [IO.Path]::GetFullPath($SourceDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar) +
        [IO.Path]::DirectorySeparatorChar
    foreach ($PythonFile in (Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File -Filter '*.py')) {
        $PythonFullPath = [IO.Path]::GetFullPath($PythonFile.FullName)
        if (-not $PythonFullPath.StartsWith($SourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Python source resolved outside the pinned checkout: $PythonFullPath"
        }
        $RelativePath = $PythonFullPath.Substring($SourcePrefix.Length).Replace('\', '/')
        if (-not $TrackedPython.Contains($RelativePath)) {
            throw "Untracked Python source exists in the Blender MCP package: $RelativePath"
        }
    }

    $PackagePrefix = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar) +
        [IO.Path]::DirectorySeparatorChar
    foreach ($Artifact in (Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File)) {
        $ArtifactFullPath = [IO.Path]::GetFullPath($Artifact.FullName)
        if (-not $ArtifactFullPath.StartsWith($PackagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Package artifact resolved outside the pinned checkout: $ArtifactFullPath"
        }
        $RelativePath = $ArtifactFullPath.Substring($PackagePrefix.Length).Replace('\', '/')
        if ($RelativePath -match '(?i)(^|/)config\.(py|pyc|pyo)$' -or
            $RelativePath -match '(?i)(^|/)__pycache__/config\..*\.py[co]$') {
            throw "Ignored Blender MCP config code exists and could bypass the pinned source: $RelativePath"
        }
    }

    return $ActualRevision
}

function Get-BlenderMcpState {
    param([Parameter(Mandatory)][string] $BlenderExecutable)

    $PythonExpression = @"
import bpy, json; addon = bpy.context.preferences.addons.get('addon'); prefs = addon.preferences if addon else None; scene = bpy.context.scene; state = {'enabled': addon is not None, 'telemetryConsent': bool(prefs.telemetry_consent) if prefs else None, 'polyHaven': bool(getattr(scene, 'blendermcp_use_polyhaven', True)), 'sketchfab': bool(getattr(scene, 'blendermcp_use_sketchfab', True)), 'hyper3D': bool(getattr(scene, 'blendermcp_use_hyper3d', True)), 'hunyuan3D': bool(getattr(scene, 'blendermcp_use_hunyuan3d', True))}; print('SP_BLENDER_MCP_STATE=' + json.dumps(state))
"@.Trim()
    $Output = @(& $BlenderExecutable --background --python-expr $PythonExpression 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to inspect Blender MCP preferences:`n$($Output -join [Environment]::NewLine)"
    }

    $StateLine = $Output | Where-Object { $_ -like 'SP_BLENDER_MCP_STATE=*' } | Select-Object -Last 1
    if (-not $StateLine) {
        throw "Blender MCP preference state was not returned:`n$($Output -join [Environment]::NewLine)"
    }

    return ($StateLine.Substring('SP_BLENDER_MCP_STATE='.Length) | ConvertFrom-Json)
}

$ManifestPath = Join-Path $PSScriptRoot 'manifest.json'
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$InstallRoot = Resolve-SafeInstallRoot -Path $InstallRoot -RepositoryRoot $RepositoryRoot
$GitExecutable = Get-RequiredCommandPath -Name 'git'
$UvExecutable = Resolve-UvExecutable
$ResolvedBlenderExecutable = Resolve-SteamBlenderExecutable -ExplicitPath $BlenderExecutable
$SourceDirectory = Join-Path $InstallRoot "source-$($Manifest.upstream.shortRevision)"
$AddonSource = Join-Path $SourceDirectory 'addon.py'
$ServerExecutable = Join-Path $SourceDirectory '.venv\Scripts\blender-mcp.exe'
$InstalledAddon = Join-Path $env:APPDATA "Blender Foundation\Blender\$($Manifest.blender.profileVersion)\scripts\addons\addon.py"

if ($ForceAddonOverwrite -and -not $EnableAddon) {
    throw '-ForceAddonOverwrite requires -EnableAddon.'
}

if (-not $VerifyOnly) {
    if ($EnableAddon -and (Get-Process 'blender' -ErrorAction SilentlyContinue)) {
        throw 'Close all Blender processes before installing or enabling the addon.'
    }
    if ($EnableAddon -and
        (Get-NetTCPConnection -LocalPort $Manifest.connection.port -State Listen -ErrorAction SilentlyContinue)) {
        throw "Port $($Manifest.connection.port) is already in use. Stop the listener before enabling Blender MCP."
    }

    if (-not (Test-Path -LiteralPath $SourceDirectory)) {
        New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
        Invoke-CheckedNative -Executable $GitExecutable -Arguments @(
            'clone', '--filter=blob:none', '--no-checkout',
            $Manifest.upstream.repository, $SourceDirectory
        )
        Invoke-CheckedNative -Executable $GitExecutable -Arguments @(
            '-C', $SourceDirectory, 'checkout', '--detach', $Manifest.upstream.revision
        )
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $SourceDirectory '.git') -PathType Container)) {
    throw "Pinned Blender MCP checkout was not found: $SourceDirectory"
}

$ActualRevision = Assert-PinnedCheckout `
    -GitExecutable $GitExecutable `
    -SourceDirectory $SourceDirectory `
    -ExpectedRepository $Manifest.upstream.repository `
    -ExpectedRevision $Manifest.upstream.revision

$ActualAddonHash = (Get-FileHash -LiteralPath $AddonSource -Algorithm SHA256).Hash
if ($ActualAddonHash -ne $Manifest.upstream.addonSha256) {
    throw "Unexpected addon.py SHA256. Expected $($Manifest.upstream.addonSha256), got $ActualAddonHash."
}

$UvVersion = (& $UvExecutable --version).Trim()
$UvVersionMatch = [regex]::Match($UvVersion, '^uv\s+([0-9]+(?:\.[0-9]+)+)')
if (-not $UvVersionMatch.Success -or $UvVersionMatch.Groups[1].Value -ne $Manifest.runtime.uvVersion) {
    throw "Expected uv $($Manifest.runtime.uvVersion), got '$UvVersion'."
}

$BlenderVersionLine = (& $ResolvedBlenderExecutable --version | Select-Object -First 1).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Failed to query Blender version: $ResolvedBlenderExecutable"
}
if ($BlenderVersionLine -ne "Blender $($Manifest.blender.version)") {
    throw "Expected Blender $($Manifest.blender.version), got '$BlenderVersionLine'."
}

if (-not $VerifyOnly) {
    $PreviousPythonPreference = $env:UV_PYTHON_PREFERENCE
    try {
        $env:UV_PYTHON_PREFERENCE = 'only-managed'
        Invoke-CheckedNative -Executable $UvExecutable -Arguments @(
            'sync', '--frozen', '--python', $Manifest.runtime.pythonVersion,
            '--no-dev', '--project', $SourceDirectory
        )
    }
    finally {
        $env:UV_PYTHON_PREFERENCE = $PreviousPythonPreference
    }

    $ActualRevision = Assert-PinnedCheckout `
        -GitExecutable $GitExecutable `
        -SourceDirectory $SourceDirectory `
        -ExpectedRepository $Manifest.upstream.repository `
        -ExpectedRevision $Manifest.upstream.revision

    if ($EnableAddon) {
        $ShouldInstallAddon = -not (Test-Path -LiteralPath $InstalledAddon -PathType Leaf)
        if (-not $ShouldInstallAddon) {
            $InstalledAddonHash = (Get-FileHash -LiteralPath $InstalledAddon -Algorithm SHA256).Hash
            if ($InstalledAddonHash -ne $Manifest.upstream.addonSha256) {
                if (-not $ForceAddonOverwrite) {
                    throw "A different addon.py already exists at $InstalledAddon. Use -ForceAddonOverwrite to back it up and replace it."
                }

                $BackupPath = "$InstalledAddon.backup-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
                Copy-Item -LiteralPath $InstalledAddon -Destination $BackupPath
                Write-Warning "Existing addon.py was backed up to $BackupPath"
                $ShouldInstallAddon = $true
            }
        }

        $HadAddonPathEnvironment = Test-Path Env:SP_BLENDER_MCP_ADDON_PATH
        $PreviousAddonPathEnvironment = $env:SP_BLENDER_MCP_ADDON_PATH
        $env:SP_BLENDER_MCP_ADDON_PATH = $AddonSource
        try {
            $InstallStatement = if ($ShouldInstallAddon) {
                "bpy.ops.preferences.addon_install(filepath=addon_path, overwrite=True); "
            }
            else {
                ''
            }
            $PythonExpression = "import bpy, os; addon_path = os.environ['SP_BLENDER_MCP_ADDON_PATH']; $InstallStatement" +
                "bpy.ops.preferences.addon_enable(module='addon'); prefs = bpy.context.preferences.addons['addon'].preferences; " +
                "prefs.telemetry_consent = False; bpy.ops.wm.save_userpref(); print('BLENDER_MCP_ADDON_ENABLED=True')"
            Invoke-CheckedNative -Executable $ResolvedBlenderExecutable -Arguments @(
                '--background', '--python-expr', $PythonExpression
            )
        }
        finally {
            if ($HadAddonPathEnvironment) {
                $env:SP_BLENDER_MCP_ADDON_PATH = $PreviousAddonPathEnvironment
            }
            else {
                Remove-Item Env:SP_BLENDER_MCP_ADDON_PATH -ErrorAction SilentlyContinue
            }
        }
    }
}

if (-not (Test-Path -LiteralPath $ServerExecutable -PathType Leaf)) {
    throw "Blender MCP server executable was not found: $ServerExecutable"
}

$RuntimePython = Join-Path $SourceDirectory '.venv\Scripts\python.exe'
$RuntimeProbe = @(& $RuntimePython -c "import blender_mcp, importlib.metadata, json, pathlib, sys; print(json.dumps({'python': '.'.join(map(str, sys.version_info[:3])), 'package': importlib.metadata.version('blender-mcp'), 'module': str(pathlib.Path(blender_mcp.__file__).resolve())}))")
if ($LASTEXITCODE -ne 0 -or $RuntimeProbe.Count -ne 1) {
    throw "Failed to inspect the pinned Blender MCP runtime: $RuntimePython"
}
$RuntimeState = $RuntimeProbe[0] | ConvertFrom-Json
if ($RuntimeState.python -ne $Manifest.runtime.pythonVersion) {
    throw "Expected Python $($Manifest.runtime.pythonVersion), got $($RuntimeState.python)."
}
if ($RuntimeState.package -ne $Manifest.upstream.packageVersion) {
    throw "Expected blender-mcp $($Manifest.upstream.packageVersion), got $($RuntimeState.package)."
}
$ExpectedModuleRoot = [IO.Path]::GetFullPath((Join-Path $SourceDirectory 'src')).TrimEnd([IO.Path]::DirectorySeparatorChar)
$ActualModulePath = [IO.Path]::GetFullPath($RuntimeState.module)
if (-not $ActualModulePath.StartsWith(
    $ExpectedModuleRoot + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase
)) {
    throw "blender_mcp was imported outside the pinned checkout: $ActualModulePath"
}

if (Test-Path -LiteralPath $InstalledAddon -PathType Leaf) {
    $InstalledAddonHash = (Get-FileHash -LiteralPath $InstalledAddon -Algorithm SHA256).Hash
    if ($InstalledAddonHash -ne $Manifest.upstream.addonSha256) {
        throw "Installed addon.py does not match the pinned source: $InstalledAddon"
    }
}
elseif ($EnableAddon -or $VerifyOnly) {
    throw "Blender MCP addon was not found in the Blender profile: $InstalledAddon"
}

if ($EnableAddon -or $VerifyOnly) {
    $BlenderMcpState = Get-BlenderMcpState -BlenderExecutable $ResolvedBlenderExecutable
    if (-not $BlenderMcpState.enabled) {
        throw 'Blender MCP addon is installed but not enabled.'
    }
    if ($BlenderMcpState.telemetryConsent) {
        throw 'Blender MCP addon telemetry consent is enabled.'
    }
    foreach ($IntegrationName in @('polyHaven', 'sketchfab', 'hyper3D', 'hunyuan3D')) {
        if ($Manifest.externalIntegrations.$IntegrationName -or $BlenderMcpState.$IntegrationName) {
            throw "External Blender MCP integration is enabled: $IntegrationName"
        }
    }
}

$TomlServerPath = $ServerExecutable -replace '\\', '/'
$EnabledTools = ($Manifest.codex.enabledTools | ForEach-Object { '"' + $_ + '"' }) -join ', '
$TelemetryKey = $Manifest.connection.telemetryEnvironmentVariable
$TelemetryValue = $Manifest.connection.telemetryEnvironmentValue

Write-Output "Blender MCP revision: $ActualRevision"
Write-Output "Blender executable: $ResolvedBlenderExecutable"
Write-Output "Server executable: $ServerExecutable"
Write-Output 'Add the following block to the user-level Codex config.toml:'
Write-Output @"
[mcp_servers.blender]
enabled = true
required = false
command = "$TomlServerPath"
env = { $TelemetryKey = "$TelemetryValue", BLENDER_HOST = "$($Manifest.connection.host)", BLENDER_PORT = "$($Manifest.connection.port)" }
startup_timeout_sec = 20.0
tool_timeout_sec = 180.0
enabled_tools = [$EnabledTools]
"@
