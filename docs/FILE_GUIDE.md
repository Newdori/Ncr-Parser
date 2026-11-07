**파일별 기능과 기존 프로그램 연결 위치**

이 모듈의 외부 진입점은 `include/ncr_sync.h`다. 호출자는 하나의 인스턴스를 생성하고 GPS 수신 스레드와 RLE 수신부에서 공유한다. 모듈 내부에서 수신 스레드를 만들지 않는다.

```text
기존 GPS 수신 스레드: GpsTime_t 수신
    → UpdateGpsTime()                 [ncr_sync_service.c]
    → 필드/날짜 검증·epoch 변환         [ncr_gps_adapter.c]
    → 연속성·상태·오차·보정 여부 판단    [ncr_time_state.c]
    → 필요 시 Linux 클록 보정           [ncr_platform_linux.c]

기존 RLE 수신부: 패킷 최초 도착
    → CaptureNcrReceiveContext()       [ncr_sync_service.c]
    → 문맥을 패킷과 함께 보관
    → 실제 파서가 Encryption_Context 3바이트 추출
    → RestoreNcr24()                  [ncr_sync_service.c]
    → ReconstructNcr24()              [ncr_restore.c]
    → NCR32 + 송신 날짜 + 시간 품질 반환
```

**운영 모듈 파일**

| 파일 | 구체적인 기능 | 주요 함수/타입 | 기존 프로그램에서의 사용 |
|---|---|---|---|
| `include/ncr_gps_time.h` | `ucHour`가 추가된 GPS 시간 구조체 정의. 기존 호스트 타입을 포함할 수 있는 헤더 경계 제공 | `GpsTime_t`, `NCR_GPS_TIME_HEADER` | 기존 수신부가 파싱한 값을 이 계약으로 전달. 네트워크 버퍼 직접 캐스팅 용도 아님 |
| `include/ncr_sync.h` | 공개 설정, 상태/오류 enum, 시간 품질, 수신 문맥, 복원 결과 및 함수 선언 | `NcrConfig_t`, `NcrSync_t`, `NcrReceiveContext_t`, `NcrRestoreResult_t` | 호스트가 include하는 주 헤더 |
| `src/ncr_config.c` | 기본 설정 생성과 범위·설정 조합 검증. 정수 절댓값 크기와 포화 카운터 증가 헬퍼 | `GetNcrDefaultConfig()`, `ValidateNcrConfig()` | 초기화 때 설정을 만들고 검증. 잘못된 횟수·모델·지연·불확실성 조합 거절 |
| `src/ncr_gps_adapter.c` | 날짜/윤년·시분초·32비트 ms·Clock Status 검사. 원천 달력을 pseudo-Unix ms로 변환하고 결과 날짜 계산 | `ValidateGpsTime()`, `ConvertGpsTimeToSourceMs()`, 내부 날짜 변환 | GPS 입력 검증과 송신 날짜 반환에 사용. `localtime()`이나 KST에 의존하지 않음 |
| `src/ncr_time_state.c` | 초기 획득, 연속 샘플 확인, 원천 상태, HOLDOVER 제한, 재획득, 오차/드리프트/불확실성 판단 | `ObserveNcrTime()`, `EvaluateNcrQuality()` | GPS 갱신 때 내부 호출. READY/HOLDOVER/STALE 등과 복원 허용 여부 결정 |
| `src/ncr_restore.c` | 수신 NCR 하위 24비트에 맞는 송신 초 후보 검사. 자정·월말·연말·24/32비트 순환을 고려해 32비트 NCR과 송신 날짜 반환 | `ReconstructNcr24()` | 서비스 API에서 호출하는 순수 계산 코어. OS 시간 변경이나 AES 호출 없음 |
| `src/ncr_sync_service.c` | 인스턴스 할당/해제, mutex 보호, 외부 API, 클록 점프 감지, 실제 Step 실행 절차, 수신 문맥과 큐/정책 세대 검사, 통계 집계 | `CreateNcrSync()`, `UpdateGpsTime()`, `CaptureNcrReceiveContext()`, `RestoreNcr24()`, `GetNcrSyncStatus()`, `ResetNcrSync()`, `DestroyNcrSync()` | 실제 프로그램이 호출하는 함수의 구현. 복원 결과를 사용하기 전에 반환값 검사 |
| `src/ncr_platform_linux.c` | Ubuntu/Linux의 REALTIME/MONOTONIC 쌍 읽기. 설정 허용 시 `clock_settime()` 호출과 권한 오류 반환. 64비트 `time_t` 검사 | `ReadLinuxClock()`, `SetLinuxClock()`, `GetNcrDefaultPlatform()` | 기본 Linux 어댑터. 일반 호스트는 직접 호출하지 않음 |
| `src/ncr_status.c` | enum 상태/오류를 로그용 고정 문자열로 변환 | `GetNcrStatusText()`, `GetNcrRestoreStatusText()`, `GetNcrStateText()` | 호스트 로그/진단 출력에 사용. 모듈 자체가 stdout에 로그를 쓰지는 않음 |
| `src/ncr_internal.h` | 비공개 인스턴스, mutex, 내부 시간 상태, 상수와 모듈 간 함수 선언 | `struct NcrSync`, `NcrTimeState_t` | 라이브러리 내부 전용. 호스트에서 include하지 않음 |
| `examples/example_gps_callback.c` | 초기화, 기존 GPS 수신 직후 호출, RLE 최초 수신 캡처, 파싱 후 복원, 종료 순서 예시 | `InitializeGatewayClock()`, `HandleReceivedGpsTime()`, `CaptureRlePacketTime()`, `HandleParsedEncryptionContext()`, `ReleaseGatewayClock()` | 필요한 호출 위치를 기존 프로그램에 옮기는 기준. 독립 서버나 실제 전체 RLE 파서 아님 |

**시험 파일**

| 파일 | 기능 | 확인하는 문제 |
|---|---|---|
| `tests/test_ncr_sync.c` | 입력/설정 검증, 달력 변환 36,525일, 하루 전체 345,600건 NCR 복원, 날짜/순환/불확실성 경계 시험 | NCR·날짜 계산 정확성과 잘못된 입력 거절 |
| `tests/test_ncr_state.c` | 초기 HOLDOVER, UNSYNCED 전환, 지속 HOLDOVER 만료, 재획득, Step 확인/실패, 큐/문맥, 시간 오차 시험 | 운영 중 시간 상태의 올바른 변화 |
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
| `Makefile` | Linux 소스 7개로 정적 라이브러리 생성. C11/pthread/64비트 시간 설정, 헤더 의존성, 시험·예제·sanitizer 타깃 제공 |
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

운영 프로그램에 필요한 것은 `include/`와 빌드한 `libncrsync.a`다. 기존 빌드 시스템에 소스를 직접 넣는 경우에는 Makefile에 명시한 `src/*.c` 7개와 `src/ncr_internal.h`를 가져오고 같은 POSIX/시간 매크로와 `-pthread` 설정을 적용한다. tests와 examples를 운영 프로그램의 필수 소스로 함께 링크할 필요는 없다.
