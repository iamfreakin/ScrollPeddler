---
id: TASK-4
title: D3 · 재고·소유 요청 입구·꾸러미 줍기
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-3
priority: high
ordinal: 4000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
최소 문장 ID 값을 선언하고 재고 Component와 소유 Controller의 서버 요청 입구를 만든다. 꾸러미 상호작용·줍기, 컨테이너 이전과 소유권을 연결하고 두 명이 동시에 줍게 한다.

막히면 먼저 확인: 서버만 이전하는지, 월드 물품과 가방에 동시에 남는지.

파일: Runes/ScrollRuneTypes.h(최소), Inventory/ScrollInventoryTypes.h, Inventory/ScrollInventoryComponent, Interaction/ScrollInteractable, Interaction/ScrollPickupActor, Network/ScrollActionTypes.h, Network/ScrollActionComponent(최소 입구)
출처: T9 §2.2·§3.2
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 같은 물품 ID의 소유자는 언제나 한 명이다
- [ ] #2 두 명 동시 줍기 뒤에도 재고 총량이 보존된다 (EX01)
<!-- AC:END -->
