---
id: TASK-48
title: 생물 공통 부품 — 상태 흐름·감지·하루 1회 떨굼
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-2
dependencies:
  - TASK-47
priority: medium
ordinal: 48000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
생물 상태 흐름(쉼→일→경계→예고→확정→실행→빈틈→도망/떨굼), 시야·청각 감지, 한 구역 큰 공격 1개 제한, 하루 1회 떨굼(개체/무리 ID, 호스트 서버가 한 번 판정)을 만든다. 생물은 목소리·핑·UI를 듣지 않는다.

출처: W04 §2
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 같은 개체는 하루 한 번만 떨군다
- [ ] #2 떨굼은 호스트에서 한 번만 판정되고 재료는 먼저 줍는 사람 가방으로 간다
- [ ] #3 예고 범위가 바닥에 옅게 보인다
<!-- AC:END -->
