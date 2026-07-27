# Scroll Peddler — Vertical Slice Technical Spike

This document describes the executable technical spike as of 2026-07-27. The
spike is a host-authoritative UE 5.8 listen-server vertical slice for one to
four players. It now has two network entry paths:

- Legacy Online Subsystem Steam public lobbies for create, find, indexed join,
  Quick Play, and platform friend-invite acceptance.
- Direct IP travel as a development and recovery path, normally forced with
  `-nosteam`.

Steam lobby code is no longer a future-only placeholder. However, the Steam
path has only been compiled and covered by deterministic policy tests. A live
multi-account Steam package run, real friend invite, and voice call have not
yet been verified and must not be treated as complete.

The spike does not use a proprietary backend, a dedicated server, Iris, Online
Services, GAS, host migration, or a production inventory/lobby UI.

## Proven runtime loop

The current `TechSpike` flow is:

1. The listen-server host loads or creates one of three host-owned campaign
   slots.
2. One to four players form a roster through a Steam lobby or raw IP travel.
3. The server generates and replicates a deterministic eight-room dungeon
   layout from a seed, including room roles, connections, markers, and a
   checksum. Runtime graybox floors, labels, and corridors visualize it.
4. The server spawns four deterministic scroll pickup instances plus generic
   material, equipment, and large-cargo world items.
5. Players use the replicated one-hand/four-bag inventory, collect or drop
   items, and decide whether to consume or preserve scrolls.
6. Movement and actions publish authoritative noise. Echo Hunter and Paper
   Eater graybox threats react to players and items, while the threat director
   uses elapsed time, player count, and recent noise pressure to schedule
   additional pressure and fear events.
7. Each player extracts independently. Only that player's carried items are
   included in the successful outcome; unresolved players become missing.
8. The host evaluates the runtime delivery contract and atomically commits
   extracted items, gold, and guild XP to the host campaign. Repeating the same
   run settlement does not duplicate rewards.
9. The legacy client result `SaveGame` and acknowledgement remain as a
   secondary receipt/telemetry path. They are not the source of truth and no
   longer gate an already committed host campaign transaction.

The normal run phase order is `Preparing -> Expedition (25 minutes) ->
Collapse (2 minutes) -> Resolution -> Settlement`. Player condition progresses
through `Normal -> Injured -> Down -> Missing`; repeated Down states use
45/30/15-second bleed-out windows.

## Runtime content

- Map: `/Game/Maps/TechSpike`
- Playable scroll family:
  `/Game/Data/Scrolls/DA_Scroll_VeilOfSilence`
- Engravings: `DA_Engraving_Amplified`, `DA_Engraving_Stable`
- Four scroll pickups: stable instance IDs, B quality, alternating engravings
- Generic world items:
  - `material.paper_scrap` (stack of three)
  - `equipment.archive_lantern`
  - `cargo.bound_archive_crate`
- Deterministic dungeon: eight unique room roles in one connected layout, with
  the objective at least four edges from the entry
- Threats: Echo Hunter, Paper Eater, and the server-side threat director

The current playable scroll asset resolves to the Resonance/noise-suppression
family. C++ resolver and application paths also exist for damage, healing,
protection, movement, and detection, but those five families do not yet have
playable Data Assets, presentation, or balanced content.

Scroll base family, engraving, D-S quality, contamination, and malfunction are
kept as separate instance axes. The server rebuilds trusted use state from the
inventory and deterministically resolves fizzle, delay, direction shift, or
extra noise before consuming the exact instance once. Client-selected request
IDs are excluded from the malfunction hash, so retry identifiers cannot be
searched to avoid a bad roll.

Binary spike content can be regenerated idempotently from
`Scripts/generate_spike_content.py` with UnrealEditor-Cmd and the project's
editor-only Python plugins.

## Network entry paths

### Legacy OSS Steam lobby

Run packaged Development builds with Steam running and use the in-game console:

```text
SPCreateLobby 4
```

This creates a public listen-server lobby and opens `TechSpike` after the
session succeeds. Other players can search and join:

```text
SPFindLobbies
SPJoinLobby 0
```

Search results and their indices are written to the log. Quick Play searches
compatible public lobbies and selects the lowest-ping valid candidate:

```text
SPQuickPlay
```

Lobby compatibility checks include build ID, rules version, Hub/Expedition
phase, open slots, joinability, and invite metadata. Starting the expedition
updates the advertised session so new joins and invites are rejected.

