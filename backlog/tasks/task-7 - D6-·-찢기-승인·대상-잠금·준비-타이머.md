---
id: TASK-7
title: D6 · 찢기 승인·대상 잠금·준비 타이머
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-6
priority: medium
ordinal: 7000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
찢기 승인과 소비 → 단일 대상/표면 잠금 → 준비 타이머 → 발동 시 넓게 대상 목록 → 무효 이유 순으로 만들고, D4의 중복 요청 검사를 실행기에 연결해 다시 돌린다. 잠금 기준은 D4-06·D4-07을 따른다.

막히면 먼저 확인: 클릭 전 준비시간을 붙였는지, 시각·잠금 중심을 계속 덮어쓰는지.

파일: Runes/ScrollCastTypes.h, ScrollTargetResolver, ScrollCastExecutor
출처: T9 §0·§2.2·§3.2
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 대상이 이동·소멸해도 자동 재선택·환불이 없다 (EX07)
- [ ] #2 넓게 목록이 발동 시점과 일치한다
- [ ] #3 승인 한 번에 실행기 호출도 한 번이다
<!-- AC:END -->
