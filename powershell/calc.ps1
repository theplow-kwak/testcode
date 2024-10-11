# include functions
. .\uiautomation.ps1

# Main automation flow

# Start Calculator
$calcProcess = Start-Process -FilePath "calc.exe" -PassThru
Write-Host "Calculator started. Process ID: $($calcProcess.Id)"

# Example: Find application window and interact with controls using AutomationId or ControlName
$partialWindowTitle = "Calculator"
$timeoutSeconds = 20
$appWindow = Get-ApplicationWindow -partialWindowTitle $partialWindowTitle -timeoutSeconds $timeoutSeconds

if ($null -ne $appWindow) {
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
    $resultText = GetResultText -windowElement $appWindow -automationId "CalculatorResults" -timeoutSeconds 30
    if ($null -ne $resultText) {
        Write-Host "Final result from the calculator: $resultText"
    }
    else {
        Write-Error "Failed to retrieve the result text."
    }

    # Check for popup
    # $popupWindow = CheckForPopup -popupTitlePart "Alert" -timeoutSeconds 10
    # if ($null -ne $popupWindow) {
    #     Write-Host "Popup detected and handled."
    # }
    Start-Sleep -Milliseconds 2000

    # Get the native window handle
    $windowHandle = Get-NativeWindowHandle -automationElement $appWindow

    # Restore and bring the window to the front
    Restore-WindowAndBringToFront -windowHandle $windowHandle
    Start-Sleep -Milliseconds 2000

    [WindowHelper]::MinimizeWindow($windowHandle)
    Start-Sleep -Milliseconds 2000
    
    Restore-WindowAndBringToFront -windowHandle $windowHandle
    Start-Sleep -Milliseconds 2000
    [WindowHelper]::MoveWindow($windowHandle, 100, 200)
    
    # Capture screenshot of the window and save as 'window_capture.jpg'
    Capture-WindowScreenshot -windowHandle $windowHandle -outputFilePath ".\window_capture.jpg"
    Start-Sleep -Milliseconds 500

    # Stop the Calculator process
    ClickControl -windowElement $appWindow -controlName "Close Calculator"
}
else {
    Write-Error "Could not find the application window."
}
