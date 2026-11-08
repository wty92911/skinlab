# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Package an already built development app and its selected native bundle.
param([string]$Bundle = '')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (!$Bundle) { $Bundle = (Get-Content (Join-Path $taskRoot 'out/current.json') -Raw | ConvertFrom-Json).bundle }
if ($Bundle -notmatch '^session(?:-[a-z0-9-]+)?$') { throw 'Invalid native bundle name' }
$taskNative = Join-Path $taskRoot "out/$Bundle"
$taskManifest = Get-Content (Join-Path $taskNative 'build-manifest.json') -Raw | ConvertFrom-Json
foreach ($taskArtifact in $taskManifest.artifacts) {
    if ($taskArtifact.name -match '[/\\]' -or $taskArtifact.name.StartsWith('.')) { throw 'Invalid artifact name' }
    $taskHash = (Get-FileHash -LiteralPath (Join-Path $taskNative $taskArtifact.name) -Algorithm SHA256).Hash
    if ($taskHash -ne $taskArtifact.sha256) { throw "Artifact hash mismatch: $($taskArtifact.name)" }
}
$taskStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$taskOutput = Join-Path $taskRoot "out/SkinLab-debug-$taskStamp"
[void](New-Item -ItemType Directory -Path (Join-Path $taskOutput 'native'))
Copy-Item -LiteralPath (Join-Path $taskRoot 'target/debug/skinlab.exe') -Destination (Join-Path $taskOutput 'SkinLab-Debug.exe')
# GNU development builds load this beside the EXE; it is not part of native/.
Copy-Item -LiteralPath (Join-Path $taskRoot 'target/debug/WebView2Loader.dll') -Destination $taskOutput
foreach ($taskFile in @('skinlab-observe-host.exe', 'skinlab-observe.dll', 'skinlab-observe-fixture.exe', 'build-manifest.json')) {
    Copy-Item -LiteralPath (Join-Path $taskNative $taskFile) -Destination (Join-Path $taskOutput 'native')
}
$taskEncoding = [Text.UTF8Encoding]::new($false)
$taskCheck = @'
@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
set "SKINLAB_DEBUG_LOG_DIR=%~dp0debug-logs"
set "SKINLAB_CHECK_ROOT=%~dp0"
if not exist "%SKINLAB_DEBUG_LOG_DIR%" mkdir "%SKINLAB_DEBUG_LOG_DIR%"
echo 正在读取系统信息，并测试独立的自建窗口，不会连接游戏……
powershell -NoProfile -Command "$ErrorActionPreference='Continue'; & { Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,OSArchitecture | Format-List; 'PowerShell: '+$PSVersionTable.PSVersion; '64-bit process: '+[Environment]::Is64BitProcess; $i=[Security.Principal.WindowsIdentity]::GetCurrent(); $u=[Security.Principal.WindowsPrincipal]::new($i); 'Administrator: '+$u.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator); if(Get-Command Get-ProcessMitigation -ErrorAction SilentlyContinue){ Get-Process -Name 'League of Legends','SkinLab','SkinLab-Debug' -ErrorAction SilentlyContinue | ForEach-Object { 'Process: '+$_.ProcessName+' PID='+$_.Id; Get-ProcessMitigation -Id $_.Id | ConvertTo-Json -Depth 8 } } else { 'Get-ProcessMitigation unavailable' } } 2>&1 | Out-File -Encoding UTF8 -LiteralPath (Join-Path $env:SKINLAB_DEBUG_LOG_DIR 'environment.txt')"
set "SKINLAB_NATIVE_DEBUG=1"
"%~dp0native\skinlab-observe-host.exe" >"%SKINLAB_DEBUG_LOG_DIR%\fixture.json" 2>"%SKINLAB_DEBUG_LOG_DIR%\fixture-native.log"
echo %errorlevel% >"%SKINLAB_DEBUG_LOG_DIR%\fixture-exit.txt"
powershell -NoProfile -Command "Get-ChildItem -LiteralPath (Join-Path $env:SKINLAB_CHECK_ROOT 'native') -Filter 'startup-hook-*.log' -ErrorAction SilentlyContinue | Copy-Item -Destination $env:SKINLAB_DEBUG_LOG_DIR"
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $r=Get-Content -LiteralPath (Join-Path $env:SKINLAB_DEBUG_LOG_DIR 'fixture.json') -Raw | ConvertFrom-Json; $p=Join-Path (Join-Path $env:SKINLAB_CHECK_ROOT 'native') ('startup-hook-'+$r.target_pid+'.log'); $t=Get-Content -LiteralPath $p -Raw; foreach($s in @('version_mismatch','open_mapping_failed','open_event_failed','worker_created')){ if(!$t.Contains($s)){throw ('Missing trace: '+$s)}; 'PASS '+$s }; 'TRACE_SELF_CHECK_PASSED'" >"%SKINLAB_DEBUG_LOG_DIR%\trace-selfcheck.txt" 2>&1
echo 自检完成，结果在 debug-logs 文件夹。接下来按说明运行接入调试。
pause
'@
$taskLaunch = @'
@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
if not exist "%~dp0SkinLab-Debug.exe" (
  echo 缺少 SkinLab-Debug.exe，请完整解压调试包。请勿在压缩包中直接运行。
  pause
  exit /b 2
)
if not exist "%~dp0WebView2Loader.dll" (
  echo 缺少 WebView2Loader.dll，请将补丁 DLL 放到 SkinLab-Debug.exe 旁边。
  pause
  exit /b 2
)
powershell -NoProfile -Command "$i=[Security.Principal.WindowsIdentity]::GetCurrent(); $u=[Security.Principal.WindowsPrincipal]::new($i); if($u.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){exit 0}else{exit 1}"
if errorlevel 1 (
  echo 请右键此文件，选择“以管理员身份运行”。
  pause
  exit /b 1
)
set "SKINLAB_NATIVE_DIR=%~dp0native"
set "SKINLAB_DEBUG_ROOT=%~dp0"
set "SKINLAB_LOG_DIR=%~dp0debug-logs"
set "SKINLAB_DEBUG_LOG_DIR=%~dp0debug-logs"
set "SKINLAB_NATIVE_DEBUG=1"
set "SKINLAB_LOG=skinlab=debug,skinlab_session=debug"
if not exist "%SKINLAB_LOG_DIR%" mkdir "%SKINLAB_LOG_DIR%"
echo 正在启动调试版。结束检查后请关闭程序窗口，随后会自动打包日志。
"%~dp0SkinLab-Debug.exe" >"%SKINLAB_LOG_DIR%\launcher.log" 2>&1
powershell -NoProfile -Command "Get-ChildItem -LiteralPath (Join-Path $env:SKINLAB_DEBUG_ROOT 'native') -Filter 'startup-hook-*.log' -ErrorAction SilentlyContinue | Copy-Item -Destination $env:SKINLAB_DEBUG_LOG_DIR"
powershell -NoProfile -Command "Compress-Archive -LiteralPath $env:SKINLAB_DEBUG_LOG_DIR -DestinationPath (Join-Path (Split-Path $env:SKINLAB_DEBUG_LOG_DIR -Parent) 'debug-logs.zip') -Force"
echo 请把此目录下的 debug-logs.zip 发回。
pause
'@
[IO.File]::WriteAllText((Join-Path $taskOutput '1-check-environment.cmd'), ($taskCheck -replace "`r?`n", "`r`n"), $taskEncoding)
[IO.File]::WriteAllText((Join-Path $taskOutput '2-run-debug.cmd'), ($taskLaunch -replace "`r?`n", "`r`n"), $taskEncoding)
$taskReadme = @'
SkinLab 0.5.3 接入调试包 v2（非正式发布版）

