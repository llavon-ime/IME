from pathlib import Path
from datetime import datetime
import subprocess

root = Path(__file__).resolve().parent
src = root / "tsf" / "src"
temp = root / ".temp"
manifest = temp / ".src_manifest.txt"

temp.mkdir(exist_ok=True)
for p in temp.glob("*.dll"):
    try:
        p.unlink()
    except OSError:
        pass

build_dir = root / "build" / "x64-release"

current = sorted(str(p.relative_to(root)).replace("\\", "/") for p in src.rglob("*") if p.is_file())

previous = manifest.read_text(encoding="utf-8").splitlines() if manifest.exists() else []
if set(current) != set(previous) or not build_dir.is_dir():
    subprocess.run(["cmake", "--preset", "x64-release"], cwd=root, check=True)

dll = build_dir / "tsf" / "Release" / "MyIME.dll"
if dll.exists():
    t = datetime.now().strftime("%Y%m%d_%H%M%S")
    bak = temp / f"MyIME_{t}.dll"
    i = 1
    while bak.exists():
        bak = temp / f"MyIME_{t}_{i}.dll"
        i += 1
    dll.replace(bak)

subprocess.run(["cmake", "--build", "--preset", "x64-release-build"], cwd=root, check=True)
manifest.write_text("\n".join(current), encoding="utf-8")

register = (root / "scripts" / "register.ps1").resolve()
subprocess.run(
    [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        (
            "$p=Start-Process -FilePath powershell.exe -Verb RunAs -Wait -PassThru "
            f"-ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File','{register}'); "
            "exit $p.ExitCode"
        ),
    ],
    cwd=root,
    check=True,
)
