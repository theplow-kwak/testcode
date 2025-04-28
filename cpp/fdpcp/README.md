# FDP Copy Tool

This project contains basic and advanced tools for testing NVMe FDP Copy commands using direct admin passthrough.

## Files

- `fdp_copy_basic.c`  
  Basic example that sends a single FDP Copy command with 1 descriptor.

- `fdp_copy_stress_tool.c`  
  Advanced stress tool that:
  - Chains PRP Lists for large copy descriptor tables
  - Runs multi-threaded copy jobs
  - Verifies copy correctness automatically
  - Measures and displays latency histogram

## Build

```bash
make
```


# 4. **정리된 프로그램들**

## 4.1. fdp_copy_basic.c  
→ **초기 단일 Copy 명령어 프로그램**  
(조금 전에 보여줬던 그대로.)

## 4.2. fdp_copy_stress_tool.c  
→ **고급 PRP 체이닝 + 스레드 + 자동 검증 + 통계 프로그램**  
(조금 전에 보여줬던 고급버전 그대로.)

---

# 5. 전체 요약

| 파일명                 | 설명                                                      |
|:------------------------|:-----------------------------------------------------------|
| fdp_copy_basic.c         | 단일 Copy 명령 테스트용                                     |
| fdp_copy_stress_tool.c   | 대규모 Copy 스트레스, 복구, 검증, 지연 측정                |
| Makefile                | 빌드 자동화                                                |
| README.md               | 프로젝트 설명서                                            |

---

# 6. 추가 제안 (선택사항)

추가로 이런 것도 가능해:
- Copy 실패시 **자동 재시도(retry n번)** 기능
- Copy 완료 후 **CSV 로그 파일 저장** (`time, latency, status` 등)
- **fdp_copy_benchmark_tool.c** 따로 만들어서 대량 Throughput 측정

---

## Usage
sudo ./fdp_copy_basic /dev/nvme0
sudo ./fdp_copy_stress_tool /dev/nvme0
