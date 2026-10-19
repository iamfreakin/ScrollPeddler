---
id: TASK-9
title: D8 · 공통 수신기·8상태·효과 수명
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-8
priority: medium
ordinal: 9000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
사람·시험체·소품·표면의 공통 수신 입구와 8속성 상태를 만든다. 활성 효과 ID와 원래 종료 시각, 최강 선택, 만료와 대상 제거를 처리한다. 운반물과 AI 없는 시험체도 같은 입구를 쓴다.

막히면 먼저 확인: 재시전 때 기존 타이머 재시작, 약한 효과 목록 삭제, 소멸 대상의 타이머.

파일: Effects/ScrollEffectReceiver.h, ScrollEffectTypes.h, ScrollAttributeComponent, World/ScrollPhysicsProp, World/ScrollCreatureTestActor, Tests 강약 효과 설정
출처: T9 §2.2·§3.2, T8 §2
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 강한 효과와 약한 효과가 서로 다른 원래 종료 시각을 가진다
- [ ] #2 사람·시험체·소품이 같은 활성 효과 목록을 쓴다
- [ ] #3 최강 선택·만료가 자동화 테스트로 돈다
<!-- AC:END -->
