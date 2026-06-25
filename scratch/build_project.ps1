$env:IDF_PATH = "C:\esp-idf-v5.3.3\frameworks\esp-idf-v5.3.3"
. "$env:IDF_PATH\export.ps1"
python "$env:IDF_PATH\tools\idf.py" build
