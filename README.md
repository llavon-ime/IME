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