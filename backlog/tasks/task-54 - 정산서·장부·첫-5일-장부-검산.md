---
id: TASK-54
title: 정산서·장부·첫 5일 장부 검산
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
ordinal: 54000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
하루 끝 정산서와 장부(오늘 주문/재고·준비/후기/도감·공방일지 탭)를 만든다. 돈은 실제 구매/판매 거래만 차감·가산한다. 첫 5일 장부 산술을 코드로 검산한다.

출처: W07 §7, W11 §1, v2.2 §13
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 원가 C와 구매 지출을 이중 차감하지 않는다
- [ ] #2 첫 5일 장부 검산이 자동화 테스트로 돈다
<!-- AC:END -->
