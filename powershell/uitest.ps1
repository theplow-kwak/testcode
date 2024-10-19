# Import necessary .NET namespaces for UI Automation
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing


function Get-ApplicationWindow {
    param (
        [string]$partialWindowTitle = "xxx",
        [int]$timeoutSeconds = 30
    )

    $rootElement = [Windows.Automation.AutomationElement]::RootElement
    $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Window)
    $windows = $rootElement.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
    foreach ($window in $windows) {
        $controlType = $window.GetCurrentPropertyValue([Windows.Automation.AutomationElement]::ControlTypeProperty)
        if ($controlType -eq [Windows.Automation.ControlType]::Window -or $controlType -eq [Windows.Automation.ControlType]::Dialog -or $controlType -eq [Windows.Automation.ControlType]::Pane) {
            $windowName = $window.GetCurrentPropertyValue([Windows.Automation.AutomationElement]::NameProperty)
            Write-Host "Name of popup : $windowName"
        }
        Write-Host "      : $($window.Current.LocalizedControlType)- $($window.Current.Name)"
        if ($window.Current.Name -like "$partialWindowTitle*") {
            # Write-Host "Window found: $partialWindowTitle"
            return $window
        }
    }
    return $null
}

$t = Get-ApplicationWindow # -partialWindowTitle "Calculator"