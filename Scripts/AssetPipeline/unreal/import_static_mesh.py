"""Import and validate one manifest-driven static mesh with Unreal's legacy FBX path.

Run this script through UnrealEditor-Cmd. The manifest can be supplied either as
``-SPAssetManifest=<path>`` on the Unreal command line or as ``--manifest`` in
Python argv. Existing assets are reimported with their saved import settings;
only a first import creates an ``FbxImportUI`` policy.
"""

from __future__ import annotations

import json
import hashlib
import math
import os
from pathlib import Path
import re
import shlex
import sys
from typing import Any, Iterable

import unreal


LOG_PREFIX = "SP_ASSET_PIPELINE"
DEFAULT_MANIFEST = "Scripts/AssetPipeline/assets/scroll_pickup_test_blockout.json"
BLENDER_EXPORTER = "Scripts/AssetPipeline/blender/export_static_mesh.py"


def _fail(message: str) -> None:
    raise RuntimeError(f"[{LOG_PREFIX}] {message}")


def _project_root() -> Path:
    return Path(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    ).resolve()


def _is_within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _repository_path(value: str, field: str, *, must_exist: bool) -> Path:
    if not isinstance(value, str) or not value.strip():
        _fail(f"Manifest field '{field}' must be a non-empty path string")

    root = _project_root()
    raw_path = Path(value)
    candidate = raw_path if raw_path.is_absolute() else root / raw_path
    candidate = candidate.resolve(strict=False)
    if not _is_within(candidate, root):
        _fail(f"Manifest field '{field}' escapes the repository: {candidate}")
    if must_exist and not candidate.is_file():
        _fail(f"Required file for '{field}' does not exist: {candidate}")
    return candidate


def _command_line_tokens() -> list[str]:
    tokens = list(sys.argv[1:])
    try:
        tokens.extend(shlex.split(unreal.SystemLibrary.get_command_line(), posix=False))
    except (AttributeError, ValueError):
        pass
    return [token.strip().strip('"') for token in tokens]


def _argument_value(tokens: Iterable[str], names: tuple[str, ...]) -> str | None:
    token_list = list(tokens)
    lowered_names = tuple(name.lower() for name in names)
    for index, token in enumerate(token_list):
        lowered = token.lower()
        for name in lowered_names:
            if lowered.startswith(f"{name}="):
                return token.split("=", 1)[1].strip().strip('"')
            if lowered == name and index + 1 < len(token_list):
                return token_list[index + 1].strip().strip('"')
    return None


def _has_switch(tokens: Iterable[str], names: tuple[str, ...]) -> bool:
    lowered_names = {name.lower() for name in names}
    return any(token.lower() in lowered_names for token in tokens)


def _manifest_path(tokens: list[str]) -> Path:
    supplied = _argument_value(
        tokens, ("-SPAssetManifest", "--manifest", "-manifest")
    )
    return _repository_path(
        supplied or DEFAULT_MANIFEST, "manifest", must_exist=True
    )


def _read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        _fail(f"Could not read {label} JSON at {path}: {exc}")
    if not isinstance(value, dict):
        _fail(f"{label} JSON root must be an object: {path}")
    return value


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _sha256_text(path: Path) -> str:
    """Hash UTF-8 text with canonical LF newlines across Git checkouts."""
    text = path.read_text(encoding="utf-8")
    canonical = text.replace("\r\n", "\n").replace("\r", "\n")
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _nested(mapping: dict[str, Any], *keys: str) -> Any:
    current: Any = mapping
    for key in keys:
        if not isinstance(current, dict) or key not in current:
            return None
        current = current[key]
    return current


def _required_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.strip():
        _fail(f"Manifest field '{field}' must be a non-empty string")
    return value.strip()


def _package_path(object_or_package_path: str) -> str:
    """Normalize /Game/Foo/Bar.Bar and /Game/Foo/Bar to one package path."""
    leaf = object_or_package_path.rsplit("/", 1)[-1]
    if "." in leaf:
        return object_or_package_path.rsplit(".", 1)[0]
    return object_or_package_path


