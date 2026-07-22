"""Validate and export a Blender-authored static mesh for Unreal Engine.

Run through Blender, with the source .blend already opened::

    blender --background Source.blend --python export_static_mesh.py -- \
        --manifest Scripts/AssetPipeline/assets/asset.json

The exporter never saves the open .blend. It evaluates modifiers and curves into
temporary datablocks, bakes every render component into root-local coordinates,
and removes all temporary datablocks after the FBX operation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
from datetime import datetime, timezone
from typing import Any, Callable, Iterable, Sequence

import bmesh
import bpy
from mathutils import Matrix, Vector


REPORT_SCHEMA_VERSION = 1
SUPPORTED_SOURCE_TYPES = {"MESH", "CURVE"}
TEMP_EXPORT_COLLECTION = "__SP_ASSET_PIPELINE_EXPORT__"


class PipelineError(RuntimeError):
    """Raised when authored data does not satisfy the export contract."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise PipelineError(message)


def _parse_args() -> argparse.Namespace:
    script_args = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(
        description="Validate and export a manifest-driven Unreal static mesh."
    )
    parser.add_argument("--manifest", required=True, help="Asset manifest JSON path")
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Validate source data without writing FBX or report files",
    )
    return parser.parse_args(script_args)


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise PipelineError(f"Manifest does not exist: {path}") from exc
    except json.JSONDecodeError as exc:
        raise PipelineError(f"Manifest is not valid JSON: {path}: {exc}") from exc
    _require(isinstance(value, dict), "Manifest root must be a JSON object")
    return value


def _mapping(value: Any, label: str) -> dict[str, Any]:
    _require(isinstance(value, dict), f"{label} must be a JSON object")
    return value


def _string(value: Any, label: str) -> str:
    _require(isinstance(value, str) and bool(value.strip()), f"{label} must be a non-empty string")
    return value


def _number(value: Any, label: str) -> float:
    _require(
        isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value),
        f"{label} must be a finite number",
    )
    return float(value)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _find_repository_root(manifest_path: Path) -> Path:
    for candidate in (manifest_path.parent, *manifest_path.parents):
        if (candidate / ".git").exists() and (candidate / "ScrollPeddler.uproject").is_file():
            return candidate.resolve()
    raise PipelineError(
        f"Could not find repository root above manifest (expected .git and ScrollPeddler.uproject): "
        f"{manifest_path}"
    )


def _resolve_repo_path(repository_root: Path, raw_path: Any, label: str) -> Path:
    relative = Path(_string(raw_path, label))
    _require(not relative.is_absolute(), f"{label} must be repository-relative")
    resolved = (repository_root / relative).resolve()
    try:
        resolved.relative_to(repository_root)
    except ValueError as exc:
        raise PipelineError(f"{label} escapes the repository: {relative.as_posix()}") from exc
    return resolved


def _repo_relative(path: Path, repository_root: Path) -> str:
    try:
        return path.resolve().relative_to(repository_root).as_posix()
    except ValueError as exc:
        raise PipelineError(f"Path is outside repository and cannot enter report: {path}") from exc


