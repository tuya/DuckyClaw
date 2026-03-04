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

# APP_INC
set(APP_INC
    ${APP_PATH}/include
    ${APP_PATH}/tools
    ${APP_PATH}/cron_service
    ${APP_PATH}/heartbeat
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