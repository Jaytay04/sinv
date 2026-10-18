Set-StrictMode -Version Latest

if (-not ('CombinedVmFirmwareVariableApi' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class CombinedVmFirmwareVariableApi {
    private const uint TOKEN_QUERY = 0x0008;
    private const uint TOKEN_ADJUST_PRIVILEGES = 0x0020;
    private const uint SE_PRIVILEGE_ENABLED = 0x00000002;
    private const int ERROR_NOT_ALL_ASSIGNED = 1300;
    private const int BUFFER_CAPACITY = 4096;

    public sealed class DeleteArguments {
        public readonly byte[] Buffer;
        public readonly uint Size;
        public readonly uint Attributes;

        public DeleteArguments(uint existingAttributes) {
            Buffer = null;
            Size = 0;
            Attributes = existingAttributes;
        }
    }

    [StructLayout(LayoutKind.Sequential)] private struct Luid { public uint LowPart; public int HighPart; }
    [StructLayout(LayoutKind.Sequential)] private struct LuidAndAttributes { public Luid Luid; public uint Attributes; }
    [StructLayout(LayoutKind.Sequential)] private struct TokenPrivileges { public uint PrivilegeCount; public LuidAndAttributes Privileges; }

    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError=true, EntryPoint="SetLastError")] private static extern void ClearLastError(uint error);
    [DllImport("advapi32.dll", SetLastError=true)] private static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="LookupPrivilegeValueW")] private static extern bool LookupPrivilegeValue(string systemName, string name, out Luid luid);
    [DllImport("advapi32.dll", SetLastError=true)] private static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TokenPrivileges newState, uint bufferLength, out TokenPrivileges previousState, out uint returnLength);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="GetFirmwareEnvironmentVariableExW")] private static extern uint Get(string name, string guid, byte[] buffer, uint size, out uint attributes);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="SetFirmwareEnvironmentVariableExW")] private static extern bool Set(string name, string guid, byte[] buffer, uint size, uint attributes);

    private sealed class PrivilegeScope : IDisposable {
        public IntPtr Token;
        public TokenPrivileges Previous;
        public bool Adjusted;
        public int Error;
        public int RestoreError;
        public bool Enabled;
        public void Dispose() {
            if (Token == IntPtr.Zero) return;
            if (Adjusted) {
                TokenPrivileges ignored; uint ignoredLength;
                ClearLastError(0);
                if (!AdjustTokenPrivileges(Token, false, ref Previous, (uint)Marshal.SizeOf(typeof(TokenPrivileges)), out ignored, out ignoredLength)) RestoreError = Marshal.GetLastWin32Error();
                else if (Marshal.GetLastWin32Error() == ERROR_NOT_ALL_ASSIGNED) RestoreError = ERROR_NOT_ALL_ASSIGNED;
            }
            CloseHandle(Token);
            Token = IntPtr.Zero;
        }
    }

    private static PrivilegeScope EnablePrivilege() {
        PrivilegeScope scope = new PrivilegeScope();
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, out scope.Token)) {
            scope.Error = Marshal.GetLastWin32Error(); return scope;
        }
        Luid privilege;
        if (!LookupPrivilegeValue(null, "SeSystemEnvironmentPrivilege", out privilege)) {
            scope.Error = Marshal.GetLastWin32Error(); return scope;
        }
        TokenPrivileges requested = new TokenPrivileges();
        requested.PrivilegeCount = 1; requested.Privileges.Luid = privilege; requested.Privileges.Attributes = SE_PRIVILEGE_ENABLED;
        uint returned;
        ClearLastError(0);
        scope.Adjusted = AdjustTokenPrivileges(scope.Token, false, ref requested, (uint)Marshal.SizeOf(typeof(TokenPrivileges)), out scope.Previous, out returned);
        scope.Error = Marshal.GetLastWin32Error();
        scope.Enabled = scope.Adjusted && scope.Error != ERROR_NOT_ALL_ASSIGNED;
        return scope;
    }

    private static Dictionary<string, object> Base(string name, PrivilegeScope scope) {
        return new Dictionary<string, object> {
            {"name", name}, {"success", false}, {"supported", false}, {"win32Error", scope.Error},
            {"win32ErrorText", scope.Error == 0 ? null : new Win32Exception(scope.Error).Message},
            {"privilegeEnabled", scope.Enabled}, {"privilegeError", scope.Error},
            {"privilegeRestored", false}, {"privilegeRestoreError", 0},
            {"size", 0}, {"attributes", null}, {"bytesHex", null}, {"bytesBase64", null}
        };
    }

    private static void Finish(Dictionary<string, object> result, PrivilegeScope scope) {
        scope.Dispose();
        result["privilegeRestored"] = scope.RestoreError == 0;
        result["privilegeRestoreError"] = scope.RestoreError;
        if (scope.RestoreError != 0) {
            result["success"] = false;
            result["win32Error"] = scope.RestoreError;
            result["win32ErrorText"] = new Win32Exception(scope.RestoreError).Message;
        }
    }

    public static object Read(string name, string guid) {
        PrivilegeScope scope = EnablePrivilege();
        Dictionary<string, object> result = Base(name, scope);
        if (!scope.Enabled) { Finish(result, scope); return result; }
        byte[] buffer = new byte[BUFFER_CAPACITY]; uint attributes = 0; ClearLastError(0);
        uint size = Get(name, guid, buffer, (uint)buffer.Length, out attributes);
        int error = size == 0 ? Marshal.GetLastWin32Error() : 0;
        result["success"] = size != 0;
        result["win32Error"] = error;
        result["win32ErrorText"] = error == 0 ? null : new Win32Exception(error).Message;
        if (size != 0) {
            byte[] actual = new byte[(int)size]; Array.Copy(buffer, actual, (int)size);
            result["supported"] = true; result["size"] = (int)size; result["attributes"] = attributes;
            result["bytesHex"] = BitConverter.ToString(actual).Replace("-", "");
            result["bytesBase64"] = Convert.ToBase64String(actual);
        }
        Finish(result, scope);
        return result;
    }

    public static object Write(string name, string guid, byte[] bytes, uint attributes) {
        PrivilegeScope scope = EnablePrivilege();
        Dictionary<string, object> result = Base(name, scope);
        if (!scope.Enabled) { Finish(result, scope); return result; }
        ClearLastError(0);
        bool success = Set(name, guid, bytes, (uint)bytes.Length, attributes);
        int error = success ? 0 : Marshal.GetLastWin32Error();
        result["success"] = success; result["supported"] = success; result["win32Error"] = error;
        result["win32ErrorText"] = error == 0 ? null : new Win32Exception(error).Message;
        Finish(result, scope);
        return result;
    }

    public static DeleteArguments CreateDeleteArguments(uint existingAttributes) {
        return new DeleteArguments(existingAttributes);
    }

    public static object Delete(string name, string guid, uint existingAttributes) {
        PrivilegeScope scope = EnablePrivilege();
        Dictionary<string, object> result = Base(name, scope);
        if (!scope.Enabled) { Finish(result, scope); return result; }
        DeleteArguments arguments = CreateDeleteArguments(existingAttributes);
        ClearLastError(0);
        bool success = Set(name, guid, arguments.Buffer, arguments.Size, arguments.Attributes);
        int error = success ? 0 : Marshal.GetLastWin32Error();
        result["success"] = success; result["supported"] = success; result["win32Error"] = error;
        result["win32ErrorText"] = error == 0 ? null : new Win32Exception(error).Message;
        Finish(result, scope);
        return result;
    }
}
'@
}

function Read-CombinedFirmwareVariable {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Name, [string]$Guid = '{8be4df61-93ca-11d2-aa0d-00e098032b8c}')
    return [CombinedVmFirmwareVariableApi]::Read($Name, $Guid)
}

function Write-CombinedFirmwareVariable {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][byte[]]$Bytes,
          [ValidateRange(0, 4294967295)][uint32]$Attributes = 7,
          [string]$Guid = '{8be4df61-93ca-11d2-aa0d-00e098032b8c}')
    return [CombinedVmFirmwareVariableApi]::Write($Name, $Guid, $Bytes, $Attributes)
}

function Remove-CombinedFirmwareVariable {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Name,
          [Parameter(Mandatory = $true)][ValidateRange(0, 4294967295)][uint32]$Attributes,
          [string]$Guid = '{8be4df61-93ca-11d2-aa0d-00e098032b8c}')
    return [CombinedVmFirmwareVariableApi]::Delete($Name, $Guid, $Attributes)
}

Export-ModuleMember -Function Read-CombinedFirmwareVariable, Write-CombinedFirmwareVariable, Remove-CombinedFirmwareVariable
