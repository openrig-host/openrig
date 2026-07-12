$xml = [xml](Get-Content -Raw "Builds\VisualStudio2026\DaveCore_App.vcxproj")
$xml.Project.ItemGroup.ClCompile.Include
