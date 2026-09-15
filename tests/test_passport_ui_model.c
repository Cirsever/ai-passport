#include <assert.h>
#include <string.h>

#include "passport_ui_model.h"

static void test_wear_home_shows_chinese_headline(void) {
    passport_service_snapshot_t snapshot = {0};
    passport_ui_model_t model;

    snapshot.task_state = PASSPORT_TASK_IDLE;
    snapshot.page = PASSPORT_PAGE_WEAR_HOME;
    passport_ui_model_build(&snapshot, true, 87, &model);

    assert(strcmp(model.mode, "随身") == 0);
    assert(strcmp(model.link, "已连接") == 0);
    assert(strcmp(model.battery, "电 87%") == 0);
    assert(strcmp(model.title, "首页") == 0);
    assert(strcmp(model.body[0], "空闲 0%") == 0);
    assert(strcmp(model.body[1], "暂无任务") == 0);
    assert(strcmp(model.body[2], "事件 无") == 0);
    assert(strcmp(model.body[3], "卡 未贴卡") == 0);
    assert(strcmp(model.body[4], "目标 未启用") == 0);
    assert(strstr(model.hint, "贴卡开始") != NULL);
    assert(model.approval_overlay == false);
}

static void test_wear_home_reflects_goal_and_event(void) {
    passport_service_snapshot_t snapshot = {0};
    passport_ui_model_t model;

    snapshot.task_state = PASSPORT_TASK_RUNNING;
    snapshot.progress = 42;
    strcpy(snapshot.summary, "重构 tracing");
    snapshot.page = PASSPORT_PAGE_WEAR_HOME;
    snapshot.goal_mode_state = PASSPORT_GOAL_ENABLED;
    strcpy(snapshot.goal_card_id, "card-1");
    strcpy(snapshot.goal_ide, "codex");
    strcpy(snapshot.goal_session_id, "session-1");
    strcpy(snapshot.events[0].summary, "分析器扫描完成");
    snapshot.event_count = 1;
    passport_ui_model_build(&snapshot, true, -1, &model);

    assert(strcmp(model.body[0], "运行中 42%") == 0);
    assert(strcmp(model.body[1], "重构 tracing") == 0);
    assert(strcmp(model.body[2], "事件 分析器扫描完成") == 0);
    assert(strcmp(model.body[3], "卡 card-1") == 0);
    assert(strcmp(model.body[4], "目标 CODEX / session-1") == 0);
    assert(strcmp(model.battery, "电 --") == 0);
    assert(strstr(model.hint, "说话") != NULL);
}

static void test_wear_task_lists_recent_events(void) {
    passport_service_snapshot_t snapshot = {0};
    passport_ui_model_t model;

    snapshot.task_state = PASSPORT_TASK_RUNNING;
    snapshot.progress = 42;
    strcpy(snapshot.task_id, "runtime-1");
    strcpy(snapshot.summary, "跨包重构");
    snapshot.page = PASSPORT_PAGE_WEAR_TASK;
    snapshot.event_count = 3;
    strcpy(snapshot.events[0].ts, "12:35");
    strcpy(snapshot.events[0].summary, "测试运行中");
    strcpy(snapshot.events[1].ts, "12:33");
    strcpy(snapshot.events[1].summary, "补丁草稿就绪");
    strcpy(snapshot.events[2].ts, "12:30");
    strcpy(snapshot.events[2].summary, "分析器开始扫描");
    passport_ui_model_build(&snapshot, true, 55, &model);

    assert(strcmp(model.title, "任务 runtime-1") == 0);
    assert(strcmp(model.body[0], "运行中 42%") == 0);
    assert(strcmp(model.body[2], "12:35 测试运行中") == 0);
    assert(strcmp(model.body[3], "12:33 补丁草稿就绪") == 0);
    assert(strcmp(model.body[4], "12:30 分析器开始扫描") == 0);
    assert(strstr(model.hint, "已阅") != NULL);
}

