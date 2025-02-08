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
                "gt" { if ($valueAfter -gt $threshold) { return $true } }
                "lt" { if ($valueAfter -lt $threshold) { return $true } }
                "ge" { if ($valueAfter -ge $threshold) { return $true } }
                "le" { if ($valueAfter -le $threshold) { return $true } }
                "ne" { if ($valueAfter -ne $threshold) { return $true } }
                "eq" { if ($valueAfter -eq $threshold) { return $true } }
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
function Compare-SmartData {
    param (
        [string]$Customer,
        [byte[]]$SmartBefore,
        [byte[]]$SmartAfter,
        [string]$ProductKey  # Specify the product key dynamically (e.g., 'product3')
    )

    $FailCount = 0
    $WarningCount = 0

    $data | Where-Object { $_.customer -eq $Customer } | ForEach-Object {
        $byteOffset = $_.byte_offset
        $criteria = $_.criteria
        $condition = $_.PSObject.Properties[$ProductKey].Value  # Dynamically access the product key
        $Result = "Not Evaluated"
       
        # Proceed only if condition is not empty or null
        if (-not [string]::IsNullOrEmpty($condition)) {
            $Result = "PASS"
            try {
                $bytesBefore = Get-Bytes -data $SmartBefore -byteOffset $byteOffset
                $bytesAfter = Get-Bytes -data $SmartAfter -byteOffset $byteOffset

                # Convert byte arrays to numeric values
                $valueBefore = Convert-BytesToNumber -bytes $bytesBefore
                $valueAfter = Convert-BytesToNumber -bytes $bytesAfter

                $bytesBefore = ($bytesBefore | ForEach-Object { '{0:x2}' -f $_ }) -join '' 
                $bytesAfter = ($bytesAfter | ForEach-Object { '{0:x2}' -f $_ }) -join '' 

                $ret = Evaluate-Condition -valueBefore $valueBefore -valueAfter $valueAfter -conditions $condition
                if ($ret) {
                    switch ($criteria) {
                        "FAIL" { $FailCount++ }
                        "WARNING" { $WarningCount++ }
                    }
                    $Result = $criteria
                }
            }
            catch {
                Write-Host "Error processing byteOffset $byteOffset : $_"
            }
        }
        Write-Host "$Customer, $ProductKey, $byteOffset, $condition, $bytesBefore ($valueBefore) <=> $bytesAfter ($valueAfter), $($_.field_name) ==> $Result"
    }
    Write-Host "FailCount: $FailCount, WarningCount: $WarningCount"
    return $FailCount, $WarningCount
}

function Check-SmartData {
    # Load the Excel file using COMObject
    Import-Module $PSScriptRoot\importexcel\ImportExcel.psd1 -Force

    $excelFilePath = Join-Path -Path $PSScriptRoot -ChildPath "smart.xlsx"  # Update with your file path
    $data = Import-Excel -Path $excelFilePath -StartRow 3
    $data2 = Read-ExcelData -Path $excelFilePath -StartRow 3

    # Example binary data for SmartBefore and SmartAfter (512 bytes each)
    $SmartBefore = New-Object byte[] 512
    [System.Random]::new().NextBytes($SmartBefore)
    sleep 1
    $SmartAfter = New-Object byte[] 512
    [System.Random]::new().NextBytes($SmartAfter)

    $TotalFailCount = 0
    $TotalWarningCount = 0
    $product = "product2"

    $result = Compare-SmartData -Customer "NVME" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey $product
    $TotalFailCount += $result[0]
    $TotalWarningCount += $result[1]

    $result = Compare-SmartData -Customer "NVME_DELL" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey $product
    $TotalFailCount += $result[0]
    $TotalWarningCount += $result[1]

    $result = Compare-SmartData -Customer "DELL" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey $product
    $TotalFailCount += $result[0]
    $TotalWarningCount += $result[1]

    $result = Compare-SmartData -Customer "HP" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey $product
    $result = Compare-SmartData -Customer "MS" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey $product
    Write-Host "Total FailCount: $TotalFailCount, Total WarningCount: $TotalWarningCount"
    return ($TotalFailCount, $TotalWarningCount)
}

$fail, $warning = Check-SmartData
write-host "Fail: $fail, Warning: $warning"