# Ubuntu/Debian
sudo apt-get install liburing-dev
# Fedora/CentOS
sudo dnf install liburing-devel

# C++20 표준과 liburing 라이브러리를 링크하여 컴파일합니다.
g++ -std=c++20 -O2 -Wall coro_copy.cpp -o coro_copy -luring

# 1GB 크기의 테스트 파일을 생성합니다.
dd if=/dev/urandom of=test.in bs=1M count=1024

# 프로그램을 실행하여 파일을 복사합니다. (infile outfile filesize_mb)
./coro_copy test.in test.out 1024

## 주요 변경 사항 및 코루틴 설계
관심사의 분리 (Separation of Concerns)

Awaitable (io_awaitable): io_uring의 저수준 상세 구현을 캡슐화합니다. 코루틴 코드에서는 co_await 한 줄로 I/O를 요청할 뿐, SQE를 직접 다루지 않습니다.

코루틴 (read_and_write_block): "블록 하나를 읽고 쓴다"는 비즈니스 로직에만 집중합니다. 코드가 간결하고 명확해집니다.

이벤트 루프 (copy_file_coro 내부): io_uring에 작업을 제출하고 완료를 기다려 적절한 코루틴을 깨우는 '엔진'의 역할을 합니다. 기존 코드에서는 이 로직이 main 함수와 뒤섞여 있었습니다.

상태 관리

기존 코드의 전역 변수 inflight와 복잡한 while 조건문이 copy_file_coro라는 하나의 조율자 함수 안으로 깔끔하게 정리되었습니다.

각 I/O 작업에 필요한 상태(버퍼, iovec 등)는 request 구조체에 담겨 Awaitable 객체와 함께 관리되므로, 전역 상태 없이 각 작업을 독립적으로 추적할 수 있습니다.

가독성과 유지보수

read_and_write_block 함수를 보면, co_await reader; 다음 co_await writer;가 오는 순차적 코드로 비동기 흐름을 쉽게 이해할 수 있습니다. 이는 콜백(callback) 기반 코드나 복잡한 상태 머신에 비해 월등히 높은 가독성을 제공합니다.

오류 처리가 try-catch 블록으로 자연스럽게 통합되어 특정 작업에서 발생한 예외를 명확하게 처리할 수 있습니다.

이 코루틴 기반 구조는 비동기 로직을 훨씬 더 체계적으로 관리할 수 있게 해주며, 복잡한 I/O 파이프라인을 구축할 때 그 장점이 더욱 빛을 발합니다.

