param(
    [Parameter(Mandatory = $true)][string]$InputDirectory,
    [Parameter(Mandatory = $true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$parts = [ordered]@{ basic = 128; control = 32; ext_latin = 96 }
$glyphs = [Collections.Generic.List[byte[]]]::new()
$provenance = [Collections.Generic.List[string]]::new()
foreach ($part in $parts.Keys) {
    $file = Join-Path $InputDirectory ('font8x8_' + $part + '.h')
    $source = Get-Content -LiteralPath $file -Raw
    if ($source -notmatch 'License: Public Domain') { throw ('Missing font license: ' + $file) }
    $rows = [regex]::Matches($source, '\{\s*((?:0x[0-9a-fA-F]{2}\s*,?\s*){8})\}')
    if ($rows.Count -ne $parts[$part]) { throw ('Unexpected glyph count: ' + $file) }
    foreach ($row in $rows) {
        $values = [regex]::Matches($row.Groups[1].Value, '0x[0-9a-fA-F]{2}')
        $glyph = [byte[]]::new(16)
        for ($index = 0; $index -lt 8; $index++) {
            $glyph[$index * 2] = [Convert]::ToByte($values[$index].Value.Substring(2), 16)
            $glyph[$index * 2 + 1] = $glyph[$index * 2]
        }
        $glyphs.Add($glyph)
    }
    $provenance.Add(' * font8x8_' + $part + '.h SHA-256: ' + (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash)
}
if ($glyphs.Count -ne 256) { throw 'Expected exactly 256 Latin-1 glyphs.' }
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('/* 8x16 ISO-8859-1 diagnostic font; public domain.')
$lines.Add(' * Derived from Daniel Hepper font8x8 (Marcel Sondaar / IBM VGA sources).')
$lines.Add(' * Source: https://github.com/dhepper/font8x8 (basic/control/ext_latin).')
$lines.Add(' * Each original 8x8 row is repeated twice; bit 0 is the leftmost pixel.')
$lines.Add(' * Controls and spacing characters retain their upstream blank glyphs.')
foreach ($entry in $provenance) { $lines.Add($entry) }
$lines.Add(' * Regenerate with spoof-handoff/commands/generate-overlay-font.ps1.')
$lines.Add(' */')
$lines.Add('#ifndef HYPERSINV_EFI_FONT_ROM_H')
$lines.Add('#define HYPERSINV_EFI_FONT_ROM_H')
$lines.Add('#include "overlay_render.h"')
$lines.Add('static const uint8_t HvOverlayFontRom[HV_OVERLAY_FONT_GLYPHS][HV_OVERLAY_FONT_HEIGHT] = {')
for ($index = 0; $index -lt $glyphs.Count; $index++) {
    $values = ($glyphs[$index] | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', '
    $lines.Add(('    {{ {0} }}, /* U+{1:X4} */' -f $values, $index))
}
$lines.Add('};')
$lines.Add('#endif')
[IO.File]::WriteAllLines([IO.Path]::GetFullPath($OutputPath), $lines, [Text.UTF8Encoding]::new($false))
