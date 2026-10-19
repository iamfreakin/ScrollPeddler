# ScrollPeddler

1인칭 1~4인 협동(리슨 서버) 공방·던전 게임. UE 5.8 C++, 1인 개발.

## 저장소 구조
- 루트: 기획 문서. 현재 규칙 기준은 `설계-정리본-v2.2.md`. 상세는 `노션-원고/`, `claude-lead/`. 단계·통과 기준은 `노션-원고/11-개발-계획.md`, 남은 결정은 `노션-원고/12-결정-기록-남은-일.md`, 스파이크1 절차는 `claude-lead/T9-스파이크1-착수절차.md`, 기술 구조는 `claude-lead/T8-기술설계.md`.
- `backlog/`: Backlog.md 태스크·마일스톤(M1 스파이크1 ~ M5 1.0 출시). 할 일과 진행 상태의 기준.
- `Tools/Setup-DevTools.ps1`: 새 PC에서 Node·Backlog.md 설치와 git 확인. `.mcp.json`은 Backlog MCP 서버 연결.
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
- 이동 상태(스프린트 등)는 `UScrollCharacterMovementComponent`의 saved move 압축 플래그(`FLAG_Custom_0`~)로 예측·재생한다. 서버 RPC나 클라이언트에서 `MaxWalkSpeed`를 직접 바꾸는 방식은 쓰지 않는다.
- 헤드밥은 `UScrollHeadBobComponent`(로컬 플레이어 전용) + 자체 쉐이크 패턴. 걷기·달리기 기준 속도는 이동 컴포넌트 값을 따르고, `SetIntensity(0~1)`가 접근성 흔들림 설정 연결 지점이다. 걷는 동안의 박자는 발소리 컴포넌트의 `GetStepCycles()`를 따른다.
- 발소리는 `UScrollFootstepComponent`. 모든 PC(전용 서버 제외)가 복제된 이동으로 각자 발걸음을 세고 3D로 재생한다(네트워크 메시지 없음). 표면은 바닥 Physical Material(`/Game/ScrollPeddler/Physics/PM_*`). 콘솔 `Scroll.Footsteps.Debug 1`로 표면·걷기/뛰기를 로그와 구로 확인한다. 시험 바닥은 `create_surface_test_area.py`.
- 서드파티·구매 에셋은 `/Game/ThirdParty/`(git 제외)에 둔다. 지금 발소리는 Bodycam_VHS_Effect 팩 카펫 소리를 로컬 임시음으로 쓰며, 구매 사운드가 오면 `BP_SpikeCharacter` → Footsteps의 표면별 세트로 교체한다.
- 콘텐츠 경로: `/Game/ScrollPeddler/{Maps,Blueprints,Data,UI,FX,Audio}`. 시험 맵은 `/Game/ScrollPeddler/Maps/L_Spike01`.
- `/Game/Characters`, `/Game/Input`, `/Game/FirstPerson`은 엔진 First Person 템플릿(`Templates/TP_FirstPersonBP`, `TemplateResources/High`)에서 복사한 것. Epic 콘텐츠라 git에서 제외했고, 새 PC에서는 `Tools/Unreal/copy_template_content.ps1`로 다시 복사한다. 마네킹 메시·애니메이션은 `BP_SpikeCharacter`가 지정하고, 기본 GameMode는 `BP_SpikeGameMode`.
- 에셋 생성은 에디터를 닫고 `Tools/Unreal/*.py`를 `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<파일>`로 실행한다 (`create_spike_map.py`, `create_spike_blueprints.py`).

## 협동 플레이 규칙
- 상태는 서버가 기준이다. 요청은 소유한 Controller/ActionComponent로만 보낸다.
- 화면 흔들림·2D 사운드·위젯처럼 한 사람에게만 보여야 하는 것은 `IsLocallyControlled()`로 막는다. `GetPlayerController(0)`/`GetPlayerCharacter(0)`에 기대지 않는다.
- 같은 사건의 효과음·카메라 표현을 중복 재생하지 않는다. 매 프레임 Reliable RPC를 보내지 않는다.

## 아트·접근성 제약
- 툰 2단 음영. 후처리는 룬 빛에만 블룸, 지역별 색 보정 1개, 필름 잡티 없음.
- 흔들림·섬광은 설정에서 줄이기/끄기, 시야각은 사용자 설정. 연출은 이 값에 비례하게 만든다.

## 물리 표면 (DefaultEngine.ini)
SurfaceType1 Wood, SurfaceType2 Stone, SurfaceType3 Water, SurfaceType4 Paper. 새 표면은 여기와 ini에 함께 추가한다.

## 작업 관리 (Backlog.md)
- 할 일·진행 상태는 `backlog/`가 기준이다. 기획 문서에 진행 상태를 따로 적지 않는다.
- 태스크는 Backlog MCP 도구나 `backlog task ...` CLI로 고친다. `backlog/tasks/*.md`를 직접 편집하지 않는다.
- 사용자가 태스크를 지정하지 않으면 선행 작업이 끝난 To Do 중 우선순위가 가장 높은 것을 제안하고, 시작할 때 In Progress로 옮긴다. 한 번에 하나만 진행한다.
- 완료 조건은 실제로 확인한 것만 체크한다. 증거(빌드 결과·로그·테스트 결과·사람이 본 것)는 태스크 노트에 남기고, "빌드됨 / 패키지 실행됨 / 사람이 확인함"을 구분한다. 확인하지 못한 조건은 체크하지 않고 이유를 적는다.
- `결정` 라벨 태스크의 값은 사용자가 정한다. Claude는 근거·후보·영향을 정리한다.
- `검증` 라벨의 판정 태스크(TASK-20·40·67·82·125)는 사람의 플레이 관찰 기록 없이 Done으로 옮기지 않는다.
- 작업 중 새로 필요한 일은 알맞은 마일스톤·라벨로 새 태스크를 만든다. 기존 태스크의 범위를 말없이 넓히지 않는다.

## superpowers 사용 규칙
superpowers 스킬의 기본 절차보다 이 저장소 규칙이 우선한다.
- 계획: 무엇을 만들지 애매하면 brainstorming, 여러 파일·단계에 걸치면 writing-plans. 계획 요약은 태스크의 Implementation Plan에 적고, 별도 계획 파일을 만들었으면 그 경로를 태스크에 남긴다.
- 커밋·브랜치·PR을 만들지 않는다. TDD·executing-plans·finishing-a-development-branch가 커밋을 요구하는 자리에서는 커밋 메시지만 추천한다.
- using-git-worktrees를 쓰지 않는다. UE 프로젝트·LFS·Intermediate 때문에 같은 체크아웃에서 작업한다.
- TDD는 수량·문장 계산·반응 순서·효과 수명·중복 요청처럼 UE 자동화 테스트(`Tests/`)로 확인할 수 있는 로직에만 쓴다. 물리 이동·패키지 접속·재미·연출은 T9 §4 체크리스트로 사람이 확인한다. 테스트보다 먼저 있던 코드를 TDD 때문에 지우지 않는다.
- 병렬 에이전트를 쓸 때 같은 `.uasset`·맵·공통 헤더를 동시에 고치지 않는다(T9 §7.1).

## Git
- 커밋은 사용자가 직접 한다. 변경 후에는 커밋 메시지만 추천한다 (형식: `[Chore] ...`, `[Feat] ...`, `[Docs] ...`).
- `.uasset`/`.umap`은 Git LFS (`.gitattributes`). 처음 에셋을 추가하기 전에 `git lfs install`이 되어 있어야 한다.
