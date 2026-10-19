<#
.SYNOPSIS
    Installs or checks the dev tools ScrollPeddler expects on a new PC.

.DESCRIPTION
    - Node.js LTS (winget)  : needed by Backlog.md, codex-plugin-cc, npx skills
    - Backlog.md (npm -g)   : task board stored in ./backlog, MCP server for Claude Code
    - Git + Git LFS check   : reported only; pass -InstallGit to install Git for Windows (includes LFS)

    Safe to run again: anything already installed is skipped, Backlog.md is updated.
    winget asks for admin rights and license agreements itself; answer those prompts.

.EXAMPLE
    pwsh -File Tools/Setup-DevTools.ps1
    pwsh -File Tools/Setup-DevTools.ps1 -InstallGit
#>
param(
    [switch]$InstallGit
)

$ErrorActionPreference = 'Stop'

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

function Test-Command([string]$Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Install-WithWinget([string]$Id) {
    if (-not (Test-Command 'winget')) {
        throw "winget not found. Install 'App Installer' from Microsoft Store, then run this script again."
    }
    winget install --id $Id --exact --source winget
    if ($LASTEXITCODE -ne 0) { throw "winget install $Id failed (exit $LASTEXITCODE)." }
    Update-SessionPath
}

Write-Host '== Node.js =='
if (Test-Command 'node') {
    Write-Host "node $(node --version) already installed."
} else {
    Write-Host 'node not found. Installing Node.js LTS with winget...'
    Install-WithWinget 'OpenJS.NodeJS.LTS'
    if (-not (Test-Command 'node')) { throw 'Node.js installed but not on PATH. Open a new terminal and run again.' }
    Write-Host "node $(node --version) installed."
}

Write-Host ''
Write-Host '== Backlog.md =='
npm install -g backlog.md
if ($LASTEXITCODE -ne 0) { throw "npm install -g backlog.md failed (exit $LASTEXITCODE)." }
Update-SessionPath
if (-not (Test-Command 'backlog')) { throw 'backlog installed but not on PATH. Open a new terminal and run again.' }
Write-Host "backlog $(backlog --version)"

Write-Host ''
Write-Host '== Git / Git LFS =='
if (-not (Test-Command 'git')) {
    if ($InstallGit) {
        Write-Host 'git not found. Installing Git for Windows with winget...'
        Install-WithWinget 'Git.Git'
    } else {
        Write-Host 'git not found on PATH. Run again with -InstallGit, or add your existing git to PATH.' -ForegroundColor Yellow
    }
}
if (Test-Command 'git') {
    Write-Host (git --version)
    $lfs = git lfs version 2>$null
    if ($LASTEXITCODE -eq 0) { Write-Host $lfs } else { Write-Host 'git lfs not available. Install Git LFS, then run: git lfs install' -ForegroundColor Yellow }
}

Write-Host ''
Write-Host 'Done. Restart Claude (or open a new session) so it picks up the new PATH and .mcp.json.'
Write-Host 'Superpowers is an account plugin: it follows your claude.ai account, nothing to install per PC.'
