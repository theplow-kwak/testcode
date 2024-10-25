# Import necessary .NET namespaces for UI Automation
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# Add necessary .NET types for window handling and screenshot
Add-Type @"
    using System;
    using System.Runtime.InteropServices;
    using System.Drawing;

    public class WindowHelper {
        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

        [DllImport("user32.dll")]
        public static extern IntPtr GetWindowRect(IntPtr hWnd, ref RECT rect);

        [DllImport("user32.dll")]
        public static extern bool PrintWindow(IntPtr hWnd, IntPtr hDC, uint nFlags);

        public const int SW_RESTORE = 9;
        public const int SW_SHOW = 5;
        public const int SW_MINIMIZE = 6;

        public struct RECT {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        public static void RestoreAndBringToFront(IntPtr hWnd) {
            // Restore the window if it is minimized
            ShowWindow(hWnd, SW_RESTORE);
            // Bring the window to the foreground
            SetForegroundWindow(hWnd);
        }

        public static void MinimizeWindow(IntPtr hWnd) {
            // Minimize the window
            ShowWindow(hWnd, SW_MINIMIZE);
        }

        public static void MoveWindow(IntPtr hWnd, int x, int y) {
            RECT rect = new RECT();
            GetWindowRect(hWnd, ref rect);
            int width = rect.Right - rect.Left;
            int height = rect.Bottom - rect.Top;

            // Move the window to the specified coordinates
            SetWindowPos(hWnd, IntPtr.Zero, x, y, width, height, 0);
        }
    }
"@

# Function to capture and save a screenshot of a window using its handle
function Get-WindowScreenshot {
    param (
        [IntPtr]$windowHandle,
        [string]$outputFilePath
    )

    # Get the size of the screen
    if ($null -ne $windowHandle) {
        $rect = [WindowHelper+RECT]::new()
        $temp = [WindowHelper]::GetWindowRect($windowHandle, [ref]$rect)
        $Screen = [Drawing.Rectangle]::FromLTRB($rect.Left, $rect.Top, $rect.Right, $rect.Bottom)
    }
    else {
        $Screen = [Windows.Forms.SystemInformation]::VirtualScreen       
    }

    # Create a bitmap from the screen
    $Bitmap = [Drawing.Bitmap]::New($Screen.width, $Screen.height)
    $Graphics = [Drawing.Graphics]::FromImage($Bitmap)

    # Capture the screen image
    $Graphics.CopyFromScreen($Screen.Location, [Drawing.Point]::Empty, $Screen.size)
    $Bitmap.Save($outputFilePath, [Drawing.Imaging.ImageFormat]::Jpeg)
}

# Function to get the native window handle (HWND) from AutomationElement
function Get-NativeWindowHandle {
    param (
        [Windows.Automation.AutomationElement]$automationElement
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

# Function to wait for a condition with a timeout
function WaitWithTimeout {
    param (
        [int]$timeoutSeconds = 30,
        [scriptblock]$action
    )

    $startTime = [DateTime]::Now
    do {
        $result = & $action
        if ($null -ne $result) {
            return $result
        }
        Start-Sleep -Milliseconds 500
    } while (([DateTime]::Now - $startTime).TotalSeconds -lt $timeoutSeconds)
    Write-Error "Timeout of $timeoutSeconds seconds exceeded while waiting for condition."
    return $null
}

# Function to get the main application window by partial title
function Get-ApplicationWindow {
    param (
        [Windows.Automation.AutomationElement]$rootElement,
        [string]$partialWindowTitle,
        [int]$timeoutSeconds = 30
    )

    if ($null -eq $rootElement) {
        $rootElement = [Windows.Automation.AutomationElement]::RootElement
    }
    $condition_w = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Window)
    $condition_d = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Dialog)

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $windows = $rootElement.FindAll([Windows.Automation.TreeScope]::Children, [Windows.Automation.OrCondition]::new($condition_w, $condition_d))
        foreach ($window in $windows) {
            if ($window.Current.Name -like "$partialWindowTitle*") {
                Write-Host "Window found: $($window.Current.Name)"
                return $window
            }
        }
        return $null
    }
}