def _asset_path_from_manifest(manifest: dict[str, Any]) -> str:
    unreal_config = manifest.get("unreal")
    if not isinstance(unreal_config, dict):
        _fail("Manifest field 'unreal' must be an object")

    destination = unreal_config.get("destination")
    if destination is not None:
        destination_path = _required_string(
            destination, "unreal.destination"
        ).rstrip("/")
        configured_asset_name = unreal_config.get("assetName")
        if configured_asset_name is None:
            fbx_value = _nested(manifest, "paths", "fbx") or manifest.get("fbx")
            configured_asset_name = Path(
                _required_string(fbx_value, "paths.fbx")
            ).stem
        asset_name = _required_string(
            configured_asset_name, "unreal.assetName or paths.fbx stem"
        )
        result = (
            destination_path
            if destination_path.rsplit("/", 1)[-1] == asset_name
            else f"{destination_path}/{asset_name}"
        )
    else:
        destination_path = _required_string(
            unreal_config.get("destinationPath"), "unreal.destinationPath"
        ).rstrip("/")
        asset_name = _required_string(
            unreal_config.get("assetName"), "unreal.assetName"
        )
        result = f"{destination_path}/{asset_name}"

    if not result.startswith("/Game/") or result.endswith("/Game"):
        _fail(f"Static mesh destination must be below /Game: {result}")
    if "." in result or ".." in result or not re.fullmatch(r"/[A-Za-z0-9_/]+", result):
        _fail(f"Static mesh destination is not a canonical package path: {result}")
    return result


def _scroll_definition_path(manifest: dict[str, Any]) -> str:
    value = _nested(manifest, "unreal", "dataAsset")
    field = "unreal.dataAsset"
    if value is None:
        value = _nested(manifest, "unreal", "scrollDefinition")
        field = "unreal.scrollDefinition"
    result = _required_string(value, field)
    if "." in result:
        package, object_name = result.rsplit(".", 1)
        if package.rsplit("/", 1)[-1] != object_name:
            _fail(f"Scroll definition must use a canonical object path: {result}")
        result = package
    if not re.fullmatch(r"/Game/[A-Za-z0-9_/]+", result):
        _fail(f"Scroll definition must be below /Game: {result}")
    return result


def _manifest_file(manifest: dict[str, Any], key: str, *, must_exist: bool) -> Path:
    value = _nested(manifest, "paths", key)
    if value is None:
        value = manifest.get(key)
    return _repository_path(
        _required_string(value, f"paths.{key}"),
        f"paths.{key}",
        must_exist=must_exist,
    )


def _expected_dimensions(manifest: dict[str, Any]) -> tuple[dict[str, float], float]:
    dimensions = _nested(manifest, "bounds", "expectedDimensionsCm")
    if dimensions is None:
        dimensions = _nested(manifest, "unreal", "expectedDimensionsCm")
    tolerance = _nested(manifest, "bounds", "toleranceCm")
    if tolerance is None:
        tolerance = _nested(manifest, "unreal", "boundsToleranceCm")

    if isinstance(dimensions, (list, tuple)) and len(dimensions) == 3:
        dimensions = dict(zip(("x", "y", "z"), dimensions))
    if not isinstance(dimensions, dict):
        _fail("Manifest must define bounds.expectedDimensionsCm with x/y/z")

    parsed: dict[str, float] = {}
    for axis in ("x", "y", "z"):
        try:
            parsed[axis] = float(dimensions[axis])
        except (KeyError, TypeError, ValueError):
            _fail(f"Expected dimension '{axis}' must be numeric")
        if parsed[axis] <= 0.0:
            _fail(f"Expected dimension '{axis}' must be positive")

    try:
        parsed_tolerance = float(tolerance)
    except (TypeError, ValueError):
        _fail("Manifest field 'bounds.toleranceCm' must be numeric")
    if parsed_tolerance < 0.0:
        _fail("Manifest field 'bounds.toleranceCm' cannot be negative")
    return parsed, parsed_tolerance


def _required_material_slots(manifest: dict[str, Any]) -> list[str]:
    configured = _nested(manifest, "unreal", "requiredMaterialSlots")
    if configured is None:
        materials = manifest.get("materials")
        if isinstance(materials, dict):
            configured = list(materials.keys())
        elif isinstance(materials, list):
            configured = [
                item.get("slot") if isinstance(item, dict) else item
                for item in materials
            ]
    if not isinstance(configured, list) or not configured:
        _fail("Manifest must declare at least one required material slot")

    result = [_required_string(value, "required material slot") for value in configured]
    if len(result) != len(set(result)):
        _fail("Manifest required material slots must be unique")
    return result


