# Blender → Unreal 정적 메시 파이프라인

이 문서는 Scroll Peddler에서 Blender로 제작한 정적 프롭을 검토 가능한 원본, 재현 가능한 FBX 게시물, Unreal 에셋으로 전달하는 기준을 정의한다. Blender MCP는 제작 보조 수단이며 납품 파이프라인의 필수 의존성이 아니다.

## 핵심 원칙

1. 게임 규칙과 아트 표현을 분리한다. 상호작용 거리·LOS·claim 판정은 C++ 충돌 컴포넌트가 담당하고, 교체 가능한 메시 참조는 스크롤 Data Asset이 소유한다.
2. 논리적 프롭 하나를 원본 `.blend` 하나, 게시 `.fbx` 하나, Unreal `SM_` 에셋 하나로 관리한다.
3. 작업용 Curve와 Modifier는 `.blend`에 비파괴 상태로 남긴다. 게시 시에만 evaluated copy를 Mesh로 변환하고 Modifier 적용·결합·삼각화를 수행한다.
4. 수동 export/import 옵션에 의존하지 않는다. asset manifest와 저장소 스크립트를 단일 기준으로 사용한다.
5. DCC 머티리얼은 의미가 안정적인 슬롯 이름만 전달한다. 프로덕션 셰이더는 Unreal의 `M_` master와 `MI_` instance에서 제작한다.
6. `.blend`, `.fbx`, `.uasset`, `.umap`은 Git LFS로 관리하며 서로 의존하는 변경을 함께 제출한다.
7. 게시 직전 입력 hash를 다시 확인하고 FBX·검증 보고서를 한 쌍으로 교체한다. 정상적인 실패에서는 이전 게시 쌍을 복구한다.

Epic은 정적 메시의 원점 피벗, DCC 단계 삼각화, `UCX_` collision, 안정적인 material slot을 권장한다. UE 5.8의 Interchange FBX 지원은 아직 Experimental이므로 이 프로젝트는 당분간 기존 FBX import/reimport 경로를 고정한다.