# Function to check for popup windows
function CheckForPopup {
    param (
        [Windows.Automation.AutomationElement]$rootElement,
        [string]$partialWindowTitle
    )

    if ($null -eq $rootElement) {
        $rootElement = [Windows.Automation.AutomationElement]::RootElement
    }

    $condition_n = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $partialWindowTitle, [Windows.Automation.PropertyConditionFlags]::IgnoreCase)
    $condition_w = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Window)
    $condition_d = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Dialog)
    $condition = [Windows.Automation.OrCondition]::new($condition_w, $condition_d)

    $popupWindow = $rootElement.FindFirst([Windows.Automation.TreeScope]::Children, [Windows.Automation.AndCondition]::new($condition, $condition_n))
    if ($null -ne $popupWindow) {
        Write-Host "Popup window with title containing '$($popupWindow.Current.Name)' found."
        return $popupWindow
    }
    return $null
}

# Function to click a control using AutomationId or ControlName
function Get-ControlElement {
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
        throw "Either automationId or controlName must be provided."
    }
    $type_text = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Text)
    $type_button = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Button)
    $type_radio = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::RadioButton)
    $condition_type = [Windows.Automation.OrCondition]::new($type_text, $type_button, $type_radio)

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $element = $windowElement.FindFirst([Windows.Automation.TreeScope]::Subtree, [Windows.Automation.AndCondition]::new($condition, $condition_type))
        if ($null -ne $element) {
            return $element
        }
        return $null
    }
}

# Function to click a control using AutomationId or ControlName
function ClickControl {
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
        throw "Either automationId or controlName must be provided."
    }
    $condition_b = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Button)
    $condition_r = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::RadioButton)
    $condition_o = [Windows.Automation.OrCondition]::new($condition_b, $condition_r)

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $element = $windowElement.FindFirst([Windows.Automation.TreeScope]::Subtree, [Windows.Automation.AndCondition]::new($condition, $condition_o))
        if ($null -ne $element) {
            $controlType = $element.GetCurrentPropertyValue([Windows.Automation.AutomationElement]::ControlTypeProperty)
            switch ($controlType) {
                { $_ -eq [Windows.Automation.ControlType]::Button } {
                    $invokePattern = $element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)
                    $invokePattern.Invoke()
                }
                { $_ -eq [Windows.Automation.ControlType]::RadioButton } {
                    $selectPattern = $element.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern)
                    $selectPattern.Select()
                }
                default {
                    Write-Error "Control '$automationId' or '$controlName' is not a supported type (Button or RadioButton)."
                    return $null
                }
            }
            return $true
        }
        return $null
    }
}

# Function to get result text from a specific control using AutomationId or ControlName
function Get-ResultText {
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
        throw "Either automationId or controlName must be provided."
    }

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $textControl = $windowElement.FindFirst([Windows.Automation.TreeScope]::Subtree, $condition)
        if ($null -ne $textControl) {
            $resultValue = $textControl.GetCurrentPropertyValue([Windows.Automation.AutomationElement]::NameProperty)
            return $resultValue
        }
        return $null
    }
}

# Wait for a specific text to appear in the window (with timeout)
function WaitForTextInWindow {
    param (
        [Windows.Automation.AutomationElement]$windowElement,
        [string[]]$textToFind,
        [int]$timeoutSeconds = 30
    )

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Text)
        $textElements = $windowElement.FindAll([Windows.Automation.TreeScope]::Subtree, $condition)

        foreach ($element in $textElements) {
            $textValue = $element.GetCurrentPropertyValue([Windows.Automation.AutomationElement]::NameProperty)
            if ($textToFind | Where-Object { $textValue -like "*$_*" }) {
                return $textValue
            }
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
