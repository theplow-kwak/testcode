# Add necessary .NET types for window handling and screenshot
Add-Type @"
    using System;
    using System.Runtime.InteropServices;
    using System.Drawing;
    using System.Drawing.Imaging;
    using System.Windows.Forms;

    public class WindowHelper {
        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

        [DllImport("user32.dll")]
        public static extern IntPtr GetWindowRect(IntPtr hWnd, ref RECT rect);

        [DllImport("user32.dll")]
        public static extern bool PrintWindow(IntPtr hWnd, IntPtr hDC, uint nFlags);

        public const int SW_RESTORE = 9;
        public const int SW_SHOW = 5;

        public struct RECT {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        public static Bitmap CaptureWindow(IntPtr hWnd) {
            RECT rect = new RECT();
            GetWindowRect(hWnd, ref rect);
            int width = rect.Right - rect.Left;
            int height = rect.Bottom - rect.Top;

            Bitmap bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb);
            using (Graphics g = Graphics.FromImage(bitmap)) {
                IntPtr hdc = g.GetHdc();
                PrintWindow(hWnd, hdc, 0);
                g.ReleaseHdc(hdc);
            }
            return bitmap;
        }

        public static void SaveScreenshot(string filePath, Bitmap bitmap) {
            bitmap.Save(filePath, ImageFormat.Jpeg);  # Save as JPG
        }

        public static void RestoreAndBringToFront(IntPtr hWnd) {
            // Restore the window if it is minimized
            ShowWindow(hWnd, SW_RESTORE);
            // Bring the window to the foreground
            SetForegroundWindow(hWnd);
        }
    }
"@

# Function to capture and save a screenshot of a window using its handle
function Capture-WindowScreenshot {
    param (
        [IntPtr]$windowHandle,
        [string]$outputFilePath
    )

    # Capture the window as a bitmap
    $bitmap = [WindowHelper]::CaptureWindow($windowHandle)

    # Save the screenshot to the specified file path in JPG format
    [WindowHelper]::SaveScreenshot($outputFilePath, $bitmap)

    Write-Host "Screenshot saved to $outputFilePath as JPG"
}

# Function to get the native window handle (HWND) from AutomationElement
function Get-NativeWindowHandle {
    param (
        [System.Windows.Automation.AutomationElement]$automationElement
    )

    $handle = $automationElement.Current.NativeWindowHandle
    if ($handle -ne 0) {
        return [IntPtr]::new($handle)
    }
    else {
        throw "Failed to get the native window handle."
    }
}

# Function to restore and bring the application window to the front
function Restore-WindowAndBringToFront {
    param (
        [IntPtr]$windowHandle
    )

    # Call the helper function to restore and activate the window
    [WindowHelper]::RestoreAndBringToFront($windowHandle)
}

# Import necessary .NET namespaces for UI Automation
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName System.Windows.Forms

# Function to wait for a condition with a timeout
function WaitWithTimeout {
    param (
        [int]$timeoutSeconds = 30,
        [scriptblock]$action
    )

    $startTime = [DateTime]::Now
    while (([DateTime]::Now - $startTime).TotalSeconds -lt $timeoutSeconds) {
        $result = & $action
        if ($null -ne $result) {
            return $result
        }
        Start-Sleep -Milliseconds 500
    }
    Write-Error "Timeout of $timeoutSeconds seconds exceeded while waiting for condition."
    return $null
}

# Function to get the main application window by partial title
function Get-ApplicationWindow { 
    param ( 
        [string]$partialWindowTitle, 
        [int]$timeoutSeconds = 30 
    ) 
    
    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action { 
        $rootElement = [System.Windows.Automation.AutomationElement]::RootElement 
        $windows = $rootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition) 
        foreach ($window in $windows) { 
            # $windowName = $window.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::NameProperty) 
            if ($window.Current.Name -like "*$partialWindowTitle*") { 
                Write-Host "Window found: $windowName" 
                return $window 
            } 
        } 
        return $null 
    } 
} 