def _required_socket_name(manifest: dict[str, Any]) -> str:
    render_name = _required_string(
        _nested(manifest, "objects", "render"), "objects.render"
    )
    collision_name = _required_string(
        _nested(manifest, "objects", "collision", "export"),
        "objects.collision.export",
    )
    if not re.fullmatch(rf"UCX_{re.escape(render_name)}_[0-9]{{2}}", collision_name):
        _fail(f"Collision export must be named UCX_{render_name}_NN")

    socket_export = _required_string(
        _nested(manifest, "objects", "socket", "export"),
        "objects.socket.export",
    )
    if not re.fullmatch(rf"SOCKET_{re.escape(render_name)}_[0-9]{{2}}", socket_export):
        _fail(f"Socket export must be named SOCKET_{render_name}_NN")
    # UE's legacy FBX importer strips the SOCKET_ marker from the asset socket.
    return socket_export.removeprefix("SOCKET_")


def _import_flag(manifest: dict[str, Any], names: tuple[str, ...]) -> Any:
    import_config = _nested(manifest, "unreal", "import")
    if not isinstance(import_config, dict):
        _fail("Manifest field 'unreal.import' must be an object")
    for name in names:
        if name in import_config:
            return import_config[name]
    return None


def _validate_import_policy(manifest: dict[str, Any]) -> None:
    policy = (
        (("materials", "importMaterials"), False),
        (("textures", "importTextures"), False),
        (("autoCollision", "autoGenerateCollision"), False),
        (("nanite", "buildNanite"), False),
    )
    for names, expected in policy:
        actual = _import_flag(manifest, names)
        if actual is not expected:
            _fail(
                f"unreal.import.{names[0]} must be {str(expected).lower()} "
                f"for the static-mesh pipeline"
            )

    combine = _import_flag(manifest, ("combineMeshes", "combine"))
    if combine is not None and combine is not True:
        _fail("unreal.import.combineMeshes must be true when declared")


def _validate_export_report(
    manifest: dict[str, Any],
    manifest_path: Path,
    source_blend: Path,
    fbx_path: Path,
    exporter_path: Path,
    report: dict[str, Any],
) -> None:
    if report.get("schemaVersion") != manifest.get("schemaVersion"):
        _fail("Blender export report schemaVersion does not match the manifest")
    if report.get("assetId") != manifest.get("assetId"):
        _fail("Blender export report assetId does not match the manifest")

    hashes = report.get("hashes")
    if not isinstance(hashes, dict):
        _fail("Blender export report must contain a hashes object")
    expected_hashes = {
        "manifestSha256": _sha256_text(manifest_path),
        "sourceBlendSha256": _sha256(source_blend),
        "fbxSha256": _sha256(fbx_path),
        "exporterSha256": _sha256_text(exporter_path),
    }
    for field, actual in expected_hashes.items():
        if str(hashes.get(field, "")).lower() != actual.lower():
            _fail(
                f"Blender export report {field} is stale: expected {actual}, "
                f"got {hashes.get(field, '<missing>')}"
            )


def _set_property(instance: Any, name: str, value: Any) -> None:
    try:
        instance.set_editor_property(name, value)
    except Exception as exc:
        _fail(
            f"Unreal {instance.__class__.__name__} does not accept required "
            f"property '{name}': {exc}"
        )


