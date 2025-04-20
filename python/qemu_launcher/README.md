# QEMU Launcher

> 모듈형 Python CLI 도구로 QEMU 가상 머신을 효율적으로 실행하고 구성할 수 있습니다.

---

## 📦 주요 특징
- `qemu-system-*` 실행을 위한 명령어 자동 생성
- NVMe, VirtioFS, USB, SPICE 등 다양한 장치 지원
- SSH / SPICE 뷰어를 통한 자동 접속
- 유연한 아키텍처 (x86_64, aarch64 등 지원)

---

## 📁 프로젝트 구조
```
qemu_launcher/
├── core.py               # QemuLauncher 클래스
├── main.py               # 엔트리포인트
├── config.py             # argparse CLI 파서
├── logger.py             # 로깅 유틸
├── shell.py              # subprocess 헬퍼
├── device/               # 장치 설정 모듈
│   ├── usb.py            # USB 장치 설정
│   ├── kernel.py         # 커널 부팅 옵션
│   ├── net.py            # 네트워크 구성
│   ├── nvme.py           # NVMe 컨트롤러 + NS 구성
│   ├── disks.py          # 디스크 이미지
│   ├── spice.py          # SPICE 그래픽 출력
│   ├── virtiofs.py       # 호스트-게스트 공유
│   ├── tpm.py            # TPM 장치 연결
│   ├── connect.py        # SSH / Spice 자동 연결
│   ├── usb_storage.py    # USB 스틱 연결
│   ├── ssh.py            # SSH 키 삭제 등
│   ├── disk_image.py     # --disk 옵션 처리
│   └── pci.py            # PCI 패스스루
├── __init__.py
setup.py
pyproject.toml
README.md
```

---

## 🚀 설치
```bash
# 패키지 설치
pip install .

# 또는 개발 모드로
pip install -e .
```

---

## 🧪 사용 예시
```bash
# qcow2 이미지로 기본 실행
qemu-launcher ubuntu.qcow2

# NVMe 테스트
qemu-launcher --numns 2 --nvme testnvme --nssize 1 ubuntu.qcow2

# SSH 연결 모드로
qemu-launcher --connect ssh ubuntu.qcow2

# 공유 폴더 비활성화
qemu-launcher --noshare ubuntu.qcow2
```

---

## 🔧 개발자 정보
- Python 3.8 이상 권장
- QEMU 최신 버전 필요
- 기본적으로 `qemu-system-x86_64` 기준으로 테스트됨

---

## 📜 라이선스
MIT License
