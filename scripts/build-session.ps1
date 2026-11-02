# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Compatibility entry point; the native build has one implementation in xtask.
param(
    [string]$Compiler = '',
    [ValidatePattern('^session(?:-[a-z0-9-]+)?$')][string]$Bundle = 'session'
)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    $taskArguments = @('xtask', 'native', '--bundle', $Bundle)
    if ($Compiler) { $taskArguments += @('--compiler', $Compiler) }
    & cargo @taskArguments
    if ($LASTEXITCODE -ne 0) { throw 'Native session build failed' }
} finally {
    Pop-Location
}
