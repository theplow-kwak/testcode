필수 준비
시스템에 libnvme-dev 설치:

bash
복사
편집
sudo apt-get install libnvme-dev
또는 직접 빌드:

bash
복사
편집
git clone https://github.com/linux-nvme/libnvme.git
cd libnvme
meson setup build
ninja -C build
sudo ninja -C build install

고급 구조 설계도
pgsql
복사
편집
Main
 ├── Open Controller
 ├── For each thread
 │    ├── Create Submission/Completion Queue
 │    ├── Issue Copy commands (QDepth 만큼 발행)
 │    └── Wait for completions
 ├── Aggregate performance
 └── Close Controller


# 1단계 - 전체 프로젝트 스캐폴딩
파일 구조
복사
편집
fdp_copy_super_benchmark/
├── Makefile
├── fdp_copy_super_benchmark_libnvme_allin.c
└── README.md

전체 구조 정리

파트	설명
스레드 생성	각 thread가 독립적으로 Copy IO 발행
메인 대기	모든 thread join() 후 결과 집계
Throughput 계산	MB/s, IOPS 모두 출력
CSV 저장	각 요청의 latency 기록
최종 출력	전체 성능 총 정리

명령어 예시
bash
복사
편집
sudo ./fdp_copy_super_benchmark_libnvme_allin \
    -d /dev/nvme0n1 -n 1 -q 16 -T 4 -s 0 -t 1000000 -r -a -f result.csv
4개 스레드

각각 QDepth=16

랜덤 LBA 활성화

CPU Affinity 적용

결과는 result.csv 저장