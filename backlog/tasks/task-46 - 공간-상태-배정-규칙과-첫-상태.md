---
id: TASK-46
title: 공간 상태 배정 규칙과 첫 상태
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-2
dependencies:
  - TASK-44
priority: medium
ordinal: 46000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
상태 변주 배정 규칙(첫 A → 공개 미경험 우선 → 직전 상태를 피하는 순환)을 만들고 슬라이스용 첫 상태를 넣는다. 13상태 전체는 1.0에서 채운다.

출처: T3 §6·§7, W05 §7
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 출정 전에 상태가 표시된다
- [ ] #2 당일 취소·재접속 뒤에도 배정이 유지된다
<!-- AC:END -->
