@echo off
chcp 65001 > nul
set PYTHONUTF8=1
set IDF_PATH=C:\esp-idf-v5.3.3\frameworks\esp-idf-v5.3.3
set IDF_TOOLS_PATH=C:\esp-idf-v5.3.3
set PATH=C:\esp-idf-v5.3.3\python_env\idf5.3_py3.11_env\Scripts;C:\esp-idf-v5.3.3\tools\idf-git\2.44.0\cmd;%PATH%
call "%IDF_PATH%\export.bat"
idf.py build
