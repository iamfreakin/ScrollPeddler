---
id: TASK-57
title: 저장 실패 공동 중단과 복원 (EX14·EX15)
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:35'
labels:
  - 코드
milestone: m-2
dependencies:
  - TASK-56
priority: high
ordinal: 57000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
저장 실패를 확인하면 두 공간의 물때·효과·공정·새 거래 승인을 함께 멈추고 현 상태 공동 저장을 재시도한다. 계속 실패하면 마지막 정상 시점과 미보존 구간을 보인다.

출처: T1 §5·§7, v2.2 §11
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 저장 요청을 성공으로 표시하지 않는다 (EX14)
- [ ] #2 호스트 충돌 뒤 마지막 정상 공동 시점을 복원하고 개인 기록을 사건 ID로 재전달한다 (EX15)
- [ ] #3 메모리의 확정 거래를 임의로 환불하지 않는다
<!-- AC:END -->
