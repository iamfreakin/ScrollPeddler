"""Create the spike test map L_Spike01 (T9 0-13, D1): Basic template floor and lights plus four PlayerStarts.

Run with the editor closed:
  UnrealEditor-Cmd.exe <path>/ScrollPeddler.uproject -run=pythonscript -script="<path>/create_spike_map.py"

Leaves an existing map untouched.
"""
import unreal

MAP_PATH = "/Game/ScrollPeddler/Maps/L_Spike01"
TEMPLATE_PATH = "/Engine/Maps/Templates/Template_Default"
# Square corners around the template's start, wide enough that 40 cm capsules never overlap.
START_OFFSETS = [(-200.0, -200.0), (-200.0, 200.0), (200.0, -200.0), (200.0, 200.0)]


def main():
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if assets.does_asset_exist(MAP_PATH):
        unreal.log_warning(f"{MAP_PATH} already exists; leaving it as is.")
        return

    world = unreal.EditorLoadingAndSavingUtils.new_map_from_template(TEMPLATE_PATH, False)
    if world is None:
        raise RuntimeError(f"Could not create a map from {TEMPLATE_PATH}")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    starts = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
    if starts:
        base = starts[0].get_actor_location()
        rotation = starts[0].get_actor_rotation()
    else:
        base = unreal.Vector(0.0, 0.0, 100.0)
        rotation = unreal.Rotator(0.0, 0.0, 0.0)
    for start in starts:
        actors.destroy_actor(start)

    for x, y in START_OFFSETS:
        location = unreal.Vector(base.x + x, base.y + y, base.z)
        actors.spawn_actor_from_class(unreal.PlayerStart, location, rotation)

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        raise RuntimeError(f"Could not save {MAP_PATH}")
    unreal.log(f"Created {MAP_PATH} with {len(START_OFFSETS)} PlayerStarts.")


main()
