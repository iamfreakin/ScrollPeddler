"""Add four surface test tiles (Wood, Stone, Water, Paper) to L_Spike01 for footstep checks.

Creates PM_Wood/PM_Stone/PM_Water/PM_Paper in /Game/ScrollPeddler/Physics (SurfaceType1-4, see DefaultEngine.ini)
and colour material instances in /Game/ScrollPeddler/Dev/SurfaceTest, then lays one 4 m tile per surface in a row
in front of the player starts. Re-running replaces the tiles and keeps the assets.

Run with the editor closed:
  UnrealEditor-Cmd.exe <path>/ScrollPeddler.uproject -run=pythonscript -script="<path>/create_surface_test_area.py"
"""
import unreal

MAP_PATH = "/Game/ScrollPeddler/Maps/L_Spike01"
PHYSICS_FOLDER = "/Game/ScrollPeddler/Physics"
DEV_FOLDER = "/Game/ScrollPeddler/Dev/SurfaceTest"
BASE_MATERIAL = "/Engine/BasicShapes/BasicShapeMaterial"
CUBE_MESH = "/Engine/BasicShapes/Cube"
TILE_TAG = "ScrollSurfaceTest"
TILE_SIZE = 400.0
TILE_THICKNESS = 2.0
ROW_DISTANCE = 600.0  # in front of the player starts along +X

SURFACES = [
    ("Wood", "SURFACE_TYPE1", unreal.LinearColor(0.45, 0.28, 0.12, 1.0)),
    ("Stone", "SURFACE_TYPE2", unreal.LinearColor(0.35, 0.35, 0.35, 1.0)),
    ("Water", "SURFACE_TYPE3", unreal.LinearColor(0.10, 0.35, 0.45, 1.0)),
    ("Paper", "SURFACE_TYPE4", unreal.LinearColor(0.90, 0.85, 0.70, 1.0)),
]


def load_or_create(name, folder, asset_class, factory):
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    path = f"{folder}/{name}"
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, asset_class, factory)
    if asset is None:
        raise RuntimeError(f"Could not create {path}")
    return asset


def make_physical_material(name, surface_enum_name):
    material = load_or_create(f"PM_{name}", PHYSICS_FOLDER, unreal.PhysicalMaterial, unreal.PhysicalMaterialFactoryNew())
    material.set_editor_property("surface_type", getattr(unreal.PhysicalSurface, surface_enum_name))
    unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).save_loaded_asset(material, False)
    return material


def make_colour_material(name, colour):
    factory = unreal.MaterialInstanceConstantFactoryNew()
    instance = load_or_create(f"MI_Surface_{name}", DEV_FOLDER, unreal.MaterialInstanceConstant, factory)
    instance.set_editor_property("parent", unreal.load_asset(BASE_MATERIAL))
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(instance, "Color", colour)
    unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).save_loaded_asset(instance, False)
    return instance


def find_floor_top(actors):
    tops = []
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.StaticMeshActor) and "floor" in actor.get_actor_label().lower():
            origin, extent = actor.get_actor_bounds(False)
            tops.append((extent.x * extent.y, origin.z + extent.z, origin, extent))
    if not tops:
        raise RuntimeError("No floor actor found in the map")
    _, top, origin, extent = max(tops, key=lambda t: t[0])
    return top, origin, extent


def main():
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    if world is None:
        raise RuntimeError(f"Could not load {MAP_PATH}")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    for actor in actors.get_all_level_actors():
        if TILE_TAG in [str(tag) for tag in actor.tags]:
            actors.destroy_actor(actor)

    floor_top, floor_origin, floor_extent = find_floor_top(actors)
    row_x = min(floor_origin.x + ROW_DISTANCE, floor_origin.x + floor_extent.x - TILE_SIZE * 0.5)
    cube = unreal.load_asset(CUBE_MESH)

    for index, (name, surface_enum_name, colour) in enumerate(SURFACES):
        physical_material = make_physical_material(name, surface_enum_name)
        colour_material = make_colour_material(name, colour)
        y = floor_origin.y + (index - (len(SURFACES) - 1) / 2.0) * TILE_SIZE

        tile = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(row_x, y, floor_top + TILE_THICKNESS / 2.0))
        tile.set_actor_label(f"SurfaceTest_{name}")
        tile.set_actor_scale3d(unreal.Vector(TILE_SIZE / 100.0, TILE_SIZE / 100.0, TILE_THICKNESS / 100.0))
        mesh = tile.static_mesh_component
        mesh.set_static_mesh(cube)
        mesh.set_material(0, colour_material)
        mesh.set_phys_material_override(physical_material)
        tile.tags = [TILE_TAG]

        # Label faces the player starts (which look along +X).
        label = actors.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(row_x, y, floor_top + 120.0), unreal.Rotator(0.0, 0.0, 180.0))
        label.set_actor_label(f"SurfaceTest_{name}_Label")
        text = label.text_render
        text.set_text(name)
        text.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
        text.set_editor_property("world_size", 40.0)
        label.tags = [TILE_TAG]

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        raise RuntimeError(f"Could not save {MAP_PATH}")
    unreal.log(f"Placed {len(SURFACES)} surface test tiles in {MAP_PATH} at x={row_x:.0f}.")

    # Commandlet worlds do not register collision, so traces cannot check this here; check the data instead.
    # In game, walk over the tiles with `Scroll.Footsteps.Debug 1` to see the surface each step reads.
    for actor in actors.get_all_level_actors():
        label = actor.get_actor_label()
        if label.startswith("SurfaceTest_") and not label.endswith("_Label"):
            override = actor.static_mesh_component.get_editor_property("body_instance").get_editor_property("phys_material_override")
            surface = override.get_editor_property("surface_type") if override else None
            unreal.log(f"SURFACECHECK {label}: {override.get_name() if override else None} -> {surface}")


main()
