[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [ValidateRange(1, 65535)]
    [int]$Port = 5909,

    [int]$ClickX = -1,

    [int]$ClickY = -1,

    [switch]$DoubleClick,

    [string]$RunCommand = '',

    [switch]$ApproveUac
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-ExactBytes {
    param([IO.Stream]$Stream, [int]$Count)
    $Buffer = [byte[]]::new($Count)
    $Offset = 0
    while ($Offset -lt $Count) {
        $Read = $Stream.Read($Buffer, $Offset, $Count - $Offset)
        if ($Read -le 0) { throw 'The local VNC connection closed unexpectedly.' }
        $Offset += $Read
    }
    return ,$Buffer
}

function Get-UInt16Be {
    param([byte[]]$Bytes, [int]$Offset)
    return [uint16](([uint16]$Bytes[$Offset] -shl 8) -bor
        [uint16]$Bytes[$Offset + 1])
}

function Get-UInt32Be {
    param([byte[]]$Bytes, [int]$Offset)
    return [uint32](
        ([uint32]$Bytes[$Offset] -shl 24) -bor
        ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 8) -bor
        [uint32]$Bytes[$Offset + 3])
}

function Send-KeyEvent {
    param([IO.Stream]$Stream, [uint32]$KeySym, [bool]$Down)
    $Event = [byte[]](4, $(if ($Down) { 1 } else { 0 }), 0, 0,
        [byte](($KeySym -shr 24) -band 0xff),
        [byte](($KeySym -shr 16) -band 0xff),
        [byte](($KeySym -shr 8) -band 0xff),
        [byte]($KeySym -band 0xff))
    $Stream.Write($Event, 0, $Event.Length)
}

