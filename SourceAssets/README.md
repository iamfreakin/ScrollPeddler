# Source Assets

이 폴더는 Unreal 에셋을 재생성하거나 재반입할 수 있는 팀 공유 DCC 원본과 게시 파일을 보관한다.

- `.blend`는 Blender에서만 수정한다.
- `.fbx`는 `Scripts/AssetPipeline` publisher로 생성한다.
- 두 형식은 Git LFS 대상이다.
- 개인 실험본은 `Saved/`에 두며 이 폴더에 직접 복사해 게시하지 않는다.
- 에셋별 경로, bounds, material slot, collision 계약은 `Scripts/AssetPipeline/assets/*.json`에서 관리한다.
- `TestAssets/`는 기술 스파이크와 블록아웃처럼 프로덕션 납품 대상이 아닌 팀 공유 검증 에셋 전용이다.

현재 테스트 스크롤 계약은 `Scripts/AssetPipeline/assets/scroll_pickup_test_blockout.json`이며 다음 산출물만 사용한다.

- Blender 원본: `SourceAssets/TestAssets/Props/Scrolls/ScrollPickup/ScrollPickup_TestBlockout.blend`
- FBX 게시물: `SourceAssets/TestAssets/Props/Scrolls/ScrollPickup/Exports/SM_ScrollPickup_TestBlockout.fbx`
- Unreal 대상: `/Game/Art/TestAssets/Props/Scrolls/ScrollPickup/SM_ScrollPickup_TestBlockout`

전체 절차는 [Blender → Unreal 정적 메시 파이프라인](../Docs/ART_PIPELINE.md)을 따른다.
