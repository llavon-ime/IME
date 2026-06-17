from pathlib import Path
from datetime import datetime
import os
import subprocess

root = Path(__file__).resolve().parent
configure_preset = "x64-release-llama-vulkan"
build_preset = "x64-release-llama-vulkan-build"
build_config = "Release"
src = root / "tsf" / "src"
temp = root / ".temp"
manifest = temp / f".src_manifest_{configure_preset}.txt"

temp.mkdir(exist_ok=True)
for p in temp.glob("*.dll"):
    try:
        p.unlink()
    except OSError:
        pass

build_dir = root / "build" / configure_preset
tsf_dir = build_dir / "tsf" / build_config
tsf_dll = tsf_dir / "MyIME.dll"
service_exe = build_dir / "service" / build_config / "IME_Service.exe"


def stop_running_exe(exe: Path) -> None:
    if not exe.exists():
        return

    script = (
        "$target = [System.IO.Path]::GetFullPath($env:IME_SERVICE_TARGET); "
        "Get-CimInstance Win32_Process -Filter \"Name = 'IME_Service.exe'\" | "
        "Where-Object { "
        "    $_.ExecutablePath -and "
        "    ([System.IO.Path]::GetFullPath($_.ExecutablePath) -ieq $target) "
        "} | "
        "ForEach-Object { "
        "    Write-Host \"Stopping running service process PID $($_.ProcessId): $($_.ExecutablePath)\"; "
        "    Stop-Process -Id $_.ProcessId -Force -ErrorAction Stop "
        "}"
    )
    subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            script,
        ],
        cwd=root,
        env={**os.environ, "IME_SERVICE_TARGET": str(exe.resolve())},
        check=True,
    )

current = sorted(str(p.relative_to(root)).replace("\\", "/") for p in src.rglob("*") if p.is_file())

previous = manifest.read_text(encoding="utf-8").splitlines() if manifest.exists() else []
if set(current) != set(previous) or not build_dir.is_dir():
    subprocess.run(["cmake", "--preset", configure_preset], cwd=root, check=True)

if tsf_dll.exists():
    t = datetime.now().strftime("%Y%m%d_%H%M%S")
    bak = temp / f"MyIME_{t}.dll"
    i = 1
    while bak.exists():
        bak = temp / f"MyIME_{t}_{i}.dll"
        i += 1
    print(f"Moving previous TSF DLL out of the build output: {tsf_dll} -> {bak}")
    tsf_dll.replace(bak)

stop_running_exe(service_exe)

subprocess.run(["cmake", "--build", "--preset", build_preset], cwd=root, check=True)
if not tsf_dll.exists():
    raise FileNotFoundError(f"Build finished but TSF DLL was not produced: {tsf_dll}")

manifest.write_text("\n".join(current), encoding="utf-8")

register = (root / "scripts" / "register.ps1").resolve()
register_dll = tsf_dll.resolve()
subprocess.run(
    [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        (
            "$p=Start-Process -FilePath powershell.exe -Verb RunAs -Wait -PassThru "
            f"-ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File','{register}',"
            f"'-DllPath','{register_dll}'); "
            "exit $p.ExitCode"
        ),
    ],
    cwd=root,
    check=True,
)
