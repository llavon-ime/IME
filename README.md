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

# Linux (fcitx5)

The Linux port builds the 拉風輸入法 fcitx5 input method addon and a local inference service. Models are not downloaded automatically. Provide a local `.gguf` model path in the fcitx5 configuration UI or with `IME_LINUX_MODEL_PATH` when running the service manually.

## Linux dependencies

Package names vary by distribution. Install these before configuring:

- CMake 3.20 or newer
- A C++23 compiler and standard build tools
- the checked-out `vcpkg` submodule
- pkg-config
- fcitx5 runtime and development headers

Linux dependencies managed by vcpkg are declared in `linux/vcpkg.json`. During configure, vcpkg downloads and builds `llama-cpp` and `nlohmann-json`; `linux/third_party/llama.cpp` is only a local checkout and is ignored by Git.

Example package sets:

```bash
# Arch Linux
sudo pacman -S base-devel cmake ninja pkgconf fcitx5

# Debian/Ubuntu
sudo apt install build-essential cmake ninja-build pkg-config fcitx5 libfcitx5core-dev
```

## Linux CPU build

Run these commands from the repository root. Use this when you want a CPU-only build:

```bash
./vcpkg/bootstrap-vcpkg.sh
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build/linux
```

## Linux Vulkan build

Use the `llama-vulkan` vcpkg manifest feature when you want Vulkan GPU or iGPU offload:

```bash
./vcpkg/bootstrap-vcpkg.sh
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_FEATURES=llama-vulkan
cmake --build build/linux
```

## Linux install

Configure the install prefix before building. Use `/usr` for a system fcitx5 install, or a temporary prefix for packaging and dry runs.

```bash
./vcpkg/bootstrap-vcpkg.sh
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/linux
sudo cmake --install build/linux
```

After installing, restart fcitx5 and enable 拉風輸入法 from your fcitx5 configuration UI:

```bash
fcitx5 -r
```

If the desktop environment does not pick up the new addon, log out and back in.

## Linux model setup

Configure 拉風輸入法 from your fcitx5 configuration UI. The addon exposes model, service, and input settings through the standard fcitx config page, and fcitx saves them to `~/.config/fcitx5/conf/ime-linux.conf`.

Set `ModelPath` to a local `.gguf` model. For direct service testing, or if you need to override the saved model path for the current service process, start the service with:

```bash
IME_LINUX_MODEL_PATH=/path/to/model.gguf ime-linux-service
```

## Linux input behavior

The fcitx5 addon uses a McBopomofo-style live composition. Completed syllables immediately render as Han candidates in preedit, additional syllables extend the same composition, candidate selection keeps the composition active, and Enter commits the full preedit string.

fcitx5 會將設定儲存在 `~/.config/fcitx5/conf/ime-linux.conf`。設定介面顯示中文名稱，主要項目如下：

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

## Linux manual test checklist

- `cmake --build build/linux --target ime-linux-service` succeeds.
- `cmake --build build/linux --target ime-fcitx5-addon` succeeds when fcitx5 development files are installed.
- `cmake --install build/linux` stages `ime-linux-service`, fcitx5 addon metadata, and the input method metadata under the configured prefix.
- `fcitx5 -r` restarts fcitx5 without addon load errors.
- The fcitx5 configuration UI shows settings for 拉風輸入法.
- Selecting 拉風輸入法 in fcitx5 shows candidates while typing.

## Linux troubleshooting

- If CMake skips `ime-fcitx5-addon`, install the fcitx5 development package and reconfigure from a clean build directory.
- If `IME_LINUX_ENABLE_ROCM=ON` fails, verify `hipcc` is on `PATH` or use `-DIME_LINUX_ENABLE_ROCM=AUTO` or `OFF`.
- If no model loads, verify the `.gguf` path exists and is readable. There is no automatic model download.
- If the service cannot be spawned by the 拉風輸入法 addon, verify `ime-linux-service` is installed in `/usr/local/bin` or `/usr/bin`, or set `IME_LINUX_SERVICE_PATH` to the built service binary for local testing.
