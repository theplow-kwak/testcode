param (
    [string]$Title,
    [string]$SubTitle
)

# include functions
. .\uiautomation.ps1

$window = Get-ApplicationWindow -partialWindowTitle $Title
if ($null -ne $window) {
    CheckForPopup -rootElement $window -partialWindowTitle $SubTitle
}