---
id: TASK-8
title: D7 · 세 칸 조립·한 장 출력·12문장 경로
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-7
priority: medium
ordinal: 8000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
세 칸 조립과 비용/범위 자료 → 도안 사본·한 장 출력 → 출력 완료/취소 경쟁 → 12문장 각각 사용 → 최신 정의 개정 확인. 완성 공방·휴대 급조 전체는 만들지 않는다. 전체 실제 반응은 D12까지 확인한다.

막히면 먼저 확인: 도안 최신 참조 때문에 기존 제품 문장이 변하는지, 미완성 출력에 재료를 소비했는지.

파일: Runes/ScrollPrintComponent, Presentation/ScrollSentenceBoardWidget, 에셋 /Game/ScrollPeddler/UI/WBP_SentenceBoard
출처: T9 §2.2·§3.2
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 12개 실행 경로·계산값·단일 소비 표를 남겼다
- [ ] #2 미완성 출력 취소는 소비가 없고 완료 결과는 한 번만 생긴다
- [ ] #3 정상 무효 결과와 요청 거부를 구분해 기록한다
<!-- AC:END -->
