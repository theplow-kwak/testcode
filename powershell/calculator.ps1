# include functions
. ".\uiautomation.ps1"

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
    ClickControl -windowElement $appWindow -controlName "Two"
    ClickControl -windowElement $appWindow -automationId "num6Button"
    ClickControl -windowElement $appWindow -controlName "Multiply by"
    ClickControl -windowElement $appWindow -controlName "Seven"
    ClickControl -windowElement $appWindow -automationId "num7Button"
    ClickControl -windowElement $appWindow -controlName "Equals"

    # Get the result text from the display using AutomationId or ControlName
    $resultText = GetResultText -windowElement $appWindow -automationId "CalculatorResults" -timeoutSeconds 30
    if ($null -ne $resultText) {
        Write-Host "Final result from the calculator: $resultText"
    }
    else {
        Write-Error "Failed to retrieve the result text."
    }

    # Check for popup
    $popupWindow = CheckForPopup -popupTitlePart "Alert" -timeoutSeconds 10
    if ($null -ne $popupWindow) {
        Write-Host "Popup detected and handled."
    }

    # Get the native window handle
    $windowHandle = Get-NativeWindowHandle -automationElement $appWindow

    # Restore and bring the window to the front
    Restore-WindowAndBringToFront -windowHandle $windowHandle
    
    # Capture screenshot of the window and save as 'window_capture.jpg'
    Capture-WindowScreenshot -windowHandle $windowHandle -outputFilePath "C:\path\to\save\window_capture.jpg"
      
    # Stop the Calculator process
    CloseApplicationByProcessId -processId $calcProcess.Id

}
else {
    Write-Error "Could not find the application window."
}
