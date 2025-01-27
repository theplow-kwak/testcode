<#
This PowerShell script reads an Excel file and compares "smart_before" and "smart_after" values
based on conditions defined in the Excel file.
#>

# Load the Excel file using COMObject
$filePath = "C:\testcode\powershell\smart.xlsx"  # Update with your file path
$excel = New-Object -ComObject Excel.Application
$workbook = $excel.Workbooks.Open($filePath)
$sheet = $workbook.Sheets.Item(1)

# Read the header (first row) to use as column names
$headers = @()
for ($col = 1; $col -le $sheet.UsedRange.Columns.Count; $col++) {
    $headers += $sheet.Cells.Item(1, $col).Value()
}

# Read the data into an array of objects
$data = @()
$row = 2  # Start from the second row since the first row is the header
while ($sheet.Cells.Item($row, 1).Value() -ne $null) {
    $item = @{}
    for ($col = 1; $col -le $headers.Count; $col++) {
        $item[$headers[$col - 1]] = $sheet.Cells.Item($row, $col).Value()
    }
    $data += [PSCustomObject]$item
    $row++
}

# Close Excel
$workbook.Close($false)
$excel.Quit()

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
    return $bytes
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
        $criteria = $_.PSObject.Properties[$ProductKey].Value  # Dynamically access the product key
        Write-Output "$($_.customer), Offset: $byteOffset, condition: $condition"

        try {
            $bytesBefore = Get-Bytes -data $SmartBefore -byteOffset $byteOffset
            $bytesAfter = Get-Bytes -data $SmartAfter -byteOffset $byteOffset

            # Convert byte arrays to numeric values
            $valueBefore = [System.BitConverter]::ToUInt64(@($bytesBefore + (0..(7 - $bytesBefore.Count) | ForEach-Object { 0x00 }))[0..7], 0)
            $valueAfter = [System.BitConverter]::ToUInt64(@($bytesAfter + (0..(7 - $bytesAfter.Count) | ForEach-Object { 0x00 }))[0..7], 0)

            $result = Evaluate-Condition -valueBefore $valueBefore -valueAfter $valueAfter -conditions $criteria
            if ($result) {
                Write-Output "Field: $($_.field_name), Offset: $byteOffset, Result: $($_.criteria)"
            }
        }
        catch {
            Write-Output "Error processing byteOffset $byteOffset : $_"
        }
    }
}

# Example binary data for SmartBefore and SmartAfter (512 bytes each)
$SmartBefore = [byte[]](0..255)
$SmartAfter = [byte[]](0..255)

Compare-SmartData -Customer "NVME" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey "product3"
Compare-SmartData -Customer "NVME_DELL" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey "product3"
Compare-SmartData -Customer "DELL" -SmartBefore $SmartBefore -SmartAfter $SmartAfter -ProductKey "product3"

