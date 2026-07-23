# Scroll Peddler — First Technical Spike

This spike proves the smallest host-authoritative expedition loop on UE 5.8:

1. A host opens `/Game/Maps/TechSpike` as a listen server.
2. Two locally controlled players join through Unreal's raw IP travel.
3. Each owning pawn requests a server-validated pickup.
4. The server validates and consumes one scroll instance.
5. Players reach the extraction overlap.
6. The server calculates one result per player.
7. Each owning client writes and reload-verifies its local `SaveGame`.
8. The server marks settlement committed only after all client acknowledgements match the issued result hashes.

The spike intentionally does not call a proprietary backend, Online Services
or session APIs, a dedicated server, Iris, GAS, or a production inventory UI.
`OnlineSubsystemUtils` is enabled only for UE's raw `IpNetDriver`; its declared
engine dependencies are therefore present in the build receipt even though the
gameplay code uses direct IP travel only.

## Content

- Map: `/Game/Maps/TechSpike`
- Base family: `/Game/Data/Scrolls/DA_Scroll_VeilOfSilence`
- Engravings: `DA_Engraving_Amplified`, `DA_Engraving_Stable`
- Instance axes: base family, one selected engraving, D–S quality,
  contamination, and misfire are represented separately.

Binary content can be regenerated idempotently from
`Scripts/generate_spike_content.py` with UnrealEditor-Cmd and the project's
editor-only Python plugins.

## Manual two-player run

In the host console:

```text
SPHost 2
```

In the second process console:

```text
SPJoin 127.0.0.1
```

Controls:

- `W/A/S/D`: move
- Mouse: look
- `E`: request pickup of the visible scroll under the crosshair
- `Q`: request use of the first inventory scroll

The first-person crosshair is white by default, cyan over a valid pickup,
yellow while the request is pending, green after server acceptance, and red
after a rejection.

The cylindrical extraction marker is on the opposite side of the graybox room.
Normal extraction is driven by the server-side overlap callback.

## Verification commands

Run the following commands from the repository root. Change `UE_ROOT` if the
engine is installed elsewhere.

```powershell
$UE_ROOT = 'C:\Program Files\Epic Games\UE_5.8'
$ProjectRoot = (Resolve-Path '.').Path
$Project = (Resolve-Path '.\ScrollPeddler.uproject').Path
```

Development Editor:

```powershell
& "$UE_ROOT\Engine\Build\BatchFiles\Build.bat" `
  ScrollPeddlerEditor Win64 Development `
  "-Project=$Project" -WaitMutex -NoHotReloadFromIDE -architecture=x64
```

Development Game:

```powershell
& "$UE_ROOT\Engine\Build\BatchFiles\Build.bat" `
  ScrollPeddler Win64 Development `
  "-Project=$Project" -WaitMutex -architecture=x64
```

Automation:

```powershell
$Report = Join-Path $ProjectRoot 'Saved\AutomationReports\TechSpike'

& "$UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  $Project -unattended -nop4 -nosplash -nullrhi `
  '-ExecCmds=Automation RunTests ScrollPeddler;Quit' `
  "-ReportExportPath=$Report"
```

Windows package:

```powershell
$PackageRoot = Join-Path $ProjectRoot 'Saved\Packages\Windows'

& "$UE_ROOT\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
  "-project=$Project" -nop4 -utf8output -unattended `
  -target=ScrollPeddler -platform=Win64 -clientconfig=Development `
  -build -cook '-map=/Game/Maps/TechSpike' -stage -pak -iostore `
  -package -archive "-archivedirectory=$PackageRoot"
```

`-SPAutoSpike` and `-SPAutoQuit` exist only for unattended Development smoke
tests. The auto-extraction RPC rejects Shipping builds. Pass a distinct
`-SPProfileSlot=` value to each same-PC process so they do not share a save
slot. `-SPAutoContestedPickup` makes each process target the lowest stable
pickup ID after the expected roster is present, then fall back to the nearest
available pickup. In non-Shipping builds it also keeps the first claimed actor
addressable for five seconds, so the second request reaches the server and the
smoke path deterministically exercises one authoritative `Contested` pickup
rejection.

## Current limits

- Joining is direct IP travel; Steam lobby/session integration is a later step.
- A disconnect during extraction or the save-ack barrier leaves the run
  incomplete by design; reconnect/resume is not part of this spike.
- The automated two-process smoke covers the successful replicated loop and
  disk persistence. Distance, line-of-sight, ownership, duplicate-claim, and
  consume-once guards are implemented server-side; a broader adversarial
  multiplayer functional suite remains follow-up work.
