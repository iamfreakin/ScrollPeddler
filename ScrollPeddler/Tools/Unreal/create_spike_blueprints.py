"""Create BP_SpikeCharacter and BP_SpikeGameMode (T9 §2.2) with the First Person template mannequin.

Needs the First Person template content in the project (/Game/Characters, /Game/FirstPerson).
Run with the editor closed:
  UnrealEditor-Cmd.exe <path>/ScrollPeddler.uproject -run=pythonscript -script="<path>/create_spike_blueprints.py"

Safe to re-run: existing Blueprints are kept and their settings re-applied.
"""
import unreal

BLUEPRINT_FOLDER = "/Game/ScrollPeddler/Blueprints"
CHARACTER_NAME = "BP_SpikeCharacter"
GAME_MODE_NAME = "BP_SpikeGameMode"

# Same assets and body offset as the template's BP_FirstPersonCharacter.
MANNEQUIN_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"
BODY_ANIM_CLASS = "/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"
ARMS_ANIM_CLASS = "/Game/FirstPerson/Anims/ABP_FP_Copy.ABP_FP_Copy_C"
BODY_LOCATION = unreal.Vector(-20.0, 0.0, -96.0)


def load_or_create_blueprint(name, parent_class):
    path = f"{BLUEPRINT_FOLDER}/{name}"
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, BLUEPRINT_FOLDER, unreal.Blueprint, factory)
    if blueprint is None:
        raise RuntimeError(f"Could not create {path}")
    return blueprint


def require(obj, what):
    if obj is None:
        raise RuntimeError(f"Missing {what}. Is the First Person template content in the project?")
    return obj


def main():
    mesh = require(unreal.load_asset(MANNEQUIN_MESH), MANNEQUIN_MESH)
    body_anim = require(unreal.load_class(None, BODY_ANIM_CLASS), BODY_ANIM_CLASS)
    arms_anim = require(unreal.load_class(None, ARMS_ANIM_CLASS), ARMS_ANIM_CLASS)

    character_bp = load_or_create_blueprint(CHARACTER_NAME, unreal.load_class(None, "/Script/ScrollPeddler.ScrollCharacter"))
    character = unreal.get_default_object(character_bp.generated_class())

    body = character.get_editor_property("mesh")
    body.set_editor_property("skeletal_mesh_asset", mesh)
    body.set_editor_property("anim_class", body_anim)
    body.set_editor_property("relative_location", BODY_LOCATION)

    arms = character.get_editor_property("first_person_mesh")
    arms.set_editor_property("skeletal_mesh_asset", mesh)
    arms.set_editor_property("anim_class", arms_anim)

    game_mode_bp = load_or_create_blueprint(GAME_MODE_NAME, unreal.load_class(None, "/Script/ScrollPeddler.ScrollPeddlerGameModeBase"))
    game_mode = unreal.get_default_object(game_mode_bp.generated_class())
    game_mode.set_editor_property("default_pawn_class", character_bp.generated_class())

    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    for blueprint in (character_bp, game_mode_bp):
        if not assets.save_loaded_asset(blueprint, False):
            raise RuntimeError(f"Could not save {blueprint.get_path_name()}")
    unreal.log(f"Saved {CHARACTER_NAME} and {GAME_MODE_NAME} in {BLUEPRINT_FOLDER}.")


main()
