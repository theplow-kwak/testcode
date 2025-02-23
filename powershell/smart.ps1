<#
This PowerShell script reads an Excel file and compares "smart_before" and "smart_after" values
based on conditions defined in the Excel file.
#>

$SCRIPT:KernelService = Add-Type -Name 'Kernel32' -Namespace 'Win32' -PassThru -MemberDefinition @"
    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern IntPtr CreateFile(
        String lpFileName,
        UInt32 dwDesiredAccess,
        UInt32 dwShareMode,
        IntPtr lpSecurityAttributes,
        UInt32 dwCreationDisposition,
        UInt32 dwFlagsAndAttributes,
        IntPtr hTemplateFile);

    [DllImport("Kernel32.dll", SetLastError = true)]
    public static extern bool DeviceIoControl(
        IntPtr  hDevice,
        int     oControlCode,
        IntPtr  InBuffer,
        int     nInBufferSize,
        IntPtr  OutBuffer,
        int     nOutBufferSize,
        ref int pBytesReturned,
        IntPtr  Overlapped);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr hObject);
"@

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct NVMeStorageQueryProperty {
    public UInt32 PropertyId;
    public UInt32 QueryType;
    public UInt32 ProtocolType;
    public UInt32 DataType;
    public UInt32 ProtocolDataRequestValue;
    public UInt32 ProtocolDataRequestSubValue;
    public UInt32 ProtocolDataOffset;
    public UInt32 ProtocolDataLength;
    public UInt32 FixedProtocolReturnData;
    public UInt32 ProtocolDataRequestSubValue2;
    public UInt32 ProtocolDataRequestSubValue3;
    public UInt32 ProtocolDataRequestSubValue4;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 512)]
    public Byte[] SMARTData;
}
"@


class SmartDataComparer {
    [string]$INCLUDE_PATH
    [string]$CONFIG_PATH
    [string]$LOG_PATH
    [string]$SummarySMARTDataFile
    [array]$SMART_Criteria
    [array]$SmartBefore
    [array]$SmartAfter
    [string]$CustomerCode
    [string]$ProjectCode

    SmartDataComparer([int]$PhyDrvNo = 0) {
        $this.PhyDrvNo = $PhyDrvNo
        $this.INCLUDE_PATH = $PSScriptRoot
        $this.CONFIG_PATH = $PSScriptRoot
        $this.LOG_PATH = ".\"

        Import-Module "$($this.INCLUDE_PATH)\importexcel\ImportExcel.psd1"
        $SMARTexcelFilePath = Join-Path -Path $this.CONFIG_PATH -ChildPath "smart\smart_management.xlsx"
        $this.SMART_Criteria = Import-Excel -Path $SMARTexcelFilePath -StartRow 3
        $this.GetCustomerCode()
        $this.GetProjectCode()
    }

