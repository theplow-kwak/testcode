# FDP (Flexible Data Placement) copy

- **FDP**는 NVMe 2.0 이후에 추가된 새로운 기능이야.
- Flexible Data Placement = 데이터 저장을 좀 더 유연하게 하겠다는 거야.
- 여기서 나오는 **FDP copy**는, 특정한 FDP 리소스 영역 (예를 들면 특정 Placement ID를 가진 공간) 안에서 **데이터를 복사하거나 이동**하는 거야.
- **FDP 복사**는 보통 **Placement ID**나 **Stream ID** 같은 추가적인 메타데이터를 다뤄야 해.
- 또 복사 시 **Metadata**도 같이 다루는 경우가 많아.



# 요약 비교

| 항목       | nvme-cli copy        | FDP copy                               |
| ---------- | -------------------- | -------------------------------------- |
| 기반       | NVMe 1.4 Copy 명령어 | NVMe 2.0 Flexible Data Placement       |
| 목적       | 단순 LBA 복사        | Placement 영역 내 데이터 복사/이동     |
| 메타데이터 | 거의 없음            | 메타데이터 (PID 등) 중요               |
| 복잡성     | 상대적으로 간단      | 훨씬 복잡하고 컨트롤 필요              |
| 지원 장비  | 대부분 최신 NVMe SSD | FDP 기능 지원하는 특수 디바이스만 가능 |



# 1. FDP Copy란?

**FDP Copy**는 "FDP를 지원하는 NVMe 디바이스"에서,
 특정 **Placement ID (PID)**, 또는 **Namespace Resource** 안에서 데이터를 복사하는 거야.

**FDP Copy Command**의 특징:

- **Admin Command** (Opcode가 따로 있어)
- 일반 `copy`랑 다르게, **Placement ID**, **Metadata**, **FDP-specific parameter**들이 들어가야 해.
- 당연히 디바이스가 **FDP 기능**을 지원해야 성공해.

------

# 2. 기본 동작 흐름

C 코드로 보내려면 이 절차를 따를 거야:

1. `/dev/nvme0` 같은 디바이스 파일 열기
2. `nvme_admin_cmd` 구조체 준비
3. 필요한 필드 (opcode, nsid, metadata, data buffer 등) 채우기
4. `ioctl(fd, NVME_ADMIN_CMD, &cmd)` 호출해서 명령 전송
5. 성공/에러 체크

------

# 3. FDP Copy를 위한 핵심 정보

FDP Copy Command는 NVMe Spec 보면:



| 필드                | 내용                                                         |
| ------------------- | ------------------------------------------------------------ |
| Opcode              | 0xC4 (보통 FDP Copy는 이 번호를 사용함, 디바이스 따라 다를 수 있음) |
| Namespace ID (NSID) | 복사할 namespace 번호                                        |
| PRP1/PRP2           | 복사 명령의 데이터를 담은 버퍼 주소                          |
| CDW10-15            | 상세 파라미터들 (SLBA, Number of LBAs, Placement ID 등)      |

**(※ 주의)**
 디바이스/펌웨어마다 opcode가 다를 수도 있으니 꼭 해당 디바이스 spec sheet를 봐야 해. (기본 예제는 0xC4)

# 6. 요약 흐름 그림

```
scss


복사편집
open("/dev/nvme0") → 준비(nvme_admin_cmd) → ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) → 성공 or 에러 체크
```

------

# 

# 1. FDP Copy용 Copy Descriptor Table 구조

복사할 때 디바이스에 주는 데이터 버퍼 포맷은 이렇게 생겼어:



| 필드                     | 크기 (byte) | 설명                                |
| ------------------------ | ----------- | ----------------------------------- |
| SLBA (Start LBA)         | 8           | 원본 시작 주소                      |
| Number of LBAs (0-based) | 4           | 복사할 블록 수 (0이면 1블록 복사)   |
| Reserved                 | 2           | 예약                                |
| Placement ID             | 2           | 복사할 데이터에 부여할 Placement ID |

**즉, 하나의 Copy Entry는 총 16바이트야!**



# 1. FDP Copy 후 Completion 상태 디코딩 (고급 디버깅)

## 먼저 기본 개념부터

`ioctl()` 호출이 끝나면, `nvme_admin_cmd` 구조체 안에 다음 값들이 채워져 있어:



| 필드     | 설명                               |
| -------- | ---------------------------------- |
| `result` | 명령어 성공 시 반환값 (ex. LBA 등) |
| `status` | 명령의 성공/실패 상태코드 (16비트) |

→ **status 필드**를 분석하면 명령이 성공했는지, 실패했으면 이유가 뭔지 알 수 있어.

### status 필드 포맷

16비트인데 의미는 다음과 같아:



| 비트 | 설명                    |
| ---- | ----------------------- |
| 15   | Phase Tag (무시해도 됨) |
| 14-8 | SCT (Status Code Type)  |
| 7-0  | SC (Status Code)        |

즉,

- **SCT** = 상태코드 종류 (ex. Generic, Command Specific 등)
- **SC** = 실제 에러 종류 (ex. LBA out of range, Invalid Field 등)

# 3. Copy Error 디코딩 테이블

