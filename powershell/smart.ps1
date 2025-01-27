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
        [string]$valueBefore,
        [string]$valueAfter,
        [string]$conditions
    )

    $conditions -split ";" | ForEach-Object {
        if ($_ -match "^(\w+):(\d+)$") {
            $operator = $matches[1]
            $threshold = [int]$matches[2]
            Write-Host "Operator: $operator, Threshold: $threshold"
            switch ($operator) {
                "gt" { if ($valueAfter -gt $threshold) { return $true } }
                "lt" { if ($valueAfter -lt $threshold) { return $true } }
                "ge" { if ($valueAfter -ge $threshold) { return $true } }
                "le" { if ($valueAfter -le $threshold) { return $true } }
                "ne" { if ($valueAfter -ne $threshold) { return $true } }
                "eq" { if ($valueAfter -eq $threshold) { return $true } }
            }
        } else {
            Write-Host "Condition: $_"
            switch ($_) {
                "inc" { if ($valueAfter -gt $valueBefore) { return $true } }
                "dec" { if ($valueAfter -lt $valueBefore) { return $true } }
            }
        }
    }
    return $false
}

# Filter data by customer and compare values
function Compare-SmartData {
    param (
        [string]$Customer,
        [hashtable]$SmartBefore,
        [hashtable]$SmartAfter,
        [string]$ProductKey  # Specify the product key dynamically (e.g., 'product3')
    )

    $data | Where-Object { $_.customer -eq $Customer -or $_.customer -eq "NVME" -or $_.customer -eq "NVME_$Customer" } | ForEach-Object {
        $byteOffset = $_.byte_offset
        $criteria = $_.PSObject.Properties[$ProductKey].Value  # Dynamically access the product key
        Write-Output "$($_.customer), Offset: $byteOffset, condition: $criteria"

        if ($SmartBefore.ContainsKey($byteOffset) -and $SmartAfter.ContainsKey($byteOffset)) {
            $valueBefore = $SmartBefore[$byteOffset]
            $valueAfter = $SmartAfter[$byteOffset]

            $result = Evaluate-Condition -valueBefore $valueBefore -valueAfter $valueAfter -conditions $criteria
            Write-Output "Field: $($_.field_name), Offset: $byteOffset, Result: $result"
        }
    }
}

Compare-SmartData -Customer "DELL" -SmartBefore @{ "1" = 100; "5:2" = 200 } -SmartAfter @{ "1" = 110; "5:2" = 190 } -ProductKey "product3"
