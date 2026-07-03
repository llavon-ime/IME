#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Unregisters the TSF IME from the system (calls regsvr32 /u) and removes it
    from the zh-Hant-TW input method list.
.PARAMETER DllPath
    Full path to llavon-ime.dll. Defaults to ..\..\build\x64-debug\tsf\Debug\llavon-ime.dll.
#>
param(
    [string]$DllPath = (Join-Path $PSScriptRoot "..\..\build\x64-debug\tsf\Debug\llavon-ime.dll")
)

$resolved = Resolve-Path $DllPath -ErrorAction SilentlyContinue
if (-not $resolved) {
    Write-Error "DLL not found: $DllPath"
    exit 1
}

# Step 1: Remove from the Windows language input-method list first
$tip = "0404:{C262415B-2D5F-4DA8-A7F5-0798802ECFAC}{B2C3D4E5-F6A7-8901-BCDE-F01234567891}"

$langList = Get-WinUserLanguageList
$zhLang   = $langList | Where-Object { $_.LanguageTag -match "^zh-Hant-TW$|^zh-TW$" }

if ($zhLang -and ($zhLang.InputMethodTips -contains $tip)) {
    $zhLang.InputMethodTips.Remove($tip) | Out-Null
    Set-WinUserLanguageList $langList -Force
    Write-Host "IME removed from zh-Hant-TW input methods." -ForegroundColor Green
} else {
    Write-Host "IME was not in the language list (skipping)." -ForegroundColor Yellow
}

# Step 2: Unregister the COM server / TSF profile via regsvr32
Write-Host "Unregistering COM server: $resolved"
& regsvr32.exe /s /u "$resolved"

if ($LASTEXITCODE -eq 0) {
    Write-Host "Unregistration successful!" -ForegroundColor Green
} else {
    Write-Error "regsvr32 /u failed with exit code: $LASTEXITCODE"
    exit $LASTEXITCODE
}
