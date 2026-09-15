#include "passport_ui_model.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *task_name_cn(passport_task_state_t state) {
    switch (state) {
    case PASSPORT_TASK_RUNNING: return "运行中";
    case PASSPORT_TASK_WAITING_APPROVAL: return "等待审批";
    case PASSPORT_TASK_DONE: return "已完成";
    case PASSPORT_TASK_ERROR: return "出错";
    case PASSPORT_TASK_IDLE:
    default: return "空闲";
    }
}

static const char *ide_display(const char *ide) {
    if (strcmp(ide, "codex") == 0) return "CODEX";
    if (strcmp(ide, "trae") == 0) return "TRAE";
    return ide[0] ? ide : "IDE";
}

static const char *role_display(const char *role) {
    if (strcmp(role, "agent") == 0) return "主体";
    if (strcmp(role, "project") == 0) return "项目";
    if (strcmp(role, "skill") == 0) return "技能";
    if (strcmp(role, "review") == 0) return "审查";
    return role[0] ? role : "角色";
}

static const char *mode_label(passport_page_t page) {
    switch (page) {
    case PASSPORT_PAGE_COMPOSE_STACK: return "组合";
    case PASSPORT_PAGE_WEAR_HOME:
    case PASSPORT_PAGE_WEAR_TASK:
    default: return "随身";
    }
}

static const char *link_label(bool transport_ready,
                              const passport_service_snapshot_t *snapshot) {
    if (!transport_ready) return "断线";
    /* Use protocol-frame liveness, not the DTR bit. Matches the disconnected
     * banner threshold near the end of passport_ui_model_build(). */
    if (snapshot->link_idle_ms < 0) return "未连接";
    if (snapshot->link_idle_ms > 30000) return "断线";
    return "已连接";
}

static void set_row(passport_ui_model_t *model, size_t index, const char *text) {
    if (index >= PASSPORT_UI_MODEL_BODY_ROWS) return;
    snprintf(model->body[index], sizeof(model->body[index]), "%s", text);
    if (index + 1U > model->body_rows) model->body_rows = index + 1U;
}

