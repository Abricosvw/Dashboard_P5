# ПРАВИЛА И РУКОВОДСТВА ПРОЕКТА (RULES)

## ⚠️ СТРОГОЕ ПРАВИЛО БЕЗОПАСНОСТИ (SECURITY RULE)

**КАТЕГОРИЧЕСКИ ЗАПРЕЩАЕТСЯ ЗАГРУЖАТЬ (COMMIT/PUSH) НА GITHUB ИЛИ ЛЮБЫЕ ДРУГИЕ ОТКРЫТЫЕ ИСТОЧНИКИ СЛЕДУЮЩУЮ КОНФИДЕНЦИАЛЬНУЮ ИНФОРМАЦИЮ:**

1. **Токены API** (Gemini API Key, Telegram Bot Token и любые другие).
2. **Личные ID** (Telegram Chat ID, User ID и т.д.).
3. **Пароли и SSID** (Названия Wi-Fi сетей и пароли от них).
4. **IP-адреса и MAC-адреса** (Внутренние и внешние адреса устройств).
5. **Локации** (Любые координаты или физические адреса).

### Как работать с конфигурацией безопасно:
- Вся чувствительная информация должна храниться в файле `main/ai_config.h`.
- Файл `main/ai_config.h` **УЖЕ ДОБАВЛЕН** в `.gitignore` и никогда не должен попадать в историю Git.
- Если в проект добавляются новые ключи, они должны добавляться в шаблон `main/ai_config.example.h` в виде пустышек (заглушек).
- При клонировании проекта разработчик должен скопировать `main/ai_config.example.h`, переименовать его в `main/ai_config.h` и вписать свои личные данные туда.

**Antigravity (AI Assistant) Rules:**
* AI Assistant must strictly respect this rule and never extract, summarize, or expose the contents of `main/ai_config.h` into commit messages, pull requests, logs, or public GitHub comments.