def _load_contract(manifest_path: Path) -> dict[str, Any]:
    manifest = _read_json(manifest_path)
    _require(manifest.get("schemaVersion") == 1, "Unsupported manifest schemaVersion; expected 1")

    repository_root = _find_repository_root(manifest_path)
    pipeline = _mapping(manifest.get("pipeline"), "pipeline")
    paths = _mapping(manifest.get("paths"), "paths")
    objects = _mapping(manifest.get("objects"), "objects")
    collision = _mapping(objects.get("collision"), "objects.collision")
    socket = _mapping(objects.get("socket"), "objects.socket")
    bounds = _mapping(manifest.get("bounds"), "bounds")
    materials = _mapping(manifest.get("materials"), "materials")

    expected_dimensions = bounds.get("expectedDimensionsCm")
    _require(
        isinstance(expected_dimensions, list) and len(expected_dimensions) == 3,
        "bounds.expectedDimensionsCm must contain exactly three numbers",
    )
    expected_dimensions_cm = [
        _number(value, f"bounds.expectedDimensionsCm[{index}]")
        for index, value in enumerate(expected_dimensions)
    ]
    _require(all(value > 0.0 for value in expected_dimensions_cm), "Expected dimensions must be positive")
    tolerance_cm = _number(bounds.get("toleranceCm"), "bounds.toleranceCm")
    _require(tolerance_cm >= 0.0, "bounds.toleranceCm cannot be negative")

    material_groups: dict[str, list[str]] = {}
    used_tokens: set[str] = set()
    for canonical_name, raw_tokens in materials.items():
        canonical = _string(canonical_name, "materials key")
        _require(
            isinstance(raw_tokens, list) and bool(raw_tokens),
            f"materials.{canonical} must be a non-empty token array",
        )
        tokens = [_string(value, f"materials.{canonical}[]") for value in raw_tokens]
        for token in tokens:
            folded = token.casefold()
            _require(folded not in used_tokens, f"Material token is duplicated: {token}")
            used_tokens.add(folded)
        material_groups[canonical] = tokens

    source_blend = _resolve_repo_path(repository_root, paths.get("sourceBlend"), "paths.sourceBlend")
    fbx_path = _resolve_repo_path(repository_root, paths.get("fbx"), "paths.fbx")
    report_path = _resolve_repo_path(repository_root, paths.get("report"), "paths.report")
    _require(fbx_path.suffix.casefold() == ".fbx", "paths.fbx must end in .fbx")
    _require(report_path.name.endswith(".export.json"), "paths.report must end in .export.json")
    _require(report_path.parent == fbx_path.parent, "Export report must be adjacent to the FBX")
    _require(source_blend not in {fbx_path, report_path}, "Source and output paths must be distinct")

    render_name = _string(objects.get("render"), "objects.render")
    collision_export = _string(collision.get("export"), "objects.collision.export")
    socket_export = _string(socket.get("export"), "objects.socket.export")
    _require(
        re.fullmatch(rf"UCX_{re.escape(render_name)}_[0-9]{{2}}", collision_export) is not None,
        f"Collision export must be named UCX_{render_name}_NN",
    )
    _require(
        re.fullmatch(rf"SOCKET_{re.escape(render_name)}_[0-9]{{2}}", socket_export) is not None,
        f"Socket export must be named SOCKET_{render_name}_NN",
    )

    return {
        "manifest": manifest,
        "manifest_path": manifest_path,
        "repository_root": repository_root,
        "asset_id": _string(manifest.get("assetId"), "assetId"),
        "blender_version": _string(pipeline.get("blenderVersion"), "pipeline.blenderVersion"),
        "unit_system": _string(pipeline.get("unitSystem"), "pipeline.unitSystem"),
        "unit_scale": _number(pipeline.get("unitScale"), "pipeline.unitScale"),
        "source_blend": source_blend,
        "fbx_path": fbx_path,
        "report_path": report_path,
        "root_name": _string(objects.get("root"), "objects.root"),
        "render_name": render_name,
        "collision_source": _string(collision.get("source"), "objects.collision.source"),
        "collision_export": collision_export,
        "socket_source": _string(socket.get("source"), "objects.socket.source"),
        "socket_export": socket_export,
        "material_groups": material_groups,
        "expected_dimensions_cm": expected_dimensions_cm,
        "tolerance_cm": tolerance_cm,
    }


def _validate_environment(contract: dict[str, Any]) -> None:
    _require(bpy.app.background, "Exporter must run in Blender background mode")
    expected_version = tuple(int(part) for part in contract["blender_version"].split("."))
    _require(len(expected_version) == 3, "pipeline.blenderVersion must use major.minor.patch")
    _require(
        tuple(bpy.app.version[:3]) == expected_version,
        f"Blender {contract['blender_version']} is required; running {bpy.app.version_string}",
    )
    scene = bpy.context.scene
    _require(scene is not None, "No active Blender scene")
    _require(bpy.context.mode == "OBJECT", "Exporter must run in Object Mode")
    _require(
        bpy.data.collections.get(TEMP_EXPORT_COLLECTION) is None,
        f"Source file uses reserved collection name: {TEMP_EXPORT_COLLECTION}",
    )
    _require(
        scene.unit_settings.system == contract["unit_system"],
        f"Scene unit system must be {contract['unit_system']}, found {scene.unit_settings.system}",
    )
    _require(
        math.isclose(scene.unit_settings.scale_length, contract["unit_scale"], abs_tol=1.0e-9),
        f"Scene unit scale must be {contract['unit_scale']}, found {scene.unit_settings.scale_length}",
    )

    source_path = contract["source_blend"]
    _require(source_path.is_file(), f"Source .blend does not exist: {source_path}")
    _require(bool(bpy.data.filepath), "Open the manifest source .blend before running exporter")
    _require(
        Path(bpy.data.filepath).resolve() == source_path,
        f"Open file does not match manifest source: {Path(bpy.data.filepath).resolve()} != {source_path}",
    )


def _is_descendant(obj: bpy.types.Object, root: bpy.types.Object) -> bool:
    parent = obj.parent
    while parent is not None:
        if parent == root:
            return True
        parent = parent.parent
    return False


def _finite_matrix(matrix: Matrix) -> bool:
    return all(math.isfinite(value) for row in matrix for value in row)


