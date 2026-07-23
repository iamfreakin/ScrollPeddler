# Scroll Peddler

친구들과 위험한 주문서 납품 일을 하며, 돈이 될 스크롤을 보존할지 찢어서 살아남을지 결정하는 1–4인 협동 코믹 공포 익스트랙션 게임입니다.

> 현재 저장소는 완성 게임이 아니라 **첫 Unreal C++ 기술 스파이크**입니다. 호스트 권위형 멀티플레이, 아이템 소유권, 스크롤 소비, 탈출, 정산, 로컬 저장과 Windows 패키징의 기술 경계를 검증합니다.

## 현재 상태

| 영역 | 구현됨 | 다음 단계 |
|---|---|---|
| 플레이어 | 이동, 시점 조작, 상호작용, 디버그 바디 | 최종 1인칭 카메라·손·월드 바디 분리 |
| 멀티플레이 | 로컬 2프로세스 Listen Server, raw IP 접속 | Steam 세션·친구 초대, 3–4인, 재접속 |
| 아이템 | 서버 권위 줍기, 거리·시야·용량·소유권 검증 | 버리기, 퀵슬롯, 적대적 네트워크 기능 테스트 |
| 스크롤 | 기본 계열 1종, 속성 각인 2종, 서버 소비 | 제작, 6계열×2각인 버티컬 슬라이스 |
| 스크롤 축 | 품질 D–S, 오염, 오작동을 각인과 별도 표현 | 밸런스·UI·VFX·SFX |
| 세션 결과 | 탈출, 플레이어별 결과, 멱등 정산 | 계약·부분 생환·호스트 종료 복구 |
| 저장 | 클라이언트별 로컬 `SaveGame`, 저장 후 재로드 검증 | 버전 마이그레이션·Steam Cloud 충돌 처리 |
| 월드 | 코드 기반 회색 상자 테스트 맵 | 룸 모듈·절차 생성·몬스터·위협 디렉터 |
| 빌드 | Development Editor/Game, 자동화, Win64 패키지 | CI, Steam 배포 브랜치 |

현재 카메라는 검증용 3인칭 Spring Arm 구성입니다. 최종 제품 방향은 1인칭이며 다음 구현 마일스톤에서 전환합니다.

## 기술 방향

- Unreal Engine 5.8 계열, Unreal C++
- Windows PC / Steam 우선
- 1–4인 Listen Server, 호스트 권위형 협동
- JetBrains Rider + Engine 설치형 RiderLink
- GitHub + Git LFS
- Primary Data Asset + Asset Manager 기반 콘텐츠
- MVP에서는 자체 백엔드, 전용 서버, Iris, Online Services를 사용하지 않음
- 현재 스파이크 접속은 Steam Lobby가 아닌 Unreal raw IP travel 사용

## 요구 사항

- Windows 10/11 x64
- Unreal Engine 5.8
- JetBrains Rider for Unreal Engine
- Visual Studio Build Tools의 UE 호환 MSVC, Windows SDK, .NET SDK
- Git 및 Git LFS

RiderLink는 로컬 Engine에 설치합니다. IDE 인덱스, 솔루션, `Binaries`, `Intermediate`, `Saved` 등 생성 파일은 저장소에 올리지 않습니다.

Blender MCP는 모델링 자동화 파일럿에 사용하는 선택적 개발 도구이며 게임 빌드에는 필요하지 않습니다. 고정 버전, 로컬 연결과 텔레메트리 차단 절차는 [Blender MCP 개발 환경](Docs/BLENDER_MCP_SETUP.md)을 참고하세요.

팀 공유 Blender 원본, 결정적 FBX 게시, Unreal 반입과 런타임 표현 연결 규칙은 [Blender → Unreal 정적 메시 파이프라인](Docs/ART_PIPELINE.md)을 따릅니다.

## 시작하기

```powershell
git lfs install
git clone https://github.com/iamfreakin/ScrollPeddler.git
Set-Location ScrollPeddler
git lfs pull
```

Rider에서 `ScrollPeddler.uproject`를 직접 열고 프로젝트 인덱싱이 끝난 뒤 `ScrollPeddlerEditor | Win64 | Development`를 빌드합니다.

명령줄에서 빌드하려면 UE 설치 경로를 환경에 맞게 조정합니다.

```powershell
$UE_ROOT = 'C:\Program Files\Epic Games\UE_5.8'
$Project = (Resolve-Path '.\ScrollPeddler.uproject').Path

& "$UE_ROOT\Engine\Build\BatchFiles\Build.bat" `
  ScrollPeddlerEditor Win64 Development `
  "-Project=$Project" -WaitMutex -NoHotReloadFromIDE -architecture=x64
```

