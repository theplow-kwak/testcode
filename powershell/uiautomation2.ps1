# UIAutomation 모듈 로드
Import-Module UIAutomation

# 계산기 실행
Start-Process calc

# 3초 대기 (계산기 앱이 실행될 시간을 줌)
Start-Sleep -Seconds 3

# 계산기 창 가져오기
$window = Get-UIAWindow -n "계산기"

# 버튼 클릭 함수
function Click-CalcButton {
    param([string]$buttonName)
    $window | Get-UIAButton -Name $buttonName | Invoke-UIAButtonClick
}

# "5", "2", "6", "×", "7", "7", "=" 버튼 클릭
Click-CalcButton "5"
Click-CalcButton "2"
Click-CalcButton "6"
Click-CalcButton "곱하기"
Click-CalcButton "7"
Click-CalcButton "7"
Click-CalcButton "같음"

# 1초 대기 (결과가 나올 시간을 줌)
Start-Sleep -Seconds 1

# 결과 가져오기
$result = $window | Get-UIAText -Name "계산 결과"
Write-Host "계산 결과: $($result.Name)"
