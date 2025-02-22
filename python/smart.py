import os
import subprocess
import csv
import re
from openpyxl import load_workbook
import logging
import winreg

# 로깅 설정
logging.basicConfig(filename="smart_data.log", level=logging.INFO)

# 상수 변수 설정
MODULE_PATH = "path/to/module"
DATA_PATH = "path/to/data"
LOG_PATH = "path/to/log"
LOG_ERROR = logging.ERROR
LOG_INFO = logging.INFO


def get_reg_internal_firmware_revision():
    try:
        # 레지스트리 키 열기
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Your\Registry\Path")
        # 값을 읽기
        value, _ = winreg.QueryValueEx(key, "InternalFirmwareRevision")
        winreg.CloseKey(key)
        return value
    except Exception as e:
        logging.error(f"Error reading registry: {e}")
        return None


def backup_smart_data(physical_drv_num=0, step="Before"):
    try:
        result = subprocess.run(
            [f"{MODULE_PATH}/smart.exe", "-s", str(physical_drv_num)],
            capture_output=True,
            text=True,
            check=True,
        )
        smart_files = [f for f in os.listdir(DATA_PATH) if f.startswith("smart") and f.endswith(".txt")]
        smart_data_path = os.path.join(DATA_PATH, smart_files[-1])

        logging.info(f"copy {os.path.basename(smart_data_path)} to Stat_SMARTExtension_{step}.csv")
        destination = os.path.join(LOG_PATH, f"Stat_SMARTExtension_{step}.csv")
        os.system(f"cp {smart_data_path} {destination}")

        return True
    except subprocess.CalledProcessError as e:
        logging.error(f"smart.exe returns error: {e.returncode}")
        return False


def evaluate_condition(value_before, value_after, conditions):
    for condition in conditions.split(";"):
        if match := re.match(r"^(\w+):(\d+)$", condition):
            operator, threshold = match.groups()
            threshold = float(threshold)
            if (
                (operator == "gt" and value_after > threshold)
                or (operator == "lt" and value_after < threshold)
                or (operator == "ge" and value_after >= threshold)
                or (operator == "le" and value_after <= threshold)
                or (operator == "ne" and value_after != threshold)
                or (operator == "eq" and value_after == threshold)
            ):
                return True
        else:
            if (condition == "inc" and value_after > value_before) or (condition == "dec" and value_after < value_before) or (condition == "ne" and value_after != value_before):
                return True
    return False


def get_bytes(data, byte_offset):
    if match := re.match(r"^(\d+):(\d+)$", byte_offset):
        end, start = map(int, match.groups())
        length = end - start + 1
        bytes_ = data[start : start + length]
    elif match := re.match(r"^(\d+)$", byte_offset):
        start = int(match.group(1))
        bytes_ = data[start : start + 1]
    else:
        raise ValueError(f"Invalid byteOffset format: {byte_offset}")
    return bytes_


def convert_bytes_to_number(bytes_):
    if len(bytes_) == 1:
        return int.from_bytes(bytes_ + b"\x00", byteorder="little")
    elif len(bytes_) in (2, 4, 8, 16):
        return int.from_bytes(bytes_, byteorder="little")
    else:
        padded_bytes = bytes_ + b"\x00" * (8 - len(bytes_))[:8]
        return int.from_bytes(padded_bytes, byteorder="little")