def _first_import(fbx_path: Path, destination: str) -> unreal.StaticMesh:
    destination_path, asset_name = destination.rsplit("/", 1)

    options = unreal.FbxImportUI()
    _set_property(options, "automated_import_should_detect_type", False)
    _set_property(options, "import_mesh", True)
    _set_property(options, "import_as_skeletal", False)
    _set_property(options, "import_animations", False)
    _set_property(options, "import_materials", False)
    _set_property(options, "import_textures", False)
    _set_property(options, "create_physics_asset", False)
    _set_property(options, "override_full_name", True)
    _set_property(
        options, "mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH
    )

    import_data = options.get_editor_property("static_mesh_import_data")
    _set_property(import_data, "import_translation", unreal.Vector(0.0, 0.0, 0.0))
    _set_property(import_data, "import_rotation", unreal.Rotator(0.0, 0.0, 0.0))
    _set_property(import_data, "import_uniform_scale", 1.0)
    _set_property(import_data, "convert_scene", True)
    _set_property(import_data, "force_front_x_axis", False)
    _set_property(import_data, "convert_scene_unit", True)
    _set_property(import_data, "transform_vertex_to_absolute", True)
    _set_property(import_data, "bake_pivot_in_vertex", False)
    # Blender supplies normals; Unreal deterministically rebuilds MikkTSpace tangents.
    _set_property(
        import_data,
        "normal_import_method",
        unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
    )
    _set_property(
        import_data,
        "normal_generation_method",
        unreal.FBXNormalGenerationMethod.MIKK_T_SPACE,
    )
    _set_property(import_data, "combine_meshes", True)
    _set_property(import_data, "auto_generate_collision", False)
    _set_property(import_data, "one_convex_hull_per_ucx", True)
    _set_property(import_data, "build_nanite", False)
    _set_property(import_data, "remove_degenerates", True)
    _set_property(import_data, "generate_lightmap_u_vs", True)

    task = unreal.AssetImportTask()
    _set_property(task, "filename", str(fbx_path))
    _set_property(task, "destination_path", destination_path)
    _set_property(task, "destination_name", asset_name)
    _set_property(task, "automated", True)
    _set_property(task, "replace_existing", False)
    _set_property(task, "replace_existing_settings", False)
    _set_property(task, "save", False)
    _set_property(task, "options", options)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    imported_paths = list(task.get_editor_property("imported_object_paths"))
    normalized_imported_paths = [_package_path(str(path)) for path in imported_paths]
    if destination not in normalized_imported_paths:
        _fail(
            f"FBX import did not create expected asset {destination}; "
            f"reported paths: {imported_paths}"
        )
    asset = unreal.EditorAssetLibrary.load_asset(destination)
    if not isinstance(asset, unreal.StaticMesh):
        _fail(f"Imported object is not a StaticMesh: {destination}")
    return asset


def _source_filenames(asset: unreal.StaticMesh) -> list[Path]:
    try:
        import_data = asset.get_editor_property("asset_import_data")
        filenames = import_data.extract_filenames()
    except Exception as exc:
        _fail(f"Could not inspect existing StaticMesh import source: {exc}")
    return [Path(filename).resolve(strict=False) for filename in filenames if filename]


def _validate_saved_import_contract(
    asset: unreal.StaticMesh,
    fbx_path: Path,
) -> dict[str, Any]:
    source_filenames = _source_filenames(asset)
    canonical_fbx = fbx_path.resolve()
    if source_filenames != [canonical_fbx]:
        _fail(
            "StaticMesh import source drifted from the manifest. "
            f"Expected only {canonical_fbx}; found {source_filenames}"
        )

    try:
        import_data = asset.get_editor_property("asset_import_data")
        import_data_class = import_data.get_class().get_name()
    except Exception as exc:
        _fail(f"Could not inspect saved StaticMesh import data: {exc}")
    if import_data_class != "FbxStaticMeshImportData":
        _fail(
            "StaticMesh must retain legacy FbxStaticMeshImportData; "
            f"found {import_data_class}"
        )

    expected_properties = {
        "convert_scene": True,
        "force_front_x_axis": False,
        "convert_scene_unit": True,
        "transform_vertex_to_absolute": True,
        "bake_pivot_in_vertex": False,
        "normal_import_method": unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
        "normal_generation_method": unreal.FBXNormalGenerationMethod.MIKK_T_SPACE,
        "combine_meshes": True,
        "auto_generate_collision": False,
        "one_convex_hull_per_ucx": True,
        "build_nanite": False,
        "remove_degenerates": True,
        "generate_lightmap_u_vs": True,
    }
    mismatches: list[str] = []
    for property_name, expected in expected_properties.items():
        try:
            actual = import_data.get_editor_property(property_name)
        except Exception as exc:
            _fail(f"Could not read saved import property {property_name}: {exc}")
        if actual != expected:
            mismatches.append(f"{property_name} expected {expected!s}, got {actual!s}")

    try:
        translation = import_data.get_editor_property("import_translation")
        rotation = import_data.get_editor_property("import_rotation")
        uniform_scale = float(import_data.get_editor_property("import_uniform_scale"))
    except Exception as exc:
        _fail(f"Could not read saved import transform: {exc}")
    transform_values = {
        "import_translation.x": float(translation.x),
        "import_translation.y": float(translation.y),
        "import_translation.z": float(translation.z),
        "import_rotation.pitch": float(rotation.pitch),
        "import_rotation.yaw": float(rotation.yaw),
        "import_rotation.roll": float(rotation.roll),
    }
    for field, actual in transform_values.items():
        if not math.isclose(actual, 0.0, abs_tol=1.0e-6):
            mismatches.append(f"{field} expected 0, got {actual}")
    if not math.isclose(uniform_scale, 1.0, abs_tol=1.0e-6):
        mismatches.append(f"import_uniform_scale expected 1, got {uniform_scale}")

    if mismatches:
        _fail("Saved FBX import settings drifted: " + "; ".join(mismatches))
    return {
        "source": os.fspath(canonical_fbx),
        "normalPolicy": "ImportNormals+MikkTSpaceTangents",
    }


