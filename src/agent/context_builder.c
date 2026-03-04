/**
 * @file context_builder.c
 * @brief context_builder module is used to 
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "context_builder.h"

#include "tool_files.h"
#include "memory_manager.h"

#include <stdio.h>

#include "tal_api.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define SOUL_FILE CLAW_CONFIG_DIR "/SOUL.md"
#define USER_FILE CLAW_CONFIG_DIR "/USER.md"

/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
********************function declaration********************
***********************************************************/


/***********************************************************
***********************variable define**********************
***********************************************************/


/***********************************************************
***********************function define**********************
***********************************************************/

static size_t append_file(char *buf, size_t size, size_t offset, const char *path, const char *header)
{
    PR_DEBUG("append_file: %s, %s, %s", path, header, buf + offset);

    TUYA_FILE f = claw_fopen(path, "r");
    if (!f || !buf || size == 0 || offset >= size - 1) {
        if (f) {
            claw_fclose(f);
        }
        return offset;
    }

    if (header) {
        offset += snprintf(buf + offset, size - offset, "\n## %s\n\n", header);
        if (offset >= size - 1) {
            claw_fclose(f);
            return size - 1;
        }
    }

    int n = claw_fread(buf + offset, (int)(size - offset - 1), f);
    if (n > 0) {
        offset += (size_t)n;
    }
    buf[offset] = '\0';

    PR_DEBUG("append_file: %s\n", buf + offset);
    claw_fclose(f);
    return offset;
}

size_t context_build_system_prompt(char *buf, size_t size)
{
    if (!buf || size == 0) {
        return 0;
    }

    size_t off = 0;
    off += snprintf(buf + off, size - off,
                    "# DuckyClaw\n\n"
                    "You are DuckyClaw, a personal AI assistant running on a TuyaOpen device.\n"
                    "You communicate through Telegram, Discord, and Feishu.\n\n");

    off += snprintf(buf + off, size - off,
                    "Be helpful, accurate, and concise.\n\n");

    off += snprintf(buf + off, size - off,
                    "## Available Tools\n"
                    "You have access to the following tools:\n"
                    "- web_search: Search the web for current information. "
                    "Use this when you need up-to-date facts, news, weather, or anything beyond your training data.\n");

    off += snprintf(buf + off, size - off,
                    "- get_current_time: Get the current date and time. "
                    "You do NOT have an internal clock - always use this tool when you need to know the time or date.\n");

#if CLAW_FS_ROOT_PATH_EMPTY
    /* CLAW_FS_ROOT_PATH is empty: use local path wording */
    off += snprintf(buf + off, size - off,
                    "- read_file: Read a file (path must start with \"/\").\n");
    off += snprintf(buf + off, size - off,
                    "- write_file: Write/overwrite a file on \" / \".\n");
    off += snprintf(buf + off, size - off,
                    "- edit_file: Find-and-replace edit a file on \" / \".\n");
    off += snprintf(buf + off, size - off,
                    "- list_dir: List files, optionally filter by prefix on \" / \".\n");
#else
    /* CLAW_FS_ROOT_PATH has a prefix: path must start with root */
    off += snprintf(buf + off, size - off,
                    "- read_file: Read a file (path must start with " CLAW_FS_ROOT_PATH "/).\n");
    off += snprintf(buf + off, size - off,
                    "- write_file: Write/overwrite a file on " CLAW_FS_ROOT_PATH ".\n");
    off += snprintf(buf + off, size - off,
                    "- edit_file: Find-and-replace edit a file on " CLAW_FS_ROOT_PATH ".\n");
    off += snprintf(buf + off, size - off,
                    "- list_dir: List files on " CLAW_FS_ROOT_PATH ", optionally filter by prefix.\n");
#endif

    off += snprintf(buf + off, size - off,
                    "- cron_add: Schedule a recurring or one-shot task. The message will trigger an agent turn when the job "
                    "fires.\n");

    off += snprintf(buf + off, size - off,
                    "- cron_list: List all scheduled cron jobs.\n");

    off += snprintf(buf + off, size - off,
                    "- cron_remove: Remove a scheduled cron job by ID.\n");
    
    off += snprintf(buf + off, size - off,
                    "Use tools when needed. Provide your final answer as text after using tools.\n\n");

    off += snprintf(buf + off, size - off,
                    "## Memory\n"
                    "You have persistent memory stored on local flash:\n"
                    "- Long-term memory: /memory/MEMORY.md\n"
                    "- Daily notes: /memory/daily/<YYYY-MM-DD>.md\n\n");

    off += snprintf(buf + off, size - off,
                    "IMPORTANT: Actively use memory to remember things across conversations.\n\n");

    off += snprintf(buf + off, size - off,
                    "## Skills\n"
                    "Skills are specialized instruction files stored in /skills/.\n"
                    "When a task matches a skill, read the full skill file for detailed instructions.\n"
                    "You can create new skills using write_file to /skills/<name>.md.\n");

    // Personality
    off = append_file(buf, size, off, SOUL_FILE, "Personality");
    off = append_file(buf, size, off, USER_FILE, "User Info");

    // Long-term Memory
    char mem_buf[4096];
    memset(mem_buf, 0, sizeof(mem_buf));
    if (memory_read_long_term(mem_buf, sizeof(mem_buf)) == OPRT_OK && mem_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Long-term Memory\n\n%s\n", mem_buf);
    }

    /* Recent daily notes (last 3 days) */
    char recent_buf[4096];
    memset(recent_buf, 0, sizeof(recent_buf));
    if (memory_read_recent(recent_buf, sizeof(recent_buf), 3) == OPRT_OK && recent_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Recent Notes\n\n%s\n", recent_buf);
    }

    // TODO: Skills

    return off;
}