---
id: TASK-5
title: D4 · 요청 ID·한 장 소비·중복 방지
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-4
priority: high
ordinal: 5000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
요청 ID와 처리 결과를 보관하고 한 장 소비 거래를 만든다. 최소 재현 입구로 응답 재전송·연타·같은 요청 경쟁을 검사하고 최소 사건 ID를 붙인다. 실제 효과 발동은 D6~D7 뒤에 다시 검사한다.

막히면 먼저 확인: 입력마다 새 ID인지 같은 ID 재시도인지, 검사와 차감 사이 대기, 호스트 별도 경로.

파일: Network/ScrollActionComponent(RequestId·ActionId·중복 기록·승인/거부), Inventory/ScrollInventoryTransaction, Events/ScrollEventTypes.h·ScrollEventSubsystem(최소), Tests/ScrollSpikeTestTypes.h·ScrollSpikeTestController(같은 ID 재전송)
출처: T9 §2.2·§3.2, T1 §7
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 승인 1·소비 1·실행 명령 발행 1을 카운터로 확인했다
- [ ] #2 같은 요청 10회에도 추가 소비가 0이다 (EX02)
- [ ] #3 같은 ID·다른 내용 요청은 거부된다 (EX03)
- [ ] #4 중복 요청 검사가 자동화 테스트로 돈다
<!-- AC:END -->
