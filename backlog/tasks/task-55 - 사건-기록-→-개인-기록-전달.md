---
id: TASK-55
title: 사건 기록 → 개인 기록 전달
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:35'
labels:
  - 코드
milestone: m-2
dependencies:
  - TASK-41
priority: medium
ordinal: 55000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
호스트가 사건 저장에 성공한 뒤 개인 기록을 전달한다. 사건 ID로 중복을 막고 개인 쓰기 실패는 다음 접속에 다시 전달한다. 사건 시점 원정 참가자와 직접 해독자를 구별한다.

출처: T1 §3·§5, v2.2 §11
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 같은 사건이 개인 기록에 두 번 들어가지 않는다
- [ ] #2 먼저 귀환한 사람에게 던전 목격을 소급하지 않는다
<!-- AC:END -->
