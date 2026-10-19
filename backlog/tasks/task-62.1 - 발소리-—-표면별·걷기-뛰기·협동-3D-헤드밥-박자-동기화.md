---
id: TASK-62.1
title: '발소리 — 표면별·걷기/뛰기·협동 3D, 헤드밥 박자 동기화'
status: In Progress
assignee: []
created_date: '2026-10-19 05:47'
updated_date: '2026-10-19 08:26'
labels:
  - 코드
milestone: m-2
dependencies: []
parent_task_id: TASK-62
priority: medium
ordinal: 126000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
TASK-62 중 발소리 부분만 먼저 구현한다(사용자 요청, 2026-10-19). 손맛 소리·위험 경고는 TASK-62에 남긴다. 구매 사운드(W03 §3: 나무·돌·물·종이 × 걷기·뛰기 8종)가 오기 전까지 Bodycam_VHS_Effect 팩의 카펫 발소리를 로컬 임시음으로 쓰며 저장소에 넣지 않는다.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 이동 거리 기반 발걸음으로 걷기·뛰기 소리 세트를 나눠 재생한다
- [x] #2 바닥 Physical Material의 표면(Wood·Stone·Water·Paper·기본)별로 소리 세트를 고른다
- [x] #3 다른 플레이어의 발소리가 그 위치에서 3D로 들린다(복제된 이동으로 각 PC가 계산, 발소리용 네트워크 메시지 없음)
- [ ] #4 발걸음 하나당 각 PC에서 한 번만 재생된다(서버·클라 중복 없음)
- [ ] #5 헤드밥 위아래 박자가 발걸음과 맞는다
- [x] #6 서드파티·구매 사운드 원본이 저장소에 들어가지 않는다
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1) Presentation/UScrollFootstepComponent: 2D 이동 거리 누적, 속도에 따라 보폭 보간(걷기 150cm·달리기 192cm → 2.0/2.6걸음/초), 아래로 라인트레이스해 표면 판별, 걷기/뛰기 세트에서 무작위 재생, 런타임 감쇠로 3D. 전용 서버 제외 모든 PC에서 모든 캐릭터에 대해 실행. 착지 시 한 번 재생. 디버그 CVar Scroll.Footsteps.Debug. 2) 헤드밥: 발걸음 위상(GetStepCycles)으로 pitch(걸음마다)·yaw(두 걸음마다) 계산, 대기는 별도 층. 3) VHS 팩 카펫 발소리 4개를 /Game/ThirdParty(무시 목록)로 가져와 BP 기본 세트에 지정. 4) L_Spike01에 표면 시험 바닥 4칸(PM_Wood/Stone/Water/Paper) 스크립트. 5) 빌드·표면 트레이스 확인·2프로세스 접속 확인, 사람이 PIE로 소리·박자 확인.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-19 진행: UScrollFootstepComponent(거리 기반 발걸음, 표면 트레이스, 걷기/뛰기 세트, 런타임 3D 감쇠, 착지음, CVar Scroll.Footsteps.Debug)와 발걸음 위상 기반 헤드밥(UScrollHeadBobComponent 재작성) 작성. 빌드됨: 게임 타깃 Win64 Development 성공(경고 0). VHS 팩 카펫 발소리 4개 WAV 추출(로컬 스크래치). 남은 것: 에디터 빌드, /Game/ThirdParty 임시음 가져오기·BP 지정, L_Spike01 표면 시험 바닥(create_surface_test_area.py), 표면 트레이스 확인, 2프로세스 접속 확인, 사람이 PIE로 소리·박자 확인.

2026-10-19: 에디터 빌드됨. 임시음(VHS 카펫 4개, /Game/ThirdParty) BP 지정. L_Spike01 표면 시험 바닥 4칸(PM_Wood/Stone/Water/Paper) 생성. 게임 실행(-game, BugItGo+Walk 착지)에서 Wood/Stone/Water/Paper/Default 표면 판별 로그 확인. 임시 구분용으로 표면별 Pitch 필드 추가(Wood 0.9, Stone 1.2, Water 0.7, Paper 1.45). 사람 확인 남음: 소리·3D·헤드밥 박자·2인에서 중복 없음.

2026-10-19: 내 발소리가 이동 중 뒤(반대쪽)에 남던 문제 → 감쇠 NonSpatializedRadius 350→200cm로 가까운 소리 2D 블렌드. 사람이 확인함(에디터 재시작 후 정상). 늦게 들림: 임시 WAV 4개 앞 무음 약 207~284ms 측정, 처리 미정.

2026-10-19: 임시 WAV 앞 무음(약 0.2~0.28초) 제거 후 재가져오기(첫 시도는 헤더 버그로 샘플레이트 0 → 수정). 사람이 확인함: 발소리 타이밍·위치 정상. AC#2 근거: -game 실행에서 Wood/Stone/Water/Paper/Default 착지 로그.

2026-10-19: 개발용 걷는 봇(ScrollSpawnWalker/ScrollClearWalkers, Tests/ScrollDevWalkerController) 추가, 에디터 빌드됨. 사람이 확인함: PIE에서 봇 발소리가 3D로 잘 들림(AC#3). AC#4(PC당 1회)·#5(헤드밥 박자)·#1(걷기/뛰기 소리 구분, 임시음 동일)은 미확인.

2026-10-19 사용자 요청: 약 5A(캐릭터 높이 192cm 기준 9.6m)부터 무음, 그 전은 거리별 음량 차이가 들리도록 조정. NaturalSound를 Linear로 변경, 내측 반경 200cm 유지, FalloffDistance 760cm(총 960cm). 감쇠 배율은 3A 약 50.5%, 4A 약 25.3%, 5A 0%. 게임 및 에디터 Win64 Development 빌드 성공. Unreal Python 명령 실행으로 BP_SpikeCharacter 기본 컴포넌트도 Linear/200/760임을 확인(기존 BP 덮어쓰기 없음). 사람 청음은 미확인, 기존 AC는 그대로 유지. 로그: ScrollPeddler/Saved/Logs/CodexFootstepGameBuild.log, CodexFootstepBuild.log, CodexFootstepVerify.log.
<!-- SECTION:NOTES:END -->
