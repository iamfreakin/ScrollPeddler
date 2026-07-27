# Scroll Peddler

친구들과 위험한 주문서 납품 일을 하며, 돈이 될 스크롤을 보존할지 찢어서 살아남을지 결정하는 1–4인 협동 코믹 공포 익스트랙션 게임입니다.

> 현재 저장소는 완성 게임이 아니라 **Unreal C++ 버티컬 슬라이스**입니다. 핵심 규칙과 네트워크 권위 경계를 검증하는 단계이며, 런타임 회색 상자와 텍스트 HUD는 최종 콘텐츠·UI가 아닙니다.

## 현재 상태

| 영역 | 현재 구현 | 남은 핵심 작업 |
|---|---|---|
| 플레이어 | 1인칭 카메라·owner-only 손 슬롯·원격 플레이어 바디 분리, 이동·질주·웅크리기·점프·스태미나·부상·자가 치료 | 손 메시가 아직 없어 1인칭에서 보이지 않음, 원격 바디는 큐브, 최종 애니메이션·행동 피드백 |
| 파티·런 | 1–4인 Listen Server, 준비 상태, 채팅·강퇴·투표, 25분 원정+2분 붕괴, 개별 탈출·실종, 120초 서버 메모리 재접속 스냅샷 | 프로덕션 파티/투표 UI, 안정적인 재접속 UX, 호스트 재시작·이전 |
| 인벤토리 | 손 1칸+가방 4칸, 재료 10개 스택, 대형 화물, 줍기·교환·버리기, 리비전·재전송·동시 claim 방어 | 드롭된 아이템의 정의 기반 외형, 드래그·분할·정렬·비교 UI, 대형 화물 UX |
| 스크롤 | 6계열 resolver와 품질 D–S·오염·각인·오작동의 독립 축, 서버 소비·월드 효과 적용 | 현재 플레이 가능한 Data Asset은 공명 계열 `Veil of Silence` 중심이며, 나머지 5계열 콘텐츠·연출·밸런스가 필요 |
| 위협·소음 | 소음을 조사·추적하는 Echo Hunter, 아이템을 훼손하는 Paper Eater, 위협 예산·공포 이벤트 판정 | NavMesh/문/군집 이동, 최종 AI 연출·VFX·SFX, 공포 이벤트 콘텐츠 |
| 던전 | 시드 기반 8방 연결 레이아웃·checksum과 런타임 회색 상자 바닥/복도 | 방별 벽·문·조명·스폰 테이블·목표 배치; 현재 런타임 geometry는 임시 |
| 계약·제작 | 3종 계약 판정, 탈출 증거 중복 방지, 원자적 제작·보관함 초과 정산함 우회 | 실제 계약/레시피 콘텐츠, 게시판·작업대 UI, 런타임 제작 흐름 |
| 장비 | Tool·Protection·Utility 슬롯, 내구도·파손·수리·리비전·멱등 판정의 권위 모델과 테스트 | 플레이어 컴포넌트·RPC·저장·HUD에 아직 연결되지 않은 **authority foundation** |
| 저장·정산 | 호스트 캠페인 3슬롯, 보관함 40칸, 무제한 정산함, 임시 파일 검증·메모리 rollback, `RunId` 멱등 정산 | 최종 교체 뒤 매체 오류 복구, 마이그레이션, Steam Cloud 충돌, 호스트 장애 복구 |
| 온라인 | Legacy OSS Steam 공개 로비 생성·검색·참가·Quick Play·초대 수락 경로, raw IP 폴백 | Steam 친구 초대와 음성은 실제 Steam 계정 간 패키지 라이브 검증이 아직 필요 |
| UI | 크로스헤어, 상호작용 결과, 런·스태미나·인벤토리·파티·효과를 보여 주는 텍스트 HUD | 프로덕션 메뉴·인벤토리·로비·설정·결과 화면 |

