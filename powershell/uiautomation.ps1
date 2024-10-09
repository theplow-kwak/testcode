Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName System.Windows.Forms

# Utility function to handle timeouts
function WaitWithTimeout {
    param (
        [int]$timeoutSeconds,
        [scriptblock]$action
    )

    $startTime = Get-Date
    $endTime = $startTime.AddSeconds($timeoutSeconds)

    while ((Get-Date) -lt $endTime) {
        $result = & $action
        if ($null -ne $result) {
            return $result
        }
        Start-Sleep -Seconds 1
    }

    Write-Error "Timeout after $timeoutSeconds seconds."
    return $null
}

# Find application window by partial window title (with timeout)
function Get-ApplicationWindow {
    param (
        [string]$partialWindowTitle,
        [int]$timeoutSeconds = 30
    )

    Write-Host "Searching for window with title containing '$partialWindowTitle'..."

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $rootElement = [System.Windows.Automation.AutomationElement]::RootElement
        $windows = $rootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)

        foreach ($window in $windows) {
            $windowName = $window.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::NameProperty)

            if ($windowName -like "*$partialWindowTitle*") {
                Write-Host "Window found: $windowName"
                return $window
            }
        }
        return $null
    }
}

# Universal function to click Button or select RadioButton
function ClickControl {
    param (
        [System.Windows.Automation.AutomationElement]$windowElement,
        [string]$controlName
    )

    Write-Host "Searching for control: '$controlName'..."

    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $controlName)
    $control = $windowElement.FindFirst([System.Windows.Automation.TreeScope]::Subtree, $condition)

    if ($null -ne $control) {
        $controlType = $control.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::ControlTypeProperty)

        switch ($controlType) {
            { $_ -eq [System.Windows.Automation.ControlType]::Button } {
                $invokePattern = $control.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
                $invokePattern.Invoke()
                Write-Host "Button '$controlName' clicked."
            }
            { $_ -eq [System.Windows.Automation.ControlType]::RadioButton } {
                $selectPattern = $control.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
                $selectPattern.Select()
                Write-Host "RadioButton '$controlName' selected."
            }
            default {
                Write-Error "Control '$controlName' is not a supported type (Button or RadioButton)."
            }
        }
    } else {
        Write-Error "Control '$controlName' not found."
    }
}

# Wait for a specific text to appear in the window (with timeout)
function WaitForTextInWindow {
    param (
        [System.Windows.Automation.AutomationElement]$windowElement,
        [string]$textToFind,
        [int]$timeoutSeconds = 30
    )

    Write-Host "Waiting for text '$textToFind' to appear..."

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Text)
        $textElements = $windowElement.FindAll([System.Windows.Automation.TreeScope]::Subtree, $condition)

        foreach ($element in $textElements) {
            $textValue = $element.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::NameProperty)

            if ($textValue -like "*$textToFind*") {
                Write-Host "Text '$textToFind' found!"
                return $textValue
            }
        }
        return $null
    }
}

# Retrieve any text from the window (with timeout)
function GetTextFromWindow {
    param (
        [System.Windows.Automation.AutomationElement]$windowElement,
        [int]$timeoutSeconds = 30
    )

    Write-Host "Retrieving text from window..."

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Text)
        $textElements = $windowElement.FindAll([System.Windows.Automation.TreeScope]::Subtree, $condition)

        if ($textElements.Count -gt 0) {
            $texts = @()
            foreach ($element in $textElements) {
                $textValue = $element.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::NameProperty)
                $texts += $textValue
            }
            Write-Host "Text found: $($texts -join ', ')"
            return $texts -join ', '
        }
        return $null
    }
}

# Function to get result from a specific control in the window
function GetResultText {
    param (
        [System.Windows.Automation.AutomationElement]$windowElement,
        [string]$controlName,   # The name of the result text control (e.g., "Result" or "Display")
        [int]$timeoutSeconds = 30
    )

    Write-Host "Searching for result text '$controlName'..."

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        # Find the result Text control by its name
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $controlName)
        $textControl = $windowElement.FindFirst([System.Windows.Automation.TreeScope]::Subtree, $condition)

        if ($null -ne $textControl) {
            $resultValue = $textControl.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::NameProperty)
            Write-Host "Result found: $resultValue"
            return $resultValue
        }
        return $null
    }
}

# Detect a popup window with a partial title (with timeout)
function CheckForPopup {
    param (
        [string]$popupTitlePart,
        [int]$timeoutSeconds = 10
    )

    Write-Host "Waiting for popup with title containing '$popupTitlePart'..."

    return Get-ApplicationWindow -partialWindowTitle $popupTitlePart -timeoutSeconds $timeoutSeconds
}

# Example: Find application window and interact with controls
Start-Process -FilePath calc
$partialWindowTitle = "Calculator"
$timeoutSeconds = 20
$appWindow = Get-ApplicationWindow -partialWindowTitle $partialWindowTitle -timeoutSeconds $timeoutSeconds

if ($null -ne $appWindow) {
    Write-Host "Application window found. Proceeding with actions..."

    # Example of clicking buttons and selecting a radio button
    ClickControl -windowElement $appWindow -controlName "Five"
    ClickControl -windowElement $appWindow -controlName "Two"
    ClickControl -windowElement $appWindow -controlName "Six"
    ClickControl -windowElement $appWindow -controlName "Multiply by"
    ClickControl -windowElement $appWindow -controlName "Seven"
    ClickControl -windowElement $appWindow -controlName "Seven"
    ClickControl -windowElement $appWindow -controlName "Equals"

    # Wait for specific result text to appear
    $resultText = GetResultText -windowElement $appWindow -controlName "CalculatorResults" -timeoutSeconds 30
    if ($null -ne $resultText) {
        Write-Host "Result text: $resultText"
    }

    # Retrieve any text from the window
    $retrievedText = GetTextFromWindow -windowElement $appWindow -timeoutSeconds 30
    if ($null -ne $retrievedText) {
        Write-Host "Text retrieved: $retrievedText"
    }

    # Check for popup
    $popupWindow = CheckForPopup -popupTitlePart "Alert" -timeoutSeconds 10
    if ($null -ne $popupWindow) {
        Write-Host "Popup detected and handled."
    }

} else {
    Write-Error "Could not find the application window."
}
