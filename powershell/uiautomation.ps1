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
    Write-Error "Timeout of $timeoutSeconds seconds exceeded while waiting for condition."
    return $null
}

# Function to get the main application window by partial title
function Get-ApplicationWindow { 
    param ( 
        [string]$partialWindowTitle, 
        [int]$timeoutSeconds = 30 
    ) 
    
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

# Function to click a control using AutomationId or ControlName
function ClickControl {
    param (
        [System.Windows.Automation.AutomationElement]$windowElement,
        [string]$automationId = $null,
        [string]$controlName = $null
    )

    Write-Host "Searching for control with AutomationId '$automationId' or ControlName '$controlName'..."

    if (-not [string]::IsNullOrEmpty($automationId)) {
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $automationId)
        $ButtonName = $automationId
    } elseif (-not [string]::IsNullOrEmpty($controlName)) {
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $controlName)
        $ButtonName = $controlName
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
                Write-Host "Button '$ButtonName' clicked."
            }
            { $_ -eq [System.Windows.Automation.ControlType]::RadioButton } {
                $selectPattern = $control.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
                $selectPattern.Select()
                Write-Host "RadioButton '$ButtonName' selected."
            }
            default {
                Write-Error "Control '$ButtonName' is not a supported type (Button or RadioButton)."
            }
        }
    } else {
        Write-Error "Control '$ButtonName' not found."
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
        if (-not [string]::IsNullOrEmpty($automationId)) {
            $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $automationId)
        } elseif (-not [string]::IsNullOrEmpty($controlName)) {
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
