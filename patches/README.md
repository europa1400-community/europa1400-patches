# patches/

One folder per patch: `patch.ini` (manifest), sources, `CMakeLists.txt` (`e1400_add_patch(<id> SOURCES ...)`), and a short
README describing what the original does wrong and how the patch was tested. Create one with
`./scripts/new-patch.ps1 <id>`; see [../docs/patch-authoring.md](../docs/patch-authoring.md).