## 기술 스파이크 실행

테스트 맵은 `/Game/Maps/TechSpike`입니다. 호스트와 클라이언트를 별도 프로세스로 실행한 다음 각 콘솔에서 입력합니다.

호스트:

```text
SPHost 2
```

두 번째 프로세스:

```text
SPJoin 127.0.0.1
```

조작:

- `W/A/S/D`: 이동
- 마우스: 시점
- `E`: 조준한 스크롤 줍기 요청
- `Q`: 인벤토리 첫 스크롤 사용 요청

1인칭 화면 중앙의 크로스헤어는 평소 흰색이며, 유효한 스크롤을 조준하면 청록색과 `[E] PICK UP` 안내로 바뀝니다. 요청 중에는 노란색, 서버 수락은 녹색, 거부는 붉은색 결과로 표시됩니다.

반대편 원통형 마커에 도달하면 서버 권위 탈출·정산·로컬 저장 흐름이 시작됩니다.

## 검증

자동화 테스트:

```powershell
$UE_ROOT = 'C:\Program Files\Epic Games\UE_5.8'
$Project = (Resolve-Path '.\ScrollPeddler.uproject').Path
$Report = (Join-Path (Get-Location) 'Saved\AutomationReports\TechSpike')

& "$UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  $Project -unattended -nop4 -nosplash -nullrhi `
  '-ExecCmds=Automation RunTests ScrollPeddler;Quit' `
  "-ReportExportPath=$Report"
```

2026-07-23 기준으로 다음을 검증했습니다.

- Blender 5.2.0 export부터 Unreal legacy FBX import 및 strict `ValidateOnly`까지 통과
- Scroll Peddler 자동화 테스트 8개 통과
- Development Editor 및 Development Game 빌드 통과
- Win64 패키징 통과
- 패키지 호스트·클라이언트 2프로세스 smoke 통과
- 양쪽 클라이언트 `SaveGame` 저장·재로드 검증
- 정산 ACK 2/2 이후 `SettlementCommitted` 확인

전체 패키징 및 무인 smoke 명령은 [기술 스파이크 문서](Docs/TECH_SPIKE.md)를 참고하세요.

## 프로젝트 구조

```text
Config/                 Unreal 프로젝트 설정
Content/                맵과 Data Asset — Git LFS 대상
Docs/                   기술 검증 및 운영 문서
Scripts/                재현 가능한 에디터 콘텐츠 생성 스크립트
SourceAssets/           Blender 원본과 게시 FBX — Git LFS 대상
Source/ScrollPeddler/    Runtime C++ 모듈
  Core/                 공통 타입과 결과 무결성
  Data/                 스크롤·각인 Primary Data Asset 정의
  Game/                 GameMode, GameState, PlayerState, Controller
  Online/               현재 로컬 프로필 GameInstance
  Persistence/          SaveGame
  Player/               캐릭터와 인벤토리
  Tests/                Unreal 자동화 테스트
  World/                픽업, 탈출, 회색 상자 액터
```

## 다음 구현 순서

1. 최종 1인칭 카메라·손·다른 플레이어 바디 구조
2. 상호작용의 로컬 피드백과 서버 확정/거부 분리
3. 거리·LOS·소유권 위조·동시 claim 멀티플레이 테스트
4. 소리에 반응하는 회색 상자 위협 1종
5. 납품할 스크롤을 보존할지 소비할지 검증하는 10세션 플레이테스트
6. Steam Listen Session과 친구 초대

상세 게임 기획은 [Scroll Peddler 기획서](https://www.notion.so/Scroll-Peddler-222428b239a08093aca9d4930d6137d2)를 참고하세요.

## Git 및 LFS

`.uasset`, `.umap`, 원본 아트·오디오 파일은 `.gitattributes`를 통해 Git LFS로 관리합니다. 커밋 전 다음을 확인합니다.

```powershell
git lfs status
git status --short
```

기능, 저장소 설정, 문서는 가능한 한 독립적으로 되돌릴 수 있는 논리적 커밋으로 분리합니다.

커밋 제목은 `feat: 서버 권위형 상호작용 추가`처럼 영문 Conventional Commit 타입과 한국어 설명을 사용합니다. 에이전트와 자동화 작업의 전체 규칙은 [AGENTS.md](AGENTS.md)를 따릅니다.

## 라이선스

현재 오픈소스 라이선스를 부여하지 않았습니다. 별도 라이선스가 추가되기 전까지 코드와 자산의 모든 권리는 프로젝트 소유자에게 있습니다.
