# 拉風輸入法

拉風輸入法是一個中文輸入法專案，目前包含 Windows TSF 前端，以及 Linux/macOS 可用的 fcitx5 前端。fcitx5 版本會啟動本機推論服務，使用 `.gguf` 模型輔助候選字排序。

## 模型

目前測試用模型暫放於 Google Drive：

```text
https://drive.google.com/file/d/1cPvtxCcCNy56cQazP052YQz9QzjeLrWS/view?usp=sharing
```

這是臨時下載連結。模型本身採用 CC-BY-NC-4.0 授權，請依照該授權條款使用；若要散布包含模型的安裝包，也必須遵守模型授權限制。

## 取得原始碼

請連同 submodule 一起 clone：

```bash
git clone --recurse-submodules <repo url>
```

若已經 clone 但沒有 submodule：

```bash
git submodule update --init --recursive
```

## Windows TSF 開發

### 設定與建置

```bash
cmake --preset x64-release
cmake --build --preset x64-release-build
```

也可以使用整合腳本：

```bash
python build_script.py
```

或在 Windows 上執行：

```powershell
.\dev.bat
```

查看所有可用 preset：

```bash
cmake --list-presets
cmake --build --list-presets
```

### 註冊與移除

```powershell
register.ps1
unregister.ps1
```

開始使用前，請先手動啟動 `service.exe`，否則輸入法不會有模型推論結果。

測試新版 DLL 時，請務必開新的記事本視窗，確保載入的是新版 DLL。

如果 CMake 無法覆寫 DLL，通常是舊 DLL 仍被系統載入。可以先將舊 DLL 改名，重開機後再刪除。

第一次建置後，需要到 Windows 設定中手動加入輸入法；只需要做一次。

## fcitx5 前端（Linux / macOS）

fcitx5 前端包含 `拉風輸入法` addon 和本機推論服務。Linux 使用 fcitx5，macOS 使用 fcitx5-macos。模型不會在一般開發建置時自動下載，請在 fcitx5 設定介面指定本機 `.gguf` 模型，或手動啟動服務時用 `IME_FCITX5_MODEL_PATH` 覆寫模型路徑。

### 相依套件

各發行版套件名稱略有不同，設定 CMake 前請先安裝：

- CMake 3.20 或更新版本
- 支援 C++23 的編譯器與基本建置工具
- 本 repo 內已 checkout 的 `vcpkg` submodule
- `pkg-config`
- Linux 需要 fcitx5 runtime 與開發 headers
- macOS 需要 fcitx5-macos app，以及一份 fcitx5-macos 原始碼 checkout

常見套件安裝範例：

```bash
# Arch Linux
sudo pacman -S base-devel cmake ninja pkgconf fcitx5

# Debian/Ubuntu
sudo apt install build-essential cmake ninja-build pkg-config fcitx5 libfcitx5core-dev

# macOS
brew install cmake ninja pkg-config
```

macOS 請先安裝 fcitx5-macos。release app 的 runtime dylib 位於 `/Library/Input Methods/Fcitx5.app/Contents/lib`，但不包含開發 headers，所以仍需要 clone `fcitx5-macos` 原始碼，並在 CMake 設定時傳入 `FCITX5_MACOS_SOURCE_DIR`。

### fcitx5 CPU 建置

以下命令請在 repo 根目錄執行。CPU-only 建置可用：

```bash
./vcpkg/bootstrap-vcpkg.sh
cmake -S fcitx5 -B build/fcitx5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build/fcitx5
```

### fcitx5 Vulkan 建置

如果要用 Vulkan GPU 或 iGPU offload，啟用 `llama-vulkan` vcpkg manifest feature：

```bash
./vcpkg/bootstrap-vcpkg.sh
cmake -S fcitx5 -B build/fcitx5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_FEATURES=llama-vulkan
cmake --build build/fcitx5
```

### fcitx5 Linux 安裝

建置前先設定安裝 prefix。系統安裝通常使用 `/usr`，打包或 dry run 可使用暫時 prefix。

```bash
./vcpkg/bootstrap-vcpkg.sh
cmake -S fcitx5 -B build/fcitx5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/fcitx5
sudo cmake --install build/fcitx5
```

安裝後重啟 fcitx5，並在 fcitx5 設定介面啟用 `拉風輸入法`：

