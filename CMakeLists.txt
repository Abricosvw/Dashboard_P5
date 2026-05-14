cmake_minimum_required(VERSION 3.16)

# Register ESP-Claw sub-components that this project uses.
# NOTE: cap_mcp_client/server excluded (require IDF >= 5.4)
#       cap_boards excluded (requires gen_bmgr_codes which is not present)
set(EXTRA_COMPONENT_DIRS
    # --- claw core modules ---
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_modules/claw_core
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_modules/claw_cap
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_modules/claw_event_router
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_modules/claw_memory
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_modules/claw_skill

    # --- claw capabilities ---
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_im_tg
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_im_attachment
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_im_local
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_lua
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_system
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_router_mgr
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_session_mgr
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_time
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_files
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_scheduler
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_skill_mgr
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_web_search
    ${CMAKE_CURRENT_LIST_DIR}/components/claw_capabilities/cap_llm_inspect

    # --- common modules ---
    ${CMAKE_CURRENT_LIST_DIR}/components/common/app_claw
    ${CMAKE_CURRENT_LIST_DIR}/components/common/settings

    # --- lua modules (used by cap_lua) ---
    ${CMAKE_CURRENT_LIST_DIR}/components/lua_modules/lua_module_lcd_touch
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Dashboard_P5)