def _validate_collision_geometry(
    mesh: bpy.types.Mesh,
    source_name: str,
) -> dict[str, Any]:
    """Require one closed, non-degenerate convex body suitable for a UCX hull."""
    collision = bmesh.new()
    try:
        collision.from_mesh(mesh)
        _require(len(collision.verts) >= 4, f"UCX collision needs at least four vertices: {source_name}")
        _require(len(collision.faces) >= 4, f"UCX collision needs at least four faces: {source_name}")
        _require(
            all(edge.is_manifold for edge in collision.edges),
            f"UCX collision must be a closed manifold: {source_name}",
        )

        bmesh.ops.recalc_face_normals(collision, faces=list(collision.faces))
        center = sum((vertex.co for vertex in collision.verts), Vector()) / len(collision.verts)
        radius = max((vertex.co - center).length for vertex in collision.verts)
        linear_tolerance = max(radius * 1.0e-5, 1.0e-7)
        area_tolerance = linear_tolerance * linear_tolerance
        _require(
            all(face.calc_area() > area_tolerance for face in collision.faces),
            f"UCX collision contains a degenerate face: {source_name}",
        )

        volume = abs(float(collision.calc_volume(signed=True)))
        _require(
            volume > max(radius**3 * 1.0e-9, 1.0e-12),
            f"UCX collision has no usable volume: {source_name}",
        )

        for face in collision.faces:
            outward = face.normal.normalized()
            face_point = face.verts[0].co
            if (center - face_point).dot(outward) > 0.0:
                outward.negate()
            maximum_outside = max(
                (vertex.co - face_point).dot(outward)
                for vertex in collision.verts
            )
            _require(
                maximum_outside <= linear_tolerance,
                f"UCX collision must be convex: {source_name}",
            )

        return {
            "closedManifold": True,
            "convex": True,
            "volumeCubicMeters": volume,
        }
    finally:
        collision.free()


def _evaluated_mesh(
    source: bpy.types.Object,
    root_inverse: Matrix,
    depsgraph: bpy.types.Depsgraph,
    name: str,
) -> bpy.types.Mesh:
    evaluated = source.evaluated_get(depsgraph)
    _require(evaluated.type in SUPPORTED_SOURCE_TYPES, f"Unsupported geometry type: {source.name}: {source.type}")
    transform = root_inverse @ evaluated.matrix_world
    _require(_finite_matrix(transform), f"Non-finite transform on source object: {source.name}")
    mesh = bpy.data.meshes.new_from_object(
        evaluated,
        preserve_all_data_layers=True,
        depsgraph=depsgraph,
    )
    _require(mesh is not None, f"Could not evaluate source geometry: {source.name}")
    mesh.name = name
    mesh.transform(transform)
    mesh.update()
    _require(len(mesh.vertices) > 0, f"Source has no evaluated vertices: {source.name}")
    _require(len(mesh.polygons) > 0, f"Source has no evaluated polygons: {source.name}")
    for vertex in mesh.vertices:
        _require(
            all(math.isfinite(value) for value in vertex.co),
            f"Source contains a non-finite vertex: {source.name}",
        )
    return mesh


def _canonical_material(material_name: str, groups: dict[str, list[str]]) -> str:
    folded_name = material_name.casefold()
    canonical_name_matches = [
        canonical for canonical in groups if canonical.casefold() == folded_name
    ]
    if canonical_name_matches:
        return canonical_name_matches[0]
    matches = {
        canonical
        for canonical, tokens in groups.items()
        if any(token.casefold() in folded_name for token in tokens)
    }
    _require(bool(matches), f"Material is not covered by manifest mapping: {material_name}")
    _require(
        len(matches) == 1,
        f"Material maps to conflicting canonical slots {sorted(matches)}: {material_name}",
    )
    return next(iter(matches))


def _inspect_mesh_materials(
    source_name: str,
    mesh: bpy.types.Mesh,
    groups: dict[str, list[str]],
) -> dict[str, set[str]]:
    _require(len(mesh.materials) > 0, f"Render geometry has no material slots: {source_name}")
    result = {canonical: set() for canonical in groups}
    slot_targets: list[str] = []
    for slot_index, material in enumerate(mesh.materials):
        _require(material is not None, f"Empty material slot {slot_index} on {source_name}")
        canonical = _canonical_material(material.name, groups)
        slot_targets.append(canonical)
        result[canonical].add(material.name)
    for polygon in mesh.polygons:
        _require(
            polygon.material_index < len(slot_targets),
            f"Polygon references invalid material slot on {source_name}",
        )
    return result


def _extend_bounds(
    mesh: bpy.types.Mesh,
    minimum: list[float],
    maximum: list[float],
) -> None:
    for vertex in mesh.vertices:
        for axis in range(3):
            value = float(vertex.co[axis])
            minimum[axis] = min(minimum[axis], value)
            maximum[axis] = max(maximum[axis], value)