def compare_smart_data(customer, project_code, smart_before, smart_after, smart_criteria, summary_file):
    fail_count = 0
    warning_count = 0

    smart_before_filtered = [item for item in smart_before if item["customer"] == customer.split("_")[0]]
    smart_after_filtered = [item for item in smart_after if item["customer"] == customer.split("_")[0]]

    for criteria in smart_criteria:
        if criteria["customer"] == customer:
            byte_offset = criteria["byte_offset"]
            condition = criteria[project_code]
            description = criteria["field_name"]
            value_before_obj = next(
                (item for item in smart_before_filtered if item["byte_offset"] == byte_offset),
                None,
            )
            value_before = float(value_before_obj["value"]) if value_before_obj else None
            value_hex_before = value_before_obj["hex_value"] if value_before_obj else None
            value_after_obj = next(
                (item for item in smart_after_filtered if item["byte_offset"] == byte_offset),
                None,
            )
            value_after = float(value_after_obj["value"]) if value_after_obj else None
            value_hex_after = value_after_obj["hex_value"] if value_after_obj else None
            result = "Not a comparison item"

            if condition:
                result = "PASS"
                try:
                    print(f"{criteria['customer']}, {byte_offset}, {condition}, {value_before} <==> {value_after}")
                    ret = evaluate_condition(value_before, value_after, condition)
                    if ret:
                        result = criteria["criteria"]
                        logging.info(f"SMART:{result} - {customer},{byte_offset},{description},{condition}, {value_before} <==> {value_after}")
                except Exception as e:
                    logging.error(f"SMART: Error processing {customer} byteOffset {byte_offset} : {e}")
                if result.upper().strip() == "WARNING":
                    warning_count += 1
                elif result.upper().strip() == "FAIL":
                    fail_count += 1
            with open(summary_file, "a", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(
                    [
                        customer,
                        byte_offset,
                        value_hex_before,
                        value_hex_after,
                        description,
                        result,
                    ]
                )
    return fail_count, warning_count


def get_customer_code():
    internal_firmware_revision = get_reg_internal_firmware_revision()
    if len(internal_firmware_revision) == 8:
        customer_code_map = {
            "H": "HP",
            "M": "MS",
            "3": "LENOVO",
            "D": "DELL",
            "5": "ASUS",
            "K": "FACEBOOK",
        }
        customer_code = customer_code_map.get(internal_firmware_revision[4], "UNKNOWN")
        return customer_code
    return "UNKNOWN"


def get_project_code(target_device_info):
    project_code = None
    device_string = target_device_info["NVMeInstanceId"]
    if match := re.search(r"DEV_([^&]+)", device_string):
        dev_part_value = match.group(1)
        project_code = next((key for key in smart_criteria[0] if dev_part_value in key), None)
    if not project_code:
        project_code = next((key for key in smart_criteria[0] if "PCB01" in key), None)
    return project_code


def compare_smart_attribute_values(config_path, log_path):
    global smart_criteria, smart_before, smart_after, summary_smart_data

    smart_excel_file_path = os.path.join(config_path, "smart", "SMART_Version_Management.xlsx")
    workbook = load_workbook(smart_excel_file_path)
    sheet = workbook.active
    smart_criteria = [dict(zip([cell.value for cell in sheet[2]], [cell.value for cell in row])) for row in sheet.iter_rows(min_row=3, values_only=True)]

    customer_code = get_customer_code()
    target_device_info = {}  # This should be populated with actual NVMeInstanceId data
    project_code = get_project_code(target_device_info)

    pre_smart_data_file = os.path.join(log_path, "Stat_SMARTExtension_Before.csv")
    post_smart_data_file = os.path.join(log_path, "Stat_SMARTExtension_After.csv")
    summary_smart_data = os.path.join(log_path, "Stat_SMARTExtension_Summary.csv")

    smart_before = list(csv.DictReader(open(pre_smart_data_file)))
    smart_after = list(csv.DictReader(open(post_smart_data_file)))

    with open(summary_smart_data, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "customer",
                "byte_offset",
                "Before Value (HEX)",
                "After Value (HEX)",
                "field_name",
                "result",
            ]
        )

    fail_count, warning_count = compare_smart_data(
        "NVME",
        project_code,
        smart_before,
        smart_after,
        smart_criteria,
        summary_smart_data,
    )
    nvme_customer_code = {"HP": "NVME_HP", "DELL": "NVME_DELL"}.get(customer_code, "NVME_GEN")
    fail_count, warning_count = compare_smart_data(
        nvme_customer_code,
        project_code,
        smart_before,
        smart_after,
        smart_criteria,
        summary_smart_data,
    )
    fail_count, warning_count = compare_smart_data(
        customer_code,
        project_code,
        smart_before,
        smart_after,
        smart_criteria,
        summary_smart_data,
    )
    fail_count, warning_count = compare_smart_data(
        "WAI",
        project_code,
        smart_before,
        smart_after,
        smart_criteria,
        summary_smart_data,
    )
    fail_count, warning_count = compare_smart_data(
        "WAF",
        project_code,
        smart_before,
        smart_after,
        smart_criteria,
        summary_smart_data,
    )

    logging.info(f"[SMART][{customer_code}][{project_code}] Warning: {warning_count}, Fail: {fail_count}")
    return fail_count, warning_count


# 사용 예시
if __name__ == "__main__":
    config_path = "path/to/config"
    log_path = "path/to/log"
    compare_smart_attribute_values(config_path, log_path)
