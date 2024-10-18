# include functions
. .\uiautomation.ps1

$CheckError = {
    param(
        $APP_NAME,
        $libfile
    )
    . $libfile

    $errorCount = 0
    $result = @()
    $appWindow = Get-ApplicationWindow -partialWindowTitle $APP_NAME -timeoutSeconds 30
    $windowHandle = Get-NativeWindowHandle -automationElement $appWindow
    $startTime = [DateTime]::Now

    do {
        foreach ($_ in @("ERROR")) {
            $popupWindow = CheckForPopup -popupTitlePart $_
            if ($null -ne $popupWindow) {
                $popup_msg = Get-ResultText -windowElement $popupWindow -AutomationId "65535" -timeoutSeconds 30
                $result += $popup_msg
                Write-Host "Popup message : $popup_msg"
                Get-WindowScreenshot -outputFilePath "\temp\screenshot_err$errorCount.jpg"
                $errorCount += 1
                $tmp = ClickControl -windowElement $popupWindow -controlName "OK"
            }
        }   
        if (($errorCount -ne 0) -and (([DateTime]::Now - $startTime).TotalSeconds -gt 100)) {
            break
        }
        Start-Sleep -Milliseconds 1000
    } while ($true)

    # Capture the result screen
    Restore-WindowAndBringToFront -windowHandle $windowHandle
    Start-Sleep -Milliseconds 3000
    Get-WindowScreenshot -windowHandle $windowHandle -outputFilePath "\temp\app_error.jpg"
    Get-WindowScreenshot -outputFilePath "\temp\screenshot_err.jpg"

    $tmp = ClickControl -windowElement $appWindow -automationId "Close"
    return ($result)
}

# Main automation flow

# Start Calculator
$calcProcess = Start-Process -FilePath "calc.exe" -PassThru
Write-Host "Calculator started. Process ID: $($calcProcess.Id)"

# Example: Find application window and interact with controls using AutomationId or ControlName
$partialWindowTitle = "Calculator"
$timeoutSeconds = 20
$appWindow = Get-ApplicationWindow -partialWindowTitle $partialWindowTitle -timeoutSeconds $timeoutSeconds

if ($null -eq $appWindow) {
    Write-Error "Could not find the application window."
}

$job = Start-Job -ScriptBlock $CheckError -ArgumentList $appWindow, (Resolve-Path .\uiautomation.ps1).Path

Write-Host "Application window found. Proceeding with actions..."

# Example of clicking buttons and selecting a radio button
ClickControl -windowElement $appWindow -controlName "Five"
Start-Sleep -Milliseconds 500
ClickControl -windowElement $appWindow -controlName "Two"
Start-Sleep -Milliseconds 500
ClickControl -windowElement $appWindow -automationId "num6Button"
Start-Sleep -Milliseconds 500
ClickControl -windowElement $appWindow -controlName "Multiply by"
Start-Sleep -Milliseconds 500
ClickControl -windowElement $appWindow -controlName "Seven"
Start-Sleep -Milliseconds 500
ClickControl -windowElement $appWindow -automationId "num7Button"
Start-Sleep -Milliseconds 500
ClickControl -windowElement $appWindow -controlName "Equals"
Start-Sleep -Milliseconds 500

# Get the result text from the display using AutomationId or ControlName
$resultText = Get-ResultText -windowElement $appWindow -automationId "CalculatorResults" -timeoutSeconds 30
if ($null -ne $resultText) {
    Write-Host "Final result from the calculator: $resultText"
}
else {
    Write-Error "Failed to retrieve the result text."
}

$result_checkerr = Receive-Job $job
if ($null -ne $result_checkerr) {
    Write-Error "$($result_checkerr -join [Environment]::NewLine)"
}
Stop-Job    $job

# Check for popup
# $popupWindow = CheckForPopup -popupTitlePart "Alert" -timeoutSeconds 10
# if ($null -ne $popupWindow) {
#     Write-Host "Popup detected and handled."
# }
Start-Sleep -Milliseconds 5000

# Get the native window handle
$windowHandle = Get-NativeWindowHandle -automationElement $appWindow
  
# Capture screenshot of the window and save as 'window_capture.jpg'
Restore-WindowAndBringToFront -windowHandle $windowHandle
Start-Sleep -Milliseconds 500
Get-WindowScreenshot -windowHandle $windowHandle -outputFilePath "\temp\window_capture.jpg"
Get-WindowScreenshot -outputFilePath "\temp\screenshot.jpg"
Start-Sleep -Milliseconds 500

# Stop the Calculator process
ClickControl -windowElement $appWindow -automationId "Close"