def _reimport_existing(asset: unreal.StaticMesh, fbx_path: Path, destination: str) -> None:
    _validate_saved_import_contract(asset, fbx_path)

    destination_path, asset_name = destination.rsplit("/", 1)
    task = unreal.AssetImportTask()
    _set_property(task, "filename", str(fbx_path))
    _set_property(task, "destination_path", destination_path)
    _set_property(task, "destination_name", asset_name)
    _set_property(task, "automated", True)
    _set_property(task, "replace_existing", True)
    # A null Options object plus this flag makes the legacy reimport factory use
    # the UFbxStaticMeshImportData already stored on the asset.
    _set_property(task, "replace_existing_settings", False)
    _set_property(task, "save", False)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    imported_paths = [
        _package_path(str(path))
        for path in task.get_editor_property("imported_object_paths")
    ]
    imported_objects = list(task.get_objects())
    if destination not in imported_paths and asset not in imported_objects:
        _fail(
            f"Legacy reimport did not report expected asset {destination}; "
            f"reported paths: {imported_paths}"
        )


def _mesh_dimensions_cm(mesh: unreal.StaticMesh) -> dict[str, float]:
    try:
        bounds = mesh.get_bounds()
        extent = bounds.box_extent
        return {
            "x": float(extent.x) * 2.0,
            "y": float(extent.y) * 2.0,
            "z": float(extent.z) * 2.0,
        }
    except Exception as exc:
        _fail(f"Could not read StaticMesh bounds: {exc}")


def _material_slot_names(mesh: unreal.StaticMesh) -> list[str]:
    slots: list[str] = []
    try:
        for material in mesh.get_editor_property("static_materials"):
            imported_name = str(
                material.get_editor_property("imported_material_slot_name")
            )
            slot_name = str(material.get_editor_property("material_slot_name"))
            slots.append(imported_name if imported_name and imported_name != "None" else slot_name)
    except Exception as exc:
        _fail(f"Could not inspect StaticMesh material slots: {exc}")
    return slots


def _simple_collision_counts(mesh: unreal.StaticMesh) -> dict[str, int]:
    """Read every simple primitive type, including UCX convex elements.

    UE 5.8's StaticMeshEditorSubsystem.get_simple_collision_count omits
    ConvexElems, so it cannot validate the UCX contract used by this pipeline.
    """
    try:
        body_setup = mesh.get_editor_property("body_setup")
        if body_setup is None:
            return {
                "box": 0,
                "sphere": 0,
                "capsule": 0,
                "convex": 0,
                "taperedCapsule": 0,
            }
        aggregate = body_setup.get_editor_property("agg_geom")
        return {
            "box": len(aggregate.get_editor_property("box_elems")),
            "sphere": len(aggregate.get_editor_property("sphere_elems")),
            "capsule": len(aggregate.get_editor_property("sphyl_elems")),
            "convex": len(aggregate.get_editor_property("convex_elems")),
            "taperedCapsule": len(
                aggregate.get_editor_property("tapered_capsule_elems")
            ),
        }
    except Exception as exc:
        _fail(f"Could not inspect StaticMesh aggregate collision geometry: {exc}")


