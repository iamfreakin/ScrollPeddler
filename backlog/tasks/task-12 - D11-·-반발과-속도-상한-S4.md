---
id: TASK-12
title: D11 · 반발과 속도 상한 (S4)
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-11
  - TASK-23
priority: medium
ordinal: 12000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
새 충돌을 식별해 S4 반발을 적용하고 속도 상한·재충돌 제한을 둔다. 제한 운반물과 생물 이동에 연결한다.

막히면 먼저 확인: 양쪽 접촉 통지 중복, 클라이언트도 힘을 적용하는지, CharacterMovement 밖 위치 덮어쓰기.

에셋: BP_BounceMat (고정 탄성 매트)
출처: T9 §2.2·§3.2
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 정지 대상은 즉시 발사되지 않는다
- [ ] #2 접착 중 반발로 자동 해제되지 않는다
- [ ] #3 양 화면의 이동이 같은 위치로 수렴한다
<!-- AC:END -->
