<#
This PowerShell script reads an Excel file and compares "smart_before" and "smart_after" values
based on conditions defined in the Excel file.
#>

function Read-ExcelData {
    param (
        [string]$Path,
        [int]$StartRow = 1
    )
    $excel = New-Object -ComObject Excel.Application
    $workbook = $excel.Workbooks.Open($Path)
    $sheet = $workbook.Sheets.Item(1)

    # Read the header (first row) to use as column names
    $headers = @()
    for ($col = 1; $col -le $sheet.UsedRange.Columns.Count; $col++) {
        $headers += $sheet.Cells.Item($StartRow, $col).Value2
    }

    # Read the data into an array of objects
    $data = @()
    $row = $StartRow + 1  # Start from the second row since the first row is the header
    while ($null -ne $sheet.Cells.Item($row, 1).Value2) {
        $item = @{}
        for ($col = 1; $col -le $headers.Count; $col++) {
            $item[$headers[$col - 1]] = $sheet.Cells.Item($row, $col).Value2
        }
        $data += [PSCustomObject]$item
        $row++
    }

    # Close Excel
    $workbook.Close($false)
    $excel.Quit()
    return $data
}

# Function to evaluate conditions
function Evaluate-Condition {
    param (
        [double]$valueBefore,
        [double]$valueAfter,
        [string]$conditions
    )

    foreach ($condition in $conditions -split ";") {
        if ($condition -match "^(\w+):(\d+)$") {
            $operator = $matches[1]
            $threshold = [double]$matches[2]
            Write-Host "Operator: $operator, Threshold: $threshold"
            switch ($operator) {
                "gt" { if (($valueBefore -gt $threshold) -or ($valueAfter -gt $threshold)) { return $true } }
                "lt" { if (($valueBefore -lt $threshold) -or ($valueAfter -lt $threshold)) { return $true } }
                "ge" { if (($valueBefore -ge $threshold) -or ($valueAfter -ge $threshold)) { return $true } }
                "le" { if (($valueBefore -le $threshold) -or ($valueAfter -le $threshold)) { return $true } }
                "ne" { if (($valueBefore -ne $threshold) -or ($valueAfter -ne $threshold)) { return $true } }
                "eq" { if (($valueBefore -eq $threshold) -or ($valueAfter -eq $threshold)) { return $true } }
            }
        }
        else {
            Write-Host "Condition: $condition"
            switch ($condition) {
                "inc" { if ($valueAfter -gt $valueBefore) { return $true } }
                "dec" { if ($valueAfter -lt $valueBefore) { return $true } }
                "ne" { if ($valueAfter -ne $valueBefore) { return $true } }
            }
        }
    }
    return $false
}

# Function to extract bytes based on byte_offset
function Get-Bytes {
    param (
        [byte[]]$data,
        [string]$byteOffset
    )

    if ($byteOffset -match "^(\d+):(\d+)$") {
        $end = [int]$matches[1]
        $start = [int]$matches[2]
        $length = $end - $start + 1
        $bytes = $data[$start..($start + $length - 1)]
    }
    elseif ($byteOffset -match "^(\d+)$") {
        $start = [int]$matches[1]
        $bytes = @($data[$start])
    }
    else {
        throw "Invalid byteOffset format: $byteOffset"
    }

    # Return the full byte array, regardless of length
    return [byte[]]$bytes
}

# Function to convert byte array to numeric value
function Convert-BytesToNumber {
    param (
        [byte[]]$bytes
    )

    switch ($bytes.Count) {
        1 { return [System.BitConverter]::ToUInt16(($bytes + 0x00), 0) }
        2 { return [System.BitConverter]::ToUInt16($bytes, 0) }
        4 { return [System.BitConverter]::ToUInt32($bytes, 0) }
        8 { return [System.BitConverter]::ToUInt64($bytes, 0) }
        16 { return [System.Numerics.BigInteger]::new($bytes) }
        default {
            $paddedBytes = $bytes + (0..(8 - $bytes.Count) | ForEach-Object { 0x00 }) | Select-Object -First 8
            return [System.BitConverter]::ToUInt64($paddedBytes, 0)
        }
    }
}

