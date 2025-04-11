## Disk Partition Manager

### ✅ 예시 명령어: 디스크 초기화 + 파티션 생성 + 포맷 + 마운트

```bash
python3 disk_manager.py \
  --disk /dev/sdx \
  --tool parted \
  --table gpt \
  --partitions '[{"start": "1MiB", "end": "512MiB", "type": "primary"}, {"start": "512MiB", "end": "100%", "type": "primary"}]' \
  --format \
  --fstype ext4 \
  --blocksize 4096 \
  --mount
```

### ✅ `--wipe` 테스트 (기존 파티션 모두 삭제 후 새 파티션 생성)

```bash
python3 disk_manager.py \
  --disk /dev/sdx \
  --tool fdisk \
  --wipe \
  --table msdos \
  --partitions '[{"start": "2048", "end": "+500M"}, {"start": "", "end": ""}]' \
  --format \
  --fstype xfs \
  --mount
```

> 💡 `--wipe` 옵션은 기존 파티션 데이터를 모두 삭제합니다. 꼭 테스트 디스크에서만 사용해 주세요!

## backup_restore

### 사용 예시

- **백업**:

  ```bash
  python backup_restore.py backup /dev/sdb sdb_backup.qcow2
  ```

- **복원**:

  ```bash
  python backup_restore.py restore sdb_backup.qcow2 /dev/sdb
  ```



## 디스크 벤치마크

### **스크립트: `disk_benchmark_advanced.sh`**

#### **사용 예시**

```bash
./disk_benchmark_advanced.sh --disk /dev/sdb --size 1024 --bs 512K --mode randread --graph bw --outdir results_sdb
```

------

#### **결과 요약**

- `results_sdb/report.csv`: 전체 벤치마크 기록 (CSV)
- `results_sdb/qcow2.img`, `raw.img`: 테스트 이미지
- `results_sdb/perf.png`: 그래프 출력 (IOPS or MB/s)
- `results_sdb/fio.log`, `qemu-bench.log`: 상세 로그

------

### **Python 스크립트**

#### **Python 벤치마크 툴 구조**

```
disk_benchmark/
├── benchmark.py           # 메인 벤치마크 로직
├── analyzer.py            # 결과 분석 및 압축률, 속도 계산
├── visualizer.py          # matplotlib/plotly 기반 시각화
├── cli.py                 # argparse 기반 CLI
├── utils.py               # 공통 함수 모음 (타이머, 로그 등)
├── results/
│   └── (이미지, CSV, JSON 등 저장)
```

------

#### **기능 요약**

- `benchmark.py`: `fio`, `dd`, `qemu-img` 실행 및 결과 저장
- `analyzer.py`: raw/qcow2 용량, 속도, 압축률 계산
- `visualizer.py`: IOPS/BW 그래프 생성
- `cli.py`: `--disk`, `--size`, `--mode`, `--outdir`, `--graph` 등 파라미터 지원
- `results/`: 결과 파일 및 리포트 저장

#### 사용 방법 (예시)

```bash
python cli.py --disk /dev/sdb --size 1024 --bs 512K --mode randread --graph bw --outdir results_sdb
```

#### 생성되는 파일들

- `results_sdb/report.csv`: CSV 리포트
- `results_sdb/perf.png`: 그래프 (IOPS 또는 BW)
- `results_sdb/raw.img`, `qcow2.img`: 이미지 파일
- `results_sdb/fio.log`, `bench.log`: 상세 로그



## 부팅하지 않고 파티션 사이즈 줄이는 방법?

### 왜 부팅이 필요한가?

1. **파일시스템 정리(resize)**:
   - `resize2fs`, `xfs_growfs` 같은 도구는 **파일시스템이 마운트되어 있지 않아야** 작동해.
   - 루트 파티션은 일반적으로 마운트된 상태라서 줄이려면 live 환경에서 해야 해.
2. **파티션 테이블 수정**:
   - `parted`, `fdisk`, `gparted` 같은 도구도 부팅된 상태에선 디스크 변경이 제한됨.
   - 줄이고 나서 파티션 테이블도 수정해야 하므로 OS 밖에서 작업하는 게 안전함.

------

### 가능한 예외 (비루트 파티션일 때):

만약 **루트가 아닌 데이터용 파티션이라면**, 아래 순서로 **부팅 없이도 가능**할 수도 있어:

1. 해당 파티션을 **unmount**:

   ```bash
   sudo umount /dev/nvme0n1p3
   ```

2. **fsck로 오류 검사**:

   ```bash
   sudo e2fsck -f /dev/nvme0n1p3
   ```

3. **resize2fs로 줄이기** (예: 230G로):

   ```bash
   sudo resize2fs /dev/nvme0n1p3 230G
   ```

4. **parted나 fdisk로 파티션 테이블 줄이기**:

   ```bash
   sudo parted /dev/nvme0n1
   ```

하지만 여기서도 실수하면 **데이터 손상 위험이 있어서** 거의 대부분은 **Live 환경에서 하는 걸 추천**해.

------

### 결론

- **루트 파티션이라면 반드시 부팅해서 줄여야 함** (Live ISO나 qcow2 이미지 부팅).
- **데이터 파티션이라면** 마운트 해제 상태에서 줄이는 것도 가능하긴 함.