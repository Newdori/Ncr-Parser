**Ubuntu Linux용 GPS 수신 이벤트 기반 NCR 모듈**

기존 GPS 수신 스레드가 시간 데이터를 받을 때마다 `UpdateGpsTime()`을 호출하는 C11 정적 라이브러리다. 모듈은 수신 스레드나 타이머를 생성하지 않는다. Ubuntu의 POSIX 클록과 pthread를 사용하며, 빌드 결과는 Linux용 `build/libncrsync.a`다.

빌드 구성은 Ubuntu 22.04/24.04와 같은 glibc 기반 Linux, GCC 또는 Clang, GNU make, 64비트 `time_t`를 기준으로 한다. 기본 사용 환경은 64비트 Ubuntu다. 이 환경에서 실제 빌드·시험을 수행했는지는 `docs/VALIDATION.md`를 확인한다. Windows 어댑터·PowerShell 빌드·Windows 바이너리는 현재 프로젝트에 포함하지 않는다.

**Ubuntu에서 빌드**

필수 도구는 Ubuntu의 `build-essential`로 준비할 수 있다. GCC, G++, libc 개발 헤더와 make가 의존성에 포함된다. [Ubuntu 공식 패키지 정보](https://packages.ubuntu.com/noble/build-essential)

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential

# 프로젝트 폴더에서 실행
sh ./build_ubuntu.sh test
```

기본 명령은 정적 라이브러리, 전체 시험 실행 파일과 호출 예제를 빌드하고 시험한다. 빌드·시험에는 root나 CAP_SYS_TIME이 필요하지 않다. `sudo make`로 실행할 필요가 없다.

```sh
make                     # build/libncrsync.a 생성
make test example        # 전체 시험 실행 + 호출 예제 컴파일
sh ./build_ubuntu.sh build
sh ./build_ubuntu.sh test
make clean
```

호스트 프로젝트에는 `src/ncr_sync.c`와 `src/ncr_sync.h`를 함께 복사한다. 기존 C 프로그램과 직접 컴파일할 수 있다.

```sh
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64 \
    -Isrc your_app.c src/ncr_sync.c -pthread -o your_app
```

정적 라이브러리 방식으로 연결할 때는 위 컴파일 명령의 `src/ncr_sync.c` 자리에 `build/libncrsync.a`를 넣는다. 기본 `make`는 운영용 라이브러리를 만들며, `make test`의 C++ 연결 시험은 G++를 사용한다.

Clang과 sanitizer 시험은 필요한 도구를 Ubuntu에 설치한 뒤 실행한다.

```sh
# Clang을 사용할 경우에만 준비
sudo apt-get install --no-install-recommends clang
make CC=clang CXX=clang++ BUILD=build-clang test example

make asan     # ASan + UBSan, build-asan
make ubsan    # UBSan, build-ubsan
make tsan     # 동시성 시험, build-tsan
```

`build_ubuntu.sh`는 Linux 실행 환경을 확인한다. Makefile은 대상 컴파일러의 `__linux__`와 64비트 `time_t`를 확인한다. Windows 컴파일러로 이름만 `.a`인 파일이 생성되어 Ubuntu용으로 오인되지 않게 한다. `_TIME_BITS=64`는 `_FILE_OFFSET_BITS=64`와 함께 지정한다. [glibc 공식 설명](https://sourceware.org/glibc/manual/latest/html_node/Feature-Test-Macros.html)

`build_ubuntu.sh`와 Makefile은 LF 줄바꿈·UTF-8을 사용한다. ZIP의 실행 권한이 보존되지 않은 경우에도 `sh ./build_ubuntu.sh test`로 실행할 수 있다. 프로젝트 경로에 공백이 있어도 스크립트는 자신의 디렉터리로 이동한다. `BUILD`를 변경할 때에는 공백 없는 상대 디렉터리 이름을 사용한다.

**파일 구조와 먼저 볼 파일**

```text
src/ncr_sync.h             GPS 입력, 공개 API, 설정, 이벤트 시각, 복원 결과
src/ncr_sync.c             입력 검증, 시각 처리, 이벤트 기록, NCR 복원
examples/example_gps_callback.c  기존 GPS/RLE 수신부 연결 예제
tests/                     단위·경계·상태·동시성·Linux 클록 시험
Makefile                   Ubuntu/GCC/Clang 빌드
build_ubuntu.sh            빌드/시험 실행 진입점
docs/FILE_GUIDE.md          모든 파일의 역할과 호출 관계
docs/VALIDATION.md          수행·미수행 검증 구분
```

먼저 `src/ncr_sync.h`에서 공개 계약을 확인하고, `examples/example_gps_callback.c`의 호출을 기존 수신부에 연결한다. 주요 함수의 역할은 `docs/FILE_GUIDE.md`에서 확인할 수 있다.

**GPS 입력 계약**

```c
typedef struct GpsTime {
    uint16_t usYear;
    uint8_t ucMonth;
    uint8_t ucDay;
    uint8_t ucHour;
    uint8_t ucMin;
    uint8_t ucSec;
    uint32_t uiMillisecond;
    uint8_t ucClockStatus;
} GpsTime_t;
```

- 연도 2000~2099, 유효한 월/일, 시 0~23, 분/초 0~59를 수용한다. 윤초 60초 표기는 지원하지 않는다.
- `uiMillisecond`는 초 안의 0~999 ms다. 원래 32비트 값에서 검사한다.
- `ucClockStatus`는 `GPS_SYNC=1`, `HOLDOVER=2`, `UNSYNCED=3`이다. `UpdateGpsTime()`은 `UNSYNCED`의 상태를 먼저 반영하며 이 메시지의 날짜와 시각 필드는 사용하지 않는다.
- 구조체는 호스트에서 파싱한 데이터다. 패딩과 바이트 순서가 있으므로 네트워크 버퍼를 그대로 캐스팅하지 않는다.
- 입력 버퍼는 API 호출 중 안정적이어야 한다. 다른 스레드가 수정할 수 있으면 호스트에서 잠금 아래 복사한 뒤 넘긴다. 라이브러리는 입력 포인터를 보관하지 않는다.

기존 프로그램에 GPS 타입이 이미 있으면 그 타입을 포함하는 어댑터 헤더에서 `GpsTime_t`를 정의한다. 라이브러리와 호스트 전체를 동일한 `NCR_GPS_TIME_HEADER`로 다시 빌드해야 한다. `tests/host_gps_adapter.h`와 `tests/test_host_gps.c`가 예시다.

**기존 프로그램 연결 순서**

1. 시작할 때 `GetNcrDefaultConfig()` 결과를 장비에 맞게 수정하고 `CreateNcrSync()`로 인스턴스를 한 번 생성한다.
2. 기존 GPS 수신 스레드에서 1초마다 시간 데이터를 받는 즉시 `UpdateGpsTime()`을 호출한다. 결정 결과와 시간 품질을 확인한다.
3. RLE 패킷 최초 수신 시 `CaptureNcrReceiveContext()`를 호출한다. 문맥을 해당 패킷과 같이 큐/재조립 단계에 보관한다.
4. 실제 RLE 파서가 3바이트 `Encryption_Context`를 추출한 후 `RestoreNcr24()`에 인스턴스·그 패킷의 문맥·3바이트를 넘긴다.
5. `NCR_RESTORE_OK`일 때만 NCR32와 송신 날짜를 IV 생성부에 넘긴다. 실패 시 출력은 0으로 초기화된다.
6. 종료할 때 GPS/RLE 호출 스레드를 먼저 중지·join하고 `DestroyNcrSync()`를 호출한다.

`CaptureNcrReceiveContext()`와 `GetNcrSyncStatus()`가 OK를 반환하는 것은 캡처/조회 성공이다. 현재 복원 가능 여부는 `stQuality.iUsability`로 구분한다. 초기 동기화 전에도 시각 캡처 자체는 성공할 수 있다.

인스턴스 상태와 내부 시간 보정은 mutex로 보호한다. 복원 계산에는 수신 당시 품질을 사용하고, 계산 전후 큐 기한과 정책 세대를 검사한다. 기존에 캡처한 문맥은 정상적인 내부 Step 후에도 기한 내 사용 가능하다. `ResetNcrSync()`는 정책 세대를 변경하여 보관 문맥을 무효화한다. 호출자가 종료하기 전 인스턴스와 사용자 플랫폼 context를 해제하면 안 된다.

**동기 상태와 설정**

| GPS 상태 | 복원에 적용하는 기준 |
|---|---|
| `GPS_SYNC` (1) | 유효 샘플을 연속 확인해 GPS 기준을 획득한다. 기본 3회이며 시각 Step 옵션을 활성화하면 이 상태에서 보정을 허용한다. |
| `HOLDOVER` (2) | 마지막 검증 기준을 정해진 시간과 오차 범위에서 사용한다. 기준이 없거나 허용 시간이 지나면 Ubuntu 시각 기준으로 전환한다. |
| `UNSYNCED` (3) 또는 미수신 | GPS 기준 보정을 중단하고 Ubuntu의 `CLOCK_REALTIME`으로 NCR 복원을 계속한다. GPS_SYNC가 돌아오면 연속 샘플 검증으로 기준을 다시 획득한다. |

`stQuality.bUbuntuFallback`이 참이면 Ubuntu 시각과 `uiMaxUncertaintyMs`를 후보 검색에 사용한다. 이 때 Ubuntu 시간 동기화가 실제 시스템 오차를 해당 한계 안에 유지해야 한다. 날짜를 포함한 절대 시각은 `CLOCK_REALTIME`에서 얻고, 미수신과 HOLDOVER 경과시간은 `CLOCK_MONOTONIC`으로 계산한다.

`GetNcrSyncStatus()`의 `stGpsEvents`에는 최근 GPS_SYNC 해제 시각, 미수신 감지 시각, 수신 재개 시각을 Ubuntu UTC 밀리초로 보관한다. 각 시각의 초기값은 `NCR_GPS_EVENT_NOT_RECORDED_MS`이다. GPS_SYNC 해제는 상태 1에서 2 또는 3으로 바뀐 수신 시점이다. 미수신은 최초 시각 조회 이후부터 감시하며, 기한을 넘긴 뒤 API가 시각을 확인한 시점에 기록한다. 다음 유효 메시지가 도착하면 수신 재개 시각을 기록한다. 통신 재개와 GPS 동기 획득은 별도로 판정한다.

| 주요 설정 | 기본값 | 의미 |
|---|---:|---|
| `bAllowClockStep` | false | 실제 시스템 시각 변경 허용 |
| `iSourceMinusUtcSec` | 0 | 원천 시각-UTC 차이, UTC 입력이면 0 |
| `uiTimeMessageDelayMs` | 0 | 원천 측정→수신 대표 지연 |
| `uiAcquireSamples` / `uiConfirmSteps` | 3 / 3 | 획득 / Step 확인 횟수 |
| `uiStepThresholdMs` / `uiMaxSampleJitterMs` | 500 / 250ms | 보정 후보 오차 / 연속성 허용 오차 |
| `uiGpsMessageTimeoutMs` | 3000ms | 마지막 유효 메시지 이후 미수신 판단 기한 |
| `uiMaxSourceHoldoverMs` | 300000ms | 원천 HOLDOVER 지속 한계 |
| `uiMaxLocalHoldoverMs` | 300000ms | 마지막 검증 GPS_SYNC 기준 나이 한계 |
| `uiMaxClockOffsetMs` / `uiMaxUncertaintyMs` | 2000 / 2000ms | 미보정 오차 / 전체 불확실성 한계 |
| `uiSourceUncertaintyMs` / `uiReceiveUncertaintyMs` | 1 / 1ms | 원천 / 수신 측정 오차 예산 |
| `uiDelayUncertaintyMs` / `uiLocalDriftPpm` | 0ms / 100ppm | 지연 잔차 / 로컬 드리프트 상한 가정 |
| `uiMaxLinkDelayMs` | 5000ms | NCR 샘플링부터 RLE 수신까지의 최대 지연 |
| `uiMaxQueueAgeMs` | 5000ms | 수신 문맥 처리 기한 |
| `uiMaxClockJumpMs` | 1000ms | 외부 REALTIME 점프 판정 |
| `uiMaxReacquireOffsetMs` | 60000ms | 자동 획득/재획득 절대 오차 보호 |

기본값은 개발 시험용이다. 장비 정확도, 메시지/링크 지연, 발진기 드리프트를 실제 사양과 측정으로 설정한다. 원천이 GPS Time이면 원천-UTC 차이와 윤초 정책도 장비와 합의해야 한다. KST 등 로컬 타임존은 NCR 날짜 계산에 섞지 않는다.

큰 절대 오차는 RESET_REQUIRED로 래치한다. 설정과 원천을 확인한 뒤 reset으로 다시 획득하지만 reset이 오차 보호를 우회하지는 않는다. 큰 부팅 시각 오차는 시스템 초기 시각 또는 명시적인 허용 범위 설정을 먼저 준비한다.

**Linux 시간 보정과 NCR 범위**

기본 어댑터는 `CLOCK_REALTIME`과 `CLOCK_MONOTONIC`을 읽고, 명시적으로 Step을 허용했을 때만 `clock_settime()`을 호출한다. 시간 변경을 활성화하는 운영 프로세스에는 CAP_SYS_TIME 등 필요한 권한이 있어야 한다. 정상 빌드·단위 시험에는 필요하지 않다. 실제 시스템의 시간 보정 주체는 하나로 정한다.

Step은 GPS_SYNC 획득과 일관된 오차 확인을 충족해야 한다. HOLDOVER/UNSYNCED, 중복·오류·긴 간격·오차 부호 변경은 확인 횟수를 끊는다. 적용 후 MONOTONIC 경과를 제외해 보정량을 계산한다. 성공 후 재측정이 실패하면 적용과 검증을 구분해서 반환하고 재획득한다.

NCR 모델은 송신 단말이 시각에서 NCR을 만드는 규칙이다. 이 구현은 자정 이후 초 수에 90kHz를 곱하는 규칙을 사용한다.

```text
NCR32 = ((Hour * 3600 + Min * 60 + Sec) * 90000) mod 2^32
Context24 = NCR32 & 0x00FFFFFF
```

NCR은 자정에 초기화하며 초 단위로 계산한다. 불확실성 U와 최대 링크 지연 D를 사용해 `[수신시각-U-D, 수신시각+U]`와 겹치는 송신 초 후보를 검사한다. 후보가 유일할 때 성공한다.

실제 단말의 NCR 생성 규칙이 다르면 모델을 추가해야 한다. 실제 RLE 파서·IV 바이트 배치·AES 복호화는 별도 연동 대상이다. 같은 초의 여러 패킷에서 키/전체 IV가 반복되는지도 최종 암호 연동 시 확인한다.

코딩 규칙은 `docs/CODING_RULES.md`와 `docs/CODING_RULES_PROMPT.txt`에 보관했다. API와 타입은 `src/ncr_sync.h`에서 확인할 수 있다.