```bash
fcitx5 -r
```

如果桌面環境沒有抓到新的 addon，請登出後重新登入。

### macOS 建置與安裝

macOS 開發安裝建議使用 fcitx5-macos 的使用者 plugin prefix 作為 `CMAKE_INSTALL_PREFIX`：

```bash
git clone --recursive git@github.com:fcitx/fcitx5-macos.git ../fcitx5-macos
./vcpkg/bootstrap-vcpkg.sh
cmake -S fcitx5 -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DFCITX5_MACOS_SOURCE_DIR="$PWD/../fcitx5-macos" -DCMAKE_INSTALL_PREFIX="$HOME/Library/fcitx5"
cmake --build build/macos
cmake --install build/macos
```

Apple Silicon 如果要使用 Metal GPU offload，啟用 `llama-metal`：

```bash
cmake -S fcitx5 -B build/macos-metal -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_FEATURES=llama-metal -DFCITX5_MACOS_SOURCE_DIR="$PWD/../fcitx5-macos" -DCMAKE_INSTALL_PREFIX="$HOME/Library/fcitx5"
cmake --build build/macos-metal
cmake --install build/macos-metal
```

fcitx5 預設 `顯示卡分層數` (`GpuLayers`) 為 `999`，llama.cpp 會盡量將支援的模型層 offload 到可用 GPU backend。若要強制 CPU-only，請在 fcitx5 設定中將它改成 `0`。

如果 `fcitx5-macos` 已 clone 但沒有 submodule，請先執行：

```bash
git -C ../fcitx5-macos submodule update --init fcitx5
```

安裝後從選單重啟 fcitx5-macos，或執行：

```bash
pkill -x Fcitx5
open -b org.fcitx.inputmethod.Fcitx5
```

addon id 是 `ime-fcitx5`。新安裝會移除舊的 `ime-linux` metadata，但仍會讀取舊的 `ime-linux` 設定檔作為 fallback。

## macOS 安裝包

安裝好 fcitx5-macos 並 clone 它的原始碼後，可在 repo 根目錄建立 macOS Installer `.pkg`：

```bash
FCITX5_MACOS_SOURCE_DIR=/path/to/fcitx5-macos ./scripts/package-macos.sh
```

此腳本預設建置 Apple Silicon Metal 版本、執行測試、只打包拉風輸入法 payload，並輸出：

```bash
dist/macos/ime-fcitx5-0.1.0-arm64.pkg
```

安裝包會將系統 payload 放到 `/Library/Application Support/IME-Fcitx5/payload`，將內建 `.gguf` 模型放到 `/Library/Application Support/IME-Fcitx5/models`。`postinstall` script 會把 plugin 複製到目前使用者的 `~/Library/fcitx5`，並重啟 fcitx5-macos。如果使用者沒有設定 `ModelPath`，服務會自動使用內建模型。

打包腳本預設會使用 `models/` 底下唯一的 `.gguf` 檔案。如果有多個模型，請明確指定：

```bash
IME_FCITX5_PACKAGE_MODEL_PATH=/path/to/model.gguf \
FCITX5_MACOS_SOURCE_DIR=/path/to/fcitx5-macos \
./scripts/package-macos.sh
```

模型仍受其自身授權限制。目前連結的模型為 CC-BY-NC-4.0；除非模型授權允許，否則不要商業散布包含該模型的安裝包。

可選的簽章設定：

```bash
DEVELOPER_ID_APPLICATION="Developer ID Application: Your Name (TEAMID)" \
DEVELOPER_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)" \
FCITX5_MACOS_SOURCE_DIR=/path/to/fcitx5-macos \
./scripts/package-macos.sh
```

簽章後可用 Apple 的 `notarytool` 和 `stapler` 進行 notarization 與 staple。

本機測試時可用下列命令移除：

```bash
sudo ./packaging/macos/uninstall.sh
```

## macOS Release 自動化

GitHub Actions workflow `.github/workflows/release-macos.yml` 會在 push `v*` tag 時發布安裝包。流程會從 Google Drive 下載內建 GGUF 模型、建置 macOS arm64 `.pkg`、上傳到對應 GitHub Release、計算 package sha256，並更新 `billy948787/homebrew-la-fong` 裡的 Homebrew Cask。

