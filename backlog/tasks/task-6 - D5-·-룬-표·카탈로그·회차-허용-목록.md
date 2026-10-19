---
id: TASK-6
title: D5 · 룬 표·카탈로그·회차 허용 목록
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:34'
labels:
  - 코드
milestone: m-0
dependencies:
  - TASK-5
priority: medium
ordinal: 6000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
룬 행·문장·도안 버전 자료형을 만들고 동사·대상·수식 3개 표와 정의 카탈로그, 회차 허용 목록(DA_Spike01)을 만든다. 전체 18룬 효과는 만들지 않는다.

막히면 먼저 확인: 표 필드명·행 ID·부모 자료형·원본 CSV.

파일: Runes/ScrollRuneTypes.h 확장, Definitions/ScrollDefinitionCatalog, ScrollDefinitionSubsystem, ScrollExperimentDefinition
에셋: DT_RuneVerbs·DT_RuneTargets·DT_RuneModifiers·DA_ScrollDefinitionCatalog·DA_Spike01, CSV 원본은 DataSource/Runes
출처: T9 §2.2·§3.2, T8 §1
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 정식 7룬 목록과 12조합 계산표가 나온다
- [ ] #2 누락되거나 다른 종류인 ID를 거부한다
- [ ] #3 표 검사가 자동화 테스트로 돈다
<!-- AC:END -->
