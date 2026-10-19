---
id: TASK-47
title: 버팀 위험 게이지·쓰러짐·손 구조
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-2
dependencies:
  - TASK-40
priority: medium
ordinal: 47000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
숨은 체력 대신 3단계로 보이는 위험 게이지(여유/휘청임/쓰러짐)를 만든다. 아군 직접 원인은 최대 99까지만 올린다. 쓰러진 몸과 손 구조(3초, 여러 명이 잡아도 단축 없음)를 만든다.

출처: W04 §1, T1 §1·§6
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 아군 원인만으로는 쓰러지지 않는다
- [ ] #2 구조 직후 게이지 60에서 시작해 5초 뒤 회복한다
- [ ] #3 구조 중단 조건이 T1 §6과 같다
<!-- AC:END -->