- [Epic FBX Static Mesh Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-static-mesh-pipeline-in-unreal-engine)
- [Epic FBX Import Options](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-import-options-reference-in-unreal-engine)
- [Epic FBX Material Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-material-pipeline-in-unreal-engine)
- [Epic Asset Naming Conventions](https://dev.epicgames.com/documentation/en-us/unreal-engine/recommended-asset-naming-conventions-in-unreal-engine-projects)
- [Epic Interchange Import](https://dev.epicgames.com/documentation/unreal-engine/importing-assets-using-interchange-in-unreal-engine?lang=en-US)
- [Epic Material Instances](https://dev.epicgames.com/documentation/en-us/unreal-engine/instanced-materials-in-unreal-engine)
- [Blender FBX Export](https://docs.blender.org/manual/en/5.0/addons/import_export/scene_fbx.html)
- [Blender Apply Transform](https://docs.blender.org/manual/en/5.0/scene_layout/object/editing/apply.html)

## 저장소 구조

```text
SourceAssets/
├─ Props/<Category>/<Asset>/             프로덕션 작업 원본과 게시물
└─ TestAssets/Props/<Category>/<Asset>/  기술 검증용 원본과 게시물
   ├─ <Asset>.blend
   └─ Exports/
      ├─ SM_<Asset>.fbx                  Unreal 재반입 소스
      └─ SM_<Asset>.export.json          게시 검증 보고서

Scripts/AssetPipeline/
├─ assets/<asset>.json                  단위·이름·경로·검증 계약
├─ blender/export_static_mesh.py        Blender 검증 및 FBX 게시
├─ unreal/import_static_mesh.py         Unreal import/reimport 및 후검증
└─ build_asset.ps1                      전체 흐름 오케스트레이션

Content/Art/
├─ Props/<Category>/                     프로덕션 에셋
└─ TestAssets/Props/<Category>/<Asset>/  기술 검증용 에셋
   └─ SM_<Asset>.uasset
```

`Saved/`와 `/Game/Developers/`는 개인 실험에만 사용한다. 팀 공유, cook 또는 패키지 검증 대상은 `SourceAssets/`와 `/Game/Art/`에 둔다.

## 명명 규칙

```text
SM_<Asset>_<Descriptor>                  Static Mesh
UCX_SM_<Asset>_<Descriptor>_00           Convex collision
SOCKET_SM_<Asset>_<Descriptor>_00        Static Mesh socket helper
M_<Purpose>                              Master Material
MI_<Asset>_<Surface>                     Material Instance
T_<Asset>_<Descriptor>                   Texture
```

FBX 파일명, 렌더 메시 노드명과 Unreal Static Mesh 이름을 동일하게 유지한다. Material slot은 배열 번호가 아니라 `Paper`, `Binding`, `Seal`, `Rune`처럼 의미로 명명한다.

## Blender 제작 계약

- Blender 5.2.0 LTS를 사용한다.
- Scene Units는 `Metric`, Unit Scale은 `1.0`으로 유지한다. 1 Blender Unit을 1m로 모델링한다.
- 작업 원본의 Transform은 자유롭게 사용할 수 있지만 publisher는 지정 root의 로컬 공간을 게임 공간으로 정규화한다.
- 게시 메시의 피벗은 원점이며 Location/Rotation은 `0`, Scale은 `1`이어야 한다.
- 음수 스케일, 비정상 수치, 비퇴화하지 않은 geometry, 계약되지 않은 material은 게시를 실패시킨다.
- Curve, Solidify, Bevel 등은 원본에서 유지하고 publisher의 evaluated copy에서 Mesh로 변환한다.
- 카메라, 조명, 전시 플랫폼, 픽업 링 등 presentation object는 root 바깥에 두거나 `do_not_export=true`로 표시한다.
- 최종 삼각분할은 게시 단계에서 고정한다. baked normal map을 도입하면 tangent 정책도 manifest에 명시한다.

publisher는 `--factory-startup --disable-autoexec --background --python-use-system-env`로 실행해 사용자 애드온과 Blender MCP 세션으로부터 격리한다. 실행 전 `PYTHONHOME`·`PYTHONPATH`를 제거하고 user site-packages를 비활성화한 뒤 `PYTHONHASHSEED=0`으로 FBX UUID를 고정한다. FBX 헤더 시각은 1970-01-01, `ApplicationNativeFile`은 저장소 상대 경로로 정규화하며, 평가 메시의 부동소수점 흔들림은 UV 소수점 6자리 양자화로 제거한다. 고정 FBX 설정은 Selected Objects, Apply Unit, All Local scaling, `-Y Forward / Z Up`, Apply Modifiers, Triangulate, animation·texture embed·custom property 비활성이다. Blender FBX SDK 버전과 UE의 FBX 2020.2 importer 기준이 동일하지 않으므로 실제 Unreal import와 bounds 검증을 호환성 게이트로 사용한다.

## Unreal 반입 계약

- 기존 FBX importer를 사용하고 `Interchange.FeatureFlags.Import.FBX=0`을 프로젝트 설정에 명시한다.
- Static Mesh만 가져오며 import transform은 identity와 uniform scale `1.0`으로 고정한다.
- FBX 하나가 렌더 메시 하나를 만들도록 하고 material/texture 자동 생성을 끈다.
- `UCX_`가 있으므로 Auto Generate Collision을 끈다.
- 블록아웃에서는 Nanite를 끄고 LOD0만 사용한다. 최종 단계에서 측정 후 Small Prop LOD Group 또는 Nanite를 선택한다.
- Blender normal을 가져오고 tangent는 Unreal의 MikkTSpace로 재생성한다.
- 최초 반입 뒤에는 동일한 published FBX를 reimport해 source path와 material assignment를 보존한다.
- import 후 이름·클래스·bounds·정렬된 slot·저장된 import 옵션·source path를 다시 검증한다.
- collision은 `UCX_`에서 온 convex 1개만 허용하고 자동 생성 primitive가 섞이면 실패시킨다. UE 5.8이 공개하는 기본 tag socket 집합도 manifest의 `SOCKET_` helper에서 유도한 이름 정확히 1개만 허용한다.
- `.uasset`은 Unreal Editor 또는 Editor Python으로만 변경한다.

FBX material 변환은 제한적이므로 DCC material 자동 생성을 프로덕션 기본값으로 삼지 않는다. 공용 master material을 parameterize하고 프롭별 `MI_`를 만드는 것이 기본 방향이다.

## 게임플레이 연결

`USPScrollDefinition::PickupMesh`가 각 스크롤 계열의 월드 표현을 soft reference로 보유한다. `ASPScrollPickup`은 다음 두 컴포넌트를 분리한다.

```text
InteractionBounds    C++ 고정 Query collision, Visibility trace와 서버 판정
└─ PickupVisual      Data Asset에서 로드한 무충돌 Static Mesh 표현
```

서버와 클라이언트는 복제된 `FSPScrollInstance::BaseDefinitionId`를 기준으로 동일한 Primary Asset bundle을 로드한다. 아트 메시의 collision이나 크기가 바뀌어도 권위 판정 범위가 조용히 변하지 않아야 한다.

## 테스트 스크롤 픽업 블록아웃 계약

단일 기준은 `Scripts/AssetPipeline/assets/scroll_pickup_test_blockout.json`이다. 이 메시와 DCC 원본은 기술 검증용이며 프로덕션 스크롤 아트로 취급하지 않는다. 논리 스크롤 `/Game/Data/Scrolls/DA_Scroll_VeilOfSilence`와 Stable ID `scroll.veil_of_silence`는 테스트 메시의 이름·경로와 분리해 유지한다.

- 원본: `SourceAssets/TestAssets/Props/Scrolls/ScrollPickup/ScrollPickup_TestBlockout.blend`
- 게시물: `SourceAssets/TestAssets/Props/Scrolls/ScrollPickup/Exports/SM_ScrollPickup_TestBlockout.fbx`
- Unreal: `/Game/Art/TestAssets/Props/Scrolls/ScrollPickup/SM_ScrollPickup_TestBlockout`
- root: `SP_ScrollPickup_TestBlockout_ROOT`
- 중앙 피벗, +Z up, identity transform
- 예상 root-local 크기: 약 `48.8 × 16.5 × 17.9cm`
- 슬롯: `Paper`, `Binding`, `Seal`, `Rune`
- collision source/export: `SP_TestInteractionBounds_PREVIEW` / `UCX_SM_ScrollPickup_TestBlockout_00`
- socket source/export: `SP_TestPickupPivot` / `SOCKET_SM_ScrollPickup_TestBlockout_00`
- 외부 에셋 또는 라이선스 의존성: 없음

## 실행

전체 publish와 Unreal 반입은 저장소 루트에서 실행한다.

```powershell
& .\Scripts\AssetPipeline\build_asset.ps1 `
  -Manifest .\Scripts\AssetPipeline\assets\scroll_pickup_test_blockout.json
```

게시 결과를 변경하지 않고 검증하려면 다음을 사용한다.

```powershell
& .\Scripts\AssetPipeline\build_asset.ps1 `
  -Manifest .\Scripts\AssetPipeline\assets\scroll_pickup_test_blockout.json `
  -ValidateOnly
```

`-ValidateOnly`는 기존 FBX·export report·Static Mesh·Data Asset이 모두 있는 CI 검증 모드다. 원본·manifest·exporter hash, 저장된 legacy FBX import 옵션, collision, socket과 Data Asset 연결을 검사하며 네 산출물의 내용을 변경하지 않는다.

도구는 Blender MCP나 열린 GUI 세션에 의존하지 않는다. 실패 시 FBX 또는 Unreal 에셋을 성공으로 보고하지 않는다.

## 검증 게이트

1. headless Blender validation/export와 export report 확인
2. Unreal Editor Python import 또는 reimport 후 post-validation
3. Win64 Development Editor 빌드
4. `Automation RunTests ScrollPeddler`
5. `/Game/Maps/TechSpike` PIE에서 외형·피벗·collision 확인
6. `E` 이후 양쪽에서 pickup이 사라지고 `SP_TECH_SPIKE_PICKUP_COMMITTED`가 남는지 확인
7. 패키지 host/client 2프로세스 smoke
8. `git lfs status`, LFS pointer와 `git diff --check` 확인

블록아웃 단계의 통과는 최종 아트 승인을 의미하지 않는다. UV, texel density, 최종 material, LOD/Nanite, 성능 budget은 production 전환 시 별도 gate로 추가한다.
