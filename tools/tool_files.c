/**
 * @file tool_files.c
 * @brief MCP file operation tools for DuckyClaw
 * @version 0.1
 * @date 2025-03-25
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 * Implements read_file, write_file, edit_file, and list_dir MCP tools
 * using the ai_mcp_server.h interface.
 */

#include "tool_files.h"

#include "tal_api.h"
#include "cJSON.h"
#include "tkl_fs.h"

#include <string.h>
#include <stdlib.h>

/***********************************************************
************************macro define************************
***********************************************************/

#define MAX_FILE_SIZE  (32 * 1024)

/***********************************************************
***********************variable define**********************
***********************************************************/


/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * @brief Validate file path security
 *
 * Only allows paths under CLAW_FS_ROOT_PATH and rejects path traversal attempts.
 *
 * @param path File path to validate
 * @return true if path is valid, false otherwise
 */
static bool __validate_path(const char *path)
{
    if (!path) {
        return false;
    }
    if (strncmp(path, CLAW_FS_ROOT_PATH "/", strlen(CLAW_FS_ROOT_PATH "/")) != 0) {
        return false;
    }
    if (strstr(path, "..") != NULL) {
        return false;
    }
    return true;
}

/**
 * @brief Helper to extract a string property from MCP property list
 *
 * @param properties MCP property list
 * @param name Property name to find
 * @return const char* Property string value, or NULL if not found
 */
static const char *__get_str_property(const MCP_PROPERTY_LIST_T *properties, const char *name)
{
    const MCP_PROPERTY_T *prop = ai_mcp_property_list_find(properties, name);
    if (prop && prop->type == MCP_PROPERTY_TYPE_STRING) {
        return prop->default_val.str_val;
    }
    return NULL;
}

/**
 * @brief MCP tool callback: read_file
 *
 * Reads a file from the filesystem and returns its content as a string.
 *
 * Properties:
 * - path (string, required): The file path to read (must start with CLAW_FS_ROOT_PATH)
 *
 * @return OPERATE_RET OPRT_OK on success
 */
static OPERATE_RET __tool_read_file(const MCP_PROPERTY_LIST_T *properties,
                                    MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    const char *path = __get_str_property(properties, "path");
    if (!__validate_path(path)) {
        ai_mcp_return_value_set_str(ret_val, "Error: invalid path, must start with " CLAW_FS_ROOT_PATH);
        return OPRT_INVALID_PARM;
    }

    TUYA_FILE f = claw_fopen(path, "r");
    if (!f) {
        ai_mcp_return_value_set_str(ret_val, "Error: file not found");
        return OPRT_NOT_FOUND;
    }

    size_t max_read = MAX_FILE_SIZE;
    char *buf = (char *)claw_malloc(max_read + 1);
    if (!buf) {
        claw_fclose(f);
        return OPRT_MALLOC_FAILED;
    }

    int n = claw_fread(buf, (int)max_read, f);
    claw_fclose(f);

    if (n < 0) {
        claw_free(buf);
        ai_mcp_return_value_set_str(ret_val, "Error: read failed");
        return OPRT_COM_ERROR;
    }
    buf[n] = '\0';

    ai_mcp_return_value_set_str(ret_val, buf);
    claw_free(buf);

    PR_DEBUG("read_file path=%s bytes=%u", path, (unsigned)n);
    return OPRT_OK;
}

/**
 * @brief MCP tool callback: write_file
 *
 * Writes content to a file in the filesystem.
 *
 * Properties:
 * - path (string, required): The file path to write (must start with CLAW_FS_ROOT_PATH)
 * - content (string, required): The content to write
 *
 * @return OPERATE_RET OPRT_OK on success
 */
static OPERATE_RET __tool_write_file(const MCP_PROPERTY_LIST_T *properties,
                                     MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    const char *path    = __get_str_property(properties, "path");
    const char *content = __get_str_property(properties, "content");

    if (!__validate_path(path)) {
        ai_mcp_return_value_set_str(ret_val, "Error: invalid path, must start with " CLAW_FS_ROOT_PATH);
        return OPRT_INVALID_PARM;
    }

    if (!content) {
        ai_mcp_return_value_set_str(ret_val, "Error: content is required");
        return OPRT_INVALID_PARM;
    }

    TUYA_FILE f = claw_fopen(path, "w");
    if (!f) {
        ai_mcp_return_value_set_str(ret_val, "Error: open file failed");
        return OPRT_COM_ERROR;
    }

    int wn = claw_fwrite((void *)content, (int)strlen(content), f);
    claw_fclose(f);

    if (wn < 0) {
        ai_mcp_return_value_set_str(ret_val, "Error: write failed");
        return OPRT_COM_ERROR;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "OK: wrote %u bytes", (unsigned)strlen(content));
    ai_mcp_return_value_set_str(ret_val, msg);

    PR_DEBUG("write_file path=%s bytes=%u", path, (unsigned)strlen(content));
    return OPRT_OK;
}