必要 GitHub secret：

- `HOMEBREW_TAP_TOKEN`: 需要能寫入 `billy948787/homebrew-la-fong` repo 的 GitHub token

可選的簽章與 notarization secrets：

- `MACOS_CERTIFICATE_P12`: base64 編碼的 Developer ID `.p12` 憑證
- `MACOS_CERTIFICATE_PASSWORD`: `.p12` 憑證密碼
- `KEYCHAIN_PASSWORD`: CI 暫時 keychain 密碼
- `DEVELOPER_ID_APPLICATION`: Developer ID Application identity 名稱
- `DEVELOPER_ID_INSTALLER`: Developer ID Installer identity 名稱
- `APPLE_ID`: notarization 使用的 Apple ID
- `APPLE_TEAM_ID`: Apple developer team id
- `APPLE_APP_SPECIFIC_PASSWORD`: notarization 使用的 app-specific password

發布新版本：

```bash
git tag v0.1.0
git push origin v0.1.0
```

Workflow 成功後，使用者可用 Homebrew 安裝或更新：

```bash
brew install --cask billy948787/la-fong/ime-fcitx5
brew update
brew upgrade --cask ime-fcitx5
```

## 模型與設定

請從 fcitx5 設定介面設定拉風輸入法。addon 會透過標準 fcitx 設定頁提供模型、服務與輸入行為設定。Linux 設定檔位於 `~/.config/fcitx5/conf/ime-fcitx5.conf`；fcitx5-macos 設定檔位於 `~/Library/Application Support/fcitx5/conf/ime-fcitx5.conf`。

`ModelPath` 需指向本機 `.gguf` 模型。若要直接測試服務，或暫時覆寫目前服務 process 使用的模型路徑，可執行：

```bash
IME_FCITX5_MODEL_PATH=/path/to/model.gguf ime-fcitx5-service
```

主要設定項目如下：

- `模型路徑`: 本機 `.gguf` 模型路徑
- `上下文長度`: 推論上下文長度
- `執行緒數`: 處理器推論執行緒數
- `顯示卡分層數`: 要卸載到顯示卡後端的模型層數，fcitx5 預設為 `999`
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

## 輸入行為

fcitx5 addon 使用類似 McBopomofo 的即時組字方式。完成的音節會立即在 preedit 中顯示漢字候選，後續音節會延伸同一段組字；選字後組字狀態會維持，按 Enter 會送出完整 preedit 字串。

## 手動測試清單

- `cmake --build build/fcitx5 --target ime-fcitx5-service` 可成功
- 安裝 fcitx5 開發檔後，`cmake --build build/fcitx5 --target ime-fcitx5-addon` 可成功
- `cmake --install build/fcitx5` 或 `cmake --install build/macos` 會將 `ime-fcitx5-service`、表格資料、fcitx5 addon metadata、input method metadata 安裝到設定的 prefix
- `fcitx5 -r` 或重啟 macOS fcitx5 app 後，fcitx5 沒有 addon 載入錯誤
- fcitx5 設定介面能看到拉風輸入法設定
- 選取拉風輸入法後，打字時能顯示候選字

## 疑難排解

- 如果 CMake 跳過 `ime-fcitx5-addon`，請安裝 fcitx5 開發套件，並用乾淨 build 目錄重新設定
- macOS 如果 CMake 跳過 `ime-fcitx5-addon`，請確認 `/Library/Input Methods/Fcitx5.app/Contents/lib/libFcitx5Core.dylib` 存在，且 `FCITX5_MACOS_SOURCE_DIR` 指向已初始化 `fcitx5` submodule 的 fcitx5-macos checkout
- 如果 Vulkan 建置失敗，請確認 Vulkan 開發套件與驅動已安裝，或改用 CPU 建置
- 如果沒有載入模型，請確認 `.gguf` 路徑存在且可讀；一般開發建置不會自動下載模型
- 如果拉風輸入法 addon 無法啟動服務，請確認 `ime-fcitx5-service` 已安裝到設定 prefix 的 `bin` 目錄、`/opt/homebrew/bin`、`/usr/local/bin` 或 `/usr/bin`，或在本機測試時設定 `IME_FCITX5_SERVICE_PATH` 指向建置出的服務 binary
