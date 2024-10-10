# Import necessary .NET namespaces for UI Automation
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName System.Windows.Forms

# Function to wait for a condition with a timeout
function WaitWithTimeout {
    param (
        [int]$timeoutSeconds = 30,
        [scriptblock]$action
    )

    $startTime = [DateTime]::Now
    while (([DateTime]::Now - $startTime).TotalSeconds -lt $timeoutSeconds) {
        $result = & $action
        if ($null -ne $result) {
            return $result
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Timeout of $timeoutSeconds seconds exceeded while waiting for condition."
}

# Function to get the main application window by partial title
function Get-ApplicationWindow {
    param (
        [string]$partialWindowTitle,
        [int]$timeoutSeconds = 30
    )

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $partialWindowTitle, [System.Windows.Automation.PropertyConditionFlags]::IgnoreCase)
        $desktop = [System.Windows.Automation.AutomationElement]::RootElement
        $appWindow = $desktop.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition)
        if ($null -ne $appWindow) {
            return $appWindow
        }
        return $null
    }
}

# Function to click a control using AutomationId or ControlName
function ClickControl {
    param (
        [System.Windows.Automation.AutomationElement]$windowElement,
        [string]$automationId = $null,
        [string]$controlName = $null
    )

    Write-Host "Searching for control with AutomationId '$automationId' or ControlName '$controlName'..."

    if ($null -ne $automationId) {
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $automationId)
    } elseif ($null -ne $controlName) {
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $controlName)
    } else {
        throw "Either automationId or controlName must be provided."
    }

    $control = $windowElement.FindFirst([System.Windows.Automation.TreeScope]::Subtree, $condition)

    if ($null -ne $control) {
        $controlType = $control.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::ControlTypeProperty)

        switch ($controlType) {
            { $_ -eq [System.Windows.Automation.ControlType]::Button } {
                $invokePattern = $control.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
                $invokePattern.Invoke()
                Write-Host "Button clicked."
            }
            { $_ -eq [System.Windows.Automation.ControlType]::RadioButton } {
                $selectPattern = $control.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
                $selectPattern.Select()
                Write-Host "RadioButton selected."
            }
            default {
                Write-Error "Control is not a supported type (Button or RadioButton)."
            }
        }
    } else {
        Write-Error "Control not found."
    }
}

# Function to get result text from a specific control using AutomationId or ControlName
function GetResultText {
    param (
        [System.Windows.Automation.AutomationElement]$windowElement,
        [string]$automationId = $null,
        [string]$controlName = $null,
        [int]$timeoutSeconds = 30
    )

    Write-Host "Searching for result text with AutomationId '$automationId' or ControlName '$controlName'..."

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        if ($null -ne $automationId) {
            $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $automationId)
        } elseif ($null -ne $controlName) {
            $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $controlName)
        } else {
            throw "Either automationId or controlName must be provided."
        }

        $textControl = $windowElement.FindFirst([System.Windows.Automation.TreeScope]::Subtree, $condition)

        if ($null -ne $textControl) {
            $resultValue = $textControl.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::NameProperty)
            Write-Host "Result found: $resultValue"
            return $resultValue
        }
        return $null
    }
}

# Function to check for popup windows
function CheckForPopup {
    param (
        [string]$popupTitlePart,
        [int]$timeoutSeconds = 10
    )

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $desktop = [System.Windows.Automation.AutomationElement]::RootElement
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $popupTitlePart, [System.Windows.Automation.PropertyConditionFlags]::IgnoreCase)
        $popupWindow = $desktop.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition)

        if ($null -ne $popupWindow) {
            Write-Host "Popup window with title containing '$popupTitlePart' found."
            return $popupWindow
        }
        return $null
    }
}

# Function to close an application by its process ID
function CloseApplicationByProcessId {
    param (
        [int]$processId
    )

    try {
        Stop-Process -Id $processId -Force
        Write-Host "Application with Process ID $processId has been closed."
    } catch {
        Write-Error "Failed to close application with Process ID $processId."
    }
}

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

    # Example of clicking buttons using AutomationId or ControlName
    ClickControl -windowElement $appWindow -automationId "num5Button"
    ClickControl -windowElement $appWindow -automationId "num2Button"
    ClickControl -windowElement $appWindow -automationId "num6Button"
    ClickControl -windowElement $appWindow -automationId "multiplyButton"
    ClickControl -windowElement $appWindow -automationId "num7Button"
    ClickControl -windowElement $appWindow -automationId "num7Button"
    ClickControl -windowElement $appWindow -automationId "equalButton"

    # Get the result text from the display using AutomationId or ControlName
    $resultText = GetResultText -windowElement $appWindow -automationId "CalculatorResults" -timeoutSeconds 30
    if ($null -ne $resultText) {
        Write-Host "Final result from the calculator: $resultText"
    } else {
        Write-Error "Failed to retrieve the result text."
    }

    # Check for popup
    $popupWindow = CheckForPopup -popupTitlePart "Alert" -timeoutSeconds 10
    if ($null -ne $popupWindow) {
        Write-Host "Popup detected and handled."
    }

    # Stop the Calculator process
    CloseApplicationByProcessId -processId $calcProcess.Id

} else {
    Write-Error "Could not find the application window."
}
