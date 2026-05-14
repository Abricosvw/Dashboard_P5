set(app_claw_srcs
    "app_claw.c"
    "app_capabilities.c"
    "app_lua_modules.c"
)

if(CONFIG_APP_CLAW_ENABLE_CLI)
    list(APPEND app_claw_srcs "app_claw_cli.c")
endif()

# We list all potential requirements. ESP-IDF will handle those that are not found 
# based on EXTRA_COMPONENT_DIRS, but this ensures they are correctly linked 
# if they exist in the project.
set(app_claw_requires
    claw_cap
    claw_core
    claw_event_router
    claw_memory
    claw_skill
    freertos
    cap_files
    cap_im_tg
    cap_im_local
    cap_scheduler
    cap_skill_mgr
    cap_web_search
    cap_llm_inspect
    cap_system
    cap_time
    cap_router_mgr
    cap_session_mgr
    console
    json
)

if(CONFIG_APP_CLAW_ENABLE_EMOTE)
    list(APPEND app_claw_requires emote)
endif()

# Lua Modules and other optional capabilities are hardcoded to avoid evaluation order issues.
# We ONLY list components that are guaranteed to be in EXTRA_COMPONENT_DIRS in the root CMakeLists.txt.
list(APPEND app_claw_requires 
    cap_lua
    lua_module_lcd_touch
)

idf_component_register(
    SRCS
        ${app_claw_srcs}
    INCLUDE_DIRS
        "include"
    REQUIRES
        ${app_claw_requires}
)