static void test_compose_stack_renders_selection_and_status(void) {
    passport_service_snapshot_t snapshot = {0};
    passport_ui_model_t model;

    snapshot.page = PASSPORT_PAGE_COMPOSE_STACK;
    snapshot.stack_count = 3;
    snapshot.stack_selected = 0;
    strcpy(snapshot.stack[0].role, "review");
    strcpy(snapshot.stack[0].revision, "0.3.2");
    strcpy(snapshot.stack[1].role, "project");
    strcpy(snapshot.stack[1].revision, "aide");
    strcpy(snapshot.stack[2].role, "agent");
    strcpy(snapshot.stack[2].revision, "0.4.1");
    strcpy(snapshot.context_id, "ctx-14");
    snapshot.compose_status = PASSPORT_COMPOSE_OK;
    snapshot.compose_duration_ms = 640;
    passport_ui_model_build(&snapshot, true, 42, &model);

    assert(strcmp(model.mode, "组合") == 0);
    assert(strcmp(model.title, "组合 3 张") == 0);
    assert(strcmp(model.body[0], "> 审查 0.3.2") == 0);
    assert(strcmp(model.body[1], "  项目 aide") == 0);
    assert(strcmp(model.body[2], "  主体 0.4.1") == 0);
    assert(strcmp(model.body[4], "状态 已组合 640 ms") == 0);
    assert(strcmp(model.body[5], "上下文 ctx-14") == 0);
    assert(strstr(model.hint, "选下") != NULL);
}

static void test_approval_overrides_hint(void) {
    passport_service_snapshot_t snapshot = {0};
    passport_ui_model_t model;

    snapshot.page = PASSPORT_PAGE_WEAR_HOME;
    snapshot.approval_pending = true;
    strcpy(snapshot.approval_summary, "写入 3 个文件");
    passport_ui_model_build(&snapshot, true, 60, &model);

    assert(model.approval_overlay);
    assert(strcmp(model.approval_summary, "写入 3 个文件") == 0);
    assert(strstr(model.hint, "通过") != NULL);
}

static void test_stale_link_updates_header_only(void) {
    /* Product decision: no full-body banner. After 30 s idle, the header
     * label changes to 断线 but the body keeps the last known snapshot. */
    passport_service_snapshot_t snapshot = {0};
    passport_ui_model_t model;

    snapshot.page = PASSPORT_PAGE_WEAR_HOME;
    strcpy(snapshot.task_id, "runtime-1");
    strcpy(snapshot.summary, "重构 tracing");
    snapshot.task_state = PASSPORT_TASK_RUNNING;
    snapshot.progress = 42;
    snapshot.link_idle_ms = 40000;
    passport_ui_model_build(&snapshot, true, 80, &model);

    assert(model.disconnected_banner[0] == '\0');
    assert(strcmp(model.link, "断线") == 0);
    /* Body still reflects live state, not a warning banner. */
    assert(strcmp(model.body[0], "运行中 42%") == 0);
    assert(strcmp(model.body[1], "重构 tracing") == 0);
}

static void test_no_banner_before_first_frame(void) {
    /* Fresh boot: link_idle_ms is non-zero but no host frame has been applied
     * yet. Header shows 未连接 without splashing the body. */
    passport_service_snapshot_t snapshot = {0};
    passport_ui_model_t model;

    snapshot.page = PASSPORT_PAGE_WEAR_HOME;
    snapshot.link_idle_ms = 120000;
    passport_ui_model_build(&snapshot, true, 80, &model);

    assert(model.disconnected_banner[0] == '\0');
}

int main(void) {
    test_wear_home_shows_chinese_headline();
    test_wear_home_reflects_goal_and_event();
    test_wear_task_lists_recent_events();
    test_compose_stack_renders_selection_and_status();
    test_approval_overrides_hint();
    test_stale_link_updates_header_only();
    test_no_banner_before_first_frame();
    return 0;
}
