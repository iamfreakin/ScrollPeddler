"""Create and validate the KayKit Ranger locomotion Blend Spaces.

Run this script from a full Unreal Editor session, for example with::

    UnrealEditor.exe <repository>/ScrollPeddler.uproject ^
      -ExecutePythonScript="<repository>/Scripts/AssetPipeline/unreal/configure_kaykit_blend_spaces.py"

It can also be run from the Output Log of an existing editor with::

    py "<repository>/Scripts/AssetPipeline/unreal/configure_kaykit_blend_spaces.py"

The script deliberately opens each Blend Space editor before changing its
samples. In UE 5.8 ``sample_data`` is exposed to Python, while
``UBlendSpace::ResampleData`` is not. ``SBlendSpaceEditor`` resamples whenever
one of the asset's reflected properties changes, which keeps the serialized
runtime triangulation/segments in sync with the reviewable Python definition.
"""

from __future__ import annotations

import math
from typing import Any, NamedTuple

import unreal


LOG_PREFIX = "SP_KAYKIT_BLENDSPACE"
ANIMATION_ROOT = (
    "/Game/Art/ThirdParty/KayKit/Adventurers/Ranger/Animations"
)
SKELETON_PATH = (
    "/Game/Art/ThirdParty/KayKit/Adventurers/Ranger/"
    "SKEL_KayKit_Rig_Medium"
)
SKELETAL_MESH_PATH = (
    "/Game/Art/ThirdParty/KayKit/Adventurers/Ranger/"
    "SK_KayKit_Ranger"
)
LOCOMOTION_PATH = f"{ANIMATION_ROOT}/BS_KayKit_Locomotion"
CROUCH_PATH = f"{ANIMATION_ROOT}/BS_KayKit_Crouch"
WEIGHT_INTERPOLATION_SPEED = 6.0
FLOAT_TOLERANCE = 1.0e-4


class SampleSpec(NamedTuple):
    animation_name: str
    forward_speed: float
    right_speed: float
    rate_scale: float = 1.0


LOCOMOTION_SAMPLES = (
    SampleSpec("Idle_A", 0.0, 0.0),
    SampleSpec("Walking_A", 450.0, 0.0),
    SampleSpec("Running_A", 650.0, 0.0),
    SampleSpec("Walking_Backwards", -450.0, 0.0),
    SampleSpec("Walking_Backwards", -650.0, 0.0, 650.0 / 450.0),
    SampleSpec("Running_Strafe_Left", 0.0, -450.0, 450.0 / 650.0),
    SampleSpec("Running_Strafe_Left", 0.0, -650.0),
    SampleSpec("Running_Strafe_Right", 0.0, 450.0, 450.0 / 650.0),
    SampleSpec("Running_Strafe_Right", 0.0, 650.0),
)
CROUCH_SAMPLES = (
    SampleSpec("Crouching", 0.0, 0.0),
    SampleSpec("Sneaking", 220.0, 0.0),
)


def _fail(message: str) -> None:
    raise RuntimeError(f"[{LOG_PREFIX}] {message}")


def _asset_path(asset: unreal.Object) -> str:
    object_path = unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(asset)
    leaf = object_path.rsplit("/", 1)[-1]
    return object_path.rsplit(".", 1)[0] if "." in leaf else object_path


def _load(path: str, expected_type: type) -> Any:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(asset, expected_type):
        _fail(
            f"Expected {expected_type.__name__} at {path}, "
            f"got {type(asset).__name__}"
        )
    return asset


def _set(instance: Any, property_name: str, value: Any) -> None:
    try:
        instance.set_editor_property(property_name, value)
    except Exception as exc:
        _fail(
            f"{type(instance).__name__} does not accept "
            f"{property_name}={value!r}: {exc}"
        )


def _animation_path(animation_name: str) -> str:
    return f"{ANIMATION_ROOT}/A_KayKit_{animation_name}"


def _axis(
    display_name: str,
    minimum: float,
    maximum: float,
    grid_num: int,
) -> unreal.BlendParameter:
    parameter = unreal.BlendParameter()
    _set(parameter, "display_name", display_name)
    _set(parameter, "min", minimum)
    _set(parameter, "max", maximum)
    _set(parameter, "grid_num", grid_num)
    _set(parameter, "snap_to_grid", False)
    _set(parameter, "wrap_input", False)
    return parameter


def _unused_axis() -> unreal.BlendParameter:
    return _axis("None", 0.0, 100.0, 4)


def _sample(
    sequence: unreal.AnimSequence,
    spec: SampleSpec,
) -> unreal.BlendSample:
    sample = unreal.BlendSample()
    _set(sample, "animation", sequence)
    _set(
        sample,
        "sample_value",
        unreal.Vector(spec.forward_speed, spec.right_speed, 0.0),
    )
    _set(sample, "rate_scale", spec.rate_scale)
    _set(sample, "mirror", False)
    _set(sample, "use_single_frame_for_blending", False)
    _set(sample, "frame_index_to_sample", 0)
    _set(sample, "include_in_analyse_all", True)
    return sample


