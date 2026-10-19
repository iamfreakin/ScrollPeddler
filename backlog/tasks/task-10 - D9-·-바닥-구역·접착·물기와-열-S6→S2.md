---
id: TASK-10
title: D9 · 바닥 구역·접착·물기와 열 (S6→S2)
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-9
  - TASK-22
priority: medium
ordinal: 10000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
바닥 구역과 접착막·실제 결합을 만들고 물기·열량을 붙인다. 반응은 S6 먼저, S2 다음 순서로 처리하고 캐릭터 이동 제약을 연결한다. 물·열 환경 원천부터 준비한다.

막히면 먼저 확인: 열을 두 번 쓰는지, 막과 결합의 원인 ID, 고정체 처리.

파일: Effects/ScrollEffectWorldSubsystem, ScrollReactionResolver, ScrollSurfaceZone, ScrollAdhesionComponent, World/ScrollEnvironmentSource, Framework/ScrollCharacterMovementComponent
출처: T9 §2.2·§3.2, v2.2 §13
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 젖은/마른 접착 비교와 남은 열·해제 결과를 남겼다
- [ ] #2 바닥 영향과 몸 직접 도장이 구분된다
- [ ] #3 S6→S2 순서가 자동화 테스트로 돈다
<!-- AC:END -->
