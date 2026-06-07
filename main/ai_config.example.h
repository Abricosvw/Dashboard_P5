#pragma once

// ==========================================
// ⚠️ ВНИМАНИЕ: ЭТО ФАЙЛ-ШАБЛОН! ⚠️
// Переименуйте этот файл в "ai_config.h" и впишите свои данные.
// Файл "ai_config.h" добавлен в .gitignore и не попадет на GitHub.
// ==========================================

// Gemini Assistant Configuration
// Get your API Key from https://aistudio.google.com/
#define GEMINI_API_KEY "YOUR_GEMINI_API_KEY_HERE"

// Google Generative AI REST endpoint
#define GEMINI_API_ENDPOINT                                                    \
  "https://generativelanguage.googleapis.com/v1beta/models/"                   \
  "gemini-2.5-flash:generateContent"

// WiFi Credentials
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

// Telegram Integration
// Create a bot via @BotFather to get the token
#define TELEGRAM_BOT_TOKEN "YOUR_BOT_TOKEN_HERE"
// Get your Chat ID from @userinfobot
#define TELEGRAM_CHAT_ID "YOUR_PERSONAL_CHAT_ID_HERE"
