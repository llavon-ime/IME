#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Registers the TSF IME with the system (calls regsvr32).
.PARAMETER DllPath
    Full path to llavon-ime.dll. Defaults to ..\build\x64-release-llama-vulkan\tsf\Release\llavon-ime.dll.
#>
param(
    [string]$DllPath = (Join-Path $PSScriptRoot "..\build\x64-release-llama-vulkan\tsf\Release\llavon-ime.dll")
)

$resolved = (Resolve-Path $DllPath -ErrorAction SilentlyContinue).Path
if (-not $resolved) {
    Write-Error "DLL not found: $DllPath`nPlease run cmake --build first."
    exit 1
}

Write-Host "Registering IME: $resolved"
$proc = Start-Process -FilePath regsvr32.exe -ArgumentList @("/s", $resolved) -Wait -PassThru
$exitCode = $proc.ExitCode

if ($exitCode -eq 0) {
    Write-Host "Registration successful!" -ForegroundColor Green
} else {
    Write-Error "Registration failed. regsvr32 returned: $exitCode"
    exit $exitCode
}
