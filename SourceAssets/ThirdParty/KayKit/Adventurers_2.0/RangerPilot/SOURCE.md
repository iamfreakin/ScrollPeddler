# KayKit Ranger 파일럿 원본

이 폴더는 Scroll Peddler의 1인칭·원격 플레이어 표현을 검증하기 위한
KayKit Ranger 최소 반입본이다.

## 출처와 라이선스

- 제작자: Kay Lousberg
- 캐릭터: [KayKit Adventurers 2.0 Free](https://kaylousberg.itch.io/kaykit-adventurers)
- 추가 애니메이션: [KayKit Character Animations 1.1 Free](https://kaylousberg.itch.io/kaykit-character-animations)
- 라이선스: [Creative Commons Zero 1.0 Universal (CC0 1.0)](https://creativecommons.org/publicdomain/zero/1.0/)
- 권장 표기: `Kay Lousberg, www.kaylousberg.com`

두 배포본의 `License.txt`는 개인·교육·상업 프로젝트에서의 자유로운 사용을
명시한다. 크레딧은 의무가 아니지만 프로젝트 기록에는 출처를 유지한다.

## 반입 범위

| 파일 | 용도 |
|---|---|
| `Ranger.fbx` | 전신 Skeletal Mesh와 Rig_Medium Skeleton |
| `ranger_texture.png` | Ranger 1024×1024 아틀라스 |
| `Animations/Rig_Medium_General.fbx` | Idle, Interact, PickUp, Use_Item |
| `Animations/Rig_Medium_MovementBasic.fbx` | Walk, Run, Jump |
| `Animations/Rig_Medium_MovementAdvanced.fbx` | Crouching, Sneaking |

원본 5개의 합계는 6,842,688 bytes(6.526 MiB)다. GLB, OBJ, Unity용 FBX,
샘플 이미지, Rig_Large, 중복 애니메이션 파일은 반입하지 않았다.

Unreal 산출물은 `/Game/Art/ThirdParty/KayKit/Adventurers/Ranger` 아래에
생성하며, 재현 절차는
`Scripts/AssetPipeline/import_kaykit_ranger.ps1`에 고정한다.
