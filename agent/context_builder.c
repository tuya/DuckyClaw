/**
 * @file context_builder.c
 * @brief context_builder module is used to 
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "context_builder.h"

#include "tool_files.h"
#include "memory_manager.h"
#include "skill_loader.h"
#include <stdio.h>

#include "tal_api.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define CONTEXT_TMP_BUF_SIZE      4096
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
                    "You communicate through Telegram, Discord, and Feishu.\n"
                    "Be helpful, accurate, and concise.\n\n");

    /* Critical rules to prevent hallucination */
    off += snprintf(buf + off, size - off,
                    "## CRITICAL RULES\n"
                    "1. You MUST call a tool to perform any action on the device. "
                    "NEVER pretend you called a tool or fabricate a tool result.\n"
                    "2. If the user asks you to do something that requires a tool, "
                    "you MUST actually invoke the tool. Do NOT say \"done\" or describe a result "
                    "without a real tool call.\n"
                    "3. If no tool exists for the requested action, honestly tell the user: "
                    "\"I don't have a tool for that\" instead of making up a response.\n"
                    "4. NEVER invent data you haven't retrieved via a tool "
                    "(e.g. task lists, file contents, time, search results).\n\n");


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

    // Memory and skills may be long, so use a temporary buffer to read and append
    char *tmp_buf = claw_malloc(CONTEXT_TMP_BUF_SIZE);
    if (NULL == tmp_buf) {
        PR_ERR("tmp buf malloc failed 4kb");
        return off;
    }

    // Long-term Memory
    memset(tmp_buf, 0, CONTEXT_TMP_BUF_SIZE);
    if (memory_read_long_term(tmp_buf, CONTEXT_TMP_BUF_SIZE) == OPRT_OK && tmp_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Long-term Memory\n\n%s\n", tmp_buf);
    }

    /* Recent daily notes (last 3 days) */
    memset(tmp_buf, 0, CONTEXT_TMP_BUF_SIZE);
    if (memory_read_recent(tmp_buf, CONTEXT_TMP_BUF_SIZE, 3) == OPRT_OK && tmp_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Recent Notes\n\n%s\n", tmp_buf);
    }

    /* Skills summary */
    memset(tmp_buf, 0, CONTEXT_TMP_BUF_SIZE);
    size_t skills_len = skill_loader_build_summary(tmp_buf, CONTEXT_TMP_BUF_SIZE);
    if (skills_len > 0) {
        off += snprintf(buf + off, size - off,
                        "\n## Available Skills\n\n"
                        "Available skills (use read_file to load full instructions):\n%s\n",
                        tmp_buf);
    }

    // free temporary buffer
    claw_free(tmp_buf);

    return off;
}