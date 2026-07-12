# Rename DaveCore to OpenRig throughout the project
$sourceDir = "\\mycloudex2ultra\dwaugh\Antigravity\davecore\DaveCoreProject\Source"

# Replace in all .h and .cpp files
Get-ChildItem -Path $sourceDir -Include *.h,*.cpp -Recurse | ForEach-Object {
    $content = Get-Content $_.FullName -Raw
    if ($content -match 'DaveCore') {
        $newContent = $content -replace 'DaveCore', 'OpenRig'
        Set-Content $_.FullName -Value $newContent -NoNewline
        Write-Host "Updated: $($_.Name)"
    }
}

# Also update the .rc file
$rcFile = "\\mycloudex2ultra\dwaugh\Antigravity\davecore\DaveCoreProject\Builds\VisualStudio2026\resources.rc"
if (Test-Path $rcFile) {
    $content = Get-Content $rcFile -Raw
    $newContent = $content -replace 'DaveCore', 'OpenRig'
    Set-Content $rcFile -Value $newContent -NoNewline
    Write-Host "Updated: resources.rc"
}

Write-Host "Done!"
