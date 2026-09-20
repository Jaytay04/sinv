param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$windowSource = Get-Content -LiteralPath (Join-Path $SourceRoot 'src\overlay_window.cpp') -Raw
$readerSource = Get-Content -LiteralPath (Join-Path $SourceRoot 'src\shared_memory_reader.cpp') -Raw
$rendererSource = Get-Content -LiteralPath (Join-Path $SourceRoot 'src\d3d_renderer.cpp') -Raw
$allSource = $windowSource + $readerSource + $rendererSource

$required = @(
    'WS_EX_TOPMOST',
    'WS_EX_TRANSPARENT',
    'WS_EX_LAYERED',
    'WS_EX_TOOLWINDOW',
    'WS_EX_NOACTIVATE',
    'HTTRANSPARENT',
    'FILE_MAP_READ',
    'D3D11CreateDeviceAndSwapChain',
    'DXGI_FORMAT_B8G8R8A8_UNORM',
    'D2D1_ALPHA_MODE_PREMULTIPLIED',
    'DWriteCreateFactory'
)

foreach ($token in $required) {
    if ($allSource -notmatch [regex]::Escape($token)) {
        throw "Missing overlay contract token: $token"
    }
}

foreach ($forbidden in @('SetWindowsHookEx', 'CreateRemoteThread', 'WriteProcessMemory')) {
    if ($allSource -match [regex]::Escape($forbidden)) {
        throw "Forbidden host-interference API present: $forbidden"
    }
}

Write-Host 'PASS: overlay source contracts verified.'
