# 模型
https://drive.google.com/file/d/1cPvtxCcCNy56cQazP052YQz9QzjeLrWS/view?usp=sharing

臨時連結，模型 under CC-BY-NC-4.0 授權，請依此授權下使用

# clone repo
```bash
git clone --recurse-submodules <repo url>
```
# 設定
cmake --preset x64-release

# 建置
cmake --build --preset x64-release-build

# build script (all in one)
python build_script.py
or
.\dev.bat

# getting start
請務必在開始使用本輸入法時，先手動啟用service.exe否則會感覺不到任何推論。

# 查看所有可用 preset
cmake --list-presets
cmake --build --list-presets

# register
register.ps1
unregister.ps1

# 測試
務必開新視窗的記事本 (確保載入新版dll)

# cmake 無法寫入 dll
把原本的dll改名(因為刪不掉，重開機再刪除)

# first build
需手動至設定添加輸入法(只要加一次)

# fcitx5 Frontend (Linux / macOS)

The fcitx5 port builds the 拉風輸入法 fcitx5 input method addon and a local inference service. The same frontend can be built on Linux and macOS with fcitx5-macos. Models are not downloaded automatically. Provide a local `.gguf` model path in the fcitx5 configuration UI or with `IME_FCITX5_MODEL_PATH` when running the service manually.

## fcitx5 dependencies

Package names vary by distribution. Install these before configuring:

- CMake 3.20 or newer
- A C++23 compiler and standard build tools
- the checked-out `vcpkg` submodule
- pkg-config
- fcitx5 runtime and development headers on Linux
- fcitx5-macos app plus a fcitx5-macos source checkout on macOS

Example package sets:

```bash
# Arch Linux
sudo pacman -S base-devel cmake ninja pkgconf fcitx5

# Debian/Ubuntu
sudo apt install build-essential cmake ninja-build pkg-config fcitx5 libfcitx5core-dev

# macOS
brew install cmake ninja pkg-config
```

On macOS, install fcitx5-macos first. The release app keeps runtime dylibs in `/Library/Input Methods/Fcitx5.app/Contents/lib` but does not include development headers, so clone `fcitx5-macos` and pass it as `FCITX5_MACOS_SOURCE_DIR` when configuring.

## fcitx5 CPU build

Run these commands from the repository root. Use this when you want a CPU-only build:

```bash
./vcpkg/bootstrap-vcpkg.sh
cmake -S fcitx5 -B build/fcitx5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build/fcitx5
```

## fcitx5 Vulkan build

Use the `llama-vulkan` vcpkg manifest feature when you want Vulkan GPU or iGPU offload:

```bash
./vcpkg/bootstrap-vcpkg.sh
cmake -S fcitx5 -B build/fcitx5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_FEATURES=llama-vulkan
cmake --build build/fcitx5
```

## fcitx5 Linux install

Configure the install prefix before building. Use `/usr` for a system fcitx5 install, or a temporary prefix for packaging and dry runs.

```bash
./vcpkg/bootstrap-vcpkg.sh
cmake -S fcitx5 -B build/fcitx5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/fcitx5
sudo cmake --install build/fcitx5
```

After installing, restart fcitx5 and enable 拉風輸入法 from your fcitx5 configuration UI:

```bash
fcitx5 -r
```

If the desktop environment does not pick up the new addon, log out and back in.

## macOS build and install

Use the fcitx5-macos user plugin prefix as `CMAKE_INSTALL_PREFIX`:

```bash
git clone --recursive git@github.com:fcitx/fcitx5-macos.git ../fcitx5-macos
./vcpkg/bootstrap-vcpkg.sh
cmake -S fcitx5 -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DFCITX5_MACOS_SOURCE_DIR="$PWD/../fcitx5-macos" -DCMAKE_INSTALL_PREFIX="$HOME/Library/fcitx5"
cmake --build build/macos
cmake --install build/macos
```

For Apple Silicon GPU offload, enable the Metal ggml backend:

```bash
cmake -S fcitx5 -B build/macos-metal -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_FEATURES=llama-metal -DFCITX5_MACOS_SOURCE_DIR="$PWD/../fcitx5-macos" -DCMAKE_INSTALL_PREFIX="$HOME/Library/fcitx5"
cmake --build build/macos-metal
cmake --install build/macos-metal
```

After installing a Metal build, set `顯示卡分層數` (`GpuLayers`) above `0` in the fcitx configuration to offload model layers to the GPU. Use a large value such as `999` if you want llama.cpp to offload all supported layers.

If you already cloned fcitx5-macos without submodules, run `git -C ../fcitx5-macos submodule update --init fcitx5` before configuring.

Then restart the fcitx5 macOS app from its menu, or run:

```bash
pkill -x Fcitx5
open -b org.fcitx.inputmethod.Fcitx5
```

The addon id is `ime-fcitx5`. New installs remove old `ime-linux` metadata while still reading old `ime-linux` config files as a fallback.

## macOS package

Build a macOS Installer `.pkg` from the repository root after installing fcitx5-macos and cloning its source checkout:

```bash
FCITX5_MACOS_SOURCE_DIR=/path/to/fcitx5-macos ./scripts/package-macos.sh
```

The script builds the Apple Silicon Metal variant by default, runs the tests, stages only the 拉風輸入法 payload, and writes:

```bash
dist/macos/ime-fcitx5-0.1.0-arm64.pkg
```

The package installs a system-owned payload under `/Library/Application Support/IME-Fcitx5/payload`, installs the bundled `.gguf` model under `/Library/Application Support/IME-Fcitx5/models`, then its `postinstall` script copies the plugin into the active user's `~/Library/fcitx5` directory and restarts fcitx5-macos. If the user has not configured `ModelPath`, the service uses the bundled model automatically.

By default the packaging script bundles the only `.gguf` file under `models/`. If there are multiple models, choose one explicitly:

```bash
IME_FCITX5_PACKAGE_MODEL_PATH=/path/to/model.gguf \
FCITX5_MACOS_SOURCE_DIR=/path/to/fcitx5-macos \
./scripts/package-macos.sh
```

The model remains subject to its own license. The current linked model is CC-BY-NC-4.0, so do not distribute the package commercially unless the model license allows it.

Optional signing:

```bash
DEVELOPER_ID_APPLICATION="Developer ID Application: Your Name (TEAMID)" \
DEVELOPER_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)" \
FCITX5_MACOS_SOURCE_DIR=/path/to/fcitx5-macos \
./scripts/package-macos.sh
```

After signing, notarize and staple the signed package with Apple's `notarytool` and `stapler`.

For local removal during testing:

```bash
sudo ./packaging/macos/uninstall.sh
```

## macOS release automation

The GitHub Actions workflow `.github/workflows/release-macos.yml` publishes a package when a `v*` tag is pushed. It downloads the bundled GGUF model from Google Drive, builds the macOS arm64 package, uploads it to the matching GitHub Release, computes the package sha256, and updates the Homebrew Cask in `billy948787/homebrew-la-fong`.

Required GitHub secret:

- `HOMEBREW_TAP_TOKEN`: a GitHub token with write access to the `billy948787/homebrew-la-fong` repository.

Optional signing and notarization secrets:

- `MACOS_CERTIFICATE_P12`: base64-encoded Developer ID `.p12` certificate.
- `MACOS_CERTIFICATE_PASSWORD`: password for the `.p12` certificate.
- `KEYCHAIN_PASSWORD`: temporary CI keychain password.
- `DEVELOPER_ID_APPLICATION`: Developer ID Application identity name.
- `DEVELOPER_ID_INSTALLER`: Developer ID Installer identity name.
- `APPLE_ID`: Apple ID for notarization.
- `APPLE_TEAM_ID`: Apple developer team id.
- `APPLE_APP_SPECIFIC_PASSWORD`: app-specific password for notarization.