1. 将压缩包完整解压到可写目录。不要在压缩包中直接运行。
2. 关闭正式版 SkinLab。先右键 1-check-environment.cmd，以管理员身份运行。
   这一步只查询系统信息与进程保护策略，并测试自建窗口，不连接游戏、不改变系统设置。
3. 等已有对局结束后，新开一局训练模式，保持游戏画面正常显示。
   右键 2-run-debug.cmd，以管理员身份运行；不要在匹配／排位中做调试。
4. 先不要点击皮肤。出现成功或失败结果后，关闭 SkinLab 调试窗口，等待命令窗口打包完成。
5. 发回 debug-logs.zip。若脚本中途退出，也可以直接压缩 debug-logs 文件夹。

直接双击 SkinLab-Debug.exe 不会启用本包的详细日志配置，请使用上述入口。
本包不会安装或覆盖正式版，不改系统保护配置。2-run-debug 会尝试接入训练对局。
日志包含进程编号、系统版本、组件路径和技术诊断，不采集账号凭据或完整游戏内存。
v2 增加独立 startup-hook 日志、原生模块查询错误码和退出清理日志。未生成目标日志不能单独证明回调未执行。
许可与使用说明见 LICENSE 和程序内说明。
'@
[IO.File]::WriteAllText((Join-Path $taskOutput '使用说明.txt'), $taskReadme, $taskEncoding)
Copy-Item -LiteralPath (Join-Path $taskRoot 'LICENSE') -Destination $taskOutput
$taskFiles = Get-ChildItem -LiteralPath $taskOutput -File -Recurse | ForEach-Object {
    [ordered]@{ name = $_.FullName.Substring($taskOutput.Length + 1); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$taskInfo = [ordered]@{ kind = 'private-startup-debug-v2'; base_version = '0.5.3'; startup_trace = $taskManifest.startup_trace; created = $taskStamp; files = @($taskFiles) }
[IO.File]::WriteAllText((Join-Path $taskOutput 'debug-manifest.json'), ($taskInfo | ConvertTo-Json -Depth 5), $taskEncoding)
Compress-Archive -LiteralPath $taskOutput -DestinationPath "$taskOutput.zip"
Write-Output "$taskOutput.zip"
