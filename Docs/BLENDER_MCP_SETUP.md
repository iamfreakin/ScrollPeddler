# Blender MCP 개발 환경

이 문서는 Scroll Peddler의 선택적 모델링 자동화 도구인 Blender MCP를 동일한 버전으로 설치하고 검증하는 절차를 기록한다. 게임 실행, Unreal 빌드 또는 패키징에는 필요하지 않다.

Blender MCP는 Blender Foundation 공식 기능이 아닌 서드파티 도구다. 설치 소스 전체나 개인별 Codex 설정은 저장소에 넣지 않고, 검토한 upstream revision과 해시만 `Scripts/BlenderMCP/manifest.json`에 고정한다.

## 고정 기준

| 항목 | 기준 |
|---|---|
| Blender | Steam Blender 5.2.0 LTS |
| Blender MCP | `ahujasid/blender-mcp` revision `da4e16d2069ce5154eaa2535bf995e843caf5c73` |
| `addon.py` SHA256 | `FEBEC0891C67214FF1BAA3E8EFEFD539375889E9169FEC5383F5F8F92C494D9D` |
| `uv` | 0.11.30 |
| Python | uv 관리형 3.11.15 |
| 로컬 연결 | `127.0.0.1:9876` |

버전과 해시의 단일 기준은 [manifest](../Scripts/BlenderMCP/manifest.json)다.

## 보안 경계

- upstream 애드온은 `localhost`에 bind한다. 이 Windows 구성에서는 실행 후 실제 listener가 `127.0.0.1` 또는 `::1`인지 확인하며, 다른 네트워크 인터페이스, 공유기 포트 포워딩 또는 원격 호스트로 노출하지 않는다.
- MCP 서버에는 `DISABLE_TELEMETRY=true`를 전달한다. Blender 애드온의 `Allow Telemetry`도 비활성 상태로 유지한다.
- Codex에는 `get_scene_info`, `get_object_info`, `get_viewport_screenshot`, `execute_blender_code`만 노출한다. Poly Haven, Sketchfab, Hyper3D, Hunyuan3D 도구는 기본 허용 목록에 넣지 않는다.
- 도구 허용 목록은 실수로 외부 연동을 호출할 가능성을 줄일 뿐 sandbox가 아니다. `execute_blender_code`는 Blender 사용자 권한으로 임의 Python을 실행할 수 있으므로 정식 `.blend`를 직접 실험 대상으로 삼지 않고 작업 복사본과 작업 브랜치를 사용한다.
- 외부 연동 활성화 여부는 `.blend`별 Scene 속성으로 달라질 수 있다. 작업 파일을 열 때마다 Poly Haven, Sketchfab, Hyper3D와 Hunyuan3D가 꺼져 있는지 확인한다.
- API 키나 외부 서비스 자격 증명을 Blender 환경, Codex 설정 또는 저장소에 기록하지 않는다.

## 설치

1. Steam에서 Blender 5.2 LTS를 설치한다.
2. Git과 Git LFS를 설치한다.
3. `uv` 0.11.30을 설치한다.

```powershell
winget install --id astral-sh.uv -e --version 0.11.30 `
  --source winget --accept-source-agreements --accept-package-agreements
```

4. Blender를 모두 종료한다.
5. 저장소 루트에서 설치 스크립트를 실행한다. 기본 설치 위치는 `%LOCALAPPDATA%\ScrollPeddler\Tools\BlenderMCP`이며 저장소 밖이다.

```powershell
& .\Scripts\BlenderMCP\install.ps1 -EnableAddon
```

Steam 외 경로의 Blender를 사용한다면 명시적으로 전달한다.

```powershell
& .\Scripts\BlenderMCP\install.ps1 `
  -BlenderExecutable 'D:\Tools\Blender\blender.exe' `
  -EnableAddon
```

스크립트는 다음 작업만 수행한다.

1. upstream을 고정 revision으로 checkout한다.
2. `addon.py` SHA256을 검증한다.
3. `uv.lock`을 변경하지 않고 Python 3.11.15 환경을 생성한다.
4. 요청한 경우 Blender 애드온을 설치·활성화하고 애드온 텔레메트리 동의를 끈다.
5. 개인 `config.toml`에 넣을 MCP 설정 조각을 출력한다.

Blender 프로필에 다른 내용의 `addon.py`가 이미 있으면 일반적인 모듈명 충돌을 피하기 위해 설치를 중단한다. 기존 파일의 출처를 확인하고 교체가 필요하다고 판단한 경우에만 `-ForceAddonOverwrite`를 `-EnableAddon`과 함께 사용한다. 스크립트는 교체 전 파일을 같은 폴더에 타임스탬프가 붙은 이름으로 백업한다.

출력된 `[mcp_servers.blender]` 블록을 사용자 수준 `C:\Users\<사용자>\.codex\config.toml`에 추가한다. 개인 절대 경로가 포함되므로 이 파일을 저장소에 복사하거나 커밋하지 않는다. 설정 반영 후 Codex 앱을 다시 시작한다.

## 검증

설치된 소스 revision, 애드온 해시와 실행 파일을 다시 확인한다.

```powershell
& .\Scripts\BlenderMCP\install.ps1 -VerifyOnly
```

Blender GUI가 실행 중일 때 로컬 listener를 확인한다.

```powershell
Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort 9876 -State Listen
```

Codex에서 읽기 전용 smoke를 수행한다.

```text
현재 Blender 씬의 오브젝트 이름과 타입만 알려줘. 씬을 수정하지 마.
```

기본 씬이라면 `Cube`, `Light`, `Camera`가 반환되어야 한다. Blender를 종료한 뒤에는 `9876` listener가 사라져야 한다.

애드온은 Blender GUI가 시작될 때 로컬 listener를 자동으로 시작한다. listener가 `127.0.0.1` 또는 IPv6 loopback인 `::1`이 아닌 주소에 바인딩되면 MCP를 사용하지 않고 설정을 다시 점검한다. 설치 과정에서 Windows 방화벽 허용 규칙이나 공유기 포트 포워딩을 추가하지 않는다.

## 업데이트 원칙

`main`이나 최신 PyPI 버전을 자동 추적하지 않는다. 업데이트할 때는 별도 작업 브랜치에서 upstream 변경과 라이선스·텔레메트리·임의 코드 실행 경계를 검토하고, Blender 씬 조회 및 테스트 모델 생성 후 manifest의 revision과 SHA256을 함께 갱신한다.
