---
id: TASK-11
title: D10 · 발광·감광·먹막 (S1·S3)
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-10
priority: medium
ordinal: 11000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
이끼·등기구·먹물 환경을 만들고 S1 발광 → S3 감광 → 먹막 제거와 원래 광원 확인을 처리한다.

막히면 먼저 확인: 매번 밝기 배율을 누적하는지, 기본 먹을 차광 먹막으로 썼는지.

에셋: BP_MossPatch, BP_Lamp, BP_InkSource (같은 원천 C++ 클래스의 자료 설정)
출처: T9 §2.2·§3.2
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 등심 없는 젖은 대상은 빛나지 않는다
- [ ] #2 먹막 제거 뒤 현재 상태에 맞는 밝기로 돌아온다
<!-- AC:END -->