$Client = [Net.Sockets.TcpClient]::new('127.0.0.1', $Port)
try {
    $Stream = $Client.GetStream()
    $Version = [Text.Encoding]::ASCII.GetString((Read-ExactBytes $Stream 12))
    $Reply = [Text.Encoding]::ASCII.GetBytes('RFB 003.008' + "`n")
    $Stream.Write($Reply, 0, $Reply.Length)
    if ($Version.Contains('003.003')) {
        if ((Get-UInt32Be (Read-ExactBytes $Stream 4) 0) -ne 1) {
            throw 'The local VNC server requires unsupported authentication.'
        }
    }
    else {
        $Count = (Read-ExactBytes $Stream 1)[0]
        $Types = Read-ExactBytes $Stream $Count
        if ($Types -notcontains [byte]1) {
            throw 'The local VNC server requires unsupported authentication.'
        }
        $Stream.WriteByte(1)
        if ((Get-UInt32Be (Read-ExactBytes $Stream 4) 0) -ne 0) {
            throw 'The local VNC security handshake failed.'
        }
    }
    $Stream.WriteByte(1)
    $Init = Read-ExactBytes $Stream 24
    $Width = Get-UInt16Be $Init 0
    $Height = Get-UInt16Be $Init 2
    $NameLength = Get-UInt32Be $Init 20
    if ($NameLength -gt 0) { [void](Read-ExactBytes $Stream ([int]$NameLength)) }
    if ($ClickX -ge 0 -or $ClickY -ge 0) {
        if ($ClickX -lt 0 -or $ClickY -lt 0 -or
            $ClickX -ge $Width -or $ClickY -ge $Height) {
            throw 'The requested VNC click is outside the framebuffer.'
        }
        $PointerDown = [byte[]](5, 1,
            [byte]($ClickX -shr 8), [byte]($ClickX -band 0xff),
            [byte]($ClickY -shr 8), [byte]($ClickY -band 0xff))
        $PointerUp = [byte[]](5, 0,
            [byte]($ClickX -shr 8), [byte]($ClickX -band 0xff),
            [byte]($ClickY -shr 8), [byte]($ClickY -band 0xff))
        $ClickCount = if ($DoubleClick) { 2 } else { 1 }
        for ($Index = 0; $Index -lt $ClickCount; $Index++) {
            $Stream.Write($PointerDown, 0, $PointerDown.Length)
            $Stream.Flush()
            Start-Sleep -Milliseconds 100
            $Stream.Write($PointerUp, 0, $PointerUp.Length)
            $Stream.Flush()
            if ($Index -lt ($ClickCount - 1)) { Start-Sleep -Milliseconds 150 }
        }
        $Stream.Flush()
        Start-Sleep -Seconds 2
    }
    if (-not [string]::IsNullOrWhiteSpace($RunCommand)) {
        Send-KeyEvent $Stream 0xffeb $true
        Send-KeyEvent $Stream ([uint32][char]'r') $true
        Send-KeyEvent $Stream ([uint32][char]'r') $false
        Send-KeyEvent $Stream 0xffeb $false
        $Stream.Flush()
        Start-Sleep -Seconds 1
        foreach ($Character in $RunCommand.ToCharArray()) {
            $KeySym = [uint32]$Character
            Send-KeyEvent $Stream $KeySym $true
            Send-KeyEvent $Stream $KeySym $false
            Start-Sleep -Milliseconds 20
        }
        Send-KeyEvent $Stream 0xff0d $true
        Send-KeyEvent $Stream 0xff0d $false
        $Stream.Flush()
        Start-Sleep -Seconds 2
    }
    if ($ApproveUac) {
        Send-KeyEvent $Stream 0xffe9 $true
        Send-KeyEvent $Stream ([uint32][char]'y') $true
        Send-KeyEvent $Stream ([uint32][char]'y') $false
        Send-KeyEvent $Stream 0xffe9 $false
        $Stream.Flush()
        Start-Sleep -Seconds 2
    }

    $PixelFormat = [byte[]](0, 0, 0, 0, 32, 24, 0, 1,
        0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0)
    $Stream.Write($PixelFormat, 0, $PixelFormat.Length)
    $Encodings = [byte[]](2, 0, 0, 1, 0, 0, 0, 0)
    $Stream.Write($Encodings, 0, $Encodings.Length)
    $Request = [byte[]](3, 0, 0, 0, 0, 0,
        [byte]($Width -shr 8), [byte]($Width -band 0xff),
        [byte]($Height -shr 8), [byte]($Height -band 0xff))
    $Stream.Write($Request, 0, $Request.Length)
    $Stream.Flush()
    $Header = Read-ExactBytes $Stream 4
    if ($Header[0] -ne 0) { throw "Unexpected VNC server message: $($Header[0])" }
    $RectangleCount = Get-UInt16Be $Header 2
    $Frame = [byte[]]::new($Width * $Height * 4)
    for ($RectangleIndex = 0; $RectangleIndex -lt $RectangleCount;
            $RectangleIndex++) {
        $Rectangle = Read-ExactBytes $Stream 12
        $X = Get-UInt16Be $Rectangle 0
        $Y = Get-UInt16Be $Rectangle 2
        $RectangleWidth = Get-UInt16Be $Rectangle 4
        $RectangleHeight = Get-UInt16Be $Rectangle 6
        $Encoding = Get-UInt32Be $Rectangle 8
        if ($Encoding -ne 0) { throw "Unexpected VNC rectangle encoding: $Encoding" }
        $Pixels = Read-ExactBytes $Stream ($RectangleWidth * $RectangleHeight * 4)
        for ($Row = 0; $Row -lt $RectangleHeight; $Row++) {
            [Array]::Copy($Pixels, $Row * $RectangleWidth * 4, $Frame,
                (($Y + $Row) * $Width + $X) * 4, $RectangleWidth * 4)
        }
    }

    Add-Type -AssemblyName System.Drawing
    $Bitmap = [Drawing.Bitmap]::new($Width, $Height,
        [Drawing.Imaging.PixelFormat]::Format32bppRgb)
    try {
        $Bounds = [Drawing.Rectangle]::new(0, 0, $Width, $Height)
        $Data = $Bitmap.LockBits($Bounds,
            [Drawing.Imaging.ImageLockMode]::WriteOnly,
            [Drawing.Imaging.PixelFormat]::Format32bppRgb)
        try {
            for ($Row = 0; $Row -lt $Height; $Row++) {
                $Destination = [IntPtr]::Add($Data.Scan0, $Row * $Data.Stride)
                [Runtime.InteropServices.Marshal]::Copy(
                    $Frame, $Row * $Width * 4, $Destination, $Width * 4)
            }
        }
        finally { $Bitmap.UnlockBits($Data) }
        $ResolvedOutput = [IO.Path]::GetFullPath($OutputPath)
        $Bitmap.Save($ResolvedOutput, [Drawing.Imaging.ImageFormat]::Png)
    }
    finally { $Bitmap.Dispose() }
    [pscustomobject]@{
        Result = 'PASS'; OutputPath = [IO.Path]::GetFullPath($OutputPath)
        Width = $Width; Height = $Height; Rectangles = $RectangleCount
    }
}
finally { $Client.Dispose() }
