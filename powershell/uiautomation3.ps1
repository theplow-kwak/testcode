Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$calc = [Diagnostics.Process]::Start('calc')
#wait for the UI to appear
$null = $calc.WaitForInputIdle(5000)
sleep -s 2
$calcWindowId = ((Get-Process).where{$_.MainWindowTitle -eq '°è»ê±â'})[0].Id
$root = [Windows.Automation.AutomationElement]::RootElement
$condition = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ProcessIdProperty, $calcWindowId)
$calcUI = $root.FindFirst([Windows.Automation.TreeScope]::Children, $condition)

function FindAndClickButton($name){
	$condition1 = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ClassNameProperty, "Button")
	$condition2 = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::NameProperty, $name)
	$condition = New-Object Windows.Automation.AndCondition($condition1, $condition2)
	$button = $calcUI.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
	$button.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
}

#get and click the buttons for the calculation

FindAndClickButton "5"
FindAndClickButton "Plus"
FindAndClickButton "9"
FindAndClickButton "="

#get the result
$condition = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::AutomationIdProperty, "CalculatorResults")
$result = $calcUI.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
$result.current.name