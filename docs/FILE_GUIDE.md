**파일별 기능과 기존 프로그램 연결 위치**

이 모듈의 외부 진입점은 `src/ncr_sync.h`다. 호출자는 하나의 인스턴스를 생성하고 GPS 수신 스레드와 RLE 수신부에서 공유한다.

```text
기존 GPS 수신 스레드: GpsTime_t 수신
    → UpdateGpsTime()
    → GPS 상태와 유효 메시지 수신 시각 기록
    → GPS_SYNC의 시간 검증과 선택적 보정

기존 RLE 수신부: 패킷 최초 도착
    → CaptureNcrReceiveContext()
    → 문맥을 패킷과 함께 보관
    → 실제 파서가 Encryption_Context 3바이트 추출
    → RestoreNcr24()
    → ReconstructNcr24()
    → NCR32 + 송신 날짜 + 시간 품질 반환
```

**운영 모듈 파일**

| 파일 | 구체적인 기능 | 주요 함수/타입 | 기존 프로그램에서의 사용 |
|---|---|---|---|
| `src/ncr_sync.h` | GPS 입력, 공개 설정, 시간 품질, 이벤트 시각, 수신 문맥과 복원 결과 | `GpsTime_t`, `NcrConfig_t`, `NcrGpsEvents_t`, `NcrReceiveContext_t`, `NcrRestoreResult_t` | 호스트가 include하는 공개 헤더 |
| `src/ncr_sync.c` | GPS 입력 검증, Ubuntu 클록과 GPS 동기 상태 관리, 이벤트 시각 저장, mutex, NCR24 복원과 날짜 계산 | `UpdateGpsTime()`, `CaptureNcrReceiveContext()`, `RestoreNcr24()`, `GetNcrSyncStatus()` | 기존 실행 파일과 함께 C11로 컴파일하고 `-pthread`로 링크 |
| `examples/example_gps_callback.c` | 초기화, 기존 GPS 수신 직후 호출, RLE 최초 수신 캡처, 파싱 후 복원, 종료 순서 예시 | `InitializeGatewayClock()`, `HandleReceivedGpsTime()`, `CaptureRlePacketTime()`, `HandleParsedEncryptionContext()`, `ReleaseGatewayClock()` | 필요한 호출 위치를 기존 프로그램에 옮기는 기준. 독립 서버나 실제 전체 RLE 파서 아님 |

**시험 파일**

| 파일 | 기능 | 확인하는 문제 |
|---|---|---|
| `tests/test_ncr_sync.c` | 입력/설정 검증, 달력 변환 36,525일, 하루 전체 345,600건 NCR 복원, 날짜/순환/불확실성 경계 시험 | NCR·날짜 계산 정확성과 잘못된 입력 거절 |
| `tests/test_ncr_state.c` | 상태 3 수신, HOLDOVER 만료, 장기 미수신 중 복원, GPS 수신 복귀 및 이벤트 시각, 재획득, Step과 큐 시험 | 운영 중 복원 기준 전환과 장애 복귀 |
| `tests/test_ncr_concurrency.c` | GPS writer 10,000회와 RLE reader 4개, 합계 40,000회 캡처/복원 | 공유 상태와 패킷별 스냅샷의 동시 접근 |
| `tests/test_ncr_linux.c` | 실제 Linux 기본 어댑터의 시각을 독립 `clock_gettime()` 측정 범위와 비교. 64비트 `time_t`와 Step 콜백 등록 검사 | Linux 전용 클록 연결. 실제 시스템 시각 변경은 하지 않음 |
| `tests/ncr_platform_fake.c` | 가상 시각 읽기/진행/Step, 권한 실패·읽기 실패 주입, GPS/컨텍스트 생성과 공통 검사 | 실제 시스템 시간을 바꾸지 않는 결정적 장애 시험 |
| `tests/ncr_test_support.h` | 가상 클록 구조체, 공통 CHECK와 시험 헬퍼 선언 | 시험 코드가 공유하는 계약 |
| `tests/test_cpp.cpp` | C++11에서 공개 헤더를 include하고 C 정적 라이브러리를 링크 | `extern "C"`와 C++ 호스트 연동 |
| `tests/host_gps_adapter.h` | 호스트에 GPS 타입이 이미 있는 경우의 별칭 예제 | 구조체 중복 정의 없이 `GpsTime_t` 연결 |
| `tests/test_host_gps.c` | 별도 GPS 타입으로 날짜 변환과 32비트 ms 범위 검사 | 실제 호스트 타입 어댑터의 빌드/입력 호환 |

**빌드 및 문서 파일**

| 파일 | 기능 |
|---|---|
| `Makefile` | `src/ncr_sync.c`로 정적 라이브러리 생성. C11/pthread/64비트 시간 설정과 시험 타깃 제공 |
| `build_ubuntu.sh` | Linux 실행 환경 확인 후 프로젝트 루트로 이동하여 make 호출. 기본 `test` 모드는 `make test example` 실행 |
| `.gitignore` | build 디렉터리와 컴파일 산출물이 소스 관리에 섞이는 것을 방지 |
| `AGENTS.md` | 향후 코드 수정 시 적용할 코딩 규칙과 Ubuntu 대상·검증 원칙 |
| `README.md` | Ubuntu 도구 준비, 빌드, 설정, 호스트 연결 순서 |
| `docs/CODING_RULES.md` | 사용자가 제공한 C 코딩 규칙 원본 |
| `docs/CODING_RULES_PROMPT.txt` | 같은 규칙의 프롬프트 형식 원본 |
| `docs/FILE_GUIDE.md` | 현재 파일별 기능 설명 |
| `docs/VALIDATION.md` | 현재 변경과 검증의 수행/미수행 범위 |
| `docs/build_config_checks.txt` | shell/스크립트/Makefile 구성 검사 출력. 실제 Ubuntu 빌드 로그와 구분 |
| `docs/SOURCE_MANIFEST.json` | 배포 소스·문서의 SHA256 목록. 자체 파일과 빌드 산출물 제외 |

소스로 편입할 때는 `src/ncr_sync.c`와 `src/ncr_sync.h`를 함께 가져오고 Makefile과 같은 POSIX/시간 매크로와 `-pthread` 설정을 적용한다. 정적 라이브러리 방식으로 연결할 때는 `src/ncr_sync.h`와 `libncrsync.a`를 사용한다.