static void set_row_fmt(passport_ui_model_t *model, size_t index,
                        const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void set_row_fmt(passport_ui_model_t *model, size_t index,
                        const char *fmt, ...) {
    if (index >= PASSPORT_UI_MODEL_BODY_ROWS) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(model->body[index], sizeof(model->body[index]), fmt, args);
    va_end(args);
    if (index + 1U > model->body_rows) model->body_rows = index + 1U;
}

static void build_wear_home(const passport_service_snapshot_t *snapshot,
                            passport_ui_model_t *model) {
    snprintf(model->title, sizeof(model->title), "%s", "首页");
    set_row_fmt(model, 0, "%s %u%%", task_name_cn(snapshot->task_state), snapshot->progress);
    set_row_fmt(model, 1, "%s", snapshot->summary[0] ? snapshot->summary : "暂无任务");
    if (snapshot->event_count > 0 && snapshot->events[0].summary[0]) {
        set_row_fmt(model, 2, "事件 %s", snapshot->events[0].summary);
    } else {
        set_row(model, 2, "事件 无");
    }
    set_row_fmt(model, 3, "卡 %s",
                snapshot->goal_card_id[0] ? snapshot->goal_card_id : "未贴卡");
    if (snapshot->goal_mode_state == PASSPORT_GOAL_ENABLED) {
        set_row_fmt(model, 4, "目标 %s / %s",
                    ide_display(snapshot->goal_ide),
                    snapshot->goal_session_id[0] ? snapshot->goal_session_id : "会话中");
    } else if (snapshot->goal_mode_state == PASSPORT_GOAL_REQUESTED) {
        set_row(model, 4, "目标 等待 IDE");
    } else {
        set_row(model, 4, "目标 未启用");
    }
    if (snapshot->stack_count > 0) {
        snprintf(model->hint, sizeof(model->hint), "%s",
                 "上 任务  下 组合  确 说话");
    } else if (snapshot->goal_mode_state == PASSPORT_GOAL_DISABLED &&
               snapshot->goal_card_id[0] == '\0') {
        snprintf(model->hint, sizeof(model->hint), "%s", "贴卡开始  上 任务  确 说话");
    } else {
        snprintf(model->hint, sizeof(model->hint), "%s", "上 任务  确 说话");
    }
}

static void build_wear_task(const passport_service_snapshot_t *snapshot,
                            passport_ui_model_t *model) {
    snprintf(model->title, sizeof(model->title), "任务 %s",
             snapshot->task_id[0] ? snapshot->task_id : "-");
    set_row_fmt(model, 0, "%s %u%%", task_name_cn(snapshot->task_state), snapshot->progress);
    set_row_fmt(model, 1, "%s", snapshot->summary[0] ? snapshot->summary : "暂无任务");
    size_t events_shown = snapshot->event_count;
    if (events_shown > PASSPORT_UI_MODEL_BODY_ROWS - 2U) {
        events_shown = PASSPORT_UI_MODEL_BODY_ROWS - 2U;
    }
    if (events_shown == 0) {
        set_row(model, 2, "尚无事件");
    } else {
        for (size_t i = 0; i < events_shown; i++) {
            const passport_task_event_t *event = &snapshot->events[i];
            const char *ts = event->ts[0] ? event->ts : "--:--";
            set_row_fmt(model, 2U + i, "%s %s", ts,
                        event->summary[0] ? event->summary : "-");
        }
    }
    snprintf(model->hint, sizeof(model->hint), "%s", "上 返回  确 已阅");
}

static void build_compose_stack(const passport_service_snapshot_t *snapshot,
                                passport_ui_model_t *model) {
    snprintf(model->title, sizeof(model->title), "组合 %zu 张", snapshot->stack_count);
    size_t rows = snapshot->stack_count;
    if (rows > PASSPORT_UI_MODEL_BODY_ROWS - 2U) rows = PASSPORT_UI_MODEL_BODY_ROWS - 2U;
    if (rows == 0) {
        set_row(model, 0, "堆叠为空");
    } else {
        for (size_t i = 0; i < rows; i++) {
            const passport_tile_t *tile = &snapshot->stack[i];
            const char *pointer = i == snapshot->stack_selected ? ">" : " ";
            const char *revision = tile->revision[0] ? tile->revision : "-";
            set_row_fmt(model, i, "%s %s %s",
                        pointer, role_display(tile->role), revision);
        }
    }
    switch (snapshot->compose_status) {
    case PASSPORT_COMPOSE_OK:
        set_row_fmt(model, PASSPORT_UI_MODEL_BODY_ROWS - 2U,
                    "状态 已组合 %u ms", snapshot->compose_duration_ms);
        break;
    case PASSPORT_COMPOSE_CONFLICT:
        set_row(model, PASSPORT_UI_MODEL_BODY_ROWS - 2U, "状态 冲突");
        break;
    case PASSPORT_COMPOSE_EMPTY:
    default:
        set_row(model, PASSPORT_UI_MODEL_BODY_ROWS - 2U, "状态 等待堆叠");
        break;
    }
    if (snapshot->context_id[0]) {
        set_row_fmt(model, PASSPORT_UI_MODEL_BODY_ROWS - 1U,
                    "上下文 %s", snapshot->context_id);
    } else {
        set_row(model, PASSPORT_UI_MODEL_BODY_ROWS - 1U, "上下文 -");
    }
    snprintf(model->hint, sizeof(model->hint), "%s",
             snapshot->stack_count > 0 ? "上 返回  下 选下  确 打开" : "上 返回");
}

static void build_battery(int battery_soc, passport_ui_model_t *model) {
    if (battery_soc >= 0 && battery_soc <= 100) {
        snprintf(model->battery, sizeof(model->battery), "电 %d%%", battery_soc);
    } else {
        snprintf(model->battery, sizeof(model->battery), "%s", "电 --");
    }
}

static void build_legacy_mirrors(const passport_service_snapshot_t *snapshot,
                                 passport_ui_model_t *model) {
    snprintf(model->task, sizeof(model->task), "%s %u%%",
             task_name_cn(snapshot->task_state), snapshot->progress);
    snprintf(model->summary, sizeof(model->summary), "%s",
             snapshot->summary[0] ? snapshot->summary : "暂无任务");
    snprintf(model->card, sizeof(model->card), "卡 %s",
             snapshot->goal_card_id[0] ? snapshot->goal_card_id : "未贴卡");
    if (snapshot->goal_mode_state == PASSPORT_GOAL_ENABLED) {
        snprintf(model->goal, sizeof(model->goal), "目标 %s / %s",
                 ide_display(snapshot->goal_ide),
                 snapshot->goal_session_id[0] ? snapshot->goal_session_id : "会话中");
    } else if (snapshot->goal_mode_state == PASSPORT_GOAL_REQUESTED) {
        snprintf(model->goal, sizeof(model->goal), "%s", "目标 等待 IDE");
    } else {
        snprintf(model->goal, sizeof(model->goal), "%s", "目标 未启用");
    }
}

void passport_ui_model_build(const passport_service_snapshot_t *snapshot,
                             bool transport_ready,
                             int battery_soc, passport_ui_model_t *model) {
    if (!snapshot || !model) return;
    memset(model, 0, sizeof(*model));

    snprintf(model->mode, sizeof(model->mode), "%s", mode_label(snapshot->page));
    snprintf(model->link, sizeof(model->link), "%s",
             link_label(transport_ready, snapshot));
    build_battery(battery_soc, model);

    switch (snapshot->page) {
    case PASSPORT_PAGE_WEAR_TASK:
        build_wear_task(snapshot, model);
        break;
    case PASSPORT_PAGE_COMPOSE_STACK:
        build_compose_stack(snapshot, model);
        break;
    case PASSPORT_PAGE_WEAR_HOME:
    default:
        build_wear_home(snapshot, model);
        break;
    }

    if (snapshot->approval_pending) {
        model->approval_overlay = true;
        snprintf(model->approval_summary, sizeof(model->approval_summary),
                 "%s", snapshot->approval_summary[0] ? snapshot->approval_summary
                                                    : "等待审批");
        snprintf(model->hint, sizeof(model->hint), "%s", "上 拒绝  确 通过");
    }

    /* No full-body disconnected banner. Liveness state is conveyed through
     * the header (`link_label` renders 未连接 / 已连接 / 断线 based on
     * link_idle_ms) and the body keeps rendering the last known snapshot
     * without covering it. The MVP acceptance workflow types manual mock
     * frames with pauses well beyond any reasonable idle threshold, so a
     * full-body warning is more disruptive than informative. */
    (void)snapshot;

    build_legacy_mirrors(snapshot, model);
}