The subsystem also registers the Legacy OSS platform invite-accepted delegate.
An accepted friend invite is validated against the same metadata and then
joined; a client destroys its old local session first when required. A host
already serving a lobby rejects replacement by an incoming invite. There is no
in-game "send invite" UI yet.

These commands and the invite acceptance path are implemented, but they have
not yet passed a real two-account Steam end-to-end run. App ID 480 is a
development setting, not production deployment configuration.

### Raw IP fallback

Use `-nosteam` on every process to force the tested raw IP path. In the host
console:

```text
SPHost 4
```

In each client console:

```text
SPJoin 127.0.0.1
```

`SPHost` clamps the expected roster to one through four and opens
`/Game/Maps/TechSpike` as a listen server. `SPJoin` accepts an IP address or
Unreal travel URL.

Raw IP remains a development fallback rather than a player-facing matchmaking
flow. In particular, it does not provide a durable platform identity for a
production reconnect experience.

## Controls

| Input | Action |
|---|---|
| `W/A/S/D` | Move |
| Mouse | Look |
| `Space` | Jump |
| Hold `Left Shift` | Sprint |
| Hold `Left Ctrl` | Crouch |
| `E` | Interact/pick up the targeted scroll or generic world item |
| `Q` or `Left Mouse Button` | Use the scroll currently in hand |
| `1`-`4` | Atomically swap the selected bag slot with the hand |
| `R` | Begin the 20-second self-treatment action |
| `G` | Drop the item currently in hand |
| Hold `V` | Push to talk through the configured legacy voice path |
| `` ` `` / `~` | Open the developer console |

The crosshair is white by default, cyan over a valid pickup, yellow while a
pickup request is pending, green after server acceptance, and red after a
rejection. The HUD also shows run timing, condition, stamina, hand/bag state,
party readiness, recent chat, carried cargo, and temporary scroll effects.

Large cargo is hand-only. While carrying it, sprint is disabled, movement speed
is reduced to 72 percent, and movement produces more noise.

Party development commands are also available:

```text
SPReady true
SPChat message
SPKick PlayerId
SPVoteKick PlayerId
SPVote VoteId true
SPMutePlayer PlayerId true
```

## Authority and reconnect boundaries

Pickup, use, drop, inventory swap, party action, extraction, contract
evaluation, and campaign settlement are decided by the host. Requests carry an
ID and, where applicable, an expected revision. Exact replays return their
recorded result on ledger-backed interaction, inventory, and party paths,
while conflicting payload reuse is rejected. Exact scroll instances are
consumed once, so retransmission cannot apply an effect twice. Pickup checks
include distance, line of sight, ownership, capacity, item identity, and
single-claim state.

If a rostered player disconnects during the expedition, the same running host
keeps a reconnect snapshot for 120 seconds. It includes condition, Down state,
the original absolute server bleed-out deadline, inventory, acquired/consumed
scroll ledgers, and delivery value. Offline time does not pause or reset
bleed-out. A player returning with the same online identity can reclaim that
state; an unrelated late join is a spectator.

A Hub host kick removes the fixed run-roster slot so a replacement identity can
join. A field vote kick keeps the settlement roster but immediately records a
terminal Missing outcome, removes reconnect state, and rejects that identity
if it attempts to return.

This is a server-side restoration foundation, not a complete reconnect
product. There is no automatic retry/discovery UI, raw IP identities are not
durable enough for a production guarantee, the host cannot restart and restore
the in-memory snapshot, and reconnect has not yet passed an end-to-end Steam
drop/rejoin test.

## Verification commands

Run the following from the repository root. Change `UE_ROOT` if the engine is
installed elsewhere.

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
$Report = Join-Path $ProjectRoot 'Saved\AutomationReports\VerticalSlice'

& "$UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  $Project -unattended -nop4 -nosplash -nullrhi `
  '-ExecCmds=Automation RunTests ScrollPeddler;Quit' `
  "-ReportExportPath=$Report"
```

Windows package:

```powershell
$PackageRoot = Join-Path $ProjectRoot 'Saved\Packages\Windows-VerticalSlice'

& "$UE_ROOT\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
  "-project=$Project" -nop4 -utf8output -unattended `
  -target=ScrollPeddler -platform=Win64 -clientconfig=Development `
  -build -cook '-map=/Game/Maps/TechSpike' -stage -pak -iostore `
  -package -archive "-archivedirectory=$PackageRoot"
