Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName System.Windows.Forms

# ���� ����
Start-Process calc
Start-Sleep -Seconds 3  # ���Ⱑ ����� �ð��� �ݴϴ�.

# ���� â ã��
$desktop = [System.Windows.Automation.AutomationElement]::RootElement
$condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, "����")
$calcWindow = $desktop.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition)

if ($null -eq $calcWindow) {
    Write-Host "can not found calc"
    exit
}

# ��ư Ŭ�� �Լ�
function Click-Button {
    param([string]$buttonName)
    $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $buttonName)
    $button = $calcWindow.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
    
    if ($null -ne $button) {
        $invokePattern = $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
        $invokePattern.Invoke()
    }
    else {
        Write-Host "��ư $buttonName ��(��) ã�� �� �����ϴ�."
    }
}

# ��ư Ŭ��: 5, 2, 6, ��, 7, 7, =
Click-Button "5"
Click-Button "2"
Click-Button "6"
Click-Button "*"  # Windows ������ ���� "���ϱ�" ��� "Multiply"�� �� ����.
Click-Button "7"
Click-Button "7"
Click-Button "="  # Windows ������ ���� "����" ��� "Equals"�� �� ����.

# ��� �б�
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
