---
id: TASK-3
title: D2 · Windows 패키지와 LAN 2인 접속
status: To Do
assignee: []
created_date: '2026-10-19 05:34'
updated_date: '2026-10-19 05:39'
labels:
  - 코드
  - 검증
milestone: m-0
dependencies:
  - TASK-2
priority: high
ordinal: 3000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Development Windows 패키지를 만들고 같은 PC 두 프로세스 → 다른 PC LAN 두 명 → 호스트 교환 순서로 접속한다. Framework/ScrollCharacterMovementComponent 파일을 만들어 Character의 이동 Component로 실제 연결됐는지만 확인한다(접착·반발 구현은 D9~D11).

막히면 먼저 확인: 패키지 맵 포함, ?listen, 주소·UDP 포트, 같은 빌드.

이 카드가 끝나면 스파이크1 공수를 다시 산정한다(T8 §7.1).
검증: T9 §4.2~4.4
출처: T9 §2.2·§3.2, T8 §7.1
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 같은 PC 두 프로세스와 다른 PC LAN 두 명의 연결 기록·두 PC 로그가 있다
- [ ] #2 각자 상대 몸을 확인했고 호스트를 바꿔도 접속된다
- [ ] #3 ScrollCharacterMovementComponent가 Character에 연결된 것을 확인했다
- [ ] #4 공수 재산정 메모를 태스크 노트에 남겼다
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-19: UScrollCharacterMovementComponent는 이미 AScrollCharacter 생성자(SetDefaultSubobjectClass)로 연결돼 있다(스프린트 saved move 포함). 패키지에서 이동이 정상인 것을 본 뒤 완료 조건 #3을 체크한다.
<!-- SECTION:NOTES:END -->
