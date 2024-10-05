Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName System.Windows.Forms

# 계산기 실행
Start-Process calc
Start-Sleep -Seconds 3  # 계산기가 실행될 시간을 줍니다.

# 계산기 창 찾기
$desktop = [System.Windows.Automation.AutomationElement]::RootElement
$condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, "계산기")
$calcWindow = $desktop.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition)

if ($null -eq $calcWindow) {
    Write-Host "can not found calc"
    exit
}

# 버튼 클릭 함수
function Click-Button {
    param([string]$buttonName)
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $buttonName)
    $button = $calcWindow.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
    
    if ($null -ne $button) {
        $invokePattern = $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
        $invokePattern.Invoke()
    }
    else {
        Write-Host "버튼 $buttonName 을(를) 찾을 수 없습니다."
    }
}

# 버튼 클릭: 5, 2, 6, ×, 7, 7, =
Click-Button "5"
Click-Button "2"
Click-Button "6"
Click-Button "*"  # Windows 설정에 따라 "곱하기" 대신 "Multiply"일 수 있음.
Click-Button "7"
Click-Button "7"
Click-Button "="  # Windows 설정에 따라 "같음" 대신 "Equals"일 수 있음.

# 결과 읽기
$conditionResult = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, "CalculatorResults")
$result = $calcWindow.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $conditionResult)

if ($null -ne $result) {
    $valuePattern = $result.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
    $output = $valuePattern.Current.Value
    $result = $calcUI.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
    $result.current.name
    Write-Host "result: $output"
}
else {
    Write-Host "can not found result"
}