구현 범위와 아직 결정되지 않은 시스템별 쟁점은 [시스템 설계 현황](Docs/SYSTEM_DESIGN.md)에 정리합니다.

## 기술 방향

- Unreal Engine 5.8 계열, Unreal C++
- Windows PC / Steam 우선
- 1–4인 Listen Server, 호스트 권위형 협동
- JetBrains Rider + Engine 설치형 RiderLink
- GitHub + Git LFS
- Primary Data Asset + Asset Manager 기반 콘텐츠
- MVP에서는 자체 백엔드, 전용 서버, Iris, Online Services를 사용하지 않음
- Steam 연결은 Legacy Online Subsystem Steam을 사용하며 raw IP travel을 개발·회귀 테스트 폴백으로 유지

## 서버 권위 경계

- 클라이언트는 줍기, 버리기, 사용, 준비, 투표, 탈출 같은 **의도**만 전송합니다.
- Listen Server가 거리·line of sight·소유권·대상 상태·인벤토리 용량·예상 리비전·런 상태를 검사하고 결과를 확정합니다.
- 픽업·인벤토리·파티 등 멱등 원장을 쓰는 요청은 재전송 시 기존 결과를 반환하고, 같은 요청 ID에 다른 payload를 넣으면 충돌로 거부합니다. 스크롤은 정확한 인스턴스를 한 번만 소비해 재전송이 효과를 중복 적용하지 못합니다.
- 월드 아이템은 서버 전용 claim token으로 `Available → Claimed → Committed`를 거치며, 중간 실패 시 인벤토리와 원장을 되돌립니다.
- 스크롤 효과, 플레이어 조건, 위협 행동, 계약 보상, 캠페인 저장과 정산은 서버에서 판정합니다.
- 호스트 캠페인이 경제 상태의 원본이며, 클라이언트 로컬 결과 저장·ACK는 보조 영수증입니다.

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

## 버티컬 슬라이스 실행

테스트 맵은 `/Game/Maps/TechSpike`입니다. 각 명령은 인게임 콘솔(`~`)에서 실행합니다.

### Steam 공개 로비

Steam Client에 로그인하고 Steam이 활성화된 Development 빌드를 실행합니다.

```text
SPCreateLobby 4
```

다른 플레이어는 공개 로비를 검색한 뒤 로그에 표시된 인덱스로 참가하거나 Quick Play를 사용합니다.

```text
SPFindLobbies
SPJoinLobby 0
SPQuickPlay
```

현재 로비 경로는 빌드 ID·규칙 버전·진행 단계·빈자리 metadata를 검증하며, 원정이 시작되면 신규 참가와 초대를 잠급니다. 플랫폼 친구 초대 **수락 경로**와 `V` push-to-talk 설정은 코드에 연결되어 있지만, 실제 서로 다른 Steam 계정·Steam Client 간 친구 초대와 음성 송수신은 아직 라이브 검증하지 않았습니다. 게임 내 친구 초대 발송 UI, 로비 목록 UI와 음성 상태 UI도 없습니다.

### raw IP 폴백

온라인 서비스를 사용하지 않는 로컬·LAN 회귀 테스트는 실행 인자에 `-nosteam`을 추가하고 다음 명령을 사용합니다.

호스트:

```text
SPHost 4
```

클라이언트:

```text
SPJoin 127.0.0.1
```

raw IP 경로에는 Steam 고유 ID가 없으므로 원정 재접속 시 동일 플레이어를 안정적으로 식별하는 경로로 간주하지 않습니다.

### 조작

- `W/A/S/D`, 마우스: 이동과 시점
- `Space`: 점프
- `Left Shift`: 질주
- `Left Ctrl`: 웅크리기
- `E`: 조준한 월드 아이템 상호작용
- `Q` 또는 마우스 왼쪽 버튼: 손에 든 스크롤 사용
- `1`–`4`: 해당 가방 칸과 손 교환
- `G`: 손 아이템 버리기
- `R`: 자가 치료 시작
- `V`: push-to-talk