def _nanite_enabled(mesh: unreal.StaticMesh) -> bool:
    try:
        settings = mesh.get_editor_property("nanite_settings")
        return bool(settings.get_editor_property("enabled"))
    except Exception as exc:
        _fail(f"Could not inspect StaticMesh Nanite settings: {exc}")


def _socket_names(mesh: unreal.StaticMesh) -> list[str]:
    try:
        # StaticMesh.Sockets is protected in UE 5.8's Python reflection. Imported
        # FBX sockets have the default empty tag, which this public API exposes.
        sockets = list(mesh.get_sockets_by_tag(""))
        return [
            str(socket.get_editor_property("socket_name"))
            for socket in sockets
        ]
    except Exception as exc:
        _fail(f"Could not inspect StaticMesh sockets: {exc}")


def _validate_mesh(
    mesh: unreal.StaticMesh,
    destination: str,
    expected_dimensions: dict[str, float],
    tolerance_cm: float,
    required_slots: list[str],
    required_socket_name: str,
) -> dict[str, Any]:
    loaded_path = _package_path(
        unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(mesh)
    )
    if loaded_path != destination:
        _fail(f"Loaded StaticMesh path mismatch: expected {destination}, got {loaded_path}")
    if mesh.get_class().get_name() != "StaticMesh":
        _fail(f"Expected StaticMesh class at {destination}, got {mesh.get_class().get_name()}")

    dimensions = _mesh_dimensions_cm(mesh)
    mismatches = [
        f"{axis} expected {expected_dimensions[axis]:.3f}cm, got {dimensions[axis]:.3f}cm"
        for axis in ("x", "y", "z")
        if abs(dimensions[axis] - expected_dimensions[axis]) > tolerance_cm
    ]
    if mismatches:
        _fail(
            f"StaticMesh bounds exceed {tolerance_cm:.3f}cm tolerance: "
            + "; ".join(mismatches)
        )

    material_slots = _material_slot_names(mesh)
    if material_slots != required_slots:
        _fail(
            "StaticMesh material slot contract mismatch; "
            f"expected ordered slots {required_slots}, actual {material_slots}"
        )

    collision_counts = _simple_collision_counts(mesh)
    expected_collision_counts = {
        "box": 0,
        "sphere": 0,
        "capsule": 0,
        "convex": 1,
        "taperedCapsule": 0,
    }
    if collision_counts != expected_collision_counts:
        _fail(
            "StaticMesh collision contract mismatch. Expected exactly one UCX "
            f"convex and no generated primitives; actual {collision_counts}"
        )
    collision_count = sum(collision_counts.values())

    socket_names = _socket_names(mesh)
    expected_socket_names = [required_socket_name]
    if socket_names != expected_socket_names:
        _fail(
            "StaticMesh socket contract mismatch; "
            f"expected exactly {expected_socket_names}, actual {socket_names}"
        )
    actual_socket_name = socket_names[0]

    if _nanite_enabled(mesh):
        _fail("StaticMesh has Nanite enabled; this blockout pipeline requires it off")

    return {
        "asset": destination,
        "dimensionsCm": dimensions,
        "materialSlots": material_slots,
        "simpleCollisionCount": collision_count,
        "simpleCollisionPrimitives": collision_counts,
        "socket": actual_socket_name,
        "nanite": False,
    }


