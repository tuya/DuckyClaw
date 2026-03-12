/**
 * @file tool_cron.c
 * @brief MCP cron (scheduled task) tools for DuckyClaw
 * @version 0.1
 * @date 2025-03-25
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 * Implements cron_add, cron_list, and cron_remove MCP tools
 * using the ai_mcp_server.h interface.
 */

#include "tool_cron.h"
#include "tool_files.h"
#include "cron_service.h"

#include "tal_api.h"
#include "tal_time_service.h"

#include <string.h>
#include <stdlib.h>

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * __to_local_tm - Convert a UTC epoch to local broken-down time.
 * @epoch:          UTC Unix timestamp (int64_t); pass 0 for current time.
 *                  Internally cast to TIME_T (uint32) for SDK call.
 * @tm_out:         Output broken-down time struct (POSIX_TM_S convention).
 * @tz_label_buf:   Buffer for a human-readable timezone label, e.g. "UTC+8:00".
 * @tz_label_size:  Size of tz_label_buf.
 *
 * Mirrors tal_log.c: delegates to tal_time_get_local_time_custom(), which
 * applies the cloud-synced timezone offset and summer-time adjustments.
 * The timezone label is derived from tal_time_get_time_zone_seconds().
 */
static void __to_local_tm(int64_t epoch, POSIX_TM_S *tm_out,
                          char *tz_label_buf, size_t tz_label_size)
{
    memset(tm_out, 0, sizeof(*tm_out));
    tal_time_get_local_time_custom((TIME_T)epoch, tm_out);

    int tz_sec = 0;
    tal_time_get_time_zone_seconds(&tz_sec);
    int tz_h = tz_sec / 3600;
    int tz_m = (tz_sec < 0 ? -tz_sec : tz_sec) % 3600 / 60;
    snprintf(tz_label_buf, tz_label_size, "UTC%+d:%02d", tz_h, tz_m);
}

/**
 * __tool_get_current_time - MCP tool callback: get_current_time
 *
 * Returns the device's current UTC epoch and the local time formatted
 * according to the cloud-synced timezone (via tal_time_get_local_time_custom).
 * AI must call this BEFORE cron_add with schedule_type='at' to compute at_epoch.
 */
static OPERATE_RET __tool_get_current_time(const MCP_PROPERTY_LIST_T *properties,
                                           MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    TIME_T now = tal_time_get_posix();

    if (now <= 0) {
        ai_mcp_return_value_set_str(ret_val, "Error: device time not synced yet");
        return OPRT_COM_ERROR;
    }

    POSIX_TM_S tm_local;
    char tz_label[16];
    /* Pass 0 so tal_time_get_local_time_custom uses current time internally */
    __to_local_tm(0, &tm_local, tz_label, sizeof(tz_label));

    /* Include explicit formula so AI can compute at_epoch for absolute times
     * without timezone arithmetic. The formula works by computing the delta in
     * seconds between the current local time and the desired local time, then
     * adding it to the current UTC epoch – no timezone knowledge needed. */
    char result[512];
    snprintf(result, sizeof(result),
             "Current time: %04d-%02d-%02d %02d:%02d:%02d %s "
             "(UTC epoch=%lld, local H=%d M=%d S=%d). "
             "To schedule N seconds from now: at_epoch=%lld+N. "
             "To schedule at a specific local clock time HH:MM today: "
             "at_epoch = %lld + (HH*3600 + MM*60 - %d*3600 - %d*60 - %d). "
             "If the result is negative (time already passed today), add 86400 for tomorrow.",
             tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday,
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec,
             tz_label, (long long)now,
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec,
             (long long)now,
             (long long)now,
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);

    ai_mcp_return_value_set_str(ret_val, result);
    PR_DEBUG("get_current_time: epoch=%lld local=%04d-%02d-%02d %02d:%02d:%02d %s",
             (long long)now,
             tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday,
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec,
             tz_label);
    return OPRT_OK;
}

/**
 * @brief Helper to extract a string property from MCP property list
 */
static const char *__get_str_prop(const MCP_PROPERTY_LIST_T *properties, const char *name)
{
    const MCP_PROPERTY_T *prop = ai_mcp_property_list_find(properties, name);
    if (prop && prop->type == MCP_PROPERTY_TYPE_STRING) {
        return prop->default_val.str_val;
    }
    return NULL;
}

/**
 * @brief Helper to extract an integer property from MCP property list
 */
static bool __get_int_prop(const MCP_PROPERTY_LIST_T *properties, const char *name, int *out)
{
    const MCP_PROPERTY_T *prop = ai_mcp_property_list_find(properties, name);
    if (prop && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
        *out = prop->default_val.int_val;
        return true;
    }
    return false;
}