To publish a new version:

```bash
git tag v0.1.0
git push origin v0.1.0
```

After the workflow succeeds, users can upgrade through Homebrew:

```bash
brew update
brew upgrade --cask ime-fcitx5
```

## Model setup

Configure 拉風輸入法 from your fcitx5 configuration UI. The addon exposes model, service, and input settings through the standard fcitx config page. Linux saves settings to `~/.config/fcitx5/conf/ime-fcitx5.conf`; fcitx5-macos saves them under `~/Library/Application Support/fcitx5/conf/ime-fcitx5.conf`.

Set `ModelPath` to a local `.gguf` model. For direct service testing, or if you need to override the saved model path for the current service process, start the service with:

```bash
IME_FCITX5_MODEL_PATH=/path/to/model.gguf ime-fcitx5-service
```

## Input behavior

The fcitx5 addon uses a McBopomofo-style live composition. Completed syllables immediately render as Han candidates in preedit, additional syllables extend the same composition, candidate selection keeps the composition active, and Enter commits the full preedit string.

fcitx5 會將設定儲存在 `~/.config/fcitx5/conf/ime-fcitx5.conf`。設定介面顯示中文名稱，主要項目如下：

- `模型路徑`: 本機 `.gguf` 模型路徑
- `上下文長度`: 推論上下文長度
- `執行緒數`: 處理器推論執行緒數
- `顯示卡分層數`: 要卸載到顯示卡後端的模型層數
- `閒置逾時秒數`: 服務閒置多久後可退出
- `注音鍵盤配置`: `標準`
- `候選選字鍵`: `數字鍵`, `本位列`, 或 `左手鍵`
- `候選選字鍵數量`: `4` 到 `9`
- `候選頁大小`: `1` 到 `50`
- `候選窗排列`: `系統預設`, `垂直`, 或 `水平`
- `空白鍵選取候選字`: 候選窗開啟時空白鍵是否選取高亮候選字
- `候選字查詢位置`: `游標前` 或 `游標後`
- `選字後移動游標`: 選字後是否把游標移到該組字段後方
- `逸出鍵清除整個組字區`: 逸出鍵是否直接清空整個組字區
- `大寫鎖定時仍輸入注音`: 大寫鎖定開啟時，英文字母是否仍當成注音鍵輸入

## Manual test checklist

- `cmake --build build/fcitx5 --target ime-fcitx5-service` succeeds.
- `cmake --build build/fcitx5 --target ime-fcitx5-addon` succeeds when fcitx5 development files are installed.
- `cmake --install build/fcitx5` or `cmake --install build/macos` stages `ime-fcitx5-service`, table data, fcitx5 addon metadata, and the input method metadata under the configured prefix.
- `fcitx5 -r` or restarting the macOS fcitx5 app restarts fcitx5 without addon load errors.
- The fcitx5 configuration UI shows settings for 拉風輸入法.
- Selecting 拉風輸入法 in fcitx5 shows candidates while typing.

## Troubleshooting

- If CMake skips `ime-fcitx5-addon`, install the fcitx5 development package and reconfigure from a clean build directory.
- On macOS, if CMake skips `ime-fcitx5-addon`, verify `/Library/Input Methods/Fcitx5.app/Contents/lib/libFcitx5Core.dylib` exists and `FCITX5_MACOS_SOURCE_DIR` points to a fcitx5-macos checkout with the `fcitx5` submodule initialized.
- If the Vulkan build fails, verify Vulkan development packages and drivers are installed, or use the CPU build.
- If no model loads, verify the `.gguf` path exists and is readable. There is no automatic model download.
- If the service cannot be spawned by the 拉風輸入法 addon, verify `ime-fcitx5-service` is installed in the configured prefix's `bin` directory, `/opt/homebrew/bin`, `/usr/local/bin`, or `/usr/bin`, or set `IME_FCITX5_SERVICE_PATH` to the built service binary for local testing.