def _validate_source(contract: dict[str, Any]) -> dict[str, Any]:
    scene = bpy.context.scene
    root = scene.objects.get(contract["root_name"])
    _require(root is not None, f"Missing root object: {contract['root_name']}")
    _require(root.type == "EMPTY", f"Root object must be an Empty: {root.name} is {root.type}")
    _require(root.library is None, f"Root object must be local data: {root.name}")
    _require(abs(root.matrix_world.determinant()) > 1.0e-12, f"Root transform is not invertible: {root.name}")
    root_inverse = root.matrix_world.inverted()

    collision_source = scene.objects.get(contract["collision_source"])
    _require(collision_source is not None, f"Missing collision source: {contract['collision_source']}")
    _require(
        _is_descendant(collision_source, root),
        f"Collision source must be below {root.name}: {collision_source.name}",
    )
    _require(
        collision_source.type in SUPPORTED_SOURCE_TYPES,
        f"Collision source must be Mesh or Curve: {collision_source.name}",
    )

    socket_source = scene.objects.get(contract["socket_source"])
    _require(socket_source is not None, f"Missing socket source: {contract['socket_source']}")
    _require(socket_source.type == "EMPTY", f"Socket source must be an Empty: {socket_source.name}")
    _require(
        _is_descendant(socket_source, root),
        f"Socket source must be below {root.name}: {socket_source.name}",
    )
    socket_matrix_root_local = root_inverse @ socket_source.matrix_world
    _require(
        _finite_matrix(socket_matrix_root_local),
        f"Socket source has a non-finite root-local transform: {socket_source.name}",
    )

    for output_name in (
        contract["render_name"],
        contract["collision_export"],
        contract["socket_export"],
    ):
        _require(
            bpy.data.objects.get(output_name) is None,
            f"Output object name already exists in source scene: {output_name}",
        )

    descendants = [obj for obj in scene.objects if _is_descendant(obj, root)]
    unexpected = [
        obj.name
        for obj in descendants
        if obj.type not in SUPPORTED_SOURCE_TYPES | {"EMPTY"}
    ]
    _require(not unexpected, f"Unsupported objects below export root: {sorted(unexpected)}")
    geometry_sources = sorted(
        (
            obj
            for obj in descendants
            if obj.type in SUPPORTED_SOURCE_TYPES and obj != collision_source
        ),
        key=lambda obj: obj.name.casefold(),
    )
    _require(bool(geometry_sources), f"No render geometry below root: {root.name}")
    for source in geometry_sources:
        _require(source.library is None, f"Render source must be local data: {source.name}")

    depsgraph = bpy.context.evaluated_depsgraph_get()
    minimum = [math.inf, math.inf, math.inf]
    maximum = [-math.inf, -math.inf, -math.inf]
    used_materials = {canonical: set() for canonical in contract["material_groups"]}
    vertex_count = 0
    polygon_count = 0
    source_types: dict[str, int] = {}
    canonical_colors: dict[str, tuple[float, float, float, float]] = {}

    for source in geometry_sources:
        mesh = _evaluated_mesh(source, root_inverse, depsgraph, f"__SP_VALIDATE__{source.name}")
        try:
            _extend_bounds(mesh, minimum, maximum)
            vertex_count += len(mesh.vertices)
            polygon_count += len(mesh.polygons)
            source_types[source.type] = source_types.get(source.type, 0) + 1
            discovered = _inspect_mesh_materials(
                source.name,
                mesh,
                contract["material_groups"],
            )
            for canonical, names in discovered.items():
                used_materials[canonical].update(names)
            for material in mesh.materials:
                canonical = _canonical_material(material.name, contract["material_groups"])
                canonical_colors.setdefault(canonical, tuple(float(v) for v in material.diffuse_color))
        finally:
            bpy.data.meshes.remove(mesh)

    missing_groups = [canonical for canonical, names in used_materials.items() if not names]
    _require(
        not missing_groups,
        f"Manifest material groups are unused by render geometry: {missing_groups}",
    )

    collision_mesh = _evaluated_mesh(
        collision_source,
        root_inverse,
        depsgraph,
        "__SP_VALIDATE_COLLISION__",
    )
    collision_summary = {
        "source": collision_source.name,
        "vertices": len(collision_mesh.vertices),
        "polygons": len(collision_mesh.polygons),
        **_validate_collision_geometry(collision_mesh, collision_source.name),
    }
    bpy.data.meshes.remove(collision_mesh)

    unit_to_cm = bpy.context.scene.unit_settings.scale_length * 100.0
    dimensions_cm = [(maximum[axis] - minimum[axis]) * unit_to_cm for axis in range(3)]
    deviations_cm = [
        abs(actual - expected)
        for actual, expected in zip(dimensions_cm, contract["expected_dimensions_cm"])
    ]
    _require(
        all(deviation <= contract["tolerance_cm"] for deviation in deviations_cm),
        "Root-local bounds are outside tolerance: "
        f"actual={[round(value, 4) for value in dimensions_cm]} cm, "
        f"expected={contract['expected_dimensions_cm']} cm, "
        f"tolerance={contract['tolerance_cm']} cm",
    )

    return {
        "root": root,
        "root_inverse": root_inverse,
        "geometry_sources": geometry_sources,
        "collision_source_object": collision_source,
        "socket_source_object": socket_source,
        "geometry_count": len(geometry_sources),
        "source_types": source_types,
        "source_vertices": vertex_count,
        "source_polygons": polygon_count,
        "materials": {key: sorted(value) for key, value in used_materials.items()},
        "canonical_colors": canonical_colors,
        "bounds_min_cm": [value * unit_to_cm for value in minimum],
        "bounds_max_cm": [value * unit_to_cm for value in maximum],
        "dimensions_cm": dimensions_cm,
        "deviations_cm": deviations_cm,
        "collision": collision_summary,
        "socket_matrix_root_local": socket_matrix_root_local,
    }


