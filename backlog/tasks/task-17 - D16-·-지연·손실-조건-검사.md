---
id: TASK-17
title: D16 · 지연·손실 조건 검사
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 검증
milestone: m-0
dependencies:
  - TASK-16
priority: medium
ordinal: 17000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
지연만 → 손실만 → 혼합 조건 → 호스트 교환 → 원래 정상망 복귀 순으로 검사한다.

막히면 먼저 확인: 인위 지연 중복 적용, 설정 미초기화, Reliable 메시지 누적.

검증: T9 §4.6
출처: T9 §3.2
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 조건별 요청 수·소비 수·보정·종료 상태 기록이 있다
- [ ] #2 검사 뒤 네트워크 에뮬레이션 설정을 해제했다
<!-- AC:END -->