/**
 * @brief Helper to extract a boolean property from MCP property list
 */
static bool __get_bool_prop(const MCP_PROPERTY_LIST_T *properties, const char *name, bool *out)
{
    const MCP_PROPERTY_T *prop = ai_mcp_property_list_find(properties, name);
    if (prop && prop->type == MCP_PROPERTY_TYPE_BOOLEAN) {
        *out = prop->default_val.bool_val;
        return true;
    }
    return false;
}

/**
 * @brief MCP tool callback: cron_add
 *
 * Adds a new cron job (recurring "every" or one-shot "at").
 *
 * Properties:
 * - name (string, required): Descriptive name for the job
 * - schedule_type (string, required): "every" or "at"
 * - message (string, required): Message to send when job fires
 * - interval_s (int, optional): Interval in seconds for "every" schedule
 * - at_epoch (int, optional): Unix timestamp for "at" schedule
 * - delete_after_run (bool, optional): Whether to delete after firing (default true for "at")
 */
static OPERATE_RET __tool_cron_add(const MCP_PROPERTY_LIST_T *properties,
                                   MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    const char *name          = __get_str_prop(properties, "name");
    const char *schedule_type = __get_str_prop(properties, "schedule_type");
    const char *message       = __get_str_prop(properties, "message");

    if (!name || !schedule_type || !message) {
        ai_mcp_return_value_set_str(ret_val, "Error: missing required fields (name, schedule_type, message)");
        return OPRT_INVALID_PARM;
    }

    if (message[0] == '\0') {
        ai_mcp_return_value_set_str(ret_val, "Error: message must not be empty");
        return OPRT_INVALID_PARM;
    }

    cron_job_t job;
    memset(&job, 0, sizeof(job));
    strncpy(job.name, name, sizeof(job.name) - 1);
    strncpy(job.message, message, sizeof(job.message) - 1);

    if (strcmp(schedule_type, "every") == 0) {
        job.kind = CRON_KIND_EVERY;

        int interval_s = 0;
        if (!__get_int_prop(properties, "interval_s", &interval_s) || interval_s <= 0) {
            ai_mcp_return_value_set_str(ret_val, "Error: 'every' schedule requires positive 'interval_s'");
            return OPRT_INVALID_PARM;
        }

        job.interval_s       = (uint32_t)interval_s;
        job.delete_after_run = false;
    } else if (strcmp(schedule_type, "at") == 0) {
        job.kind = CRON_KIND_AT;

        int at_epoch = 0;
        if (!__get_int_prop(properties, "at_epoch", &at_epoch) || at_epoch <= 0) {
            ai_mcp_return_value_set_str(ret_val, "Error: 'at' schedule requires 'at_epoch' (unix timestamp)");
            return OPRT_INVALID_PARM;
        }

        job.at_epoch = (int64_t)at_epoch;
        int64_t now  = (int64_t)tal_time_get_posix();
        if (job.at_epoch <= now) {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg),
                     "Error: at_epoch %lld is in the past (now=%lld)",
                     (long long)job.at_epoch, (long long)now);
            ai_mcp_return_value_set_str(ret_val, err_msg);
            return OPRT_INVALID_PARM;
        }

        bool delete_after = true;
        if (__get_bool_prop(properties, "delete_after_run", &delete_after)) {
            job.delete_after_run = delete_after;
        } else {
            job.delete_after_run = true;
        }
    } else {
        ai_mcp_return_value_set_str(ret_val, "Error: schedule_type must be 'every' or 'at'");
        return OPRT_INVALID_PARM;
    }

    OPERATE_RET rt = cron_add_job(&job);
    if (rt != OPRT_OK) {
        char err_msg[64];
        snprintf(err_msg, sizeof(err_msg), "Error: failed to add job (rt=%d)", rt);
        ai_mcp_return_value_set_str(ret_val, err_msg);
        return rt;
    }

    /* Tool-chain verification: confirm the job is present in the persisted list.
     * cron_add_job() saves to CRON_FILE (/sdcard/cron.json or equivalent).
     * Reading back via cron_list_jobs() ensures the write was successful. */
    const cron_job_t *verify_jobs  = NULL;
    int               verify_count = 0;
    cron_list_jobs(&verify_jobs, &verify_count);

    bool found = false;
    for (int i = 0; i < verify_count && !found; i++) {
        if (strcmp(verify_jobs[i].id, job.id) == 0) {
            found = true;
        }
    }

    if (!found) {
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg),
                 "Error: job '%s' (id=%s) was scheduled but not found in stored list "
                 "– possible file write failure. Please retry.",
                 job.name, job.id);
        PR_ERR("cron_add verify failed: %s", err_msg);
        ai_mcp_return_value_set_str(ret_val, err_msg);
        return OPRT_FILE_WRITE_FAILED;
    }

    /* Format local time of the next run for user-readable confirmation */
    POSIX_TM_S tm_next;
    char tz_label[16];
    __to_local_tm(job.next_run, &tm_next, tz_label, sizeof(tz_label));

    char result[320];
    if (job.kind == CRON_KIND_EVERY) {
        snprintf(result, sizeof(result),
                 "OK: Added recurring job '%s' (id=%s), runs every %lu seconds. "
                 "Next run at epoch %lld (%04d-%02d-%02d %02d:%02d:%02d %s). "
                 "Verified in stored list.",
                 job.name, job.id, (unsigned long)job.interval_s, (long long)job.next_run,
                 tm_next.tm_year + 1900, tm_next.tm_mon + 1, tm_next.tm_mday,
                 tm_next.tm_hour, tm_next.tm_min, tm_next.tm_sec, tz_label);
    } else {
        POSIX_TM_S tm_at;
        char tz_label_at[16];
        __to_local_tm(job.at_epoch, &tm_at, tz_label_at, sizeof(tz_label_at));
        snprintf(result, sizeof(result),
                 "OK: Added one-shot job '%s' (id=%s), fires at epoch %lld "
                 "(%04d-%02d-%02d %02d:%02d:%02d %s).%s Verified in stored list.",
                 job.name, job.id, (long long)job.at_epoch,
                 tm_at.tm_year + 1900, tm_at.tm_mon + 1, tm_at.tm_mday,
                 tm_at.tm_hour, tm_at.tm_min, tm_at.tm_sec, tz_label_at,
                 job.delete_after_run ? " Will be deleted after firing." : "");
    }

    ai_mcp_return_value_set_str(ret_val, result);
    PR_DEBUG("cron_add: %s", result);
    return OPRT_OK;
}