def _consolidate_mesh_materials(
    mesh: bpy.types.Mesh,
    source_name: str,
    groups: dict[str, list[str]],
    canonical_materials: dict[str, bpy.types.Material],
) -> None:
    old_slots = list(mesh.materials)
    _require(bool(old_slots), f"Temporary render mesh lost materials: {source_name}")
    canonical_order = list(groups)
    used = {
        _canonical_material(material.name, groups)
        for material in old_slots
        if material is not None
    }
    ordered_used = [canonical for canonical in canonical_order if canonical in used]
    slot_index = {canonical: index for index, canonical in enumerate(ordered_used)}
    old_to_new: dict[int, int] = {}
    for index, material in enumerate(old_slots):
        _require(material is not None, f"Temporary mesh has empty material slot: {source_name}")
        old_to_new[index] = slot_index[_canonical_material(material.name, groups)]
    remapped_polygon_slots: list[int] = []
    for polygon in mesh.polygons:
        _require(
            polygon.material_index in old_to_new,
            f"Temporary mesh polygon has invalid material slot: {source_name}",
        )
        remapped_polygon_slots.append(old_to_new[polygon.material_index])
    mesh.materials.clear()
    for canonical in ordered_used:
        mesh.materials.append(canonical_materials[canonical])
    for polygon, material_index in zip(mesh.polygons, remapped_polygon_slots):
        polygon.material_index = material_index

    polygon_counts = [0] * len(ordered_used)
    for polygon in mesh.polygons:
        _require(
            0 <= polygon.material_index < len(polygon_counts),
            f"Temporary mesh polygon remap failed: {source_name}",
        )
        polygon_counts[polygon.material_index] += 1
    _require(
        all(count > 0 for count in polygon_counts),
        f"Temporary mesh contains an empty canonical material slot: {source_name}",
    )


def _triangulate(mesh: bpy.types.Mesh) -> None:
    edit_mesh = bmesh.new()
    try:
        edit_mesh.from_mesh(mesh)
        bmesh.ops.triangulate(edit_mesh, faces=list(edit_mesh.faces))
        edit_mesh.to_mesh(mesh)
    finally:
        edit_mesh.free()
    mesh.update()
    _require(all(len(polygon.vertices) == 3 for polygon in mesh.polygons), f"Triangulation failed: {mesh.name}")


