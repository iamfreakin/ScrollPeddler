---
id: TASK-31
title: 계획 재고와 실제 예약·장별 완료/취소
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-1
dependencies:
  - TASK-30
priority: high
ordinal: 31000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
계획 몫은 경고만, 실제 예약은 소비를 막는다. 인쇄는 장별 완료가 거래 단위다. 묶음 취소는 남은 장의 예약만 풀고 완료한 장은 유지한다. 선반·주문 카드에 네 몫을 보이고 새 창고 화면은 만들지 않는다.

출처: W02 §1.0 계획 재고와 실제 예약, T1 §7, 11번 종이 검사
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 4장 예약→2장 완료→취소면 완료 2·소비 2장분·예약 0·잔여 원료 2장분이다
- [ ] #2 계획 몫을 다른 작업에 써도 경고만 나고 실제 예약은 소비되지 않는다 (EX04)
- [ ] #3 완료·취소가 동시에 와도 장별 상태를 한 번만 적용한다 (EX05)
- [ ] #4 위 경우가 자동화 테스트로 돈다
<!-- AC:END -->