# Function to click a control using AutomationId or ControlName
function ClickControl {
    param (
        [System.Windows.Automation.AutomationElement]$windowElement,
        [string]$automationId = $null,
        [string]$controlName = $null
    )

    Write-Host "Searching for control with AutomationId '$automationId' or ControlName '$controlName'..."

    if (-not [string]::IsNullOrEmpty($automationId)) {
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $automationId)
        $ButtonName = $automationId
    }
    elseif (-not [string]::IsNullOrEmpty($controlName)) {
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $controlName)
        $ButtonName = $controlName
    }
    else {
        throw "Either automationId or controlName must be provided."
    }

    $control = $windowElement.FindFirst([System.Windows.Automation.TreeScope]::Subtree, $condition)

    if ($null -ne $control) {
        $controlType = $control.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::ControlTypeProperty)

        switch ($controlType) {
            { $_ -eq [System.Windows.Automation.ControlType]::Button } {
                $invokePattern = $control.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
                $invokePattern.Invoke()
                Write-Host "Button '$ButtonName' clicked."
            }
            { $_ -eq [System.Windows.Automation.ControlType]::RadioButton } {
                $selectPattern = $control.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
                $selectPattern.Select()
                Write-Host "RadioButton '$ButtonName' selected."
            }
            default {
                Write-Error "Control '$ButtonName' is not a supported type (Button or RadioButton)."
            }
        }
    }
    else {
        Write-Error "Control '$ButtonName' not found."
    }
}

# Function to get result text from a specific control using AutomationId or ControlName
function GetResultText {
    param (
        [System.Windows.Automation.AutomationElement]$windowElement,
        [string]$automationId = $null,
        [string]$controlName = $null,
        [int]$timeoutSeconds = 30
    )

    Write-Host "Searching for result text with AutomationId '$automationId' or ControlName '$controlName'..."

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        if (-not [string]::IsNullOrEmpty($automationId)) {
            $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $automationId)
        }
        elseif (-not [string]::IsNullOrEmpty($controlName)) {
            $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $controlName)
        }
        else {
            throw "Either automationId or controlName must be provided."
        }

        $textControl = $windowElement.FindFirst([System.Windows.Automation.TreeScope]::Subtree, $condition)

        if ($null -ne $textControl) {
            $resultValue = $textControl.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::NameProperty)
            Write-Host "Result found: $resultValue"
            return $resultValue
        }
        return $null
    }
}

# Wait for a specific text to appear in the window (with timeout)
function WaitForTextInWindow {
    param (
        [System.Windows.Automation.AutomationElement]$windowElement,
        [string]$textToFind,
        [int]$timeoutSeconds = 30
    )

    Write-Host "Waiting for text '$textToFind' to appear..."

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Text)
        $textElements = $windowElement.FindAll([System.Windows.Automation.TreeScope]::Subtree, $condition)

        foreach ($element in $textElements) {
            $textValue = $element.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::NameProperty)

            if ($textValue -like "*$textToFind*") {
                Write-Host "Text '$textToFind' found!"
                return $textValue
            }
        }
        return $null
    }
}

# Function to check for popup windows
function CheckForPopup {
    param (
        [string]$popupTitlePart,
        [int]$timeoutSeconds = 10
    )

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $desktop = [System.Windows.Automation.AutomationElement]::RootElement
        $condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $popupTitlePart, [System.Windows.Automation.PropertyConditionFlags]::IgnoreCase)
        $popupWindow = $desktop.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition)

        if ($null -ne $popupWindow) {
            Write-Host "Popup window with title containing '$popupTitlePart' found."
            return $popupWindow
        }
        return $null
    }
}

# Function to close an application by its process ID
function CloseApplicationByProcessId {
    param (
        [int]$processId
    )

    try {
        Stop-Process -Id $processId -Force
        Write-Host "Application with Process ID $processId has been closed."
    }
    catch {
        Write-Error "Failed to close application with Process ID $processId."
    }
}
