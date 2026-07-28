"""Import and validate the minimal KayKit Ranger first-person pilot.

Run through UnrealEditor-Cmd via ``import_kaykit_ranger.ps1``. The script keeps
each FBX mesh node as a separate material section so the owner-only component
can hide Head, Cape, and Quiver while sharing the world body's evaluated pose.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shlex
import sys
from typing import Any

import unreal


LOG_PREFIX = "SP_KAYKIT_IMPORT"
DEFAULT_MANIFEST = "Scripts/AssetPipeline/assets/kaykit_ranger_pilot.json"
EXPECTED_PARTS = (
    "Body",
    "Head",
    "ArmLeft",
    "ArmRight",
    "LegLeft",
    "LegRight",
    "Cape",
    "Quiver",
)


def _fail(message: str) -> None:
    raise RuntimeError(f"[{LOG_PREFIX}] {message}")


def _project_root() -> Path:
    return Path(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    ).resolve()


def _tokens() -> list[str]:
    tokens = list(sys.argv[1:])
    try:
        tokens.extend(shlex.split(unreal.SystemLibrary.get_command_line(), posix=False))
    except (AttributeError, ValueError):
        pass
    return [token.strip().strip('"') for token in tokens]


def _argument(tokens: list[str], name: str) -> str | None:
    lowered_name = name.lower()
    for index, token in enumerate(tokens):
        lowered = token.lower()
        if lowered.startswith(f"{lowered_name}="):
            return token.split("=", 1)[1].strip().strip('"')
        if lowered == lowered_name and index + 1 < len(tokens):
            return tokens[index + 1].strip().strip('"')
    return None


def _has_switch(tokens: list[str], name: str) -> bool:
    return name.lower() in {token.lower() for token in tokens}


def _repository_file(value: str, label: str) -> Path:
    root = _project_root()
    candidate = Path(value)
    resolved = (candidate if candidate.is_absolute() else root / candidate).resolve()
    try:
        resolved.relative_to(root)
    except ValueError:
        _fail(f"{label} escapes the repository: {resolved}")
    if not resolved.is_file():
        _fail(f"{label} does not exist: {resolved}")
    return resolved


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def _read_manifest(path: Path) -> dict[str, Any]:
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        _fail(f"Could not read manifest {path}: {exc}")
    if not isinstance(result, dict) or result.get("schemaVersion") != 1:
        _fail("KayKit manifest schemaVersion must be 1")
    return result


def _required_string(mapping: dict[str, Any], key: str, label: str) -> str:
    value = mapping.get(key)
    if not isinstance(value, str) or not value.strip():
        _fail(f"{label} must be a non-empty string")
    return value.strip()


def _validate_source(entry: dict[str, Any], label: str) -> Path:
    path = _repository_file(_required_string(entry, "path", f"{label}.path"), label)
    expected_hash = _required_string(entry, "sha256", f"{label}.sha256").upper()
    actual_hash = _sha256(path)
    if actual_hash != expected_hash:
        _fail(f"{label} SHA-256 mismatch: expected {expected_hash}, got {actual_hash}")
    return path


def _package_path(object_path: str) -> str:
    leaf = object_path.rsplit("/", 1)[-1]
    return object_path.rsplit(".", 1)[0] if "." in leaf else object_path


def _asset_path(asset: unreal.Object) -> str:
    return _package_path(
        unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(asset)
    )


def _set(instance: Any, name: str, value: Any) -> None:
    try:
        instance.set_editor_property(name, value)
    except Exception as exc:
        _fail(
            f"{instance.__class__.__name__} does not accept property "
            f"{name}={value!r}: {exc}"
        )


def _load(path: str, expected_type: type) -> Any:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(asset, expected_type):
        _fail(f"Expected {expected_type.__name__} at {path}, got {type(asset).__name__}")
    return asset


def _move_asset(asset: unreal.Object, destination: str) -> None:
    source = _asset_path(asset)
    if source == destination:
        return
    if unreal.EditorAssetLibrary.does_asset_exist(destination):
        _fail(f"Destination already exists while moving {source}: {destination}")
    if not unreal.EditorAssetLibrary.rename_asset(source, destination):
        _fail(f"Could not move {source} to {destination}")


def _stage_assets(path: str) -> list[unreal.Object]:
    result: list[unreal.Object] = []
    for object_path in unreal.EditorAssetLibrary.list_assets(
        path, recursive=True, include_folder=False
    ):
        asset = unreal.EditorAssetLibrary.load_asset(object_path)
        if asset:
            result.append(asset)
    return result


def _mesh_options() -> unreal.FbxImportUI:
    options = unreal.FbxImportUI()
    _set(options, "automated_import_should_detect_type", False)
    _set(options, "import_mesh", True)
    _set(options, "import_as_skeletal", True)
    _set(options, "import_animations", False)
    _set(options, "import_materials", True)
    _set(options, "import_textures", True)
    _set(options, "create_physics_asset", False)
    _set(options, "override_full_name", True)
    _set(options, "mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)

    import_data = options.get_editor_property("skeletal_mesh_import_data")
    _set(import_data, "import_translation", unreal.Vector(0.0, 0.0, 0.0))
    _set(import_data, "import_rotation", unreal.Rotator(0.0, 0.0, 0.0))
    _set(import_data, "import_uniform_scale", 1.0)
    _set(import_data, "convert_scene", True)
    _set(import_data, "force_front_x_axis", False)
    _set(import_data, "convert_scene_unit", True)
    _set(import_data, "normal_import_method",
         unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    _set(import_data, "normal_generation_method",
         unreal.FBXNormalGenerationMethod.MIKK_T_SPACE)
    _set(import_data, "import_content_type",
         unreal.FBXImportContentType.FBXICT_ALL)
    _set(import_data, "update_skeleton_reference_pose", False)
    _set(import_data, "use_t0_as_ref_pose", False)
    _set(import_data, "preserve_smoothing_groups", True)
    _set(import_data, "keep_sections_separate", True)
    _set(import_data, "import_meshes_in_bone_hierarchy", True)
    _set(import_data, "import_morph_targets", False)
    return options


def _animation_options(skeleton: unreal.Skeleton) -> unreal.FbxImportUI:
    options = unreal.FbxImportUI()
    _set(options, "automated_import_should_detect_type", False)
    _set(options, "import_mesh", False)
    _set(options, "import_animations", True)
    _set(options, "import_materials", False)
    _set(options, "import_textures", False)
    _set(options, "create_physics_asset", False)
    _set(options, "override_full_name", False)
    _set(options, "mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    _set(options, "skeleton", skeleton)
    _set(options, "override_animation_name", "")

    import_data = options.get_editor_property("anim_sequence_import_data")
    _set(import_data, "import_translation", unreal.Vector(0.0, 0.0, 0.0))
    _set(import_data, "import_rotation", unreal.Rotator(0.0, 0.0, 0.0))
    _set(import_data, "import_uniform_scale", 1.0)
    _set(import_data, "convert_scene", True)
    _set(import_data, "force_front_x_axis", False)
    _set(import_data, "convert_scene_unit", True)
    _set(import_data, "animation_length",
         unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    _set(import_data, "use_default_sample_rate", True)
    _set(import_data, "snap_to_closest_frame_boundary", True)
    _set(import_data, "import_meshes_in_bone_hierarchy", True)
    _set(import_data, "import_custom_attribute", False)
    _set(import_data, "import_bone_tracks", True)
    _set(import_data, "remove_redundant_keys", True)
    return options


def _run_import(
    filename: Path,
    destination_path: str,
    destination_name: str,
    options: unreal.FbxImportUI,
) -> list[str]:
    task = unreal.AssetImportTask()
    _set(task, "filename", str(filename))
    _set(task, "destination_path", destination_path)
    _set(task, "destination_name", destination_name)
    _set(task, "automated", True)
    _set(task, "replace_existing", False)
    _set(task, "replace_existing_settings", False)
    _set(task, "save", False)
    _set(task, "options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return [
        _package_path(str(path))
        for path in task.get_editor_property("imported_object_paths")
    ]


def _standardize_material_slots(mesh: unreal.SkeletalMesh) -> list[str]:
    materials = list(mesh.get_editor_property("materials"))
    found: dict[str, int] = {}
    for index, material in enumerate(materials):
        imported_name = str(material.get_editor_property("imported_material_slot_name"))
        current_name = str(material.get_editor_property("material_slot_name"))
        probe = f"{imported_name} {current_name}".lower()
        matches = [part for part in EXPECTED_PARTS if part.lower() in probe]
        if len(matches) != 1:
            _fail(
                f"Could not map Ranger material slot {index}: "
                f"imported={imported_name!r}, current={current_name!r}"
            )
        part = matches[0]
        if part in found:
            _fail(f"Ranger material part appears more than once: {part}")
        found[part] = index
        material.set_editor_property("material_slot_name", f"KayKit_{part}")

    missing = sorted(set(EXPECTED_PARTS) - set(found))
    if missing:
        _fail(f"Ranger mesh is missing expected material parts: {missing}")
    mesh.set_editor_property("materials", materials)
    return [f"KayKit_{part}" for part in EXPECTED_PARTS]


def _configure_material(
    material: unreal.Material,
    texture: unreal.Texture2D,
) -> None:
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    texture_sample = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionTextureSample,
        -320,
        0,
    )
    if not texture_sample:
        _fail("Could not create the Ranger base-color texture sample")
    _set(texture_sample, "texture", texture)
    if not unreal.MaterialEditingLibrary.connect_material_property(
        texture_sample,
        "RGB",
        unreal.MaterialProperty.MP_BASE_COLOR,
    ):
        _fail("Could not connect the Ranger texture to material Base Color")
    compiler_errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if compiler_errors:
        _fail(f"Ranger material did not compile: {list(compiler_errors)}")
    if texture_sample not in unreal.MaterialEditingLibrary.get_material_expressions(
        material
    ):
        _fail("Ranger texture sample was not added to the material graph")


def _import_mesh(
    mesh_source: Path,
    texture_source: Path,
    unreal_paths: dict[str, str],
) -> tuple[unreal.SkeletalMesh, unreal.Skeleton]:
    root = unreal_paths["root"]
    stage = f"{root}/_ImportMesh"
    if _stage_assets(stage):
        _fail(f"Mesh import staging directory is not empty: {stage}")

    _run_import(mesh_source, stage, "SK_KayKit_Ranger", _mesh_options())
    staged = _stage_assets(stage)
    meshes = [asset for asset in staged if isinstance(asset, unreal.SkeletalMesh)]
    materials = [asset for asset in staged if isinstance(asset, unreal.Material)]
    textures = [asset for asset in staged if isinstance(asset, unreal.Texture2D)]
    if len(meshes) != 1:
        _fail(f"Expected one SkeletalMesh in {stage}, found {len(meshes)}")
    mesh = meshes[0]
    skeleton = mesh.get_editor_property("skeleton")
    if not isinstance(skeleton, unreal.Skeleton):
        _fail("Imported Ranger mesh did not create a Skeleton")
    if len(materials) != 1:
        _fail(f"Expected one imported Material in {stage}, found {len(materials)}")

    if not textures:
        _run_import(texture_source, stage, "ranger_texture", unreal.TextureFactory())
        staged = _stage_assets(stage)
        textures = [asset for asset in staged if isinstance(asset, unreal.Texture2D)]
    if len(textures) != 1:
        _fail(f"Expected one imported Texture2D in {stage}, found {len(textures)}")

    _standardize_material_slots(mesh)
    _move_asset(materials[0], unreal_paths["material"])
    _move_asset(textures[0], unreal_paths["texture"])
    _move_asset(skeleton, unreal_paths["skeleton"])
    _move_asset(mesh, unreal_paths["skeletalMesh"])

    material = _load(unreal_paths["material"], unreal.Material)
    texture = _load(unreal_paths["texture"], unreal.Texture2D)
    _configure_material(material, texture)

    for path in (
        unreal_paths["material"],
        unreal_paths["texture"],
        unreal_paths["skeleton"],
        unreal_paths["skeletalMesh"],
    ):
        if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
            _fail(f"Could not save imported asset: {path}")
    unreal.EditorAssetLibrary.delete_directory(stage)
    return (
        _load(unreal_paths["skeletalMesh"], unreal.SkeletalMesh),
        _load(unreal_paths["skeleton"], unreal.Skeleton),
    )


def _import_animation_set(
    source: Path,
    clips: list[str],
    skeleton: unreal.Skeleton,
    unreal_paths: dict[str, str],
) -> None:
    stage = f"{unreal_paths['animations']}/_Import_{source.stem}"
    if _stage_assets(stage):
        _fail(f"Animation import staging directory is not empty: {stage}")
    _run_import(source, stage, source.stem, _animation_options(skeleton))
    animations = [
        asset
        for asset in _stage_assets(stage)
        if isinstance(asset, unreal.AnimSequence)
    ]
    if not animations:
        _fail(f"No AnimSequence assets were imported from {source}")

    matched: set[str] = set()
    for animation in animations:
        source_name = animation.get_name()
        matches = [clip for clip in clips if source_name.lower().endswith(
            f"_{clip}".lower()
        )]
        if len(matches) != 1:
            continue
        clip = matches[0]
        destination = (
            f"{unreal_paths['animations']}/"
            f"{unreal_paths['animationPrefix']}{clip}"
        )
        _set(animation, "enable_root_motion", False)
        _move_asset(animation, destination)
        if not unreal.EditorAssetLibrary.save_asset(
            destination, only_if_is_dirty=False
        ):
            _fail(f"Could not save animation: {destination}")
        matched.add(clip)

    missing = sorted(set(clips) - matched)
    if missing:
        imported_names = sorted(animation.get_name() for animation in animations)
        _fail(
            f"Animation clips missing from {source.name}: {missing}; "
            f"imported={imported_names}"
        )
    if not unreal.EditorAssetLibrary.delete_directory(stage):
        _fail(f"Could not remove animation staging directory: {stage}")


def _validate(
    mesh: unreal.SkeletalMesh,
    skeleton: unreal.Skeleton,
    unreal_paths: dict[str, str],
    requested_clips: list[str],
) -> dict[str, Any]:
    if mesh.get_editor_property("skeleton") != skeleton:
        _fail("Ranger SkeletalMesh does not reference the expected Skeleton")

    material_names = [
        str(material.get_editor_property("material_slot_name"))
        for material in mesh.get_editor_property("materials")
    ]
    expected_names = {f"KayKit_{part}" for part in EXPECTED_PARTS}
    if len(material_names) != len(expected_names) or set(material_names) != expected_names:
        _fail(
            f"Ranger material slots are not stable: "
            f"expected={sorted(expected_names)}, actual={material_names}"
        )

    animation_summary: dict[str, float] = {}
    for clip in requested_clips:
        path = (
            f"{unreal_paths['animations']}/"
            f"{unreal_paths['animationPrefix']}{clip}"
        )
        animation = _load(path, unreal.AnimSequence)
        if animation.get_editor_property("skeleton") != skeleton:
            _fail(f"Animation uses a different Skeleton: {path}")
        if animation.get_editor_property("enable_root_motion"):
            _fail(f"Root motion must remain disabled: {path}")
        animation_summary[clip] = float(animation.get_play_length())

    material = _load(unreal_paths["material"], unreal.Material)
    texture = _load(unreal_paths["texture"], unreal.Texture2D)
    for index, mesh_material in enumerate(mesh.get_editor_property("materials")):
        if mesh_material.get_editor_property("material_interface") != material:
            _fail(
                f"Ranger material slot {index} does not reference "
                f"{unreal_paths['material']}"
            )
    texture_samples = [
        expression
        for expression in unreal.MaterialEditingLibrary.get_material_expressions(
            material
        )
        if isinstance(expression, unreal.MaterialExpressionTextureSample)
    ]
    sample_textures = [
        sample.get_editor_property("texture")
        for sample in texture_samples
    ]
    if texture not in sample_textures:
        sample_texture_paths = [
            _asset_path(sample_texture)
            for sample_texture in sample_textures
            if isinstance(sample_texture, unreal.Texture)
        ]
        _fail(
            "Ranger material does not reference the imported Ranger texture: "
            f"expected={unreal_paths['texture']}, actual={sample_texture_paths}"
        )
    base_color_node = (
        unreal.MaterialEditingLibrary.get_material_property_input_node(
            material,
            unreal.MaterialProperty.MP_BASE_COLOR,
        )
    )
    base_color_output = (
        unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(
            material,
            unreal.MaterialProperty.MP_BASE_COLOR,
        )
    )
    if base_color_node not in texture_samples or base_color_output != "RGB":
        _fail(
            "Ranger material Base Color is not connected to the Ranger "
            f"texture RGB output: node={base_color_node}, "
            f"output={base_color_output!r}"
        )

    return {
        "mesh": unreal_paths["skeletalMesh"],
        "skeleton": unreal_paths["skeleton"],
        "materialSlots": material_names,
        "materialTexture": unreal_paths["texture"],
        "animations": animation_summary,
    }


def main() -> None:
    tokens = _tokens()
    manifest_path = _repository_file(
        _argument(tokens, "-SPKayKitManifest") or DEFAULT_MANIFEST,
        "KayKit manifest",
    )
    validate_only = _has_switch(tokens, "-SPValidateOnly")
    repair_material = _has_switch(tokens, "-SPRepairMaterial")
    if validate_only and repair_material:
        _fail("-SPValidateOnly and -SPRepairMaterial cannot be combined")
    manifest = _read_manifest(manifest_path)
    mesh_source = _validate_source(manifest["mesh"], "mesh")
    texture_source = _validate_source(manifest["texture"], "texture")

    animation_sources: list[tuple[Path, list[str]]] = []
    requested_clips: list[str] = []
    for index, entry in enumerate(manifest.get("animationSets", [])):
        if not isinstance(entry, dict):
            _fail(f"animationSets[{index}] must be an object")
        source = _validate_source(entry, f"animationSets[{index}]")
        clips = entry.get("clips")
        if not isinstance(clips, list) or not clips or not all(
            isinstance(clip, str) and clip for clip in clips
        ):
            _fail(f"animationSets[{index}].clips must be a non-empty string array")
        animation_sources.append((source, clips))
        requested_clips.extend(clips)
    if len(requested_clips) != len(set(requested_clips)):
        _fail("Animation clip names must be unique across source sets")

    unreal_config = manifest.get("unreal")
    if not isinstance(unreal_config, dict):
        _fail("Manifest unreal field must be an object")
    required_unreal_paths = (
        "root",
        "skeletalMesh",
        "skeleton",
        "material",
        "texture",
        "animations",
        "animationPrefix",
    )
    unreal_paths = {
        key: _required_string(unreal_config, key, f"unreal.{key}")
        for key in required_unreal_paths
    }
    for key, path in unreal_paths.items():
        if key != "animationPrefix" and not path.startswith("/Game/"):
            _fail(f"unreal.{key} must be below /Game: {path}")

    expected_assets = (
        unreal_paths["skeletalMesh"],
        unreal_paths["skeleton"],
        unreal_paths["material"],
        unreal_paths["texture"],
    )
    assets_exist = [unreal.EditorAssetLibrary.does_asset_exist(path)
                    for path in expected_assets]
    if validate_only or repair_material:
        if not all(assets_exist):
            _fail("Validation and repair require all Ranger base assets to exist")
        mesh = _load(unreal_paths["skeletalMesh"], unreal.SkeletalMesh)
        skeleton = _load(unreal_paths["skeleton"], unreal.Skeleton)
        if repair_material:
            material = _load(unreal_paths["material"], unreal.Material)
            texture = _load(unreal_paths["texture"], unreal.Texture2D)
            _configure_material(material, texture)
            if not unreal.EditorAssetLibrary.save_asset(
                unreal_paths["material"], only_if_is_dirty=False
            ):
                _fail(
                    "Could not save repaired Ranger material: "
                    f"{unreal_paths['material']}"
                )
            operation = "material-repaired"
        else:
            operation = "validated"
    else:
        if any(assets_exist):
            _fail(
                "Ranger base assets already exist. Use -ValidateOnly rather "
                "than overwriting editor assets."
            )
        mesh, skeleton = _import_mesh(mesh_source, texture_source, unreal_paths)
        for source, clips in animation_sources:
            _import_animation_set(source, clips, skeleton, unreal_paths)
        operation = "imported"

    summary = _validate(mesh, skeleton, unreal_paths, requested_clips)
    summary.update(
        {
            "operation": operation,
            "manifest": str(manifest_path),
            "sourceBytes": sum(
                path.stat().st_size
                for path in [mesh_source, texture_source]
                + [source for source, _ in animation_sources]
            ),
        }
    )
    unreal.log(f"[{LOG_PREFIX}] {json.dumps(summary, sort_keys=True)}")


try:
    main()
except Exception as error:
    unreal.log_error(str(error))
    raise