    [bool] EvaluateCondition([double]$valueBefore, [double]$valueAfter, [string]$conditions) {
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

    [byte[]] GetBytes([byte[]]$data, [string]$byteOffset) {
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

    [object] ConvertBytesToNumber([byte[]]$bytes) {
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
        return $null
    }

    [array] CompareSmartCustomer([string]$Customer) {
        $_FailCount = 0
        $_WarningCount = 0

        $CustomerPart = $Customer.Split('_')[0]
        $_SmartBefore = $this.SmartBefore | Where-Object { $_.customer -eq $CustomerPart }
        $_SmartAfter = $this.SmartAfter | Where-Object { $_.customer -eq $CustomerPart }

        $this.SMART_Criteria | Where-Object { $_.customer -eq $Customer } | ForEach-Object {
            $byteOffset = $_.byte_offset
            $condition = $_.PSObject.Properties[$this.ProjectCode].Value  # Dynamically access the product key
            $Description = $_.field_name
            $valueBeforeObj = $_SmartBefore | Where-Object { $_.byte_offset -eq $byteOffset }
            $valueBefore = $valueBeforeObj.value
            $valueHexBefore = $valueBeforeObj.hex_value
            $valueAfterObj = $_SmartAfter | Where-Object { $_.byte_offset -eq $byteOffset }
            $valueAfter = $valueAfterObj.value 
            $valueHexAfter = $valueAfterObj.hex_value 
            $Result = "Not Evaluated"
            
            # Proceed only if condition is not empty or null
            if (-not [string]::IsNullOrEmpty($condition)) {
                $Result = "PASS"
                try {
                    Write-Host "$($_.customer), $byteOffset, $condition, $valueBefore <==> $valueAfter"
                    $ret = $this.EvaluateCondition(@{valueBefore = $valueBefore; valueAfter = $valueAfter; conditions = $condition })
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
            Add-Content -Path $this.SummarySMARTDataFile -Value "$Customer,$byteOffset,$valueHexBefore,$valueHexAfter,$Description,$Result"
        }
        return $_FailCount, $_WarningCount
    }

    [void] GetCustomerCode() {
        try {
            $InternalFirmwareRevision = "C0003000"
            if ($InternalFirmwareRevision.Length -eq 8) {
                $this.CustomerCode = switch ($InternalFirmwareRevision[4]) {
                    "H" { "HP" }
                    "M" { "MS" }
                    "3" { "LENOVO" }
                    "D" { "DELL" }
                    "5" { "ASUS" }
                    "K" { "FACEBOOK" }
                    default { "UNKNOWN" }
                }
            }
        }
        catch {
            Write-Host "Error getting customer code: $_"
        }
    }

    [void] GetProjectCode() {
        try {
            $deviceString = $Global:TargetDeviceInfo.NVMeInstanceId
            $devPartMatch = [regex]::Match($deviceString, 'DEV_([^&]+)')
            if ($devPartMatch.Success) {
                $devPartValue = $devPartMatch.Groups[1].Value
                $this.ProjectCode = $this.SMART_Criteria[0].PSObject.Properties.Name | Where-Object { $_ -like "*$devPartValue*" } | Select-Object -First 1
            }
            if (-not $this.ProjectCode) {
                $this.ProjectCode = $this.SMART_Criteria[0].PSObject.Properties.Name | Where-Object { $_ -like "*PCB01*" } | Select-Object -First 1
            }
        }
        catch {
            Write-Host "Error getting project code: $_"
        }
    }

    [array] CompareSmartData() {
        try {
            $this.SummarySMARTDataFile = "$($this.LOG_PATH)\smart_summary.csv"
            $PreSMARTDataFile = "$($this.LOG_PATH)\smart_before.csv"
            $PostSMARTDataFile = "$($this.LOG_PATH)\smart_after.csv"
            $this.SmartBefore = Import-Csv -Path $PreSMARTDataFile
            $this.SmartAfter = Import-Csv -Path $PostSMARTDataFile
            [int]$FailCount = 0
            [int]$WarningCount = 0

            $_tmp = New-Item -Path $this.SummarySMARTDataFile -ItemType File -Force
            $_tmp = Add-Content -Path $this.SummarySMARTDataFile -Value "customer,byte_offset,Before Value (HEX),After Value (HEX),field_name,result"

            $NvmeCustomerCode = switch ($this.CustomerCode) {
                "HP" { "NVME_HP" }
                "DELL" { "NVME_DELL" }
                default { "NVME_GEN" }
            }

            $result = $this.CompareSmartCustomer("NVME")
            $FailCount += $result[0]
            $WarningCount += $result[1]

            $result = $this.CompareSmartCustomer($NvmeCustomerCode)
            $FailCount += $result[0]
            $WarningCount += $result[1]

            $result = $this.CompareSmartCustomer($this.CustomerCode)
            $FailCount += $result[0]
            $WarningCount += $result[1]

            $result = $this.CompareSmartCustomer("WAI")
            $result = $this.CompareSmartCustomer("WAF")
            Write-Host "[SMART][$this.CustomerCode][$this.ProjectCode] Warning: $WarningCount, Fail: $FailCount"
            return $FailCount, $WarningCount
        }
        catch {
            Write-Host "Error comparing SMART data: $_"
        }
        return -1, -1
    }

    [array] GetLogPage($LogID, $LogPageSize = 512) {
        try {
            $AccessMask = "3221225472"; # = 0xC00000000 = GENERIC_READ (0x80000000) | GENERIC_WRITE (0x40000000)
            $AccessMode = 3; # FILE_SHARE_READ | FILE_SHARE_WRITE
            $AccessEx = 3; # OPEN_EXISTING
            $AccessAttr = 0x40; # FILE_ATTRIBUTE_DEVICE

            $DeviceHandle = $SCRIPT:KernelService::CreateFile("\\.\PhysicalDrive$($this.PhyDrvNo)", [System.Convert]::ToUInt32($AccessMask), $AccessMode, [System.IntPtr]::Zero, $AccessEx, $AccessAttr, [System.IntPtr]::Zero);
            # offsetof(STORAGE_PROPERTY_QUERY, AdditionalParameters)
            #  + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)
            #  + sizeof(NVME_SMART_INFO_LOG) = 560
            $OutBufferSize = 8 + 40 + $LogPageSize; # = 560
            $OutBuffer = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($OutBufferSize);

            $Property = New-Object NVMeStorageQueryProperty;
            $Property.PropertyId = 50; # StorageDeviceProtocolSpecificProperty
            $Property.QueryType = 0; # PropertyStandardQuery
            $Property.ProtocolType = 3; # ProtocolTypeNvme
            $Property.DataType = 2; # NVMeDataTypeLogPage

            $Property.ProtocolDataRequestValue = $LogID; # NVME_LOG_PAGE_HEALTH_INFO
            $Property.ProtocolDataRequestSubValue = 0; # LPOL
            $Property.ProtocolDataRequestSubValue2 = 0; # LPOU
            $Property.ProtocolDataRequestSubValue3 = 0; # Log Specific Identifier in CDW11
            $Property.ProtocolDataRequestSubValue4 = 0; # Retain Asynchronous Event (RAE) and Log Specific Field (LSP) in CDW10

            $Property.ProtocolDataOffset = 40; # sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)
            $Property.ProtocolDataLength = $LogPageSize; # sizeof(NVME_SMART_INFO_LOG)

            $ByteRet = 0;
            $IoControlCode = 0x2d1400; # IOCTL_STORAGE_QUERY_PROPERTY

            [System.Runtime.InteropServices.Marshal]::StructureToPtr($Property, $OutBuffer, [System.Boolean]::false);
            $CallResult = $SCRIPT:KernelService::DeviceIoControl($DeviceHandle, $IoControlCode, $OutBuffer, $OutBufferSize, $OutBuffer, $OutBufferSize, [ref]$ByteRet, [System.IntPtr]::Zero);
            $LogPageData = New-Object byte[] $LogPageSize
            [System.Runtime.InteropServices.Marshal]::Copy([IntPtr]($OutBuffer.ToInt64() + 48), $LogPageData, 0, $LogPageSize)
        }
        catch {
            Write-Output "`n[E] GetLogPage failed: $_";
            Return @();
        }
        finally {
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($OutBuffer);
            [void]$SCRIPT:KernelService::CloseHandle($DeviceHandle);
        }
        return $LogPageData
    }

    [void] SaveSmartData($step = "before") {
        $filePath = "$($this.LOG_PATH)\smart_$step.csv"
        $smartData = $this.GetLogPage(2, 4096)
        $smartData | Export-Csv -Path $filePath -NoTypeInformation
    }

}


# Ensure $Global:TargetDeviceInfo is initialized
$Global:TargetDeviceInfo = @{
    NVMeInstanceId = "DEV_1F69"
}

$comparer = [SmartDataComparer]::new(1)
$logpagedata = $comparer.GetLogPage(2, 4096)

$fail, $warning = $comparer.CompareSmartData()
write-host "Fail: $fail, Warning: $warning"