대형 화물은 손에만 들 수 있고 운반 중 질주가 막히며 이동 속도와 소음에 불이익이 있습니다. 1인칭 HUD는 상호작용 pending·수락·거부, 손·가방, 런 타이머, 스태미나, 파티 상태를 개발용 텍스트로 표시합니다.

파티 기능은 현재 프로덕션 UI 대신 개발자 콘솔 명령으로 확인합니다.

```text
SPReady true
SPChat 테스트 메시지
SPKick 2
SPVoteKick 2
SPVote <VoteId> true
SPMutePlayer 2 true
```

## 검증

### Development Game 빌드

```powershell
$UE_ROOT = 'C:\Program Files\Epic Games\UE_5.8'
$Project = (Resolve-Path '.\ScrollPeddler.uproject').Path

& "$UE_ROOT\Engine\Build\BatchFiles\Build.bat" `
  ScrollPeddler Win64 Development `
  "-Project=$Project" -WaitMutex -architecture=x64
```

### 전체 자동화 테스트

```powershell
$UE_ROOT = 'C:\Program Files\Epic Games\UE_5.8'
$Project = (Resolve-Path '.\ScrollPeddler.uproject').Path
$Report = (Join-Path (Get-Location) 'Saved\AutomationReports\VerticalSlice')

& "$UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  $Project -unattended -nop4 -nosplash -nullrhi `
  '-ExecCmds=Automation RunTests ScrollPeddler;Quit' `
  "-ReportExportPath=$Report"
```

### Win64 Development 패키징

```powershell
$UE_ROOT = 'C:\Program Files\Epic Games\UE_5.8'
$ProjectRoot = (Get-Location).Path
$Project = (Resolve-Path '.\ScrollPeddler.uproject').Path
$PackageRoot = Join-Path $ProjectRoot 'Saved\Packages\Windows-VerticalSlice'

& "$UE_ROOT\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
  "-project=$Project" -nop4 -utf8output -unattended `
  -target=ScrollPeddler -platform=Win64 -clientconfig=Development `
  -build -cook '-map=/Game/Maps/TechSpike' -stage -pak -iostore `
  -package -archive "-archivedirectory=$PackageRoot"
```

### 패키지 raw IP 다중 프로세스 smoke

다음 예시는 4프로세스를 실행합니다. `$ExpectedPlayers`를 `2`로 바꾸면 동일한 흐름으로 2프로세스를 검증합니다.

```powershell
$ExpectedPlayers = 4
$Game = (Resolve-Path '.\Saved\Packages\Windows-VerticalSlice\ScrollPeddler.exe').Path
$SmokeRoot = Join-Path (Get-Location) 'Saved\Smoke\Manual'
New-Item -ItemType Directory -Force -Path $SmokeRoot | Out-Null

$Common = @(
  '-nosteam', '-windowed', '-ResX=480', '-ResY=270',
  '-unattended', '-nosplash', '-nosound',
  '-SPAutoSpike', '-SPAutoContestedPickup', '-SPAutoQuit', '-log'
)

$HostArgs = @(
  "/Game/Maps/TechSpike?listen?ExpectedPlayers=${ExpectedPlayers}?CampaignSlot=2?DungeonSeed=515151",
  '-SPProfileSlot=SmokeHost',
  "-abslog=$SmokeRoot\Host.log"
) + $Common
$Processes = @(Start-Process $Game -ArgumentList $HostArgs -PassThru)

Start-Sleep -Seconds 5
foreach ($Index in 1..($ExpectedPlayers - 1)) {
  $ClientArgs = @(
    '127.0.0.1',
    "-SPProfileSlot=SmokeClient$Index",
    "-abslog=$SmokeRoot\Client$Index.log"
  ) + $Common
  $Processes += Start-Process $Game -ArgumentList $ClientArgs -PassThru
}

