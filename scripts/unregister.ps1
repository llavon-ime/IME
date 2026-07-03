#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Unregisters the TSF IME from the system (calls regsvr32 /u).
.PARAMETER DllPath
    Full path to llavon-ime.dll. Defaults to ..\build\x64-release-llama-vulkan\tsf\Release\llavon-ime.dll.
#>
param(
    [string]$DllPath = (Join-Path $PSScriptRoot "..\build\x64-release-llama-vulkan\tsf\Release\llavon-ime.dll")
)

$resolved = (Resolve-Path $DllPath -ErrorAction SilentlyContinue).Path
if (-not $resolved) {
    Write-Error "DLL not found: $DllPath"
    exit 1
}

Write-Host "Unregistering IME: $resolved"
$proc = Start-Process -FilePath regsvr32.exe -ArgumentList @("/s", "/u", $resolved) -Wait -PassThru
$exitCode = $proc.ExitCode

if ($exitCode -eq 0) {
    Write-Host "Unregistration successful!" -ForegroundColor Green
} else {
    Write-Error "Unregistration failed. regsvr32 returned: $exitCode"
    exit $exitCode
}