def _load_sequences(
    specs: tuple[SampleSpec, ...],
    skeleton: unreal.Skeleton,
) -> dict[str, unreal.AnimSequence]:
    result: dict[str, unreal.AnimSequence] = {}
    for spec in specs:
        if spec.animation_name in result:
            continue
        path = _animation_path(spec.animation_name)
        sequence = _load(path, unreal.AnimSequence)
        if sequence.get_editor_property("skeleton") != skeleton:
            _fail(f"Animation uses a different skeleton: {path}")
        result[spec.animation_name] = sequence
    return result


def _create_or_load(
    path: str,
    asset_type: type,
    factory_type: type,
    skeleton: unreal.Skeleton,
    skeletal_mesh: unreal.SkeletalMesh,
) -> Any:
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = _load(path, asset_type)
    else:
        factory = factory_type()
        _set(factory, "target_skeleton", skeleton)
        _set(factory, "preview_skeletal_mesh", skeletal_mesh)
        asset_name = path.rsplit("/", 1)[-1]
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            ANIMATION_ROOT,
            asset_type,
            factory,
        )
        if not isinstance(asset, asset_type):
            _fail(f"Could not create {asset_type.__name__} at {path}")
        unreal.log(f"[{LOG_PREFIX}] Created {path}")

    if asset.get_editor_property("skeleton") != skeleton:
        _fail(f"Blend Space uses a different skeleton: {path}")
    asset.set_preview_skeletal_mesh(skeletal_mesh)
    return asset


def _open_blend_space_editor(
    editor_subsystem: unreal.AssetEditorSubsystem,
    asset: unreal.BlendSpace,
) -> None:
    # UBlendSpace::ResampleData is not reflected into Python in UE 5.8. The
    # editor widget invokes it on construction and on every property change.
    if not editor_subsystem.open_editor_for_assets([asset]):
        _fail(
            "Could not open the Blend Space editor. Run this script from a "
            "full Unreal Editor session, not UnrealEditor-Cmd."
        )


def _configure_common(
    asset: unreal.BlendSpace,
    parameters: list[unreal.BlendParameter],
    samples: list[unreal.BlendSample],
) -> None:
    _set(asset, "blend_parameters", parameters)
    _set(asset, "interpolate_using_grid", False)
    _set(asset, "target_weight_interpolation_speed_per_sec",
         WEIGHT_INTERPOLATION_SPEED)
    _set(asset, "target_weight_interpolation_ease_in_out", True)
    _set(asset, "loop", True)
    _set(asset, "allow_marker_based_sync", True)
    _set(asset, "should_match_sync_phases", False)
    _set(asset, "allow_mesh_space_blending", False)
    _set(asset, "sample_data", samples)


def _configure_locomotion(
    asset: unreal.BlendSpace,
    sequences: dict[str, unreal.AnimSequence],
) -> None:
    parameters = [
        _axis("ForwardSpeed", -650.0, 650.0, 8),
        _axis("RightSpeed", -650.0, 650.0, 8),
        _unused_axis(),
    ]
    samples = [_sample(sequences[spec.animation_name], spec)
               for spec in LOCOMOTION_SAMPLES]
    _configure_common(asset, parameters, samples)


def _configure_crouch(
    asset: unreal.BlendSpace1D,
    sequences: dict[str, unreal.AnimSequence],
) -> None:
    parameters = [
        _axis("Speed", 0.0, 220.0, 4),
        _unused_axis(),
        _unused_axis(),
    ]
    samples = [_sample(sequences[spec.animation_name], spec)
               for spec in CROUCH_SAMPLES]
    _configure_common(asset, parameters, samples)
    _set(asset, "scale_animation", False)


def _float_equal(left: float, right: float) -> bool:
    return math.isclose(
        float(left),
        float(right),
        rel_tol=FLOAT_TOLERANCE,
        abs_tol=FLOAT_TOLERANCE,
    )


def _validate_axis(
    actual: unreal.BlendParameter,
    expected: unreal.BlendParameter,
    asset_path: str,
    axis_index: int,
) -> None:
    for property_name in ("display_name", "grid_num", "snap_to_grid", "wrap_input"):
        if actual.get_editor_property(property_name) != expected.get_editor_property(
            property_name
        ):
            _fail(
                f"{asset_path} axis {axis_index} has unexpected "
                f"{property_name}"
            )
    for property_name in ("min", "max"):
        if not _float_equal(
            actual.get_editor_property(property_name),
            expected.get_editor_property(property_name),
        ):
            _fail(
                f"{asset_path} axis {axis_index} has unexpected "
                f"{property_name}"
            )