/**
 * @brief MCP tool callback: cron_list
 *
 * Lists all scheduled cron jobs.
 */
static OPERATE_RET __tool_cron_list(const MCP_PROPERTY_LIST_T *properties,
                                    MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    const cron_job_t *jobs  = NULL;
    int               count = 0;
    cron_list_jobs(&jobs, &count);

    if (count == 0) {
        ai_mcp_return_value_set_str(ret_val, "No cron jobs scheduled.");
        return OPRT_OK;
    }

    /* Build listing string */
    size_t buf_size = 4096;
    char *buf = (char *)claw_malloc(buf_size);
    if (!buf) {
        return OPRT_MALLOC_FAILED;
    }

    size_t off = 0;
    off += (size_t)snprintf(buf + off, buf_size - off, "Scheduled jobs (%d):\n", count);

    for (int i = 0; i < count && off < buf_size - 1; i++) {
        const cron_job_t *j = &jobs[i];

        if (j->kind == CRON_KIND_EVERY) {
            off += (size_t)snprintf(buf + off, buf_size - off,
                                    "  %d. [%s] \"%s\" - every %lus, %s, next=%lld, last=%lld\n",
                                    i + 1, j->id, j->name,
                                    (unsigned long)j->interval_s,
                                    j->enabled ? "enabled" : "disabled",
                                    (long long)j->next_run, (long long)j->last_run);
        } else {
            off += (size_t)snprintf(buf + off, buf_size - off,
                                    "  %d. [%s] \"%s\" - at %lld, %s, last=%lld%s\n",
                                    i + 1, j->id, j->name,
                                    (long long)j->at_epoch,
                                    j->enabled ? "enabled" : "disabled",
                                    (long long)j->last_run,
                                    j->delete_after_run ? " (auto-delete)" : "");
        }
    }

    ai_mcp_return_value_set_str(ret_val, buf);
    claw_free(buf);

    PR_DEBUG("cron_list: %d jobs", count);
    return OPRT_OK;
}

/**
 * @brief MCP tool callback: cron_remove
 *
 * Removes a cron job by its ID.
 *
 * Properties:
 * - job_id (string, required): The ID of the cron job to remove
 */
static OPERATE_RET __tool_cron_remove(const MCP_PROPERTY_LIST_T *properties,
                                      MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    const char *job_id = __get_str_prop(properties, "job_id");
    if (!job_id || job_id[0] == '\0') {
        ai_mcp_return_value_set_str(ret_val, "Error: missing 'job_id' field");
        return OPRT_INVALID_PARM;
    }

    OPERATE_RET rt = cron_remove_job(job_id);

    char result[128];
    if (rt == OPRT_OK) {
        snprintf(result, sizeof(result), "OK: Removed cron job %s", job_id);
    } else if (rt == OPRT_NOT_FOUND) {
        snprintf(result, sizeof(result), "Error: job '%s' not found", job_id);
    } else {
        snprintf(result, sizeof(result), "Error: failed to remove job (rt=%d)", rt);
    }

    ai_mcp_return_value_set_str(ret_val, result);
    PR_DEBUG("cron_remove: %s -> %d", job_id, rt);
    return rt;
}

