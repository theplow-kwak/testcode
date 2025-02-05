param (
    [string]$Title = "notepad",
    [string]$SubTitle
)

# include functions
. .\uiautomation.ps1

function SearchControl {
    param (
        [Windows.Automation.AutomationElement]$windowElement,
        [string]$automationId = $null,
        [string]$controlName = $null,
        [int]$timeoutSeconds = 30
    )

    if (-not [string]::IsNullOrEmpty($automationId)) {
        $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $automationId)
    }
    elseif (-not [string]::IsNullOrEmpty($controlName)) {
        $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $controlName)
    }
    else {
        $condition = [Windows.Automation.Condition]::TrueCondition
    }
    $type_text = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Text)
    $type_button = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Button)
    $type_radio = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::RadioButton)
    $type_document = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Document)
    $condition_type = [Windows.Automation.OrCondition]::new($type_text, $type_button, $type_radio, $type_document)

    # $elements = $windowElement.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.AndCondition]::new($condition, $condition_type))
    $elements = $windowElement.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition)
    foreach ($element in $elements) {
        Write-Host "$($element.Current.LocalizedControlType) : $($element.Current.AutomationId) - $($element.Current.Name),  $($element.Current.AcceleratorKey)"
    }
    return
}

# $window = Get-ApplicationWindow -WindowClassName "Shell_TrayWnd"
# SearchControl -windowElement $window
# ClickControl -windowElement $window -controlName "Show Hidden Icons"
# $window = Get-ApplicationWindow -partialWindowTitle "System tray overflow window." -timeoutSeconds 0
# SearchControl -windowElement $window

# $icons = $window.FindAll([System.Windows.Automation.TreeScope]::Children, (New-Object System.Windows.Automation.PropertyCondition ([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Button)))    # 툴바에서 아이콘 찾기
# foreach ($icon in $icons) {
#     $name = $icon.Current.Name
#     Write-Output "Icon: $name"
# }

# $testProcess = Start-Process -FilePath "c:\utility\DiskMark64.exe" -PassThru
# Write-Host "Calculator started. Process ID: $($testProcess.Id)"

# Example: Find application window and interact with controls using AutomationId or ControlName
$partialWindowTitle = " CrystalDiskMark"
$appWindow = Get-ApplicationWindow -partialWindowTitle $partialWindowTitle
if ($null -eq $appWindow) {
    Write-Error "Could not find the application window."
}
# SearchControl -windowElement $appWindow
# $menubar = Get-ControlElement -windowElement $appWindow -automationId "MenuBar"
# Write-Host "SearchControl"
# SearchControl -windowElement $menubar
Write-Host "SelectMenuItem"
SelectMenuItem -windowElement $appWindow -automationId "MenuBar" -menuPath @("File", "Save (text)")

# if ($null -ne $window) {
#     $licensewindow = Get-ApplicationWindow -rootElement $window -partialWindowTitle "BurnInTest by PassMark Software" -timeoutSeconds 0
#     if ($null -ne $licensewindow) {
#         $license = Get-Content .\sn.txt

#         $type_document = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Document)
#         $documentElement = $licensewindow.FindFirst([Windows.Automation.TreeScope]::Descendants, $type_document) 
#         $documentElement.SetFocus()
#         [System.Windows.Forms.SendKeys]::SendWait($license)
#         ClickControl -windowElement $licensewindow -controlName "Continue"

#         $thanksewindow = Get-ApplicationWindow -rootElement $licensewindow -partialWindowTitle "Thanks" -timeoutSeconds 0
#         ClickControl -windowElement $thanksewindow -controlName "OK"
#     }
# }