/**
 * @brief MCP tool callback: edit_file
 *
 * Replaces the first occurrence of old_string with new_string in a file.
 *
 * Properties:
 * - path (string, required): The file path to edit (must start with CLAW_FS_ROOT_PATH)
 * - old_string (string, required): The string to find and replace
 * - new_string (string, required): The replacement string
 *
 * @return OPERATE_RET OPRT_OK on success
 */
static OPERATE_RET __tool_edit_file(const MCP_PROPERTY_LIST_T *properties,
                                    MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    const char *path  = __get_str_property(properties, "path");
    const char *old_s = __get_str_property(properties, "old_string");
    const char *new_s = __get_str_property(properties, "new_string");

    if (!__validate_path(path)) {
        ai_mcp_return_value_set_str(ret_val, "Error: invalid path, must start with " CLAW_FS_ROOT_PATH);
        return OPRT_INVALID_PARM;
    }

    if (!old_s || !new_s) {
        ai_mcp_return_value_set_str(ret_val, "Error: old_string and new_string are required");
        return OPRT_INVALID_PARM;
    }

    /* Read the original file */
    TUYA_FILE f = claw_fopen(path, "r");
    if (!f) {
        ai_mcp_return_value_set_str(ret_val, "Error: file not found");
        return OPRT_NOT_FOUND;
    }

    char *buf = (char *)claw_malloc(MAX_FILE_SIZE + 1);
    if (!buf) {
        claw_fclose(f);
        return OPRT_MALLOC_FAILED;
    }

    int n = claw_fread(buf, MAX_FILE_SIZE, f);
    claw_fclose(f);

    if (n < 0) {
        claw_free(buf);
        ai_mcp_return_value_set_str(ret_val, "Error: read failed");
        return OPRT_COM_ERROR;
    }
    buf[n] = '\0';

    /* Find old_string */
    char *pos = strstr(buf, old_s);
    if (!pos) {
        claw_free(buf);
        ai_mcp_return_value_set_str(ret_val, "Error: old_string not found");
        return OPRT_NOT_FOUND;
    }

    /* Build new content */
    size_t prefix_len = (size_t)(pos - buf);
    size_t suffix_off = prefix_len + strlen(old_s);
    size_t need       = prefix_len + strlen(new_s) + strlen(buf + suffix_off) + 1;

    char *new_buf = (char *)claw_malloc(need);
    if (!new_buf) {
        claw_free(buf);
        return OPRT_MALLOC_FAILED;
    }

    memcpy(new_buf, buf, prefix_len);
    memcpy(new_buf + prefix_len, new_s, strlen(new_s));
    strcpy(new_buf + prefix_len + strlen(new_s), buf + suffix_off);

    /* Write back */
    f = claw_fopen(path, "w");
    if (!f) {
        claw_free(new_buf);
        claw_free(buf);
        ai_mcp_return_value_set_str(ret_val, "Error: open file for writing failed");
        return OPRT_COM_ERROR;
    }

    (void)claw_fwrite(new_buf, (int)strlen(new_buf), f);
    claw_fclose(f);

    ai_mcp_return_value_set_str(ret_val, "OK: edit done");

    claw_free(new_buf);
    claw_free(buf);

    PR_DEBUG("edit_file path=%s", path);
    return OPRT_OK;
}

/**
 * @brief MCP tool callback: list_dir
 *
 * Lists files in a directory under the filesystem root.
 *
 * Properties:
 * - prefix (string, optional): Directory path to list (defaults to CLAW_FS_ROOT_PATH)
 *
 * @return OPERATE_RET OPRT_OK on success
 */
static OPERATE_RET __tool_list_dir(const MCP_PROPERTY_LIST_T *properties,
                                   MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    const char *prefix = __get_str_property(properties, "prefix");
    if (!prefix || !__validate_path(prefix)) {
        prefix = CLAW_FS_ROOT_PATH "/";
    }

    TUYA_DIR dir = NULL;
    if (claw_dir_open(prefix, &dir) != OPRT_OK || !dir) {
        ai_mcp_return_value_set_str(ret_val, "Error: open dir failed");
        return OPRT_COM_ERROR;
    }

    /* Build directory listing string */
    size_t buf_size = 4096;
    char *buf = (char *)claw_malloc(buf_size);
    if (!buf) {
        claw_dir_close(dir);
        return OPRT_MALLOC_FAILED;
    }

    size_t off = 0;
    off += snprintf(buf + off, buf_size - off, "Dir: %s\n", prefix);

    while (off < buf_size - 2) {
        TUYA_FILEINFO info = NULL;
        if (claw_dir_read(dir, &info) != OPRT_OK || !info) {
            break;
        }

        const char *name = NULL;
        if (claw_dir_name(info, &name) != OPRT_OK || !name) {
            continue;
        }

        off += snprintf(buf + off, buf_size - off, "- %s\n", name);
    }

    claw_dir_close(dir);

    ai_mcp_return_value_set_str(ret_val, buf);
    claw_free(buf);

    PR_DEBUG("list_dir prefix=%s", prefix);
    return OPRT_OK;
}

/**
 * @brief Create a file with default content if it does not exist
 *
 * @param path File path to create
 * @param default_content Default content to write
 * @return OPERATE_RET OPRT_OK on success
 */
