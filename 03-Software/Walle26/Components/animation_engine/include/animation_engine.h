#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ANIM_JOINT_COUNT 7

/* ====== 关键帧 ====== */
typedef struct {
    int8_t  sound_folder;                   // -1 = 无音效
    int8_t  sound_file;
    uint16_t duration_ms;                   // 这一帧持续毫秒
    int8_t  joint[ANIM_JOINT_COUNT];        // 百分比 0~100，-1 = 不动
} anim_keyframe_t;

/* ====== 动画定义 ====== */
typedef struct {
    const char             *name;
    const anim_keyframe_t  *keyframes;
    uint16_t                frame_count;
} anim_definition_t;

/* ====== 引擎命令 ====== */
typedef enum {
    ANIM_CMD_PLAY,      // 播放（打断当前）
    ANIM_CMD_STOP,      // 停止
    ANIM_CMD_QUEUE,     // 排入待播队列
} anim_cmd_type_t;

typedef struct {
    anim_cmd_type_t type;
    int8_t          anim_id;
} anim_engine_cmd_t;

/* ====== 引擎状态 ====== */
typedef enum {
    ANIM_STATE_IDLE,
    ANIM_STATE_PLAYING,
} anim_state_t;

/* ====== 公开 API ====== */

// 初始化引擎（创建命令队列、启动引擎 task）
void anim_engine_init(void);

// 播放动画（非阻塞，若正在播则打断）
esp_err_t anim_engine_play(int anim_id);

// 排入队列（当前播完后自动播放）
esp_err_t anim_engine_enqueue(int anim_id);

// 立即停止
void anim_engine_stop(void);

// 查询状态
bool anim_engine_is_playing(void);
int  anim_engine_current_id(void);

// 查询动画信息（给 CLI -l 列表用）
int  anim_engine_get_count(void);
const anim_definition_t *anim_engine_get_def(int anim_id);

// 空闲微表情控制
void anim_engine_enable_idle(bool enable);
bool anim_engine_is_idle_enabled(void);

#ifdef __cplusplus
}
#endif
