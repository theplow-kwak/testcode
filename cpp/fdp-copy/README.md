# 필수 준비

시스템에 `libnvme-dev` 설치:

```
bash


복사편집
sudo apt-get install libnvme-dev
```

또는 직접 빌드:

```
bash복사편집git clone https://github.com/linux-nvme/libnvme.git
cd libnvme
meson setup build
ninja -C build
sudo ninja -C build install
```

# 고급 구조 설계도

```
pgsql복사편집Main
 ├── Open Controller
 ├── For each thread
 │    ├── Create Submission/Completion Queue
 │    ├── Issue Copy commands (QDepth 만큼 발행)
 │    └── Wait for completions
 ├── Aggregate performance
 └── Close Controller
```

# 1단계 - 전체 프로젝트 스캐폴딩

## 파일 구조

```
복사편집fdp_copy_super_benchmark/
├── Makefile
├── fdp_copy_super_benchmark_libnvme_allin.c
└── README.md
```

# 명령어 예시

```
bash복사편집sudo ./fdp_copy_super_benchmark_libnvme_allin \
    -d /dev/nvme0n1 -n 1 -q 16 -T 4 -s 0 -t 1000000 -r -a -f result.csv
```

- 4개 스레드
- 각각 QDepth=16
- 랜덤 LBA 활성화
- CPU Affinity 적용
- 결과는 `result.csv` 저장





## 예시 실행

```
bash복사편집sudo ./fdp_copy \
  --threads 4 --qdepth 8 \
  --range 4 --count 100 \
  --src-lba 0 --dst-lba 100000 \
  --random --quiet
```

------

## 