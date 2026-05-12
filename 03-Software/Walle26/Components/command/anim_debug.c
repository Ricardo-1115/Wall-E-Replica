#include <stdio.h>
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "animation_engine.h"

/* CLI -l 用的中文名表（引擎数据只存英文名） */
static const char *s_anim_name_cn[] = {
    "开机唤醒", "好奇歪头", "惊喜发现", "害羞躲藏",
    "太空舞",   "追踪小强", "心情低落", "自我介绍",
};

static struct {
    struct arg_int *id;
    struct arg_lit *list;
    struct arg_end *end;
} anim_debug_args;

static int cmd_anim_debug(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&anim_debug_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, anim_debug_args.end, argv[0]);
        return 1;
    }

    /* -l: 列表 */
    if (anim_debug_args.list->count > 0) {
        int count = anim_engine_get_count();
        printf("\n可用动画序列:\n");
        printf("  ID | 名称              | 英文 ID        | 帧数 | 总时长\n");
        printf("-----+-------------------+----------------+------+---------\n");
        for (int i = 0; i < count; i++) {
            const anim_definition_t *def = anim_engine_get_def(i);
            uint16_t total_ms = 0;
            for (int f = 0; f < def->frame_count; f++)
                total_ms += def->keyframes[f].duration_ms;
            printf("  %2d | %-16s | %-14s |  %3d | %d.%02ds\n",
                   i, s_anim_name_cn[i], def->name,
                   def->frame_count, total_ms / 1000, (total_ms % 1000) / 10);
        }
        printf("\n使用: anim_debug <id> 播放动画\n");
        return 0;
    }

    /* 无参数：提示 */
    if (anim_debug_args.id->count == 0) {
        printf("用法: anim_debug <id>  或  anim_debug -l  列出所有动画\n");
        return 1;
    }

    /* 播放 */
    int id = anim_debug_args.id->ival[0];
    esp_err_t err = anim_engine_play(id);
    if (err != ESP_OK) {
        printf("播放失败，无效 ID: %d (有效范围 0-%d)\n", id, anim_engine_get_count() - 1);
    }
    return 0;
}

void register_anim_debug(void) {
    anim_debug_args.id   = arg_int0(NULL, NULL, "<id>", "Animation ID (0-7)");
    anim_debug_args.list = arg_lit0("l",  NULL, "List all animations");
    anim_debug_args.end  = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "anim_debug",
        .help = "Play animation via engine. Use -l to list.",
        .hint = NULL,
        .func = &cmd_anim_debug,
        .argtable = &anim_debug_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ====== anim_idle 命令 ====== */

static struct {
    struct arg_lit *enable;
    struct arg_lit *disable;
    struct arg_end *end;
} anim_idle_args;

static int cmd_anim_idle(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&anim_idle_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, anim_idle_args.end, argv[0]);
        return 1;
    }

    if (anim_idle_args.enable->count > 0) {
        anim_engine_enable_idle(true);
        printf("空闲微表情: ON\n");
    } else if (anim_idle_args.disable->count > 0) {
        anim_engine_enable_idle(false);
        printf("空闲微表情: OFF\n");
    } else {
        printf("空闲微表情: %s\n", anim_engine_is_idle_enabled() ? "ON" : "OFF");
    }
    return 0;
}

void register_anim_idle(void) {
    anim_idle_args.enable  = arg_lit0("e", "enable",  "Enable idle expressions");
    anim_idle_args.disable = arg_lit0("d", "disable", "Disable idle expressions");
    anim_idle_args.end     = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "anim_idle",
        .help = "Control idle expressions: -e enable, -d disable, no args = show status",
        .hint = NULL,
        .func = &cmd_anim_idle,
        .argtable = &anim_idle_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