def _validate_samples(
    asset: unreal.BlendSpace,
    expected_specs: tuple[SampleSpec, ...],
) -> None:
    path = _asset_path(asset)
    actual_samples = list(asset.get_editor_property("sample_data"))
    if len(actual_samples) != len(expected_specs):
        _fail(
            f"{path} expected {len(expected_specs)} samples, "
            f"got {len(actual_samples)}"
        )

    for index, (actual, expected) in enumerate(
        zip(actual_samples, expected_specs)
    ):
        animation = actual.get_editor_property("animation")
        if _asset_path(animation) != _animation_path(expected.animation_name):
            _fail(f"{path} sample {index} has an unexpected animation")
        value = actual.get_editor_property("sample_value")
        if not (
            _float_equal(value.x, expected.forward_speed)
            and _float_equal(value.y, expected.right_speed)
            and _float_equal(value.z, 0.0)
        ):
            _fail(f"{path} sample {index} has an unexpected position")
        if not _float_equal(
            actual.get_editor_property("rate_scale"),
            expected.rate_scale,
        ):
            _fail(f"{path} sample {index} has an unexpected rate scale")
        if actual.get_editor_property("mirror"):
            _fail(f"{path} sample {index} unexpectedly enables mirroring")
        if actual.get_editor_property("use_single_frame_for_blending"):
            _fail(f"{path} sample {index} unexpectedly uses one frame")


def _validate_common(
    asset: unreal.BlendSpace,
    expected_parameters: list[unreal.BlendParameter],
    expected_samples: tuple[SampleSpec, ...],
    skeleton: unreal.Skeleton,
) -> None:
    path = _asset_path(asset)
    if asset.get_editor_property("skeleton") != skeleton:
        _fail(f"{path} uses an unexpected skeleton")

    actual_parameters = list(asset.get_editor_property("blend_parameters"))
    if len(actual_parameters) != len(expected_parameters):
        _fail(f"{path} has an unexpected axis count")
    for index, (actual, expected) in enumerate(
        zip(actual_parameters, expected_parameters)
    ):
        _validate_axis(actual, expected, path, index)

    if asset.get_editor_property("interpolate_using_grid"):
        _fail(f"{path} unexpectedly uses legacy grid interpolation")
    if not _float_equal(
        asset.get_editor_property(
            "target_weight_interpolation_speed_per_sec"
        ),
        WEIGHT_INTERPOLATION_SPEED,
    ):
        _fail(f"{path} has an unexpected weight interpolation speed")
    if not asset.get_editor_property(
        "target_weight_interpolation_ease_in_out"
    ):
        _fail(f"{path} does not use eased sample-weight smoothing")
    if not asset.get_editor_property("loop"):
        _fail(f"{path} is not configured to loop")

    _validate_samples(asset, expected_samples)


def _save(asset: unreal.BlendSpace) -> None:
    path = _asset_path(asset)
    if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
        _fail(f"Could not save {path}")


def _close_blend_space_editor(
    editor_subsystem: unreal.AssetEditorSubsystem,
    asset: unreal.BlendSpace,
) -> None:
    # Both Blend Spaces can share one Animation Editor toolkit. Closing either
    # asset may therefore close the other one too; ``False`` then simply means
    # that no editor remained for this asset.
    editor_subsystem.close_all_editors_for_asset(asset)


def main() -> None:
    skeleton = _load(SKELETON_PATH, unreal.Skeleton)
    skeletal_mesh = _load(SKELETAL_MESH_PATH, unreal.SkeletalMesh)
    locomotion_sequences = _load_sequences(LOCOMOTION_SAMPLES, skeleton)
    crouch_sequences = _load_sequences(CROUCH_SAMPLES, skeleton)

    locomotion = _create_or_load(
        LOCOMOTION_PATH,
        unreal.BlendSpace,
        unreal.BlendSpaceFactoryNew,
        skeleton,
        skeletal_mesh,
    )
    crouch = _create_or_load(
        CROUCH_PATH,
        unreal.BlendSpace1D,
        unreal.BlendSpaceFactory1D,
        skeleton,
        skeletal_mesh,
    )

    editor_subsystem = unreal.get_editor_subsystem(
        unreal.AssetEditorSubsystem
    )
    _open_blend_space_editor(editor_subsystem, locomotion)
    _open_blend_space_editor(editor_subsystem, crouch)

    _configure_locomotion(locomotion, locomotion_sequences)
    _configure_crouch(crouch, crouch_sequences)

    expected_locomotion_parameters = [
        _axis("ForwardSpeed", -650.0, 650.0, 8),
        _axis("RightSpeed", -650.0, 650.0, 8),
        _unused_axis(),
    ]
    expected_crouch_parameters = [
        _axis("Speed", 0.0, 220.0, 4),
        _unused_axis(),
        _unused_axis(),
    ]
    _validate_common(
        locomotion,
        expected_locomotion_parameters,
        LOCOMOTION_SAMPLES,
        skeleton,
    )
    _validate_common(
        crouch,
        expected_crouch_parameters,
        CROUCH_SAMPLES,
        skeleton,
    )
    if crouch.get_editor_property("scale_animation"):
        _fail(f"{CROUCH_PATH} unexpectedly scales playback from filtered input")

    _save(locomotion)
    _save(crouch)
    _close_blend_space_editor(editor_subsystem, crouch)
    _close_blend_space_editor(editor_subsystem, locomotion)
    unreal.log(
        f"[{LOG_PREFIX}] Configured and validated "
        f"{LOCOMOTION_PATH} ({len(LOCOMOTION_SAMPLES)} samples) and "
        f"{CROUCH_PATH} ({len(CROUCH_SAMPLES)} samples)"
    )


main()
