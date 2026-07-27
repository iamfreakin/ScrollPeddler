"""Place deterministic, editor-visible PlayerStarts in the TechSpike map.

Run from the Unreal Editor Python environment while /Game/Maps/TechSpike is
open. The script is idempotent: an existing start with the expected actor label
or PlayerStartTag is updated instead of duplicated.
"""

from __future__ import annotations

import unreal


MAP_PATH = "/Game/Maps/TechSpike"
PLAYER_STARTS = (
    ("SP_PlayerStart_0", unreal.Vector(-650.0, -200.0, 110.0)),
    ("SP_PlayerStart_1", unreal.Vector(-650.0, 200.0, 110.0)),
    ("SP_PlayerStart_2", unreal.Vector(-750.0, 0.0, 110.0)),
    ("SP_PlayerStart_3", unreal.Vector(-600.0, 0.0, 110.0)),
)


def _current_map_path() -> str:
    world = unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem
    ).get_editor_world()
    if not world:
        raise RuntimeError("No editor world is open")
    return world.get_path_name().split(".", 1)[0]


def _player_start_tag(actor: unreal.PlayerStart) -> str:
    return str(actor.get_editor_property("player_start_tag"))


def _matching_start(
    starts: list[unreal.PlayerStart], expected_name: str
) -> unreal.PlayerStart | None:
    matches = [
        actor
        for actor in starts
        if actor.get_actor_label() == expected_name
        or _player_start_tag(actor) == expected_name
    ]
    if len(matches) > 1:
        paths = ", ".join(actor.get_path_name() for actor in matches)
        raise RuntimeError(
            f"Duplicate PlayerStarts match {expected_name}: {paths}"
        )
    return matches[0] if matches else None


def _configure_start(
    actor: unreal.PlayerStart,
    expected_name: str,
    location: unreal.Vector,
) -> bool:
    changed = False
    if actor.get_actor_label() != expected_name:
        actor.set_actor_label(expected_name)
        changed = True

    if _player_start_tag(actor) != expected_name:
        actor.set_editor_property(
            "player_start_tag", unreal.Name(expected_name)
        )
        changed = True

    expected_tags = [
        unreal.Name("SP.PlayerStart"),
        unreal.Name(f"SP.PlayerStart.{expected_name.rsplit('_', 1)[-1]}"),
    ]
    current_tags = actor.get_editor_property("tags")
    if [str(tag) for tag in current_tags] != [
        str(tag) for tag in expected_tags
    ]:
        actor.set_editor_property("tags", expected_tags)
        changed = True

    current_location = actor.get_actor_location()
    if any(
        abs(current - expected) > 0.01
        for current, expected in (
            (current_location.x, location.x),
            (current_location.y, location.y),
            (current_location.z, location.z),
        )
    ):
        actor.set_actor_location(location, False, False)
        changed = True

    current_rotation = actor.get_actor_rotation()
    if any(
        abs(value) > 0.01
        for value in (
            current_rotation.pitch,
            current_rotation.yaw,
            current_rotation.roll,
        )
    ):
        actor.set_actor_rotation(
            unreal.Rotator(0.0, 0.0, 0.0), False
        )
        changed = True

    return changed


def main() -> None:
    level_subsystem = unreal.get_editor_subsystem(
        unreal.LevelEditorSubsystem
    )
    if not level_subsystem.load_level(MAP_PATH):
        raise RuntimeError(f"Failed to load {MAP_PATH}")

    current_map = _current_map_path()
    if current_map != MAP_PATH:
        raise RuntimeError(
            f"Expected {MAP_PATH} to be open, found {current_map}"
        )

    actor_subsystem = unreal.get_editor_subsystem(
        unreal.EditorActorSubsystem
    )
    starts = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if isinstance(actor, unreal.PlayerStart)
    ]

    changed = False
    with unreal.ScopedEditorTransaction(
        "Configure TechSpike deterministic PlayerStarts"
    ):
        for expected_name, location in PLAYER_STARTS:
            actor = _matching_start(starts, expected_name)
            if actor is None:
                actor = actor_subsystem.spawn_actor_from_class(
                    unreal.PlayerStart,
                    location,
                    unreal.Rotator(0.0, 0.0, 0.0),
                )
                if actor is None:
                    raise RuntimeError(
                        f"Failed to spawn PlayerStart {expected_name}"
                    )
                starts.append(actor)
                changed = True
            changed = (
                _configure_start(actor, expected_name, location)
                or changed
            )

    configured = {
        _player_start_tag(actor): actor
        for actor in starts
        if _player_start_tag(actor).startswith("SP_PlayerStart_")
    }
    expected_names = {name for name, _ in PLAYER_STARTS}
    if set(configured) != expected_names:
        raise RuntimeError(
            "Configured PlayerStart tags do not match the expected set: "
            f"{sorted(configured)}"
        )

    if changed and not level_subsystem.save_current_level():
        raise RuntimeError(f"Failed to save {MAP_PATH}")

    summary = ", ".join(
        f"{name}={configured[name].get_actor_location()}"
        for name, _ in PLAYER_STARTS
    )
    unreal.log(
        "SP_TECH_SPIKE_PLAYER_STARTS_CONFIGURED "
        f"map={MAP_PATH} count={len(configured)} "
        f"changed={int(changed)} {summary}"
    )


main()