/**
 * @brief Register all cron MCP tools
 *
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET tool_cron_register(void)
{
    OPERATE_RET rt = OPRT_OK;

    /* get_current_time tool - AI must call this first to know current epoch */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "get_current_time",
        "Get the device's current real time.\n"
        "MUST be called before cron_add with schedule_type='at'.\n"
        "The response includes both current epoch and the formula to compute at_epoch:\n"
        "- Relative time (e.g. '20 minutes later'): at_epoch = current_epoch + N\n"
        "- Absolute clock time (e.g. '17:40'): use the formula in the response:\n"
        "    at_epoch = current_epoch + (HH*3600 + MM*60 - cur_H*3600 - cur_M*60 - cur_S)\n"
        "  where HH:MM is the desired local time and cur_H/M/S are from this response.\n"
        "  If the result is negative (time already passed today), add 86400.\n"
        "  NEVER subtract a fixed timezone offset; always use the delta formula above.",
        __tool_get_current_time,
        NULL
    ));

    /* cron_add tool */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "cron_add",
        "Add a scheduled cron job. IMPORTANT: Before calling this tool with schedule_type='at',\n"
        "you MUST first call 'get_current_time' to obtain the current epoch, then calculate\n"
        "at_epoch = current_epoch + delay_seconds.\n"
        "Parameters:\n"
        "- name (string): Descriptive name for the job.\n"
        "- schedule_type (string): 'every' for recurring or 'at' for one-shot.\n"
        "- message (string): Message content to send when the job fires.\n"
        "- interval_s (int): Interval in seconds (required for 'every' type).\n"
        "- at_epoch (int): Unix timestamp to fire at (required for 'at' type, use get_current_time first).\n"
        "- delete_after_run (bool): Whether to auto-delete after firing (default true for 'at').\n"
        "Response:\n"
        "- Returns job ID and schedule details on success, including 'Verified in stored list'.\n"
        "Tool-chain verification (REQUIRED after this call):\n"
        "- The tool internally verifies the job was saved to the persistence file.\n"
        "- If the response does NOT contain 'Verified in stored list', the file write failed.\n"
        "- On file write failure, call cron_list to check existing jobs, then retry cron_add.\n"
        "- After success, you MAY optionally call cron_list to display the updated job list to the user.",
        __tool_cron_add,
        NULL,
        MCP_PROP_STR("name", "Descriptive name for the cron job"),
        MCP_PROP_STR("schedule_type", "'every' for recurring or 'at' for one-shot"),
        MCP_PROP_STR("message", "Message content to send when the job fires"),
        /* NOTE: MCP_PROP_INT_DEF macro has a bug - parameter 'name' collides with
         * struct member designator '.name', causing preprocessor to produce invalid
         * syntax like '."interval_s"'. Use inline compound literal instead. */
        &(MCP_PROPERTY_DEF_T){ .name = "interval_s", .type = MCP_PROPERTY_TYPE_INTEGER,
            .description = "Interval in seconds for 'every' schedule type",
            .has_default = TRUE, .default_val.int_val = 0, .has_range = FALSE },
        &(MCP_PROPERTY_DEF_T){ .name = "at_epoch", .type = MCP_PROPERTY_TYPE_INTEGER,
            .description = "Unix timestamp for 'at' schedule type",
            .has_default = TRUE, .default_val.int_val = 0, .has_range = FALSE },
        MCP_PROP_BOOL_DEF("delete_after_run", "Auto-delete after firing (default true for 'at')", true)
    ));

    /* cron_list tool */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "cron_list",
        "List all scheduled cron jobs.\n"
        "Response:\n"
        "- Returns a list of all cron jobs with their IDs, names, schedules, and status.",
        __tool_cron_list,
        NULL
    ));

    /* cron_remove tool */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "cron_remove",
        "Remove a scheduled cron job by its ID.\n"
        "Parameters:\n"
        "- job_id (string): The ID of the cron job to remove (from cron_list output).\n"
        "Response:\n"
        "- Returns confirmation of removal or error if not found.",
        __tool_cron_remove,
        NULL,
        MCP_PROP_STR("job_id", "The ID of the cron job to remove")
    ));

    PR_DEBUG("Cron MCP tools registered successfully");
    return OPRT_OK;
}
