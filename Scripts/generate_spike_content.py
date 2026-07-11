"""Create the reproducible binary content used by the first technical spike."""

import unreal


SCROLL_PATH = "/Game/Data/Scrolls/DA_Scroll_VeilOfSilence"
AMPLIFIED_PATH = "/Game/Data/Engravings/DA_Engraving_Amplified"
STABLE_PATH = "/Game/Data/Engravings/DA_Engraving_Stable"
MAP_PATH = "/Game/Maps/TechSpike"


def get_or_create_data_asset(asset_path, asset_class):
    existing = unreal.EditorAssetLibrary.load_asset(asset_path)
    if existing:
        if existing.get_class() != asset_class.static_class():
            raise RuntimeError(
                f"Existing asset {asset_path} has class {existing.get_class().get_name()}, "
                f"expected {asset_class.get_name()}"
            )
        return existing

    package_path, asset_name = asset_path.rsplit("/", 1)
    unreal.EditorAssetLibrary.make_directory(package_path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    created = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, package_path, asset_class, factory
    )
    if not created:
        raise RuntimeError(f"Failed to create {asset_path}")
    return created


def save_asset(asset):
    asset_path = unreal.EditorAssetLibrary.get_path_name_for_loaded_asset(asset)
    if not unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {asset_path}")


def configure_assets():
    amplified = get_or_create_data_asset(
        AMPLIFIED_PATH, unreal.SPScrollEngravingDefinition
    )
    amplified.set_editor_property("stable_id", "engraving.amplified")
    amplified.set_editor_property("display_name", "증폭 각인 / Amplified")
    amplified.set_editor_property(
        "advantage", "효과 범위가 넓어져 떨어진 동료도 함께 숨길 수 있다."
    )
    amplified.set_editor_property(
        "cost", "마력 소음과 오작동 위험이 증가해 위협을 자극한다."
    )
    amplified.set_editor_property("duration_multiplier", 1.0)
    amplified.set_editor_property("radius_multiplier", 1.5)
    amplified.set_editor_property("misfire_chance_delta", 5.0)
    amplified.set_editor_property("added_noise", 15.0)

    stable = get_or_create_data_asset(
        STABLE_PATH, unreal.SPScrollEngravingDefinition
    )
    stable.set_editor_property("stable_id", "engraving.stable")
    stable.set_editor_property("display_name", "안정 각인 / Stable")
    stable.set_editor_property(
        "advantage", "오작동 편차를 낮춰 탈출 경로에서 믿고 사용할 수 있다."
    )
    stable.set_editor_property(
        "cost", "최대 범위와 납품 프리미엄이 줄어든다."
    )
    stable.set_editor_property("duration_multiplier", 1.1)
    stable.set_editor_property("radius_multiplier", 0.8)
    stable.set_editor_property("misfire_chance_delta", -5.0)
    stable.set_editor_property("added_noise", -5.0)

    scroll = get_or_create_data_asset(SCROLL_PATH, unreal.SPScrollDefinition)
    scroll.set_editor_property("stable_id", "scroll.veil_of_silence")
    scroll.set_editor_property("display_name", "고요의 장막 / Veil of Silence")
    scroll.set_editor_property(
        "description", "8초 동안 범위 내 행동 소음을 낮추는 도주용 스크롤."
    )
    scroll.set_editor_property("base_duration_seconds", 8.0)
    scroll.set_editor_property("base_radius", 500.0)
    scroll.set_editor_property("delivery_value", 100)
    scroll.set_editor_property("allowed_engravings", [amplified, stable])

    save_asset(amplified)
    save_asset(stable)
    save_asset(scroll)


def configure_map():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP_PATH):
            raise RuntimeError(f"Failed to load existing map {MAP_PATH}")
    else:
        unreal.EditorAssetLibrary.make_directory("/Game/Maps")
        if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level(
            MAP_PATH, False
        ):
            raise RuntimeError(f"Failed to create non-partitioned map {MAP_PATH}")

    if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True):
        raise RuntimeError("Failed to save dirty map/content packages")


def main():
    configure_assets()
    configure_map()
    unreal.log(
        "SP_SPIKE_CONTENT_GENERATED map=/Game/Maps/TechSpike "
        "scrolls=1 engravings=2"
    )


main()
