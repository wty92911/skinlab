param([string]$Compiler='',[ValidatePattern('^session(?:-[a-z0-9-]+)?$')][string]$Bundle='session')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
Push-Location $taskRoot
try {
if (-not $Compiler) {
    $taskCommand=Get-Command x86_64-w64-mingw32-g++.exe,g++.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if (Test-Path -LiteralPath 'C:\Program Files\mingw-w64-gcc-7.3-stable-r28\bin\x86_64-w64-mingw32-g++.exe') { $Compiler='C:\Program Files\mingw-w64-gcc-7.3-stable-r28\bin\x86_64-w64-mingw32-g++.exe' }
    elseif ($taskCommand) { $Compiler=$taskCommand.Source }
    else { throw 'Install an x64 MinGW-w64 compiler or pass -Compiler <path>' }
}
$taskPython=$null
foreach ($taskCandidate in @('.venv\Scripts\python.exe','.tools\python\Scripts\python.exe','python.exe')) {
    $taskCommand=Get-Command $taskCandidate -ErrorAction SilentlyContinue
    if ($taskCommand) { $taskPython=$taskCommand.Source;break }
}
if (-not $taskPython) { throw 'Install Python 3.10+ with Tkinter and add python.exe to PATH' }
    if ((& $Compiler -dumpmachine) -notmatch '^x86_64-') { throw 'Windows x64 compiler required' }
    New-Item -ItemType Directory -Force -Path ("out\$Bundle") | Out-Null
    & $taskPython -X utf8 scripts\generate_skin_catalog.py --source skinlab\data\catalog.json --output native\loader\skin_catalog_generated.h
    if ($LASTEXITCODE -ne 0) { throw 'Skin catalog generation failed' }
    $taskCommon=@('-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-D_WIN32_WINNT=0x0601','-DSKINLAB_EMBEDDED_CORE','-Inative/include','-Inative/loader')
    $taskScan=@('native/src/profile_scan.cpp','native/src/profile_rules_generated.cpp')
    & $Compiler @taskCommon '-shared' @taskScan native/src/profile_snapshot_win.cpp native/src/core.cpp native/src/dx11_bridge.cpp native/loader/observe_hook.cpp '-o' "out/$Bundle/skinlab-observe.dll" '-Wl,--exclude-all-symbols' '-luser32' '-lbcrypt'
    if ($LASTEXITCODE -ne 0) { throw 'Session DLL build failed' }
    & $Compiler @taskCommon '-municode' native/lab/observe_fixture.cpp '-o' "out/$Bundle/skinlab-observe-fixture.exe" '-luser32' '-ld3d11' '-ldxgi' '-ldxguid'
    if ($LASTEXITCODE -ne 0) { throw 'Session fixture build failed' }
    & $Compiler @taskCommon '-municode' @taskScan native/loader/observe_host.cpp '-o' "out/$Bundle/skinlab-observe-host.exe" '-luser32' '-lbcrypt' '-ladvapi32'
    if ($LASTEXITCODE -ne 0) { throw 'Session host build failed' }
    $taskPaths=@('native/include/skinlab/api.h','native/include/skinlab/dx11_bridge.h','native/include/skinlab/profile_scan.h','native/include/skinlab/league_fields.h','native/src/core.cpp','native/src/dx11_bridge.cpp','native/src/profile_scan.cpp','native/src/profile_rules_generated.cpp','native/src/profile_snapshot_win.cpp','native/loader/observe_hook.cpp','native/loader/observe_host.cpp','native/loader/observe_protocol.h','native/loader/observe_hash.h','native/loader/session_binding.h','native/loader/game_session.h','native/loader/session_fixture.h','native/loader/skin_catalog.h','native/lab/observe_fixture.cpp','scripts/build-session.ps1')
    $taskPaths+=@('native/loader/skin_catalog_generated.h','scripts/generate_skin_catalog.py')
    $taskSources=@(foreach ($taskPath in $taskPaths) { [ordered]@{path=$taskPath;sha256=(Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant()} })
    $taskArtifacts=@(foreach ($taskName in @('skinlab-observe.dll','skinlab-observe-fixture.exe','skinlab-observe-host.exe')) {
        $taskPath=Join-Path "out\$Bundle" $taskName
        [ordered]@{name=$taskName;bytes=(Get-Item -LiteralPath $taskPath).Length;sha256=(Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    [ordered]@{compiler=$Compiler;target='x86_64-w64-mingw32';bundle=$Bundle;sources=$taskSources;artifacts=$taskArtifacts} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "out\$Bundle\build-manifest.json" -Encoding utf8
    [ordered]@{bundle=$Bundle} | ConvertTo-Json | Set-Content -LiteralPath out\current.json -Encoding utf8
} finally { Pop-Location }
