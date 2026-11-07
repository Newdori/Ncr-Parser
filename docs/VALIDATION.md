**Ubuntu 전용 구성 변경 및 검증 범위**

작성일: 2026-11-07 (Asia/Seoul)

Ubuntu/Linux 전용 빌드로 정리했다. Windows 어댑터, PowerShell 빌드 스크립트, Windows 빌드 산출물과 Windows 시험 로그를 현재 프로젝트에서 제외했다. 기존 버전은 변경 전 별도 작업 백업에 보관했다.

변경 사항은 다음과 같다.

- Makefile에 Linux 소스 7개를 명시하고 대상 컴파일러가 `__linux__`를 정의하는지 검사한다.
- C11, pthread, `_POSIX_C_SOURCE=200809L`, `_FILE_OFFSET_BITS=64`, `_TIME_BITS=64`를 적용한다. `time_t`가 64비트인지 컴파일 시 확인한다.
- `.d` 헤더 의존성을 생성하고 C/C++와 sanitizer 컴파일/링크 옵션을 분리했다.
- Ubuntu 빌드·시험 진입점 `build_ubuntu.sh`와 실제 Linux 클록을 확인하는 `test_ncr_linux.c`를 추가했다.
- Ubuntu 준비·실행 방법과 모든 파일의 역할을 문서화했다.

현재 실행 호스트는 Windows이며 사용 가능한 Ubuntu/WSL 또는 Docker 실행 환경이 없다. 따라서 **현재 Ubuntu 전용 버전의 Linux 컴파일·링크·테스트 실행은 아직 수행하지 못했다.** 기존 Windows 시험 통과를 Ubuntu 시험 통과로 표시하지 않는다.

이번에 수행한 확인은 shell 구문 검사, 빌드 스크립트의 잘못된 OS 차단, 스크립트 인자/경로 전달 시험(명령 대역 사용), Makefile dry-run 및 Linux 소스 목록·매크로·시험 타깃 확인, 변경된 C 파일의 코딩 규칙 검토, 소스 패키지 무결성 확인이다. 구체적인 검사 출력은 `build_config_checks.txt`에 기록한다. 명령 대역을 사용한 스크립트 시험은 Linux 컴파일러 실행을 의미하지 않는다.

시간 상태·달력·복원 알고리즘과 공개 API는 이전 버전에서 유지했다. 이전 버전은 Windows GCC/Clang에서 345,600건 복원, 36,525일 달력, 상태·장애·동시성·C++ 연결·호스트 GPS 어댑터 시험을 통과했다. 이는 변경 이력으로만 기록하며 현재 Linux 경로의 검증 결과와 구분한다.

Ubuntu에서 수행할 순서는 다음과 같다.

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential
sh ./build_ubuntu.sh test
make asan
make ubsan
make tsan
```

`make test`는 코어, 상태, 동시성, 실제 Linux 클록, C++ 연결, 기존 GPS 타입 어댑터 시험을 실행한다. `make example`은 호스트 연결 예제를 컴파일한다. 전체 시험은 root 권한을 요구하지 않으며 실제 시스템 시간을 변경하지 않는다. Step 시험에는 가상 클록을 사용한다.

실제 GPS 시간척도·윤초·상태 값·지연, 위성 링크, 단말 NCR 모델, RLE 추출 위치, IV/AES 및 장기 운용은 별도 장비 통합 검증 항목이다.
