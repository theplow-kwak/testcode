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
    [string]$SmartFileBefore
    [string]$SmartFileAfter
    [string]$SmartFileSummary
    [string]$SmartfilePath
    [array]$SmartCriteria
    [array]$SmartBefore
    [array]$SmartAfter
    [string]$CustomerCode
    [string]$ProjectCode
    [int]$PhyDrvNo
    [int]$FailCount
    [int]$WarningCount

    SmartDataComparer([int]$PhyDrvNo = 0) {
        $this.PhyDrvNo = $PhyDrvNo
        $this.INCLUDE_PATH = $PSScriptRoot
        $this.CONFIG_PATH = $PSScriptRoot
        $this.LOG_PATH = ".\"

        $this.SmartFileBefore = "$($this.LOG_PATH)\smart_before.csv"
        $this.SmartFileAfter = "$($this.LOG_PATH)\smart_after.csv"
        $this.SmartFileSummary = "$($this.LOG_PATH)\smart_summary.csv"
        Import-Module "$($this.INCLUDE_PATH)\importexcel\ImportExcel.psd1"
        $SMARTexcelFilePath = Join-Path -Path $this.CONFIG_PATH -ChildPath "smart\smart_management.xlsx"
        $this.SmartCriteria = Import-Excel -Path $SMARTexcelFilePath -StartRow 3
        $this.GetCustomerCode()
        $this.GetProjectCode()
    }

    [void] GetCustomerCode() {
        try {
            $InternalFirmwareRevision = "C0000000"
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
                $this.ProjectCode = $this.SmartCriteria[0].PSObject.Properties.Name | Where-Object { $_ -like "*$devPartValue*" } | Select-Object -First 1
            }
            if (-not $this.ProjectCode) {
                $this.ProjectCode = $this.SmartCriteria[0].PSObject.Properties.Name | Where-Object { $_ -like "*PCB01*" } | Select-Object -First 1
            }
        }
        catch {
            Write-Host "Error getting project code: $_"
        }
    }

    [bool] EvaluateCondition([double]$valueBefore, [double]$valueAfter, [string]$conditions) {
        foreach ($condition in $conditions -split ";") {
            if ($condition -match "^(\w+):(\d+)$") {
                $operator = $matches[1]
                $threshold = [double]$matches[2]
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
                switch ($condition) {
                    "inc" { if ($valueAfter -gt $valueBefore) { return $true } }
                    "dec" { if ($valueAfter -lt $valueBefore) { return $true } }
                    "ne" { if ($valueAfter -ne $valueBefore) { return $true } }
                }
            }
        }
        return $false
    }

    CompareSmartCustomer([string]$Customer) {
        $_SmartBefore = $this.SmartBefore | Where-Object { $_.customer -eq $Customer }
        $_SmartAfter = $this.SmartAfter | Where-Object { $_.customer -eq $Customer }

        $this.SmartCriteria | Where-Object { $_.customer -eq $Customer } | ForEach-Object {
            $byteOffset = $_.byte_offset
            $Description = $_.field_name
            $condition = $_.PSObject.Properties[$this.ProjectCode].Value  # Dynamically access the product key
            $valueBeforeObj = $_SmartBefore | Where-Object { $_.byte_offset -eq $byteOffset }
            $valueAfterObj = $_SmartAfter | Where-Object { $_.byte_offset -eq $byteOffset }
            $Result = "Not Evaluated"
            
            # Proceed only if condition is not empty or null
            if (-not [string]::IsNullOrEmpty($condition)) {
                $valueBefore = $valueBeforeObj.value
                $valueAfter = $valueAfterObj.value 
                $Result = "PASS"
                $ret = $this.EvaluateCondition($valueBefore, $valueAfter, $condition)
                if ($ret) {
                    $Result = $_.criteria
                    Write-Host "SMART:$Result - $Customer,$byteOffset,$Description,$condition, $valueBefore <==> $valueAfter"
                }
                switch ($Result.ToUpper().Trim()) {
                    "WARNING" { $this.WarningCount++ }
                    "FAIL" { $this.FailCount++ }
                }
            }
            if ($valueBeforeObj -and $valueAfterObj) {
                Add-Content -Path $this.SmartFileSummary -Value "$Customer,$byteOffset,$($valueBeforeObj.hex_value),$($valueAfterObj.hex_value),$Description,$Result"
            }
            else {
                Add-Content -Path $this.SmartFileSummary -Value "$Customer,$byteOffset,,,$Description,$Result"
            }
        }
    }

    [array] CompareSmartData() {
        try {
            $this.SmartBefore = Import-Csv -Path $this.SmartFileBefore
            $this.SmartAfter = Import-Csv -Path $this.SmartFileAfter
            $this.FailCount = 0
            $this.WarningCount = 0

            $_tmp = New-Item -Path $this.SmartFileSummary -ItemType File -Force
            $_tmp = Add-Content -Path $this.SmartFileSummary -Value "customer,byte_offset,Before Value (HEX),After Value (HEX),field_name,result"

            $NvmeCustomerCode = switch ($this.CustomerCode) {
                "HP" { "NVME_HP" }
                "DELL" { "NVME_DELL" }
                default { "NVME_GEN" }
            }

            $this.CompareSmartCustomer("NVME")
            $this.CompareSmartCustomer($NvmeCustomerCode)
            $this.CompareSmartCustomer($this.CustomerCode)
            $this.CompareSmartCustomer("WAI")
            $this.CompareSmartCustomer("WAF")
            Write-Host "[SMART][$($this.CustomerCode)][$($this.ProjectCode)] Warning: $($this.WarningCount), Fail: $($this.FailCount)"
            return $this.FailCount, $this.WarningCount
        }
        catch {
            Write-Host "Error comparing SMART data: $_"
        }
        return -1, -1
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

    [string] ConvertBytesToHex([byte[]]$bytes) {
        if ($bytes.Count -gt 40) {
            return ""
        }
        if ($bytes.Count -eq 2 -or $bytes.Count -eq 4 -or $bytes.Count -eq 8 -or $bytes.Count -eq 16) {
            [Array]::Reverse($bytes)
        }
        return ($bytes | ForEach-Object { $_.ToString("X2") }) -join '' -replace '(.{8})(?!$)', '$1 '
    }

    [array] GetLogPage($CustomerCode) {
        $logIdMap = @{
            "SKH"    = ("0xFE", 8192)
            "HP"     = ("0xC7", 512)
            "MS"     = ("0xC0", 512)
            "LENOVO" = ("0xDF", 512)
            "DELL"   = ("0xCA", 512)
        }
        $LogID, $LogPageSize = $logIdMap[$CustomerCode]
        if (-not $LogID) {
            $LogID, $LogPageSize = ("0x02", 512)
        }

        try {
            $AccessMask = "3221225472"; # = 0xC00000000 = GENERIC_READ (0x80000000) | GENERIC_WRITE (0x40000000)
            $AccessMode = 3; # FILE_SHARE_READ | FILE_SHARE_WRITE
            $AccessEx = 3; # OPEN_EXISTING
            $AccessAttr = 0x40; # FILE_ATTRIBUTE_DEVICE

            $DeviceHandle = $SCRIPT:KernelService::CreateFile("\\.\PhysicalDrive$($this.PhyDrvNo)", [System.Convert]::ToUInt32($AccessMask), $AccessMode, [System.IntPtr]::Zero, $AccessEx, $AccessAttr, [System.IntPtr]::Zero);
            $OutBufferSize = 8 + 40 + $LogPageSize; # offsetof(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + $LogPageSize
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
            if ($CallResult) {
                [System.Runtime.InteropServices.Marshal]::Copy([IntPtr]($OutBuffer.ToInt64() + 48), $LogPageData, 0, $LogPageSize)
            }
            else {
                Write-Host "Can't get Logpage data for $CustomerCode ($LogID)";
            }
        }
        catch {
            Write-Host "`n[E] GetLogPage failed: $_";
            Return @();
        }
        finally {
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($OutBuffer);
            [void]$SCRIPT:KernelService::CloseHandle($DeviceHandle);
        }
        return $LogPageData
    }

    [void] ParseSmartData($smartData, $CustomerCode) {
        $this.SmartCriteria | Where-Object { $_.customer -eq $CustomerCode } | ForEach-Object {
            $byteOffset = $_.byte_offset
            $Description = $_.field_name
            if ($Description -eq "Reserved") {
                $value = ""
                $hexValue = ""
            }
            else {
                $bytes = $this.GetBytes($smartData, $byteOffset)
                $value = $this.ConvertBytesToNumber($bytes)
                $hexValue = $this.ConvertBytesToHex($bytes)
            }
            Add-Content -Path $this.SmartfilePath -Value "$CustomerCode,$byteOffset,$hexValue,$value,$Description"
        }
    }

    [array] GetWaiWaf($smartData) {
        $ecSlcTotal = $this.ConvertBytesToNumber($this.GetBytes($smartData, "227:224"))
        $ecTlcTotal = $this.ConvertBytesToNumber($this.GetBytes($smartData, "243:240"))
        $writtenToSlcUser = $this.ConvertBytesToNumber($this.GetBytes($smartData, "127:120"))
        $writtenToTlcUser = $this.ConvertBytesToNumber($this.GetBytes($smartData, "119:112"))
        $writeFromHost = $this.ConvertBytesToNumber($this.GetBytes($smartData, "111:104"))
        if ($writeFromHost -eq 0) {
            $wai = 0
            $waf = 0
        }
        else {
            $wai = ($writtenToSlcUser + $writtenToTlcUser) / $writeFromHost
            $waf = ($ecSlcTotal + $ecTlcTotal) / $writeFromHost
        }
        return $wai, $waf
    }

    [void] SaveSmartData($step = "before") {
        if ($step -eq "before") {
            $this.SmartfilePath = $this.SmartFileBefore
        }
        else {
            $this.SmartfilePath = $this.SmartFileAfter
        }
        New-Item -Path $this.SmartfilePath -ItemType File -Force
        Add-Content -Path $this.SmartfilePath -Value "customer,byte_offset,hex_value,value,field_name"

        $NvmeCustomerCode = switch ($this.CustomerCode) {
            "HP" { "NVME_HP" }
            "DELL" { "NVME_DELL" }
            default { "NVME_GEN" }
        }

        $smartData = $this.GetLogPage("NVME")
        $this.ParseSmartData($smartData, "NVME")
        $this.ParseSmartData($smartData, $NvmeCustomerCode)
        $smartData = $this.GetLogPage("HP")
        $this.ParseSmartData($smartData, "HP")
        $smartData = $this.GetLogPage("MS")
        $this.ParseSmartData($smartData, "MS")
        $smartData = $this.GetLogPage("LENOVO")
        $this.ParseSmartData($smartData, "LENOVO")
        $smartData = $this.GetLogPage("DELL")
        $this.ParseSmartData($smartData, "DELL")
        $smartData = $this.GetLogPage("SKH")
        $wai, $waf = $this.GetWaiWaf($smartData[512..($smartData.Length - 1)])
        Add-Content -Path $this.SmartfilePath -Value "WAI,000,$('{0:F6}' -f $wai),$('{0:F6}' -f $wai),[ETC] (ec_slc_total + ec_tlc_total) / write_from_host"
        Add-Content -Path $this.SmartfilePath -Value "WAF,000,$('{0:F6}' -f $waf),$('{0:F6}' -f $waf),[ETC] (written_to_tlc_user + written_to_slc_buf) / write_from_host"
    }
}


# Ensure $Global:TargetDeviceInfo is initialized
$Global:TargetDeviceInfo = @{
    NVMeInstanceId = "DEV_1F69"
}

$comparer = [SmartDataComparer]::new(1)
$logpagedata = $comparer.SaveSmartData("before")
$logpagedata = $comparer.SaveSmartData("after")

$fail, $warning = $comparer.CompareSmartData()
write-host "Fail: $fail, Warning: $warning"