$Processes | Wait-Process
```

`-SPAutoSpike`, `-SPAutoContestedPickup`, `-SPAutoQuit`은 Development smoke 전용입니다. 각 프로세스는 같은 PC에서 SaveGame 슬롯이 겹치지 않도록 서로 다른 `-SPProfileSlot`을 사용해야 합니다.
예시 명령은 패키지의 캠페인 2번 슬롯에 smoke 정산 결과를 기록합니다.

### 최근 확인 결과

2026-07-27 기준으로 다음을 확인했습니다.

- Win64 Development Editor와 Development Game 빌드 통과
- 전체 자동화 65개 실행: 64개 성공, 1개 예상 경고와 함께 성공, 실패·미실행 0개
- 예상 경고는 잘못된 런 단계 전환을 의도대로 거부하는 `ScrollPeddler.Run.GameStateDeadlinesAndRoster` 로그
- `/Game/Maps/TechSpike` cook, stage, pak, IoStore와 Win64 Development 패키징 통과
- `-nosteam` 패키지 raw IP 2프로세스 smoke에서 호스트·클라이언트 정상 종료와 호스트 캠페인 정산 commit 확인
- `-nosteam` 패키지 raw IP 4프로세스 smoke에서 4인 roster 시작, 각 클라이언트 자동 완료와 단일 호스트 캠페인 정산 commit 확인
- 현재 패키지 smoke는 raw IP 회귀 경로이며 Steam 로비·친구 초대·음성의 실계정 라이브 검증을 대체하지 않음

전체 패키징 및 무인 smoke 명령은 [기술 스파이크 문서](Docs/TECH_SPIKE.md)를 참고하세요.

## 프로젝트 구조

```text
Config/                 Unreal 프로젝트 설정
Content/                맵과 Data Asset — Git LFS 대상
Docs/                   기술 검증 및 운영 문서
Scripts/                재현 가능한 에디터 콘텐츠 생성 스크립트
SourceAssets/           Blender 원본과 게시 FBX — Git LFS 대상
Source/ScrollPeddler/    Runtime C++ 모듈
  Core/                 아이템·런·상호작용·파티·장비·스크롤 공통 규칙
  Data/                 아이템·스크롤·위협·계약 등 데이터 정의
  Game/                 GameMode, GameState, PlayerState, Controller
  Hub/                  Workshop 제작 트랜잭션
  Online/               Steam/raw 세션과 호스트 캠페인 서브시스템
  Persistence/          캠페인·로컬 프로필 SaveGame
  Player/               캐릭터와 인벤토리
  Tests/                Unreal 자동화 테스트
  UI/                   현재 개발용 HUD
  World/                아이템, 탈출, 던전, 소음, 위협과 디렉터
```

## 다음 구현 순서

새 콘텐츠 수를 늘리기 전에 다음 기술 게이트를 순서대로 닫습니다.

1. 1인칭 손과 원격 플레이어 바디에 최종 교체 가능한 에셋·애니메이션 구조 확정
2. 픽업 외 스크롤·장비·제작·투표까지 로컬 pending과 서버 수락·거부 피드백 통일
3. 거리·LOS·소유권 위조·오래된 리비전·동시 claim을 실제 다중 프로세스 RPC 회귀 테스트로 확장
4. 패키지 2·4프로세스 smoke를 반복 가능한 스크립트와 CI 게이트로 고정
5. 소음 반응 위협과 보존/소비 선택을 포함한 G1/G2 성공 기준을 문서화하고 10세션 플레이테스트

그 다음 콘텐츠 단계는 나머지 5개 스크롤 계열 Data Asset, 방별 던전 구성, 장비 플레이 연결, 계약·제작·로비 프로덕션 UI 순으로 진행합니다. Steam 친구 초대와 음성은 별도의 두 Steam 계정 패키지 검증 게이트로 다룹니다.

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