def _build_temporary_export(
    contract: dict[str, Any],
    inspection: dict[str, Any],
) -> dict[str, Any]:
    collection = bpy.data.collections.new(TEMP_EXPORT_COLLECTION)
    bpy.context.scene.collection.children.link(collection)
    mesh_names: set[str] = set()
    material_names: set[str] = set()

    canonical_materials: dict[str, bpy.types.Material] = {}
    for canonical in contract["material_groups"]:
        _require(
            bpy.data.materials.get(canonical) is None,
            f"Canonical export material name already exists in source file: {canonical}",
        )
        material = bpy.data.materials.new(canonical)
        material.diffuse_color = inspection["canonical_colors"][canonical]
        canonical_materials[canonical] = material
        material_names.add(material.name)

    depsgraph = bpy.context.evaluated_depsgraph_get()
    render_parts: list[bpy.types.Object] = []
    for index, source in enumerate(inspection["geometry_sources"]):
        mesh = _evaluated_mesh(
            source,
            inspection["root_inverse"],
            depsgraph,
            f"__SP_EXPORT_PART_MESH_{index:03d}__",
        )
        mesh_names.add(mesh.name)
        _consolidate_mesh_materials(
            mesh,
            source.name,
            contract["material_groups"],
            canonical_materials,
        )
        part = bpy.data.objects.new(f"__SP_EXPORT_PART_{index:03d}__", mesh)
        part.matrix_world = Matrix.Identity(4)
        collection.objects.link(part)
        render_parts.append(part)

    bpy.ops.object.select_all(action="DESELECT")
    for part in render_parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = render_parts[0]
    result = bpy.ops.object.join()
    _require(result == {"FINISHED"}, f"Failed to join render geometry: {result}")
    render_object = bpy.context.view_layer.objects.active
    _require(render_object is not None and render_object.type == "MESH", "Joined render object is invalid")
    render_object.name = contract["render_name"]
    render_object.data.name = contract["render_name"]
    mesh_names.add(render_object.data.name)
    render_object.matrix_world = Matrix.Identity(4)
    _consolidate_mesh_materials(
        render_object.data,
        render_object.name,
        contract["material_groups"],
        canonical_materials,
    )
    _triangulate(render_object.data)

    collision_mesh = _evaluated_mesh(
        inspection["collision_source_object"],
        inspection["root_inverse"],
        depsgraph,
        contract["collision_export"],
    )
    collision_mesh.materials.clear()
    _triangulate(collision_mesh)
    mesh_names.add(collision_mesh.name)
    collision_object = bpy.data.objects.new(contract["collision_export"], collision_mesh)
    collision_object.matrix_world = Matrix.Identity(4)
    collection.objects.link(collision_object)

    socket_object = bpy.data.objects.new(contract["socket_export"], None)
    socket_object.empty_display_type = "PLAIN_AXES"
    socket_object.matrix_world = inspection["socket_matrix_root_local"]
    collection.objects.link(socket_object)

    _require(
        [material.name for material in render_object.data.materials]
        == list(contract["material_groups"]),
        "Joined render material slots do not match canonical manifest order",
    )
    material_polygon_counts = {
        material.name: 0 for material in render_object.data.materials
    }
    for polygon in render_object.data.polygons:
        material_name = render_object.data.materials[polygon.material_index].name
        material_polygon_counts[material_name] += 1
    _require(
        all(count > 0 for count in material_polygon_counts.values()),
        "Joined render contains an empty canonical material slot",
    )

    return {
        "collection": collection,
        "objects": [render_object, collision_object, socket_object],
        "render": render_object,
        "collision": collision_object,
        "socket": socket_object,
        "material_polygon_counts": material_polygon_counts,
        "mesh_names": mesh_names,
        "material_names": material_names,
    }