# Function to filter data by customer and compare values
function Compare-SmartCustomer {
    param (
        [string]$Customer,
        [string]$ProjectCode  # Specify the product key dynamically (e.g., 'product3')
    )
    $_FailCount = 0
    $_WarningCount = 0

    $CustomerPart = $Customer.Split('_')[0]
    $SmartBefore = $SCRIPT:SmartBefore | Where-Object { $_.customer -eq $CustomerPart }
    $SmartAfter = $SCRIPT:SmartAfter | Where-Object { $_.customer -eq $CustomerPart }

    $SCRIPT:SMART_Criteria | Where-Object { $_.customer -eq $Customer } | ForEach-Object {
        $byteOffset = $_.byte_offset
        $condition = $_.PSObject.Properties[$ProjectCode].Value  # Dynamically access the product key
        $Description = $_.field_name
        $valueBeforeObj = $SmartBefore | Where-Object { $_.byte_offset -eq $byteOffset }
        $valueBefore = $valueBeforeObj.value
        $valueHexBefore = $valueBeforeObj.hex_value
        $valueAfterObj = $SmartAfter | Where-Object { $_.byte_offset -eq $byteOffset }
        $valueAfter = $valueAfterObj.value 
        $valueHexAfter = $valueAfterObj.hex_value 
        $Result = "Not Evaluated"
        
        # Proceed only if condition is not empty or null
        if (-not [string]::IsNullOrEmpty($condition)) {
            $Result = "PASS"
            try {
                Write-Host "$($_.customer), $byteOffset, $condition, $valueBefore <==> $valueAfter"
                $ret = Evaluate-Condition -valueBefore $valueBefore -valueAfter $valueAfter -conditions $condition
                if ($ret) {
                    $Result = $_.criteria
                    Write-Host "SMART:$Result - $Customer,$byteOffset,$Description,$condition, $valueBefore <==> $valueAfter"
                }
            }
            catch {
                Write-Host "SMART: Error processing $Customer byteOffset $byteOffset : $_"
            }
            switch ($Result.ToUpper().Trim()) {
                "WARNING" { $_WarningCount++ }
                "FAIL" { $_FailCount++ }
            }
        }
        Add-Content -Path $SCRIPT:SummarySMARTData -Value "$Customer,$byteOffset,$valueHexBefore,$valueHexAfter,$Description,$Result"
    }
    return $_FailCount, $_WarningCount
}

function Get-CustomerCode {
    $InternalFirmwareRevision = Get-RegInternalFirmwareRevision
    if ($InternalFirmwareRevision.Length -eq 8) {
        $CustomerCode = switch ($InternalFirmwareRevision[4]) {
            "H" { "HP" }
            "M" { "MS" }
            "3" { "LENOVO" }
            "D" { "DELL" }
            "5" { "ASUS" }
            "K" { "FACEBOOK" }
            default { "UNKNOWN" }
        }
        return $CustomerCode
    }
    return "UNKNOWN"
}

function Get-ProjectCode {
    $ProjectCode = $null
    $deviceString = $TargetDeviceInfo.NVMeInstanceId
    $devPartMatch = [regex]::Match($deviceString, 'DEV_([^&]+)')
    if ($devPartMatch.Success) {
        $devPartValue = $devPartMatch.Groups[1].Value
        $ProjectCode = $SCRIPT:SMART_Criteria[0].PSObject.Properties.Name | Where-Object { $_ -like "*$devPartValue*" } | Select-Object -First 1
    }
    if (-not $ProjectCode) {
        $ProjectCode = $SCRIPT:SMART_Criteria[0].PSObject.Properties.Name | Where-Object { $_ -like "*PCB01*" } | Select-Object -First 1
    }
    return $ProjectCode
}

function Compare-SmartData {
    Import-Module $Global:INCLUDE_PATH\importexcel\ImportExcel.psd1 -Force
    $SMARTexcelFilePath = Join-Path -Path $Global:CONFIG_PATH -ChildPath "smart\smart_management.xlsx"

    $SCRIPT:SMART_Criteria = Import-Excel -Path $SMARTexcelFilePath -StartRow 3
    $CustomerCode = Get-CustomerCode
    $ProjectCode = Get-ProjectCode

    $PreSMARTDataFile = "$LOG_PATH\smart_before.csv"
    $PostSMARTDataFile = "$LOG_PATH\smart_after.csv"
    $SCRIPT:SummarySMARTData = "$LOG_PATH\smart_summary.csv"
    $SCRIPT:SmartBefore = Import-Csv -Path $PreSMARTDataFile
    $SCRIPT:SmartAfter = Import-Csv -Path $PostSMARTDataFile
    [int]$FailCount = 0
    [int]$WarningCount = 0

    $_tmp = New-Item -Path $SCRIPT:SummarySMARTData -ItemType File -Force
    $_tmp = Add-Content -Path $SCRIPT:SummarySMARTData -Value "customer,byte_offset,Before Value (HEX),After Value (HEX),field_name,result"

    $NvmeCustomerCode = switch ($CustomerCode) {
        "HP" { "NVME_HP" }
        "DELL" { "NVME_DELL" }
        default { "NVME_GEN" }
    }

    $result = Compare-SmartCustomer -Customer "NVME" -ProjectCode $ProjectCode
    $FailCount += $result[0]
    $WarningCount += $result[1]

    $result = Compare-SmartCustomer -Customer $NvmeCustomerCode -ProjectCode $ProjectCode
    $FailCount += $result[0]
    $WarningCount += $result[1]

    $result = Compare-SmartCustomer -Customer $CustomerCode -ProjectCode $ProjectCode
    $FailCount += $result[0]
    $WarningCount += $result[1]

    $result = Compare-SmartCustomer -Customer "WAI" -ProjectCode $ProjectCode
    $result = Compare-SmartCustomer -Customer "WAF" -ProjectCode $ProjectCode
    Write-Host "[SMART][$CustomerCode][$ProjectCode] Warning: $WarningCount, Fail: $FailCount"
    return $FailCount, $WarningCount
}

$Global:INCLUDE_PATH = Split-Path -Parent $MyInvocation.MyCommand.Path
$Global:CONFIG_PATH = Split-Path -Parent $MyInvocation.MyCommand.Path
$LOG_PATH = ".\"

$fail, $warning = Compare-SmartData
write-host "Fail: $fail, Warning: $warning"