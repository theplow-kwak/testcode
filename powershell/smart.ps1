<#
This PowerShell script reads an Excel file and compares "smart_before" and "smart_after" values
based on conditions defined in the Excel file.
#>

function Read-ExcelData {
    param (
        [string]$FilePath
    )
    $excel = New-Object -ComObject Excel.Application
    $workbook = $excel.Workbooks.Open($FilePath)
    $sheet = $workbook.Sheets.Item(1)

    # Read the header (first row) to use as column names
    $headers = @()
    for ($col = 1; $col -le $sheet.UsedRange.Columns.Count; $col++) {
        $headers += $sheet.Cells.Item(1, $col).Value2
    }

    # Read the data into an array of objects
    $data = @()
    $row = 2  # Start from the second row since the first row is the header
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

    $data | Where-Object { $_.customer -eq $Customer } | ForEach-Object {
        $byteOffset = $_.byte_offset
        $condition = $_.PSObject.Properties[$ProductKey].Value  # Dynamically access the product key
        
        # Proceed only if condition is not empty or null
        if (-not [string]::IsNullOrEmpty($condition)) {
            Write-Output "$($_.customer), Offset: $byteOffset, condition: $condition"

            try {
                $bytesBefore = Get-Bytes -data $SmartBefore -byteOffset $byteOffset
                $bytesAfter = Get-Bytes -data $SmartAfter -byteOffset $byteOffset

                # Convert byte arrays to numeric values
                $valueBefore = Convert-BytesToNumber -bytes $bytesBefore
                $valueAfter = Convert-BytesToNumber -bytes $bytesAfter

                $bytesBefore = ($bytesBefore | ForEach-Object { '{0:x2}' -f $_ }) -join '' 
                $bytesAfter = ($bytesAfter | ForEach-Object { '{0:x2}' -f $_ }) -join '' 

                Write-Output "valueBefore: $bytesBefore ($valueBefore), valueAfter: $bytesAfter ($valueAfter)"
                $result = Evaluate-Condition -valueBefore $valueBefore -valueAfter $valueAfter -conditions $condition
                if ($result) {
                    Write-Output "Field: $($_.field_name), Offset: $byteOffset, Result: $($_.criteria)"
                }
            }
            catch {
                Write-Output "Error processing byteOffset $byteOffset : $_"
            }
        }
    }
}

# Load the Excel file using COMObject
# Import-Module $PSScriptRoot\importexcel\ImportExcel.psd1 -Force

$PWDScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path  # Get the script's directory path
$excelFilePath = Join-Path -Path $PWDScriptRoot -ChildPath "smart.xlsx"  # Update with your file path
# $data = Import-Excel -Path $excelFilePath -StartRow 3
$data = Read-ExcelData -FilePath $excelFilePath

# Example binary data for SmartBefore and SmartAfter (512 bytes each)
$SmartBefore = New-Object byte[] 512
[System.Random]::new().NextBytes($SmartBefore)

$SmartAfter = New-Object byte[] 512
[System.Random]::new().NextBytes($SmartAfter)

$offset = "5:4"
$target = $data | Where-Object { $_.customer -eq "NVME" } | where-object { $_.byte_offset -eq $offset } 
Write-Host $target.criteria

Compare-SmartData -Customer "NVME" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey "PCB01"
Compare-SmartData -Customer "NVME_DELL" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey "PCB01"
Compare-SmartData -Customer "DELL" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey "PCB01"
