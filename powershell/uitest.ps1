# include functions
. .\uiautomation.ps1

$window = Get-ApplicationWindow -partialWindowTitle "*FreeCommander XE"
if ($null -ne $window) {
    CheckForPopup -rootElement $window -partialWindowTitle "FreeCommander"
}