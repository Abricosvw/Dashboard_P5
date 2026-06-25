import os
import sys
import subprocess

# Define paths - USE THE VIRTUAL ENV PYTHON WHICH HAS 'click' AND OTHER PACKAGES
python_exe = "C:/esp-idf-v5.3.3/python_env/idf5.3_py3.11_env/Scripts/python.exe"
idf_tools_py = "C:/esp-idf-v5.3.3/frameworks/esp-idf-v5.3.3/tools/idf_tools.py"
idf_py = "C:/esp-idf-v5.3.3/frameworks/esp-idf-v5.3.3/tools/idf.py"

# Set initial environment variables for idf_tools.py to run correctly
os.environ["IDF_PATH"] = "C:/esp-idf-v5.3.3/frameworks/esp-idf-v5.3.3"
os.environ["IDF_TOOLS_PATH"] = "C:/esp-idf-v5.3.3"
os.environ["PYTHONIOENCODING"] = "utf-8"
os.environ["PYTHONUTF8"] = "1"

print("Exporting ESP-IDF environment variables...")
# Run idf_tools.py export to get environment settings
result = subprocess.run(
    [python_exe, idf_tools_py, "export", "--format", "key-value"],
    capture_output=True,
    text=True,
    encoding="utf-8"
)

if result.returncode != 0:
    print("Failed to run idf_tools.py export:")
    print(result.stderr)
    sys.exit(1)

# Parse output (which is in key=value format per line)
env_vars = {}
for line in result.stdout.splitlines():
    line = line.strip()
    if not line or "=" not in line:
        continue
    # Let's split on first '='
    key, value = line.split("=", 1)
    # Strip quotes from value
    value = value.strip('"').strip("'")
    
    # If the key is PATH, we want to prepend the new paths to the existing system PATH
    if key.upper() == "PATH":
        # Resolve variables like %PATH% in the new path value
        # idf_tools.py export gives path like: C:\esp-idf-v5.3.3\tools\...;%PATH%
        # Let's replace %PATH% with current system PATH
        if "%PATH%" in value:
            value = value.replace("%PATH%", os.environ.get("PATH", ""))
        elif "$PATH" in value:
            value = value.replace("$PATH", os.environ.get("PATH", ""))
        os.environ["PATH"] = value
    else:
        os.environ[key] = value

# Print a summary of the environment we set up
print("Environment set up successfully:")
print("  IDF_PATH:", os.environ.get("IDF_PATH"))
print("  IDF_PYTHON_ENV_PATH:", os.environ.get("IDF_PYTHON_ENV_PATH"))

# Run the build command
print("\nRunning 'idf.py build'...")
build_result = subprocess.run(
    [python_exe, idf_py, "build"],
    cwd="C:/Users/11218/Dashboard_P5",
    stdout=sys.stdout,
    stderr=sys.stderr
)

if build_result.returncode != 0:
    print("\nBuild failed with exit code:", build_result.returncode)
    sys.exit(build_result.returncode)
else:
    print("\nBuild completed successfully!")
