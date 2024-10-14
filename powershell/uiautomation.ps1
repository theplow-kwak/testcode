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

    $rect = [WindowHelper+RECT]::new()
    $return = [WindowHelper]::GetWindowRect($windowHandle, [ref]$rect)
    $bounds = [Drawing.Rectangle]::FromLTRB($rect.Left, $rect.Top, $rect.Right, $rect.Bottom)

    $bmp = New-Object Drawing.Bitmap $bounds.width, $bounds.height
    $graphics = [Drawing.Graphics]::FromImage($bmp)

    $graphics.CopyFromScreen($bounds.Location, [Drawing.Point]::Empty, $bounds.size)
    $bmp.Save($outputFilePath)

    $graphics.Dispose()
    $bmp.Dispose()
}

function Get-WindowFullScreenshot {
    param (
        [IntPtr]$windowHandle,
        [string]$outputFilePath
    )

    # 화면의 크기 가져오기
    $Screen = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $Width = $Screen.Width
    $Height = $Screen.Height

    # 화면에서 비트맵 생성
    $Bitmap = New-Object System.Drawing.Bitmap $Width, $Height
    $Graphics = [System.Drawing.Graphics]::FromImage($Bitmap)

    # 비트맵에 화면 그리기
    $Graphics.CopyFromScreen($Screen.Location, [System.Drawing.Point]::Empty, $Screen.Size)

    # 비트맵을 JPG 파일로 저장
    $Bitmap.Save($outputFilePath)
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
        [Parameter(Mandatory = $true)][ValidateNotNull()]$windowElement,
        [Parameter(Mandatory = $false)][string]$automationId,
        [Parameter(Mandatory = $false)][string]$controlName,
        [int]$timeoutSeconds = 30
    )

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $element = $null

        if (-not [string]::IsNullOrEmpty($automationId)) {
            $element = $windowElement.FindFirst([Windows.Automation.TreeScope]::Subtree, 
                (New-Object Windows.Automation.PropertyCondition ([Windows.Automation.AutomationElement]::AutomationIdProperty, $automationId)))
        }
        elseif (-not [string]::IsNullOrEmpty($controlName)) {
            $element = $windowElement.FindFirst([Windows.Automation.TreeScope]::Subtree, 
                (New-Object Windows.Automation.PropertyCondition ([Windows.Automation.AutomationElement]::NameProperty, $controlName)))
        }

        if ($null -eq $element) {
            throw "Control with AutomationId '$automationId' or Name '$controlName' not found."
        }

        $controlType = $element.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::ControlTypeProperty)

        switch ($controlType) {
            { $_ -eq [System.Windows.Automation.ControlType]::Button } {
                try {
                    $invokePattern = $element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
                    $invokePattern.Invoke()
                }
                catch {
                    Write-Error "Failed to invoke control. Error: $_"
                    return $null
                }
            }
            { $_ -eq [System.Windows.Automation.ControlType]::RadioButton } {
                try {
                    $selectPattern = $element.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
                    $selectPattern.Select()
                }
                catch {
                    Write-Error "Failed to select control. Error: $_"
                    return $null
                }
            }
            default {
                Write-Error "Control '$automationId' or '$controlName' is not a supported type (Button or RadioButton)."
                return $null
            }
        }
        return $true
    }
}

# Function to get result text from a specific control using AutomationId or ControlName
function Get-ResultText {
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