```

### Recorded verification

The 2026-07-27 worktree has the following recorded evidence:

- Win64 Development Editor and Development Game compilation completed.
- The full `ScrollPeddler` automation report at
  `Saved/AutomationReports/VerticalSlice-20260727-AuthorityVerified` recorded
  64 clean successes, one successful test with an expected rejection-path
  warning, and zero failures or not-run tests.
- BuildCookRun completed build, cook, stage, pak/IoStore, package, and archive
  for `TechSpike` at `Saved/Packages/Windows-VerticalSlice`.
- A packaged `-nosteam` two-process host/client smoke completed with exit code
  zero for both processes, both auto clients reaching
  `SP_SPIKE_AUTO_FINISHED`, and host campaign settlement committed.
- A packaged `-nosteam` four-process smoke completed with exit code zero for
  the host and all three clients, all four players reaching the automated
  finish path, four distinct scroll consumptions, and one host campaign
  settlement for four extracted players.

The final two- and four-process runs used small windowed D3D12 clients rather
than `-nullrhi`. Neither run emitted `FNetGUIDCache`, saved-move saturation, or
fatal errors. The four-process run also exposed and then verified the fix for a
graybox pickup that had been outside one PlayerStart's 350 cm authority range.
Longer network soak, packet-loss simulation, and cross-machine testing remain
required.

`-SPAutoSpike`, `-SPAutoContestedPickup`, and `-SPAutoQuit` exist only for
unattended Development smoke tests. The auto-extraction RPC rejects Shipping
builds. Give each same-PC process a distinct `-SPProfileSlot=` value so local
receipts do not share a save slot.

`-SPAutoContestedPickup` first targets the lowest stable pickup ID once the
expected roster is present, then falls back to the nearest available pickup. In
non-Shipping builds, it keeps the first claimed actor addressable briefly so a
second request reaches the server and deterministically exercises an
authoritative pickup rejection.

The recorded package smokes use raw IP with `-nosteam`. They do not validate
Steam discovery, Steam connect strings, platform invite acceptance, NAT
behavior, or voice transport.

Those full automation and packaged smoke records predate the follow-up run
phase guard and dropped-item presentation fixes. The follow-up code passes UHT
and C++/Unity compilation, but a linked DLL, automation, and package-smoke rerun
remain pending until the open editor releases the module DLL.

## Current limits

- Manual two-player Listen Server PIE confirmed bidirectional movement,
  stamina, replicated pickup removal, hand/bag swaps, drop and re-pickup, and
  single scroll consumption. The first-person hands component still has no
  skeletal mesh, so no hands are visible.
- The same manual pass exposed two defects that now have code fixes awaiting a
  fresh linked-DLL PIE pass: dropped `ASPWorldItem` actors resolve their
  definition's `Pickup` mesh bundle instead of always keeping the cube
  fallback, and pickup/use/extraction now require an active participant in
  `Expedition` or `Collapse`.
- Large cargo remains hand-only, cannot use the bag route, and disables sprint
  while carried. These constraints have automated policy coverage but were not
  manually rechecked in the two-player pass.
- Same-item contention does not require simultaneous keyboard input on one PC:
  `-SPAutoContestedPickup` keeps the claimed actor addressable long enough for
  a second server request, and the recorded packaged smoke observed the
  authoritative rejection path.
- Steam lobby create/find/join/Quick Play and friend-invite acceptance are
  implemented but have not been validated with separate live Steam accounts.
- `V` push-to-talk and mute/unmute are wired to the legacy voice path, but live
  microphone transport, device selection, indicators, and failure UX are
  unverified.
- Equipment has only a pure host-authority foundation: Tool/Protection/Utility
  slots, compatibility, durability, breakage, repair, revision, replay, and
  conflict tests. It is not integrated into the live Character, RPC,
  persistence/reconnect snapshot, or HUD. The archive lantern is currently
  only a carried generic item.
- Only Veil of Silence has playable scroll content. The other five resolver
  families still need Data Assets, icons, VFX, SFX, descriptions, and balance.
- Dungeon markers are generated and replicated, but fixed spike coordinates
  still drive pickups, generic items, threats, and extraction. The rooms do not
  yet have production walls, doors, navigation, lighting, or encounter tables.
- Echo Hunter, Paper Eater, and director decisions are executable graybox
  behavior. They still use simple movement and lack production navigation,
  animation, audio, VFX, spawn presentation, and stuck recovery.
- The HUD is diagnostic. There is no production lobby, inventory, equipment,
  contract, crafting, reconnect, or settlement UI.
- Save bytes are verified before and after replacing the final file, and memory
  rolls back on a failed transaction. A post-replace media/read failure does
  not yet restore a previous on-disk backup.
- There is no dedicated server, host migration, cloud-save conflict handling,
  host-process recovery, or production backend.