def _cleanup_temporary_export(temporary: dict[str, Any] | None) -> None:
    if temporary is None:
        return
    collection = temporary.get("collection")
    if collection is not None:
        for obj in list(collection.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(collection)
    for mesh_name in temporary.get("mesh_names", set()):
        mesh = bpy.data.meshes.get(mesh_name)
        if mesh is not None and mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for material_name in temporary.get("material_names", set()):
        material = bpy.data.materials.get(material_name)
        if material is not None and material.users == 0:
            bpy.data.materials.remove(material)


def _export_settings() -> tuple[dict[str, Any], dict[str, Any]]:
    report = {
        "selectionOnly": True,
        "objectTypes": ["MESH", "EMPTY"],
        "globalScale": 1.0,
        "applyUnitScale": True,
        "applyScaleOptions": "FBX_SCALE_NONE",
        "axisForward": "-Y",
        "axisUp": "Z",
        "applyModifiers": True,
        "triangles": True,
        "animation": False,
        "textures": False,
        "customProperties": False,
    }
    operator = {
        "check_existing": False,
        "use_selection": True,
        "use_visible": False,
        "use_active_collection": False,
        "global_scale": 1.0,
        "apply_unit_scale": True,
        "apply_scale_options": "FBX_SCALE_NONE",
        "use_space_transform": True,
        "bake_space_transform": False,
        "axis_forward": "-Y",
        "axis_up": "Z",
        "object_types": {"MESH", "EMPTY"},
        "use_mesh_modifiers": True,
        "use_mesh_modifiers_render": True,
        "mesh_smooth_type": "FACE",
        "use_subsurf": False,
        "use_mesh_edges": False,
        "use_tspace": False,
        "use_triangles": True,
        "use_custom_props": False,
        "add_leaf_bones": False,
        "bake_anim": False,
        "bake_anim_use_all_bones": False,
        "bake_anim_use_nla_strips": False,
        "bake_anim_use_all_actions": False,
        "bake_anim_force_startend_keying": False,
        "path_mode": "STRIP",
        "embed_textures": False,
        "batch_mode": "OFF",
        "use_metadata": False,
    }
    return report, operator


def _restore_selection(selected_names: Iterable[str], active_name: str | None) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    for name in selected_names:
        obj = bpy.context.scene.objects.get(name)
        if obj is not None:
            obj.select_set(True)
    bpy.context.view_layer.objects.active = (
        bpy.context.scene.objects.get(active_name) if active_name else None
    )


def _write_temporary_json(path: Path, value: dict[str, Any]) -> Path:
    encoded = (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode("utf-8")
    with tempfile.NamedTemporaryFile(
        mode="wb",
        prefix=f".{path.stem}.",
        suffix=".json.tmp",
        dir=path.parent,
        delete=False,
    ) as stream:
        temporary_path = Path(stream.name)
        stream.write(encoded)
        stream.flush()
        os.fsync(stream.fileno())
    return temporary_path


def _publish_outputs(
    replacements: Sequence[tuple[Path, Path]],
    validate_inputs: Callable[[], None],
) -> None:
    """Publish an FBX/report pair and restore the old pair on any normal failure."""
    backups: dict[Path, Path] = {}
    replaced: list[Path] = []
    backups_to_preserve: set[Path] = set()
    try:
        validate_inputs()
        for _, destination in replacements:
            if destination.exists():
                with tempfile.NamedTemporaryFile(
                    prefix=f".{destination.stem}.",
                    suffix=".backup",
                    dir=destination.parent,
                    delete=False,
                ) as backup_stream:
                    backup = Path(backup_stream.name)
                shutil.copy2(destination, backup)
                backups[destination] = backup

        for temporary_path, destination in replacements:
            os.replace(temporary_path, destination)
            replaced.append(destination)
        validate_inputs()
    except Exception as exc:
        rollback_errors: list[str] = []
        for destination in reversed(replaced):
            backup = backups.get(destination)
            try:
                if backup is not None and backup.exists():
                    os.replace(backup, destination)
                elif destination.exists():
                    destination.unlink()
            except OSError as rollback_error:
                if backup is not None and backup.exists():
                    backups_to_preserve.add(backup)
                    recovery_note = f"; previous output preserved at {backup}"
                else:
                    recovery_note = "; no previous-output backup is available"
                rollback_errors.append(
                    f"{destination}: {rollback_error}{recovery_note}"
                )
        if rollback_errors:
            raise PipelineError(
                "Output publication failed and rollback was incomplete: "
                + "; ".join(rollback_errors)
            ) from exc
        raise
    finally:
        for temporary_path, _ in replacements:
            if temporary_path.exists():
                temporary_path.unlink()
        for backup in backups.values():
            if backup.exists() and backup not in backups_to_preserve:
                backup.unlink()


def _export(
    contract: dict[str, Any],
    inspection: dict[str, Any],
    source_sha256: str,
    manifest_sha256: str,
    exporter_sha256: str,
) -> dict[str, Any]:
    output_path: Path = contract["fbx_path"]
    report_path: Path = contract["report_path"]
    output_path.parent.mkdir(parents=True, exist_ok=True)

    selected_names = [obj.name for obj in bpy.context.selected_objects]
    active_name = bpy.context.view_layer.objects.active.name if bpy.context.view_layer.objects.active else None
    temporary: dict[str, Any] | None = None
    temporary_fbx: Path | None = None
    temporary_report: Path | None = None
    export_settings_report, export_settings_operator = _export_settings()

    def validate_inputs() -> None:
        inputs = (
            (contract["source_blend"], source_sha256, "Source .blend"),
            (contract["manifest_path"], manifest_sha256, "Manifest"),
            (Path(__file__).resolve(), exporter_sha256, "Exporter script"),
        )
        for path, expected_hash, label in inputs:
            _require(
                path.is_file() and _sha256(path) == expected_hash,
                f"{label} changed during export; existing outputs were preserved",
            )

    try:
        temporary = _build_temporary_export(contract, inspection)
        bpy.ops.object.select_all(action="DESELECT")
        for obj in temporary["objects"]:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = temporary["render"]

        with tempfile.NamedTemporaryFile(
            prefix=f".{output_path.stem}.",
            suffix=".fbx",
            dir=output_path.parent,
            delete=False,
        ) as stream:
            temporary_fbx = Path(stream.name)
        result = bpy.ops.export_scene.fbx(
            filepath=str(temporary_fbx),
            **export_settings_operator,
        )
        _require(result == {"FINISHED"}, f"FBX exporter did not finish: {result}")
        _require(temporary_fbx.is_file() and temporary_fbx.stat().st_size > 0, "FBX exporter wrote no data")
        fbx_sha256 = _sha256(temporary_fbx)

        render_mesh = temporary["render"].data
        collision_mesh = temporary["collision"].data
        triangle_count = len(render_mesh.polygons)
        collision_triangle_count = len(collision_mesh.polygons)
        report = {
            "schemaVersion": REPORT_SCHEMA_VERSION,
            "assetId": contract["asset_id"],
            "generatedAtUtc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
            "blender": {
                "version": bpy.app.version_string,
                "unitSystem": bpy.context.scene.unit_settings.system,
                "unitScale": bpy.context.scene.unit_settings.scale_length,
            },
            "paths": {
                "manifest": _repo_relative(contract["manifest_path"], contract["repository_root"]),
                "sourceBlend": _repo_relative(contract["source_blend"], contract["repository_root"]),
                "fbx": _repo_relative(output_path, contract["repository_root"]),
                "report": _repo_relative(report_path, contract["repository_root"]),
            },
            "objects": {
                "root": contract["root_name"],
                "render": {
                    "name": contract["render_name"],
                    "sourceObjects": [source.name for source in inspection["geometry_sources"]],
                    "vertices": len(render_mesh.vertices),
                    "triangles": triangle_count,
                    "materialPolygonCounts": temporary["material_polygon_counts"],
                },
                "collision": {
                    "source": contract["collision_source"],
                    "name": contract["collision_export"],
                    "vertices": len(collision_mesh.vertices),
                    "triangles": collision_triangle_count,
                    "closedManifold": inspection["collision"]["closedManifold"],
                    "convex": inspection["collision"]["convex"],
                    "volumeCubicMeters": inspection["collision"]["volumeCubicMeters"],
                },
                "socket": {
                    "source": contract["socket_source"],
                    "name": contract["socket_export"],
                },
            },
            "materials": inspection["materials"],
            "bounds": {
                "minimumCm": inspection["bounds_min_cm"],
                "maximumCm": inspection["bounds_max_cm"],
                "dimensionsCm": inspection["dimensions_cm"],
                "expectedDimensionsCm": contract["expected_dimensions_cm"],
                "toleranceCm": contract["tolerance_cm"],
            },
            "exportSettings": export_settings_report,
            "hashes": {
                "manifestSha256": manifest_sha256,
                "sourceBlendSha256": source_sha256,
                "fbxSha256": fbx_sha256,
                "exporterSha256": exporter_sha256,
            },
        }
        temporary_report = _write_temporary_json(report_path, report)
        _publish_outputs(
            (
                (temporary_fbx, output_path),
                (temporary_report, report_path),
            ),
            validate_inputs,
        )
        temporary_fbx = None
        temporary_report = None
        return report
    finally:
        if temporary_fbx is not None and temporary_fbx.exists():
            temporary_fbx.unlink()
        if temporary_report is not None and temporary_report.exists():
            temporary_report.unlink()
        _cleanup_temporary_export(temporary)
        _restore_selection(selected_names, active_name)


def _validation_result(contract: dict[str, Any], inspection: dict[str, Any]) -> dict[str, Any]:
    return {
        "status": "validated",
        "assetId": contract["asset_id"],
        "sourceBlend": _repo_relative(contract["source_blend"], contract["repository_root"]),
        "root": contract["root_name"],
        "geometryObjects": inspection["geometry_count"],
        "sourceTypes": inspection["source_types"],
        "sourceVertices": inspection["source_vertices"],
        "sourcePolygons": inspection["source_polygons"],
        "materials": inspection["materials"],
        "dimensionsCm": inspection["dimensions_cm"],
        "expectedDimensionsCm": contract["expected_dimensions_cm"],
        "toleranceCm": contract["tolerance_cm"],
    }


def main() -> int:
    args = _parse_args()
    manifest_path = Path(args.manifest).resolve()
    manifest_sha256 = _sha256(manifest_path)
    contract = _load_contract(manifest_path)
    _require(
        _sha256(manifest_path) == manifest_sha256,
        "Manifest changed while it was being parsed",
    )
    _validate_environment(contract)

    source_path: Path = contract["source_blend"]
    source_sha256 = _sha256(source_path)
    exporter_path = Path(__file__).resolve()
    exporter_sha256 = _sha256(exporter_path)
    inspection = _validate_source(contract)

    if args.validate_only:
        result = _validation_result(contract, inspection)
    else:
        result = _export(
            contract,
            inspection,
            source_sha256,
            manifest_sha256,
            exporter_sha256,
        )
        result = {
            "status": "exported",
            "assetId": contract["asset_id"],
            "fbx": result["paths"]["fbx"],
            "report": result["paths"]["report"],
            "fbxSha256": result["hashes"]["fbxSha256"],
        }

    _require(
        _sha256(source_path) == source_sha256
        and _sha256(manifest_path) == manifest_sha256
        and _sha256(exporter_path) == exporter_sha256,
        "Pipeline input changed during validation or export",
    )
    print("SP_ASSET_PIPELINE_RESULT=" + json.dumps(result, sort_keys=True, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except PipelineError as exc:
        print(f"SP_ASSET_PIPELINE_ERROR={exc}", file=sys.stderr)
        raise SystemExit(2) from exc
