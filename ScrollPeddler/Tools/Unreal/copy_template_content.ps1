# Copies the engine's First Person template content into this project, the same layout "Add Feature Pack" uses:
#   TemplateResources/High/Characters/Content -> Content/Characters   (mannequin meshes, animations)
#   TemplateResources/High/Input/Content      -> Content/Input
#   TP_FirstPersonBP/Content/FirstPerson      -> Content/FirstPerson  (arms anim blueprint; template level left out)
# These folders are git-ignored, so run this once per machine before opening the project.
# Needs "Templates and Feature Packs" installed for the engine (Epic Games Launcher > UE 5.8 > Options).
param(
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = 'Stop'
$content = Join-Path $PSScriptRoot '..\..\Content' | Resolve-Path
$resources = Join-Path $EngineRoot 'Templates\TemplateResources\High'
$template = Join-Path $EngineRoot 'Templates\TP_FirstPersonBP\Content\FirstPerson'

if (-not (Test-Path $resources) -or -not (Test-Path $template)) {
    throw "Template content not found under $EngineRoot. Install 'Templates and Feature Packs' in the Epic Games Launcher."
}

$copies = @(
    @{ From = Join-Path $resources 'Characters\Content'; To = Join-Path $content 'Characters' },
    @{ From = Join-Path $resources 'Input\Content'; To = Join-Path $content 'Input' }
)
foreach ($copy in $copies) {
    if (Test-Path $copy.To) { Write-Host "Skip (exists): $($copy.To)"; continue }
    Copy-Item $copy.From $copy.To -Recurse
    Write-Host "Copied: $($copy.To)"
}

$firstPerson = Join-Path $content 'FirstPerson'
if (Test-Path $firstPerson) {
    Write-Host "Skip (exists): $firstPerson"
} else {
    New-Item -ItemType Directory $firstPerson | Out-Null
    Get-ChildItem $template | Where-Object { $_.Name -notlike 'Lvl_FirstPerson*' } | Copy-Item -Destination $firstPerson -Recurse
    Write-Host "Copied: $firstPerson"
}
