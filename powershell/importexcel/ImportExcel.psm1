﻿#region import everything we need
$culture = $host.CurrentCulture.Name -replace '-\w*$', ''
Import-LocalizedData  -UICulture $culture -BindingVariable Strings -FileName Strings -ErrorAction Ignore
if (-not $Strings) {
    Import-LocalizedData  -UICulture "en" -BindingVariable Strings -FileName Strings -ErrorAction Ignore
}
try { [void] [System.Reflection.Assembly]::LoadWithPartialName("System.Drawing") }
catch { Write-Warning -Message $Strings.SystemDrawingAvailable }

foreach ($directory in @('Public')) {
    Get-ChildItem -Path "$PSScriptRoot\$directory\*.ps1" | ForEach-Object { . $_.FullName }
}

#endregion

if (($IsLinux -or $IsMacOS) -or $env:NoAutoSize) {
    $ExcelPackage = [OfficeOpenXml.ExcelPackage]::new()
    $Cells = ($ExcelPackage | Add-Worksheet).Cells['A1']
    $Cells.Value = 'Test'
    try {
        $Cells.AutoFitColumns()
        if ($env:NoAutoSize) { Remove-Item Env:\NoAutoSize }
    }
    catch {
        $env:NoAutoSize = $true
        if ($IsLinux) {
            $msg = @"
ImportExcel Module Cannot Autosize. Please run the following command to install dependencies:
apt-get -y update && apt-get install -y --no-install-recommends libgdiplus libc6-dev
"@
            Write-Warning -Message $msg
        }
        if ($IsMacOS) {
            $msg = @"
ImportExcel Module Cannot Autosize. Please run the following command to install dependencies:
brew install mono-libgdiplus
"@
            Write-Warning -Message $msg
        }
    }
    finally {
        $ExcelPackage | Close-ExcelPackage -NoSave
    }
}
