---
id: TASK-14
title: D13 · 사건 기록 확장·원인·시간·표현
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-13
priority: medium
ordinal: 14000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
D4의 최소 사건 구조를 전체 인과 기록으로 확장한다. 문장→이전 상태→반응→결과를 한 사건으로 묶고, 원인·남은 시간·시각/소리 표현 자료를 서버 상태에서 만든다.

막히면 먼저 확인: OnRep 도착 순서 의존, 호스트 이중 재생, 제조자·사용자 혼동.

파일: Events/ScrollEventSubsystem 확장, Presentation/ScrollCauseViewTypes.h, ScrollEffectPresentationComponent
출처: T9 §2.2·§3.2, T8 §4
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 사건 하나로 인과를 조회할 수 있다
- [ ] #2 호스트와 원격 표시가 같은 상태 개정을 쓴다
- [ ] #3 같은 사건의 효과음·연출이 한 번만 재생된다
<!-- AC:END -->
