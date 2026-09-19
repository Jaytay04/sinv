Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($null -eq ('Hypersinv.HostRunner.CredentialVault' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security;

namespace Hypersinv.HostRunner
{
    internal static class NativeMethods
    {
        internal const uint CredTypeGeneric = 1;
        internal const uint CredPersistLocalMachine = 2;
        internal const int ErrorNotFound = 1168;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        internal struct NativeCredential
        {
            internal uint Flags;
            internal uint Type;
            [MarshalAs(UnmanagedType.LPWStr)] internal string TargetName;
            [MarshalAs(UnmanagedType.LPWStr)] internal string Comment;
            internal System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
            internal uint CredentialBlobSize;
            internal IntPtr CredentialBlob;
            internal uint Persist;
            internal uint AttributeCount;
            internal IntPtr Attributes;
            [MarshalAs(UnmanagedType.LPWStr)] internal string TargetAlias;
            [MarshalAs(UnmanagedType.LPWStr)] internal string UserName;
        }

        [DllImport("advapi32.dll", EntryPoint = "CredWriteW",
            CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool CredWrite(
            ref NativeCredential credential,
            uint flags);

        [DllImport("advapi32.dll", EntryPoint = "CredReadW",
            CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool CredRead(
            string target,
            uint type,
            uint flags,
            out IntPtr credential);

        [DllImport("advapi32.dll", EntryPoint = "CredDeleteW",
            CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool CredDelete(
            string target,
            uint type,
            uint flags);

        [DllImport("advapi32.dll")]
        internal static extern void CredFree(IntPtr buffer);
    }

    public static class CredentialVault
    {
        public static void Write(string target, SecureString password)
        {
            if (String.IsNullOrWhiteSpace(target))
                throw new ArgumentException("Credential target is required.", "target");
            if (password == null || password.Length == 0)
                throw new ArgumentException("Password must not be empty.", "password");

            IntPtr blob = IntPtr.Zero;
            try
            {
                blob = Marshal.SecureStringToGlobalAllocUnicode(password);
                int blobBytes = checked(password.Length * 2);
                if (blobBytes > 512)
                    throw new ArgumentException(
                        "Password exceeds the Credential Manager blob limit.",
                        "password");
                for (int index = 0; index < password.Length; ++index)
                {
                    if (Marshal.ReadInt16(blob, index * 2) == 0)
                        throw new ArgumentException(
                            "Password must not contain NUL characters.",
                            "password");
                }
                NativeMethods.NativeCredential credential =
                    new NativeMethods.NativeCredential();
                credential.Type = NativeMethods.CredTypeGeneric;
                credential.TargetName = target;
                credential.Comment = "Hypersinv encrypted test VM";
                credential.CredentialBlobSize = checked((uint)blobBytes);
                credential.CredentialBlob = blob;
                credential.Persist = NativeMethods.CredPersistLocalMachine;
                credential.UserName = "HypersinvHostRunner";

                if (!NativeMethods.CredWrite(ref credential, 0))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "CredWriteW failed.");
            }
            finally
            {
                if (blob != IntPtr.Zero)
                    Marshal.ZeroFreeGlobalAllocUnicode(blob);
            }
        }

        public static SecureString Read(string target)
        {
            IntPtr pointer = IntPtr.Zero;
            if (!NativeMethods.CredRead(target, NativeMethods.CredTypeGeneric,
                    0, out pointer))
            {
                int error = Marshal.GetLastWin32Error();
                if (error == NativeMethods.ErrorNotFound)
                    return null;
                throw new Win32Exception(error, "CredReadW failed.");
            }

            try
            {
                NativeMethods.NativeCredential credential =
                    (NativeMethods.NativeCredential)Marshal.PtrToStructure(
                        pointer, typeof(NativeMethods.NativeCredential));
                if (credential.CredentialBlobSize == 0U ||
                    credential.CredentialBlobSize > 512U ||
                    (credential.CredentialBlobSize & 1U) != 0U)
                    throw new InvalidOperationException(
                        "Credential blob size is invalid.");

                SecureString result = new SecureString();
                try
                {
                    int characters = checked((int)(
                        credential.CredentialBlobSize / 2U));
                    for (int index = 0; index < characters; ++index)
                    {
                        char value = (char)Marshal.ReadInt16(
                            credential.CredentialBlob, index * 2);
                        if (value == '\0')
                            throw new InvalidOperationException(
                                "Stored VM credential contains a NUL character.");
                        result.AppendChar(value);
                    }
                    result.MakeReadOnly();
                    return result;
                }
                catch
                {
                    result.Dispose();
                    throw;
                }
            }
            finally
            {
                NativeMethods.CredFree(pointer);
            }
        }

        public static bool Exists(string target)
        {
            IntPtr pointer = IntPtr.Zero;
            if (!NativeMethods.CredRead(target, NativeMethods.CredTypeGeneric,
                    0, out pointer))
            {
                int error = Marshal.GetLastWin32Error();
                if (error == NativeMethods.ErrorNotFound)
                    return false;
                throw new Win32Exception(error, "CredReadW failed.");
            }
            NativeMethods.CredFree(pointer);
            return true;
        }

        public static bool Delete(string target)
        {
            if (NativeMethods.CredDelete(target, NativeMethods.CredTypeGeneric, 0))
                return true;
            int error = Marshal.GetLastWin32Error();
            if (error == NativeMethods.ErrorNotFound)
                return false;
            throw new Win32Exception(error, "CredDeleteW failed.");
        }
    }
}
'@
}

function Test-HrCredentialTargetName {
    param([Parameter(Mandatory=$true)][string]$TargetName)

    return $TargetName -cmatch `
        '^HypersinvHostRunner/[A-Za-z0-9._-]{1,80}$'
}

function Set-HrVmCredential {
    param(
        [Parameter(Mandatory=$true)][string]$TargetName,
        [Parameter(Mandatory=$true)][Security.SecureString]$Password
    )

    if (-not (Test-HrCredentialTargetName $TargetName)) {
        throw "Unsafe Credential.TargetName: $TargetName"
    }
    [Hypersinv.HostRunner.CredentialVault]::Write($TargetName, $Password)
}

function Get-HrVmCredential {
    param([Parameter(Mandatory=$true)][string]$TargetName)

    if (-not (Test-HrCredentialTargetName $TargetName)) {
        throw "Unsafe Credential.TargetName: $TargetName"
    }
    $Password = [Hypersinv.HostRunner.CredentialVault]::Read($TargetName)
    if ($null -eq $Password) {
        throw "VM credential is not stored: $TargetName"
    }
    return $Password
}

function Test-HrVmCredential {
    param([Parameter(Mandatory=$true)][string]$TargetName)

    if (-not (Test-HrCredentialTargetName $TargetName)) {
        throw "Unsafe Credential.TargetName: $TargetName"
    }
    return [Hypersinv.HostRunner.CredentialVault]::Exists($TargetName)
}

function Remove-HrVmCredential {
    param([Parameter(Mandatory=$true)][string]$TargetName)

    if (-not (Test-HrCredentialTargetName $TargetName)) {
        throw "Unsafe Credential.TargetName: $TargetName"
    }
    return [Hypersinv.HostRunner.CredentialVault]::Delete($TargetName)
}

Export-ModuleMember -Function @(
    'Test-HrCredentialTargetName',
    'Set-HrVmCredential',
    'Get-HrVmCredential',
    'Test-HrVmCredential',
    'Remove-HrVmCredential'
)
