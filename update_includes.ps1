# Update include statements for renamed files
$sourceDir = "\\mycloudex2ultra\dwaugh\Antigravity\davecore\DaveCoreProject\Source"

Get-ChildItem -Path $sourceDir -Include *.h, *.cpp -Recurse | ForEach-Object {
    $content = Get-Content $_.FullName -Raw
    $changed = $false
    
    if ($content -match 'DaveCoreConstants\.h') {
        $content = $content -replace 'DaveCoreConstants\.h', 'OpenRigConstants.h'
        $changed = $true
    }
    if ($content -match 'DaveCoreEngine\.h') {
        $content = $content -replace 'DaveCoreEngine\.h', 'OpenRigEngine.h'
        $changed = $true
    }
    if ($content -match 'DaveCoreEngine_Sequential\.h') {
        $content = $content -replace 'DaveCoreEngine_Sequential\.h', 'OpenRigEngine_Sequential.h'
        $changed = $true
    }
    if ($content -match 'DaveCore_Icon') {
        $content = $content -replace 'DaveCore_Icon', 'OpenRig_Icon'
        $changed = $true
    }
    
    if ($changed) {
        Set-Content $_.FullName -Value $content -NoNewline
        Write-Host "Updated includes: $($_.Name)"
    }
}

# Update the .rc file for the icon path
$rcFile = "\\mycloudex2ultra\dwaugh\Antigravity\davecore\DaveCoreProject\Builds\VisualStudio2026\resources.rc"
if (Test-Path $rcFile) {
    $content = Get-Content $rcFile -Raw
    $content = $content -replace 'DaveCore_Icon', 'OpenRig_Icon'
    Set-Content $rcFile -Value $content -NoNewline
    Write-Host "Updated: resources.rc icon path"
}

Write-Host "Done!"
