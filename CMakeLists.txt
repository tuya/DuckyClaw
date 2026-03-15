##
# @file CMakeLists.txt
# @brief 
#/

# APP_PATH
set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})

# APP_NAME
get_filename_component(APP_NAME ${APP_PATH} NAME)

# APP_SRCS
aux_source_directory(${APP_PATH}/src APP_SRCS)
aux_source_directory(${APP_PATH}/tools APP_SRCS)
aux_source_directory(${APP_PATH}/cron_service APP_SRCS)
aux_source_directory(${APP_PATH}/heartbeat APP_SRCS)
aux_source_directory(${APP_PATH}/memory APP_SRCS)
aux_source_directory(${APP_PATH}/gateway APP_SRCS)
aux_source_directory(${APP_PATH}/skills APP_SRCS)
aux_source_directory(${APP_PATH}/agent APP_SRCS)
# DuckyClaw UI overrides (e.g. expanded emoji list for all 27 emotions)
aux_source_directory(${APP_PATH}/ui APP_SRCS)

# APP_INC – ui/include is listed BEFORE TuyaOpen paths so DuckyClaw headers
# take precedence when the same header name exists in both trees.
set(APP_INC
    ${APP_PATH}
    ${APP_PATH}/include
    ${APP_PATH}/tools
    ${APP_PATH}/cron_service
    ${APP_PATH}/heartbeat
    ${APP_PATH}/memory
    ${APP_PATH}/gateway
    ${APP_PATH}/skills
    ${APP_PATH}/agent
)

########################################
# Target Configure
########################################
add_library(${EXAMPLE_LIB})

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_SRCS}
    )

target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_INC}
    )

add_subdirectory(${APP_PATH}/TuyaOpen/apps/tuya.ai/ai_components)
add_subdirectory(${APP_PATH}/IM ${CMAKE_BINARY_DIR}/IM)

# ---------------------------------------------------------------------------
# DuckyClaw UI overrides – remove TuyaOpen's versions of any file that
# DuckyClaw provides its own implementation for, preventing duplicate-symbol
# linker errors when the TuyaOpen submodule is initialised.
# ---------------------------------------------------------------------------
get_target_property(_EXAMPLE_LIB_SRCS ${EXAMPLE_LIB} SOURCES)
if(_EXAMPLE_LIB_SRCS)
    # Remove TuyaOpen's ai_ui_icon_font.c; DuckyClaw's ui/ version is used.
    list(FILTER _EXAMPLE_LIB_SRCS EXCLUDE REGEX ".*/TuyaOpen/.*ai_ui_icon_font\\.c$")
    set_property(TARGET ${EXAMPLE_LIB} PROPERTY SOURCES ${_EXAMPLE_LIB_SRCS})
endif()