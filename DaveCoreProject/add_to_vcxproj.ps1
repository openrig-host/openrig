# Helper script to add new C++ files to vcxproj, filters and jucer files

$vcxprojPath = "Builds\VisualStudio2026\DaveCore_App.vcxproj"
$filtersPath = "Builds\VisualStudio2026\DaveCore_App.vcxproj.filters"
$jucerPath = "DaveCore.jucer"

# 1. Update vcxproj
[xml]$vcXml = Get-Content -Raw $vcxprojPath
$itemGroupClCompile = $vcXml.Project.ItemGroup | Where-Object { $_.ClCompile }
$itemGroupClInclude = $vcXml.Project.ItemGroup | Where-Object { $_.ClInclude }

# Add CPP files
$cpps = @("..\..\Source\RigBuilder.cpp", "..\..\Source\RigTransitioner.cpp")
foreach ($cpp in $cpps) {
    $exists = $itemGroupClCompile.ClCompile | Where-Object { $_.Include -eq $cpp }
    if (-not $exists) {
        $newNode = $vcXml.CreateElement("ClCompile", $vcXml.DocumentElement.NamespaceURI)
        $newNode.SetAttribute("Include", $cpp)
        $itemGroupClCompile.AppendChild($newNode) | Out-Null
        Write-Host "Added $cpp to vcxproj"
    }
}

# Add Header files
$headers = @(
    "..\..\Source\RigModel.h", 
    "..\..\Source\RigSerializer.h", 
    "..\..\Source\RigBuilder.h", 
    "..\..\Source\RigTransitioner.h", 
    "..\..\Source\MidiLearnBus.h",
    "..\..\Source\LibraryPanel.h",
    "..\..\Source\SamplerProcessor.h",
    "..\..\Source\SamplerComponent.h",
    "..\..\Source\WaveformSpliceEditor.h",
    "..\..\Source\SetupMidiTriggers.h"
)
foreach ($h in $headers) {
    $exists = $itemGroupClInclude.ClInclude | Where-Object { $_.Include -eq $h }
    if (-not $exists) {
        $newNode = $vcXml.CreateElement("ClInclude", $vcXml.DocumentElement.NamespaceURI)
        $newNode.SetAttribute("Include", $h)
        $itemGroupClInclude.AppendChild($newNode) | Out-Null
        Write-Host "Added $h to vcxproj"
    }
}
$vcXml.Save($vcxprojPath)

# 2. Update filters
[xml]$filtXml = Get-Content -Raw $filtersPath
$fGroupClCompile = $filtXml.Project.ItemGroup | Where-Object { $_.ClCompile }
$fGroupClInclude = $filtXml.Project.ItemGroup | Where-Object { $_.ClInclude }

foreach ($cpp in $cpps) {
    $exists = $fGroupClCompile.ClCompile | Where-Object { $_.Include -eq $cpp }
    if (-not $exists) {
        $newNode = $filtXml.CreateElement("ClCompile", $filtXml.DocumentElement.NamespaceURI)
        $newNode.SetAttribute("Include", $cpp)
        $filterNode = $filtXml.CreateElement("Filter", $filtXml.DocumentElement.NamespaceURI)
        $filterNode.InnerText = "Source"
        $newNode.AppendChild($filterNode) | Out-Null
        $fGroupClCompile.AppendChild($newNode) | Out-Null
        Write-Host "Added $cpp to filters"
    }
}

foreach ($h in $headers) {
    $exists = $fGroupClInclude.ClInclude | Where-Object { $_.Include -eq $h }
    if (-not $exists) {
        $newNode = $filtXml.CreateElement("ClInclude", $filtXml.DocumentElement.NamespaceURI)
        $newNode.SetAttribute("Include", $h)
        $filterNode = $filtXml.CreateElement("Filter", $filtXml.DocumentElement.NamespaceURI)
        $filterNode.InnerText = "Source"
        $newNode.AppendChild($filterNode) | Out-Null
        $fGroupClInclude.AppendChild($newNode) | Out-Null
        Write-Host "Added $h to filters"
    }
}
$filtXml.Save($filtersPath)

# 3. Update jucer
[xml]$juXml = Get-Content -Raw $jucerPath
# Find the GROUP name="Source"
$sourceGroup = $juXml.JUCERPROJECT.MAINGROUP.GROUP | Where-Object { $_.name -eq "Source" }

# Add files to Jucer
# Structure: <FILE id="uhRr7a" name="Main.cpp" compile="1" resource="0" file="Source/Main.cpp"/>
$jucerFiles = @(
    @{ id="rigModelH"; name="RigModel.h"; compile="0"; file="Source/RigModel.h" },
    @{ id="rigSerializerH"; name="RigSerializer.h"; compile="0"; file="Source/RigSerializer.h" },
    @{ id="rigBuilderH"; name="RigBuilder.h"; compile="0"; file="Source/RigBuilder.h" },
    @{ id="rigBuilderCpp"; name="RigBuilder.cpp"; compile="1"; file="Source/RigBuilder.cpp" },
    @{ id="rigTransitionerH"; name="RigTransitioner.h"; compile="0"; file="Source/RigTransitioner.h" },
    @{ id="rigTransitionerCpp"; name="RigTransitioner.cpp"; compile="1"; file="Source/RigTransitioner.cpp" },
    @{ id="midiLearnBusH"; name="MidiLearnBus.h"; compile="0"; file="Source/MidiLearnBus.h" },
    @{ id="libraryPanelH"; name="LibraryPanel.h"; compile="0"; file="Source/LibraryPanel.h" },
    @{ id="samplerProcessorH"; name="SamplerProcessor.h"; compile="0"; file="Source/SamplerProcessor.h" },
    @{ id="samplerComponentH"; name="SamplerComponent.h"; compile="0"; file="Source/SamplerComponent.h" },
    @{ id="waveformSpliceEditorH"; name="WaveformSpliceEditor.h"; compile="0"; file="Source/WaveformSpliceEditor.h" },
    @{ id="setupMidiTriggersH"; name="SetupMidiTriggers.h"; compile="0"; file="Source/SetupMidiTriggers.h" }
)

foreach ($jf in $jucerFiles) {
    $exists = $sourceGroup.FILE | Where-Object { $_.name -eq $jf.name }
    if (-not $exists) {
        $newNode = $juXml.CreateElement("FILE")
        $newNode.SetAttribute("id", $jf.id)
        $newNode.SetAttribute("name", $jf.name)
        $newNode.SetAttribute("compile", $jf.compile)
        $newNode.SetAttribute("resource", "0")
        $newNode.SetAttribute("file", $jf.file)
        $sourceGroup.AppendChild($newNode) | Out-Null
        Write-Host "Added $($jf.name) to jucer"
    }
}
$juXml.Save($jucerPath)

Write-Host "Done adding sources!"
