param()

function Convert-FileToByteArray([string]$filePath, [string]$varName) {
    $bytes = [System.IO.File]::ReadAllBytes($filePath)
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine("static const unsigned char " + $varName + "[] = {")
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if ($i % 24 -eq 0) { [void]$sb.Append("    ") }
        [void]$sb.Append(("0x{0:x2}, " -f $bytes[$i]))
        if ($i % 24 -eq 23) { [void]$sb.AppendLine() }
    }
    [void]$sb.AppendLine("0x00")
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine(("static const size_t " + $varName + "_LEN = " + $bytes.Length + ";"))
    [void]$sb.AppendLine()
    return $sb.ToString()
}

$targetHeader = (Join-Path $PSScriptRoot "EmbeddedWebAssets.h")
$sbAll = [System.Text.StringBuilder]::new()
[void]$sbAll.AppendLine("#pragma once")
[void]$sbAll.AppendLine("#include <cstddef>")
[void]$sbAll.AppendLine()
[void]$sbAll.AppendLine("namespace OpenRig {")
[void]$sbAll.AppendLine("namespace EmbeddedWebAssets {")
[void]$sbAll.AppendLine()

[void]$sbAll.Append((Convert-FileToByteArray (Join-Path $PSScriptRoot "static\index.html") "INDEX_HTML_DATA"))
[void]$sbAll.Append((Convert-FileToByteArray (Join-Path $PSScriptRoot "static\style.css") "STYLE_CSS_DATA"))
[void]$sbAll.Append((Convert-FileToByteArray (Join-Path $PSScriptRoot "static\app.js") "APP_JS_DATA"))

[void]$sbAll.AppendLine("} // namespace EmbeddedWebAssets")
[void]$sbAll.AppendLine("} // namespace OpenRig")

[System.IO.File]::WriteAllText($targetHeader, $sbAll.ToString())
Write-Output "Successfully generated EmbeddedWebAssets.h"