static OPERATE_RET __create_default_file(const char *path, const char *default_content)
{
    /* Check if file already exists by trying to open for read */
    TUYA_FILE f = claw_fopen(path, "r");
    if (f) {
        claw_fclose(f);
        PR_DEBUG("File already exists: %s", path);
        return OPRT_OK;
    }

    /* Create file with default content */
    f = claw_fopen(path, "w");
    if (!f) {
        PR_ERR("Failed to create file: %s", path);
        return OPRT_COM_ERROR;
    }

    if (default_content && strlen(default_content) > 0) {
        claw_fwrite((void *)default_content, (int)strlen(default_content), f);
    }

    claw_fclose(f);
    PR_DEBUG("Created default file: %s", path);
    return OPRT_OK;
}

/**
 * @brief Initialize filesystem
 *
 * Mounts SD card if CLAW_USE_SDCARD is enabled.
 * Creates default config directory and files if not present.
 *
 * @return OPERATE_RET OPRT_OK on success, error code on failure
 */
OPERATE_RET tool_files_fs_init(void)
{
    OPERATE_RET rt = OPRT_OK;

#if (CLAW_USE_SDCARD == 1)
    /* Mount SD card filesystem */
    rt = claw_fs_mount(CLAW_FS_MOUNT_PATH, DEV_SDCARD);
    if (rt != OPRT_OK) {
        PR_ERR("Mount SD card failed: %d, retrying...", rt);
        /* Retry mount */
        for (int i = 0; i < 3; i++) {
            tal_system_sleep(1000);
            rt = claw_fs_mount(CLAW_FS_MOUNT_PATH, DEV_SDCARD);
            if (rt == OPRT_OK) {
                break;
            }
            PR_ERR("Mount SD card retry %d failed: %d", i + 1, rt);
        }
        if (rt != OPRT_OK) {
            PR_ERR("Mount SD card failed after retries");
            return rt;
        }
    }
    PR_DEBUG("SD card mounted at %s", CLAW_FS_MOUNT_PATH);
#endif

    /* Create config directory */
    // claw_fs_mkdir(CLAW_CONFIG_DIR);

    // /* Create default config files */
    // TUYA_CALL_ERR_LOG(
    //     __create_default_file(USER_FILE,
    //                           "# User Config\n"));

    // TUYA_CALL_ERR_LOG(
    //     __create_default_file(SOUL_FILE,
    //                           "# Soul Config\n"));

    PR_DEBUG("Filesystem initialized, root: %s", CLAW_FS_ROOT_PATH);
    return OPRT_OK;
}

/**
 * @brief Register all file operation MCP tools
 *
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET tool_files_register(void)
{
    OPERATE_RET rt = OPRT_OK;

    /* read_file tool */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "read_file",
        "Read a file from the device filesystem.\n"
        "Parameters:\n"
        "- path (string): The file path to read, must start with " CLAW_FS_ROOT_PATH ".\n"
        "Response:\n"
        "- Returns the file content as a string.",
        __tool_read_file,
        NULL,
        MCP_PROP_STR("path", "The file path to read, must start with " CLAW_FS_ROOT_PATH)
    ));

    /* write_file tool */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "write_file",
        "Write content to a file on the device filesystem.\n"
        "Parameters:\n"
        "- path (string): The file path to write, must start with " CLAW_FS_ROOT_PATH ".\n"
        "- content (string): The content to write to the file.\n"
        "Response:\n"
        "- Returns the number of bytes written.",
        __tool_write_file,
        NULL,
        MCP_PROP_STR("path", "The file path to write, must start with " CLAW_FS_ROOT_PATH),
        MCP_PROP_STR("content", "The content to write to the file")
    ));

    /* edit_file tool */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "edit_file",
        "Replace the first occurrence of a string in a file.\n"
        "Parameters:\n"
        "- path (string): The file path to edit, must start with " CLAW_FS_ROOT_PATH ".\n"
        "- old_string (string): The string to find.\n"
        "- new_string (string): The replacement string.\n"
        "Response:\n"
        "- Returns 'OK: edit done' on success.",
        __tool_edit_file,
        NULL,
        MCP_PROP_STR("path", "The file path to edit, must start with " CLAW_FS_ROOT_PATH),
        MCP_PROP_STR("old_string", "The string to find and replace"),
        MCP_PROP_STR("new_string", "The replacement string")
    ));

    /* list_dir tool */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "list_dir",
        "List files in a directory on the device filesystem.\n"
        "Parameters:\n"
        "- prefix (string, optional): Directory path to list, defaults to " CLAW_FS_ROOT_PATH ".\n"
        "Response:\n"
        "- Returns a list of filenames in the directory.",
        __tool_list_dir,
        NULL,
        MCP_PROP_STR_DEF("prefix", "Directory path to list, defaults to " CLAW_FS_ROOT_PATH, CLAW_FS_ROOT_PATH "/")
    ));

    PR_DEBUG("File operation MCP tools registered successfully");
    return OPRT_OK;
}
