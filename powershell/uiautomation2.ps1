# UIAutomation ��� �ε�
Import-Module UIAutomation

# ���� ����
Start-Process calc

# 3�� ��� (���� ���� ����� �ð��� ��)
Start-Sleep -Seconds 3

# ���� â ��������
$window = Get-UIAWindow -n "����"

# ��ư Ŭ�� �Լ�
function Click-CalcButton {
    param([string]$buttonName)
    $window | Get-UIAButton -Name $buttonName | Invoke-UIAButtonClick
}

# "5", "2", "6", "��", "7", "7", "=" ��ư Ŭ��
Click-CalcButton "5"
Click-CalcButton "2"
Click-CalcButton "6"
Click-CalcButton "���ϱ�"
Click-CalcButton "7"
Click-CalcButton "7"
Click-CalcButton "����"

# 1�� ��� (����� ���� �ð��� ��)
Start-Sleep -Seconds 1

# ��� ��������
$result = $window | Get-UIAText -Name "��� ���"
Write-Host "��� ���: $($result.Name)"
