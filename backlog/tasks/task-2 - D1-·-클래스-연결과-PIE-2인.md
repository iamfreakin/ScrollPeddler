---
id: TASK-2
title: D1 · 클래스 연결과 PIE 2인
status: Done
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-1
priority: high
ordinal: 2000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
생성 클래스를 T8 역할에 연결하고 GameState·PlayerState·PlayerController를 GameMode에 지정한다. 기본 방에 호스트 포함 네 명의 시작 위치를 두고, 타인 몸과 1인칭 카메라를 확인한 뒤 PIE 2인으로 띄운다.

막히면 먼저 확인: GameMode 기본 Pawn/Controller, 소유 플레이어 입력, 1인칭 전용 메시가 타인 몸을 숨겼는지.

파일: Framework/ScrollGameState, ScrollPlayerController, ScrollPlayerState, ScrollCharacter
검증: T9 §4.1
출처: T9 §2.2·§3.2
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 호스트·원격 역할별 이동 화면을 남겼다
- [x] #2 시작 위치 네 개가 겹치지 않는다
- [x] #3 서로의 몸이 보이고 자기 몸은 1인칭 팔만 보인다
<!-- AC:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
2026-10-19 Backlog 도입 전에 완료한 작업. 완료 조건은 당시 확인 결과를 옮겨 적은 것이다.
<!-- SECTION:FINAL_SUMMARY:END -->
