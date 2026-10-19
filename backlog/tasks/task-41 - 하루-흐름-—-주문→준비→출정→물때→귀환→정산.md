---
id: TASK-41
title: 하루 흐름 — 주문→준비→출정→물때→귀환→정산
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-2
dependencies:
  - TASK-40
priority: high
ordinal: 41000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
하루 한 판의 단계 전환을 서버 상태로 만든다. 공방 약 12분·원정 최대 12분이 대표 흐름이고, 먼저 귀환한 사람의 공방 시간을 원정 시간에 다시 더하지 않는다.

출처: v2.2 §3, T4 §1
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 주문부터 정산까지 한 판이 끊기지 않고 이어진다
- [ ] #2 단계 전환이 모든 화면에서 같은 서버 시각을 쓴다
<!-- AC:END -->
