---
id: TASK-15
title: D14 · 손 해제
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-14
priority: medium
ordinal: 15000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
손 해제 시작·취소·완료를 만들고 완료 때 막과 결합을 함께 제거해 이동을 복구한다. 설명 없이 해제 조작을 찾을 수 있는지 점검한다.

막히면 먼저 확인: 입력 전체 비활성화, 결합만 제거, 낙하 중 옛 이동 모드 복원.

파일: Interaction/ScrollRemoveEffectComponent
출처: T9 §2.2·§3.2, T1 §6
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 접착 중에도 손 해제가 가능하다
- [ ] #2 완료 직후 재접착이 없다
- [ ] #3 양쪽 화면에서 이동이 정상으로 돌아온다
<!-- AC:END -->
