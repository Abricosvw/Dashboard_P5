@echo off
set IDF_PATH=C:\esp-idf-v5.3.3\frameworks\esp-idf-v5.3.3
set PATH=C:\esp-idf-v5.3.3\tools\idf-python\3.11.2;%PATH%
call C:\esp-idf-v5.3.3\frameworks\esp-idf-v5.3.3\export.bat
python C:\esp-idf-v5.3.3\frameworks\esp-idf-v5.3.3\tools\idf.py build
