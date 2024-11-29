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

$type_text = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Text)
$type_button = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Button)
$type_radio = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::RadioButton)
$type_menubar = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::MenuBar)
$type_menuitem = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::MenuItem)

$type_window = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Window)
$type_dialog = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Dialog)
$type_pane = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Pane)

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
    # Write-Error "Timeout of $timeoutSeconds seconds exceeded while waiting for condition."
    return $null
}

# Function to get the main application window by partial title
function Get-ApplicationWindow {
    param (
        [Windows.Automation.AutomationElement]$rootElement,
        [string]$partialWindowTitle,
        [string]$WindowClassName,
        [int]$timeoutSeconds = 30
    )

    if ($null -eq $rootElement) {
        $rootElement = [Windows.Automation.AutomationElement]::RootElement
    }

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $windows = $rootElement.FindAll([Windows.Automation.TreeScope]::Children, [Windows.Automation.OrCondition]::new($type_window, $type_dialog, $type_pane))
        foreach ($window in $windows) {
            Write-Verbose "Window -- $($window.Current.Name)"
            if (($window.Current.Name -like "$partialWindowTitle*") -or ($window.Current.ClassName -like "$WindowClassName")) {
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

    $condition_name = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $partialWindowTitle, [Windows.Automation.PropertyConditionFlags]::IgnoreCase)
    $condition_window = [Windows.Automation.OrCondition]::new($type_window, $type_dialog)

    $popupWindow = $rootElement.FindFirst([Windows.Automation.TreeScope]::Children, [Windows.Automation.AndCondition]::new($condition_window, $condition_name))
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
    $condition_type = [Windows.Automation.OrCondition]::new($type_text, $type_button, $type_radio, $type_menubar)

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
    $condition_type = [Windows.Automation.OrCondition]::new($type_button, $type_radio)

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $element = $windowElement.FindFirst([Windows.Automation.TreeScope]::Subtree, [Windows.Automation.AndCondition]::new($condition, $condition_type))
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

function Set-EditText {
    param (
        [Windows.Automation.AutomationElement]$windowElement,
        [string]$automationId = $null,
        [string]$controlName = $null,
        [string]$textToInput = $null,
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

    if ([string]::IsNullOrEmpty($textToInput)) {
        throw "textToInput cannot be null or empty."
    }

    return WaitWithTimeout -timeoutSeconds $timeoutSeconds -action {
        $textControl = $windowElement.FindFirst([Windows.Automation.TreeScope]::Subtree, $condition)
        if ($null -ne $textControl) {
            $valuePattern = $textControl.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)
            if ($null -ne $valuePattern) {
                $valuePattern.SetValue($textToInput)
                return $true
            }
            else {
                Write-Warning "The element does not support ValuePattern, possibly not a text input field."
                return $null
            }
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
        $textElements = $windowElement.FindAll([Windows.Automation.TreeScope]::Subtree, $type_text)
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

function SelectMenuItem {
    param (
        [Windows.Automation.AutomationElement]$windowElement,
        [string]$automationId = $null,
        [string[]]$menuPath,
        [int]$timeoutInSeconds = 10
    )

    $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $automationId)
    $menuBar = $windowElement.FindFirst([Windows.Automation.TreeScope]::Subtree, [Windows.Automation.AndCondition]::new($condition, $type_menubar))
    $currentElement = $menuBar

    foreach ($menuItemName in $menuPath) {
        # Find the next menu item in the path
        $menuItem = $currentElement.FindFirst(
            [Windows.Automation.TreeScope]::Subtree, # [Windows.Automation.Condition]::TrueCondition
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $menuItemName)
        )

        if ($null -eq $menuItem) {
            Write-Output "Menu item '$menuItemName' not found."
            Continue;
        }

        $expandCollapsePattern = 0
        # Expand if it's a menu (ExpandCollapsePattern)
        if ($menuItem.TryGetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$expandCollapsePattern)) {
            $expandCollapsePattern.Expand()
            Start-Sleep -Milliseconds 500 # Short delay for UI updates
        }

        $invokePattern = 0
        # Invoke if it's a clickable item (InvokePattern)
        if ($menuItem.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern, [ref]$invokePattern)) {
            $invokePattern.Invoke()
        }

        # Move to the next menu level
        $currentElement = $menuItem
    }

    Write-Output "Menu path '$($menuPath -join " -> ")' selected successfully."
}

