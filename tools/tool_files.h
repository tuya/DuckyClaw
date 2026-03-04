/**
 * @file tool_files.h
 * @brief MCP file operation tools for DuckyClaw
 * @version 0.1
 * @date 2025-03-25
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TOOL_FILES_H__
#define __TOOL_FILES_H__

#include "tuya_cloud_types.h"
#include "ai_mcp_server.h"
#include "tal_fs.h"

#ifndef CLAW_USE_SDCARD
#if defined(PLATFORM_LINUX) && (PLATFORM_LINUX == 1)
#define CLAW_USE_SDCARD 0
#else
#define CLAW_USE_SDCARD 1
#endif
#endif

#if defined(CLAW_USE_SDCARD) && (CLAW_USE_SDCARD == 1)
#include "tkl_fs.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/**
 * @brief Filesystem type selection
 *
 * CLAW_FS_FLASH  - Use internal flash filesystem (tal interfaces)
 * CLAW_FS_SDCARD - Use SD card filesystem (tkl interfaces)
 *
 * Set CLAW_USE_SDCARD to 1 to use SD card, 0 for flash.
 */


/* Filesystem root path macros */
#if defined(CLAW_USE_SDCARD) && (CLAW_USE_SDCARD == 1)
#define CLAW_FS_ROOT_PATH          "/sdcard"
#define CLAW_FS_MOUNT_PATH         "/sdcard"
#define CLAW_FS_ROOT_PATH_EMPTY    0
#else
#if defined(PLATFORM_LINUX) && (PLATFORM_LINUX == 1)
#define CLAW_FS_ROOT_PATH          ""
#define CLAW_FS_ROOT_PATH_EMPTY    1
#else
#define CLAW_FS_ROOT_PATH          "/spiffs"
#define CLAW_FS_ROOT_PATH_EMPTY    0
#endif
#endif

/* Default config file paths */
#define CLAW_CONFIG_DIR        CLAW_FS_ROOT_PATH "/config"
#define CLAW_USER_CONFIG_PATH  CLAW_CONFIG_DIR "/user.md"
#define CLAW_AGENT_CONFIG_PATH CLAW_CONFIG_DIR "/agent.md"

/**
 * Unified macro redefinitions for filesystem interfaces.
 *
 * When CLAW_USE_SDCARD == 1, use tkl (SD card) interfaces.
 * Otherwise, use tal (flash) interfaces.
 *
 * These macros allow easy switching between flash and SD card filesystems.
 */
#if defined(CLAW_USE_SDCARD) && (CLAW_USE_SDCARD == 1)
#define claw_fopen     tkl_fopen
#define claw_fclose    tkl_fclose
#define claw_fread     tkl_fread
#define claw_fwrite    tkl_fwrite
#define claw_fgets     tkl_fgets
#define claw_fgetsize  tkl_fgetsize
#define claw_dir_open  tkl_dir_open
#define claw_dir_close tkl_dir_close
#define claw_dir_read  tkl_dir_read
#define claw_dir_name  tkl_dir_name
#define claw_fs_mkdir       tkl_fs_mkdir
#define claw_fs_mount       tkl_fs_mount
#define claw_fs_remove      tkl_fs_remove
#define claw_fs_is_exist    tkl_fs_is_exist
#define claw_dir_is_regular tkl_dir_is_regular
#else
#define claw_fopen          tal_fopen
#define claw_fclose         tal_fclose
#define claw_fread          tal_fread
#define claw_fwrite         tal_fwrite
#define claw_fgets          tal_fgets
#define claw_fgetsize       tal_fgetsize
#define claw_dir_open       tal_dir_open
#define claw_dir_close      tal_dir_close
#define claw_dir_read       tal_dir_read
#define claw_dir_name       tal_dir_name
#define claw_fs_mkdir       tal_fs_mkdir
#define claw_fs_remove      tal_fs_remove
#define claw_fs_is_exist    tal_fs_is_exist
#define claw_dir_is_regular tal_dir_is_regular
#endif

#define claw_malloc    tal_psram_malloc
#define claw_free      tal_psram_free

/***********************************************************
********************function declaration********************
***********************************************************/

/**
 * @brief Initialize filesystem
 *
 * Mounts SD card if CLAW_USE_SDCARD is enabled.
 * Creates default config directory and files if not present.
 *
 * @return OPERATE_RET OPRT_OK on success, error code on failure
 */
OPERATE_RET tool_files_fs_init(void);

/**
 * @brief Register all file operation MCP tools
 *
 * Registers read_file, write_file, edit_file, and list_dir tools
 * with the MCP server.
 *
 * @return OPERATE_RET OPRT_OK on success, error code on failure
 */
OPERATE_RET tool_files_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __TOOL_FILES_H__ */