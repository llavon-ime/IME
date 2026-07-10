# 拉風輸入法

拉風輸入法的 Windows TSF 前端與推論服務。

Linux/macOS 的 fcitx5 frontend、AUR package 與 macOS release workflow 已移至 [llavon-ime/ime-fcitx5](https://github.com/llavon-ime/ime-fcitx5)。

## 模型

模型可從 Hugging Face 下載：

```text
https://huggingface.co/tony65535/llavon-ime-llama-250m-GGUF
```

模型本身採用 CC-BY-NC-4.0 授權，請依照該授權條款使用；若要散布包含模型的安裝包，也必須遵守模型授權限制。

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

開始使用前，請先手動啟動 `llavon-ime-service.exe`，否則輸入法不會有模型推論結果。

測試新版 DLL 時，請務必開新的記事本視窗，確保載入的是新版 DLL。

如果 CMake 無法覆寫 DLL，通常是舊 DLL 仍被系統載入。可以先將舊 DLL 改名，重開機後再刪除。

第一次建置後，需要到 Windows 設定中手動加入輸入法；只需要做一次。