**에러 디코딩 테이블**은 이렇게 만들 수 있어.

우선 많이 나오는 NVMe 표준 상태코드 목록을 참고할게:



| SCT  | SC   | 의미                         |
| ---- | ---- | ---------------------------- |
| 0x0  | 0x00 | 성공 (Successful Completion) |
| 0x0  | 0x01 | Invalid Command Opcode       |
| 0x0  | 0x02 | Invalid Field in Command     |
| 0x0  | 0x04 | Data Transfer Error          |
| 0x0  | 0x05 | Aborted Power Loss           |
| 0x1  | 0x80 | LBA Out of Range             |
| 0x1  | 0x81 | Capacity Exceeded            |
| 0x1  | 0x82 | Namespace Not Ready          |

(FDP 관련은 보통 **SCT=0x1 (Command Specific)** 쪽에서 많이 나와.)

# 1. Vendor-specific Error 디코딩

일부 NVMe SSD는 자기들만의 고유 상태코드를 추가로 사용해.
 (특히 삼성, 인텔, 키옥시아 같은 데.)

일반적으로:

- `SCT = 0x7` (Vendor Specific)
- `SC`는 회사마다 다르게 정의함.

**예를 들어, 삼성 SSD에서는:**



| SC   | 의미                              |
| ---- | --------------------------------- |
| 0x80 | Internal Media Error              |
| 0x81 | Write Amplification Limit Reached |
| 0x82 | Thermal Throttle                  |

# PRP (Physical Region Page) 복습

NVMe는 DMA 전송할 때 메모리 주소를 넘길 때
 **PRP** 라는 포인터를 써.



| 이름 | 설명                                                   |
| ---- | ------------------------------------------------------ |
| PRP1 | 첫 번째 페이지 (4KB) 포인터                            |
| PRP2 | 두 번째 페이지 포인터 or PRP List (추가 페이지 리스트) |

즉:

- 데이터 크기 <= 4KB → PRP1만 사용
- 데이터 크기 > 4KB → PRP1 + PRP2 사용
- **PRP2가 PRP List**로 동작해서 여러 페이지를 체이닝할 수 있어.

> PRP2가 직접 다음 page 주소 or "PRP List" 첫 번째 페이지를 가리킴!

# Copy Command 관점에서는?

Copy Descriptor Table (CDT)이

- **4KB 넘는 크기**가 될 수도 있어.
- 그러면 PRP1으로 첫 4KB,
- PRP2로 나머지 주소 또는 PRP List를 보내야 해.

✅ → 즉, **엔트리 수 많아지면 PRP2까지 세팅해야 정상 작동!**

# 전체 큰 흐름 요약

1. **Copy Descriptor Table** 메모리 할당 (예: 16KB)
2. **PRP1** = CDT 시작 주소
3. **PRP2** =
   - 남은 데이터가 1 page → 그냥 물리주소
   - 남은 데이터가 2 pages 이상 → PRP List 만들어야 함
4. NVMe Admin Command에 PRP1/PRP2 설정
5. Admin-passthru로 전송

#  핵심! PRP List 동작 방식



| PRP1       | PRP2                          | 의미                                  |
| ---------- | ----------------------------- | ------------------------------------- |
| 0x12340000 | 0x56780000                    | PRP1: 첫 페이지, PRP2: 두 번째 페이지 |
| 0x12340000 | 0x9ABC0000                    | PRP2: PRP List 포인터                 |
| (PRP List) | [0x56780000, 0x67890000, ...] | PRP List: 다음 페이지들 주소 나열     |

# 전체 그림 요약

```
css복사편집[ CDT 16KB 준비 ] → [ PRP1: 첫 4KB 주소 ]
                        ↓
                [ PRP2: PRP List 포인터 ]
                         ↓
            [ PRP List: 나머지 페이지 주소들 ]
                         ↓
            [ NVMe Admin Command 전송 ]
```

# 주의사항

- Copy Descriptor Table은 반드시 **Page-Aligned** 되어야 함 (4096 단위)
- PRP List도 별도 **Page-Aligned** 로 메모리 따로 만들어야 함
- 16KB 이상의 대형 CDT는 PRP List가 필수
- PRP List는 1페이지 안에 최대 512개 엔트리 가능 (512 * 8B = 4096B)
- 더 많으면 PRP List 체이닝 (Advanced Topic)

------

# 1. PRP List 체이닝 (Advanced)

**문제**:

- PRP List 하나에 최대 512개 주소만 저장 가능. (4096B/8B = 512개)
- 512개 넘는 페이지 필요하면?
   → **PRP List를 이어서 체이닝 해야 함.**

**체이닝 방식**:

- PRP List 1페이지에 511개 주소 + 마지막에 **다음 PRP List 주소** 적어.
- 반복 반복~ 해서 무제한 연결 가능.

# 체이닝 그림

```
mathematica


복사편집
PRP1 → PRP2(첫 PRP List) → 다음 PRP List → 다음 PRP List → ...
```

각 PRP List 페이지는:



| Index | 내용                                   |
| ----- | -------------------------------------- |
| 0~510 | 실제 데이터 페이지 주소                |
| 511   | 다음 PRP List 페이지 주소 (또는 0, 끝) |

------

# 