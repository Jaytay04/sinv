function ConvertFrom-RestoreLayoutJson([string]$Json) {
    $rows = ConvertFrom-Json -InputObject $Json
    foreach ($row in $rows) { Write-Output $row }
}

function ConvertTo-RestorePath([string]$Path) {
    $normalized = $Path.Replace('/','\')
    if ([string]::IsNullOrWhiteSpace($normalized) -or $normalized -match '^[\\/]|:' -or
        @($normalized.Split('\') | Where-Object { $_ -in @('','.','..') }).Count) {
        throw ('Invalid ESP relative path: '+$Path)
    }
    return $normalized
}

function Compare-RestoreEspFiles([object[]]$Expected,[object[]]$Actual) {
    $expectedMap = @{}; $actualMap = @{}
    foreach ($collection in @(@{ rows=$Expected; map=$expectedMap },@{ rows=$Actual; map=$actualMap })) {
        foreach ($row in $collection.rows) {
            $key = ConvertTo-RestorePath $row.path
            if ($collection.map.ContainsKey($key)) { throw ('Duplicate ESP path: '+$key) }
            $collection.map[$key] = $row
        }
    }
    foreach ($key in @($expectedMap.Keys | Sort-Object)) {
        $before = $expectedMap[$key]; $after = $actualMap[$key]
        $classification = 'unchanged'; $pass = $true
        if ($null -eq $after) { $classification='missing'; $pass=$false }
        elseif ([bool]$before.directory -ne [bool]$after.directory) { $classification='type_mismatch'; $pass=$false }
        elseif ($before.directory) { $classification='directory' }
        elseif (-not $before.sha256) {
            if ($key -imatch '^EFI\\Microsoft\\Boot\\BCD(?:\.LOG[0-9]*)?$') { $classification='active_bcd_export_and_enumeration_scope' }
            else { $classification='missing_baseline_hash'; $pass=$false }
        }
        elseif (-not $after.sha256) { $classification='missing_current_hash'; $pass=$false }
        elseif ($before.sha256 -ine $after.sha256) {
            if ($key -ieq 'EFI\Microsoft\Boot\BOOTSTAT.DAT') { $classification='mutable_boot_status_changed' }
            else { $classification='hash_mismatch'; $pass=$false }
        }
        [pscustomobject][ordered]@{ path=$key; classification=$classification; pass=$pass; expectedSha256=$before.sha256; actualSha256=if($after){$after.sha256}else{$null} }
    }
    foreach ($key in @($actualMap.Keys | Sort-Object)) {
        if (-not $expectedMap.ContainsKey($key)) {
            [pscustomobject][ordered]@{path=$key;classification='unexpected';pass=$false;expectedSha256=$null;actualSha256=$actualMap[$key].sha256}
        }
    }
}
