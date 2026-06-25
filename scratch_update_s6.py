import re
import os

with open('main/ui/screens/ui_Screen6.c', 'r', encoding='utf-8') as f:
    s6 = f.read()

# Remove the ESP-Claw Terminal from Screen 6
# It starts around line 965 and ends at 1073
s6 = re.sub(r'// --- ESP-Claw Terminal ---.*?// Note: Virtual keyboard removed to save memory\.\n  // Lua code is injected by the AI or edited via text input\.', '', s6, flags=re.DOTALL)

# Remove the corresponding callbacks in Screen 6
s6 = re.sub(r'static void lua_run_event_cb.*?static void ta_event_cb.*?}\n\n', '', s6, flags=re.DOTALL)

with open('main/ui/screens/ui_Screen6.c', 'w', encoding='utf-8') as f:
    f.write(s6)

print("Screen 6 updated successfully.")