def _bind_scroll_definition(
    mesh: unreal.StaticMesh, definition_path: str, *, validate_only: bool
) -> None:
    definition = unreal.EditorAssetLibrary.load_asset(definition_path)
    if not definition:
        _fail(
            f"Scroll definition is missing: {definition_path}. Generate the base "
            "technical-spike data assets before importing presentation assets."
        )
    if definition.get_class().get_name() != "SPScrollDefinition":
        _fail(
            f"Asset {definition_path} has class {definition.get_class().get_name()}, "
            "expected SPScrollDefinition"
        )

    try:
        current = definition.get_editor_property("pickup_mesh")
    except Exception as exc:
        _fail(
            "SPScrollDefinition does not expose pickup_mesh. Build the current "
            f"ScrollPeddlerEditor target before running the asset pipeline: {exc}"
        )

    if validate_only:
        current_path = (
            _package_path(unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(current))
            if current
            else ""
        )
        expected_path = _package_path(
            unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(mesh)
        )
        if current_path != expected_path:
            _fail(
                f"Scroll definition pickup_mesh mismatch: expected {expected_path}, "
                f"got {current_path or '<unset>'}"
            )
        return

    definition.set_editor_property("pickup_mesh", mesh)
    if not unreal.EditorAssetLibrary.save_asset(
        definition_path, only_if_is_dirty=False
    ):
        _fail(f"Could not save scroll definition: {definition_path}")


def main() -> None:
    tokens = _command_line_tokens()
    validate_only = _has_switch(tokens, ("-SPValidateOnly", "--validate-only"))
    manifest_path = _manifest_path(tokens)
    manifest = _read_json(manifest_path, "asset manifest")
    if manifest.get("schemaVersion") != 1:
        _fail("Only asset manifest schemaVersion 1 is supported")

    _validate_import_policy(manifest)
    source_blend = _manifest_file(manifest, "sourceBlend", must_exist=True)
    fbx_path = _manifest_file(manifest, "fbx", must_exist=True)
    report_path = _manifest_file(manifest, "report", must_exist=True)
    exporter_path = _repository_path(
        BLENDER_EXPORTER, "Blender exporter", must_exist=True
    )
    expected_extensions = {
        source_blend: ".blend",
        fbx_path: ".fbx",
        report_path: ".json",
    }
    for pipeline_path, expected_extension in expected_extensions.items():
        if pipeline_path.suffix.lower() != expected_extension:
            _fail(
                f"Pipeline file must use {expected_extension}: {pipeline_path}"
            )
    if len(expected_extensions) != 3:
        _fail("sourceBlend, fbx, and report must be distinct files")
    report = _read_json(report_path, "Blender export report")
    _validate_export_report(
        manifest,
        manifest_path,
        source_blend,
        fbx_path,
        exporter_path,
        report,
    )

    destination = _asset_path_from_manifest(manifest)
    definition_path = _scroll_definition_path(manifest)
    expected_dimensions, tolerance_cm = _expected_dimensions(manifest)
    required_slots = _required_material_slots(manifest)
    required_socket_name = _required_socket_name(manifest)

    existing = (
        unreal.EditorAssetLibrary.load_asset(destination)
        if unreal.EditorAssetLibrary.does_asset_exist(destination)
        else None
    )
    if validate_only:
        if not isinstance(existing, unreal.StaticMesh):
            _fail(f"Validate-only requires an existing StaticMesh at {destination}")
        mesh = existing
        operation = "validated"
    elif existing:
        if not isinstance(existing, unreal.StaticMesh):
            _fail(f"Existing object is not a StaticMesh: {destination}")
        _reimport_existing(existing, fbx_path, destination)
        mesh = unreal.EditorAssetLibrary.load_asset(destination)
        operation = "reimported"
    else:
        mesh = _first_import(fbx_path, destination)
        operation = "imported"

    if not isinstance(mesh, unreal.StaticMesh):
        _fail(f"Could not load StaticMesh after {operation}: {destination}")

    import_contract = _validate_saved_import_contract(mesh, fbx_path)
    summary = _validate_mesh(
        mesh,
        destination,
        expected_dimensions,
        tolerance_cm,
        required_slots,
        required_socket_name,
    )
    if not validate_only and not unreal.EditorAssetLibrary.save_asset(
        destination, only_if_is_dirty=False
    ):
        _fail(f"Could not save StaticMesh: {destination}")
    _bind_scroll_definition(mesh, definition_path, validate_only=validate_only)

    summary.update(
        {
            "operation": operation,
            "manifest": os.fspath(manifest_path),
            "scrollDefinition": definition_path,
            "importContract": import_contract,
        }
    )
    unreal.log(f"[{LOG_PREFIX}] {json.dumps(summary, sort_keys=True)}")


try:
    main()
except Exception as error:
    unreal.log_error(str(error))
    raise
