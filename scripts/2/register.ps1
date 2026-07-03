#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Registers the TSF IME with the system (calls regsvr32) and adds it to the
    zh-Hant-TW input method list so it appears in the language switcher.
.PARAMETER DllPath
    Full path to llavon-ime.dll. Defaults to ..\..\build\x64-debug\tsf\Debug\llavon-ime.dll.
#>
param(
    [string]$DllPath = (Join-Path $PSScriptRoot "..\..\build\x64-debug\tsf\Debug\llavon-ime.dll")
)

$resolved = Resolve-Path $DllPath -ErrorAction SilentlyContinue
if (-not $resolved) {
    Write-Error "DLL not found: $DllPath`nPlease run cmake --build first."
    exit 1
}

# Step 1: Register the COM server / TSF profile via regsvr32
Write-Host "Registering COM server: $resolved"
& regsvr32.exe /s "$resolved"

if ($LASTEXITCODE -ne 0) {
    Write-Error "regsvr32 failed with exit code: $LASTEXITCODE"
    exit $LASTEXITCODE
}
Write-Host "COM/TSF registration successful." -ForegroundColor Green

# Step 2: Add to the Windows language input-method list
# Format: LANGID:{CLSID}{ProfileGUID}  (matches Globals.h values)
$tip = "0404:{C262415B-2D5F-4DA8-A7F5-0798802ECFAC}{B2C3D4E5-F6A7-8901-BCDE-F01234567891}"

$langList = Get-WinUserLanguageList
$zhLang   = $langList | Where-Object { $_.LanguageTag -match "^zh-Hant-TW$|^zh-TW$" }

if (-not $zhLang) {
    Write-Warning "zh-Hant-TW language not found. Please add it in Windows Settings first."
} elseif ($zhLang.InputMethodTips -contains $tip) {
    Write-Host "IME already in language list." -ForegroundColor Yellow
} else {
    $zhLang.InputMethodTips.Add($tip)
    Set-WinUserLanguageList $langList -Force
    Write-Host "IME added to zh-Hant-TW input methods." -ForegroundColor Green
    Write-Host "You may need to sign out and back in for the change to take effect."
}
