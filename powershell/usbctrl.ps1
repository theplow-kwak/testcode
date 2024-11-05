# Set the search criteria for the device (e.g., by hardware ID)
$targetDeviceID = "USB\VID_04E8&PID_6860&REV_0504"  # Replace with the actual hardware ID

# Get device information using WMI
$devices = Get-WmiObject Win32_PnPEntity

# Change the active state of the found device (Disable)
foreach ($device in $devices) {
    Write-Verbose "$($device.Name) : $($device.HardwareID)"
    if ($device.HardwareID -like "*$targetDeviceID*") {
        $device.PNPDeviceID
        $device.Disable()
        Write-Host "The device has been disabled."
        Start-Sleep -Seconds 2
        $device.Enable()
        Write-Host "The device has been enabled."
    }
}
