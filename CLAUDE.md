# ScrollPeddler

1인칭 1~4인 협동(리슨 서버) 공방·던전 게임. UE 5.8 C++, 1인 개발.

## 저장소 구조
- 루트: 기획 문서. 현재 기준은 `설계-정리본-v2.2.md`. 상세는 `노션-원고/`, `claude-lead/` (작업 절차는 `claude-lead/T9-스파이크1-착수절차.md`, 기술 구조는 `T8-기술설계.md`).
- `ScrollPeddler/`: 언리얼 프로젝트 (`ScrollPeddler.uproject`, 실행 모듈 `ScrollPeddler` 하나).
- 이 저장소는 **공개(public)** 상태다. 구매 에셋(사운드·모델)이나 라이선스상 재배포가 안 되는 파일은 넣지 않는다.

## 빌드
- 엔진: `C:\Program Files\Epic Games\UE_5.8`
- 에디터를 닫고 빌드한다. 헤더·UCLASS·생성자·Component 구성을 바꿨으면 Live Coding에 의존하지 않는다.
```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ScrollPeddlerEditor Win64 Development -Project="C:\Geniesoft\CBS\ScrollPeddler\ScrollPeddler\ScrollPeddler.uproject" -WaitMutex
```

- 에디터가 열려 있으면 에디터 타깃 대신 게임 타깃(`ScrollPeddler Win64 Development`)으로 컴파일만 검증한다. 에디터 DLL은 사용자가 에디터를 닫은 뒤 다시 빌드한다.

## 코드 규칙
- 새 코드는 `Source/ScrollPeddler/Public/<기능>/`와 `Private/<기능>/`에 둔다. 기능 폴더: Framework, Interaction, Network, Inventory, Runes, Definitions, Effects, Events, World, Presentation, Tests.
- 모듈 루트(`Source/ScrollPeddler/`)는 Private 하위 파일의 include 경로에 없다. 공용 헤더는 `Public/`에 둔다. 로그는 `#include "ScrollLog.h"`의 `LogScroll`.
- 클래스 이름은 `Scroll` 접두사 (`AScrollCharacter`, `UScrollFootstepComponent` 등). 예외: T8의 `AScrollGameMode` 역할은 생성된 `AScrollPeddlerGameModeBase`가 맡는다 (T9 §2.2, 이름 맞추기용 개명 안 함).
- 입력은 Enhanced Input. 레거시 입력 배율은 껐다(`bEnableLegacyInputScales=False`). `AddControllerPitchInput(+)`가 위를 본다. 입력 에셋이 생기기 전에는 `AScrollPlayerController`가 런타임 기본 입력(WASD·마우스·Space)을 만든다.
- 발소리·카메라 흔들림 같은 표현 기능은 `Presentation/`에 두고 룬·네트워크 로직과 섞지 않는다.
- 콘텐츠 경로: `/Game/ScrollPeddler/{Maps,Blueprints,Data,UI,FX,Audio}`. 시험 맵은 `/Game/ScrollPeddler/Maps/L_Spike01`.

## 협동 플레이 규칙
- 상태는 서버가 기준이다. 요청은 소유한 Controller/ActionComponent로만 보낸다.
- 화면 흔들림·2D 사운드·위젯처럼 한 사람에게만 보여야 하는 것은 `IsLocallyControlled()`로 막는다. `GetPlayerController(0)`/`GetPlayerCharacter(0)`에 기대지 않는다.
- 같은 사건의 효과음·카메라 표현을 중복 재생하지 않는다. 매 프레임 Reliable RPC를 보내지 않는다.

## 아트·접근성 제약
- 툰 2단 음영. 후처리는 룬 빛에만 블룸, 지역별 색 보정 1개, 필름 잡티 없음.
- 흔들림·섬광은 설정에서 줄이기/끄기, 시야각은 사용자 설정. 연출은 이 값에 비례하게 만든다.

## 물리 표면 (DefaultEngine.ini)
SurfaceType1 Wood, SurfaceType2 Stone, SurfaceType3 Water, SurfaceType4 Paper. 새 표면은 여기와 ini에 함께 추가한다.

## Git
- 커밋은 사용자가 직접 한다. 변경 후에는 커밋 메시지만 추천한다 (형식: `[Chore] ...`, `[Feat] ...`, `[Docs] ...`).
- `.uasset`/`.umap`은 Git LFS (`.gitattributes`). 처음 에셋을 추가하기 전에 `git lfs install`이 되어 있어야 한다.
