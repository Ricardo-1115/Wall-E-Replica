/**
 * @file energy_manager.c
 * @brief WALL-E 能量管理系统
 *
 * ====== 设计思路 ======
 *
 * 整个系统只维护一个 0~100 的能量值，代替多级超时/定时器嵌套：
 *
 *   - 有人在操控（WebSocket 活动）→ 能量拉到 100，状态 ENGAGED
 *   - 长时间无人操控 → 能量每秒衰减 0.08，逐步从兴奋过渡到低落
 *   - 突发外部事件（声音等）→ 能量 +30，播一次惊喜动画
 *   - 电量低于 20% 时 → 衰减速度加倍
 *
 * 能量值就是 WALL-E 当前"兴奋程度"的单一指标。行为按能量区间选取：
 *
 *   80~100  微表情（启用引擎空闲系统）
 *   40~80   播放 CURIOUS / BUG_TRACK（好奇、探索）
 *   10~40   播放 SAD / SHY（无聊、低落）
 *   0       低头假寐（固定姿态，关闭微表情）
 *
 * 动画引擎（8 套动画）作为"素材库"使用，能量管理器只负责"选片"不改片。
 * ================================================================ */

#include "energy_manager.h"
#include "Servo_app.h"
#include "animation_engine.h"
#include "battery.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>

static const char *TAG = "ENERGY";

/* ==================================================================
 * 常量定义
 * ================================================================ */

#define TASK_INTERVAL_MS    1000            /* 管理器每秒 tick 一次 */
#define ACTIVITY_TIMEOUT_MS 3000            /* 超过 3s 无 WS 消息 = 无人操控 */
#define DECAY_NORMAL        0.08f           /* 正常衰减：0.08/秒 */
#define DECAY_LOW_BATTERY   0.16f           /* 低电量衰减加倍 */
#define SURPRISE_BOOST      30.0f           /* 突发事件能量加成 */
#define LOW_BATTERY_THRESH  20              /* 电量低于此值视为低电量 */

/* 能量区间边界 */
#define ZONE_HIGH_MIN       80
#define ZONE_MID_MIN        40
#define ZONE_LOW_MIN        10

/* 冷却时间范围（毫秒） */
#define COOLDOWN_MID_MS     35000u
#define COOLDOWN_MID_JITTER 20000u
#define COOLDOWN_LOW_MS     60000u
#define COOLDOWN_LOW_JITTER 30000u

/* 串口调试输出间隔（tick 数） */
#define DEBUG_INTERVAL      3

/* 关节索引 */
#define J_HEAD_PAN    0
#define J_NECK_UPPER  1
#define J_NECK_LOWER  2
#define J_LEFT_EYE    3
#define J_RIGHT_EYE   4
#define J_LEFT_ARM    5
#define J_RIGHT_ARM   6

/* 动画 ID */
#define ANIM_CURIOUS    1
#define ANIM_SURPRISE   2
#define ANIM_SHY        3
#define ANIM_BUG_TRACK  5
#define ANIM_SAD        6

/* ==================================================================
 * 全局状态
 * ================================================================ */

static float         s_energy            = 100.0f;
static energy_state_t s_state            = ENERGY_STATE_IDLE;
static _Atomic uint32_t s_last_activity_ms = 0;
static atomic_bool     s_pending_surprise  = false;

static uint32_t      s_last_anim_ms      = 0;
static uint32_t      s_cooldown_ms       = 0;
static bool          s_sleepy_active     = false;
static int           s_zone              = -1;   /* -1=未初始化, 0/1/2/3 */

/* ==================================================================
 * 内部辅助
 * ================================================================ */

static inline uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int energy_to_zone(float e) {
    if (e >= ZONE_HIGH_MIN) return 0;
    if (e >= ZONE_MID_MIN)  return 1;
    if (e >= ZONE_LOW_MIN)  return 2;
    return 3;
}

/* ==================================================================
 * 假寐姿态：头低下、眼半闭、手臂垂落
 * ================================================================ */

static void set_sleepy_pose(void) {
    /* 用较慢的速度（2000ms）过渡到假寐姿态，显得自然 */
    walle_joint_move(J_HEAD_PAN,   50, 2000);
    walle_joint_move(J_NECK_UPPER, 10, 2000);
    walle_joint_move(J_NECK_LOWER, 45, 2000);
    walle_joint_move(J_LEFT_EYE,   60, 2000);
    walle_joint_move(J_RIGHT_EYE,  40, 2000);
    walle_joint_move(J_LEFT_ARM,    0, 2000);
    walle_joint_move(J_RIGHT_ARM,   0, 2000);
    // ESP_LOGI(TAG, "Sleepy pose set (head down, eyes half-closed, arms down)");
}

/* ==================================================================
 * 能量区间行为选择
 * ================================================================ */

static void pick_animation_by_energy(uint32_t now) {
    int zone = energy_to_zone(s_energy);

    switch (zone) {
    case 0:
        /* 80~100：微表情区间 —— 交给引擎的空闲系统 */
        anim_engine_enable_idle(true);
        s_last_anim_ms = now;
        s_cooldown_ms = 30000;   /* 30s 后重新确认区间 */
        // ESP_LOGI(TAG, ">> Zone 0 (high energy) — idle expressions enabled");
        break;

    case 1: {
        /* 40~80：好奇/探索 */
        anim_engine_enable_idle(false);
        uint32_t jitter = esp_random() % COOLDOWN_MID_JITTER;
        int anim = (esp_random() % 2 == 0) ? ANIM_CURIOUS : ANIM_BUG_TRACK;
        anim_engine_play(anim);
        s_last_anim_ms = now;
        s_cooldown_ms = COOLDOWN_MID_MS + jitter;
        const char *name = (anim == ANIM_CURIOUS) ? "CURIOUS" : "BUG_TRACK";
        // ESP_LOGI(TAG, ">> Zone 1 play: %s  (E=%.0f, cooldown=%lums)",
        //          name, s_energy, s_cooldown_ms);
        break;
    }

    case 2: {
        /* 10~40：无聊/低落 */
        anim_engine_enable_idle(false);
        uint32_t jitter = esp_random() % COOLDOWN_LOW_JITTER;
        int anim = (esp_random() % 2 == 0) ? ANIM_SAD : ANIM_SHY;
        anim_engine_play(anim);
        s_last_anim_ms = now;
        s_cooldown_ms = COOLDOWN_LOW_MS + jitter;
        const char *name = (anim == ANIM_SAD) ? "SAD" : "SHY";
        // ESP_LOGI(TAG, ">> Zone 2 play: %s  (E=%.0f, cooldown=%lums)",
        //          name, s_energy, s_cooldown_ms);
        break;
    }

    default:
        break;
    }
}

/* ==================================================================
 * 串口调试输出（每 3 秒打印一次状态 + 7 关节角度）
 * ================================================================ */

static void print_debug(void) {
    const char *state_str;
    switch (s_state) {
    case ENERGY_STATE_ENGAGED: state_str = "ENG"; break;
    default:                   state_str = "IDL"; break;
    }

    int zone = energy_to_zone(s_energy);

    /* 读取 7 个关节的当前实际角度（get_servo_angle 返回 PCA9685 设定角度 0-180） */
    float a0 = get_servo_angle(J_HEAD_PAN);
    float a1 = get_servo_angle(J_NECK_UPPER);
    float a2 = get_servo_angle(J_NECK_LOWER);
    float a3 = get_servo_angle(J_LEFT_EYE);
    float a4 = get_servo_angle(J_RIGHT_EYE);
    float a5 = get_servo_angle(J_LEFT_ARM);
    float a6 = get_servo_angle(J_RIGHT_ARM);

    /* 单行紧凑输出，方便串口工具/逻辑分析仪抓取 */
    ESP_LOGI(TAG, "E:%3.0f [%s Z%d]  "
             "Ch0:%5.1f Ch1:%5.1f Ch2:%5.1f  "
             "EyeL:%5.1f EyeR:%5.1f  "
             "ArmL:%5.1f ArmR:%5.1f",
             s_energy, state_str, zone,
             a0, a1, a2, a3, a4, a5, a6);
}

/* ==================================================================
 * 能量衰减
 * ================================================================ */

static void decay_energy(void) {
    float decay = DECAY_NORMAL;
    uint8_t batt_pct = battery_get_percentage();
    if (batt_pct > 0 && batt_pct < LOW_BATTERY_THRESH) {
        decay = DECAY_LOW_BATTERY;
    }
    s_energy = clampf(s_energy - decay, 0.0f, 100.0f);
}

/* ==================================================================
 * 进入 ENGAGED 状态
 * ================================================================ */

static void enter_engaged(void) {
    if (s_state == ENERGY_STATE_ENGAGED) return;  /* 已经在 ENGAGED */

    // ESP_LOGI(TAG, "User active -> ENGAGED  (energy pulled to 100)");
    s_state = ENERGY_STATE_ENGAGED;
    s_energy = 100.0f;
    s_sleepy_active = false;
    s_zone = -1;

    /* 动画打断已由 ws_handler 在收到指令时立即处理，
     * 能量管理器不再负责此逻辑 */
}

/* ==================================================================
 * 处理惊喜事件
 * ================================================================ */

static void handle_surprise(void) {
    s_energy = clampf(s_energy + SURPRISE_BOOST, 0.0f, 100.0f);
    s_sleepy_active = false;

    anim_engine_enable_idle(false);
    anim_engine_play(ANIM_SURPRISE);

    // ESP_LOGI(TAG, "Surprise event! E +%.0f -> %.0f", SURPRISE_BOOST, s_energy);

    s_last_anim_ms = now_ms();
    s_cooldown_ms = 35000;   /* 惊喜后给 35s 冷静期 */
    s_zone = energy_to_zone(s_energy);
}

/* ==================================================================
 * 能量管理器主任务（每秒执行一次）
 * ================================================================ */

static void energy_task(void *arg) {
    uint32_t tick = 0;
    vTaskDelay(pdMS_TO_TICKS(2000));  /* 启动延迟 2s，等所有子系统就绪 */

    ESP_LOGI(TAG, "Energy manager started (decay=%.2f/s, activity_timeout=%dms)",
             DECAY_NORMAL, ACTIVITY_TIMEOUT_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(TASK_INTERVAL_MS));
        tick++;
        uint32_t now = now_ms();

        /* ----------------------------------------------------------
         * 第 1 步：检查用户操控活动（原子读，消除跨核竞争）
         * ---------------------------------------------------------- */
        if (now - atomic_load(&s_last_activity_ms) < ACTIVITY_TIMEOUT_MS) {
            enter_engaged();
            goto debug_out;
        }

        /* 从 ENGAGED 回到 IDLE */
        if (s_state == ENERGY_STATE_ENGAGED) {
            // ESP_LOGI(TAG, "ENGAGED -> IDLE  (decay starts from %.0f)", s_energy);
            s_state = ENERGY_STATE_IDLE;
            s_zone = -1;   /* 强制重新检测区间 */
        }

        /* ----------------------------------------------------------
         * 第 2 步：处理惊喜事件（原子交换，避免跨核竞争）
         * ---------------------------------------------------------- */
        if (atomic_exchange(&s_pending_surprise, false)) {
            handle_surprise();
            goto debug_out;
        }

        /* ----------------------------------------------------------
         * 第 3 步：能量衰减
         * ---------------------------------------------------------- */
        decay_energy();

        /* ----------------------------------------------------------
         * 第 4 步：能量为 0 → 假寐
         * ---------------------------------------------------------- */
        if (s_energy <= 0.05f) {
            if (!s_sleepy_active) {
                set_sleepy_pose();
                anim_engine_enable_idle(false);
                s_sleepy_active = true;
                s_zone = 3;
                // ESP_LOGI(TAG, "Energy at 0 -> SLEEPY mode");
            }
            goto debug_out;
        }

        /* 从假寐中醒来 */
        if (s_sleepy_active) {
            s_sleepy_active = false;
            s_zone = -1;
            // ESP_LOGI(TAG, "Woke up from sleepy (E=%.1f)", s_energy);
        }

        /* ----------------------------------------------------------
         * 第 5 步：能量区间变化检测
         * ---------------------------------------------------------- */
        int new_zone = energy_to_zone(s_energy);
        if (new_zone != s_zone) {
            // ESP_LOGI(TAG, "Zone change: %d -> %d  (E=%.0f)", s_zone, new_zone, s_energy);
            s_zone = new_zone;

            /* 离开 Zone 0（高能）时关掉空闲微表情 */
            if (new_zone != 0) {
                anim_engine_enable_idle(false);
            }

            /* 清空冷却，允许立刻播新区间的动画 */
            s_last_anim_ms = 0;
            s_cooldown_ms = 0;
        }

        /* ----------------------------------------------------------
         * 第 6 步：等待当前动画播完
         * ---------------------------------------------------------- */
        if (anim_engine_is_playing()) {
            goto debug_out;
        }

        /* ----------------------------------------------------------
         * 第 7 步：冷却期检查
         * ---------------------------------------------------------- */
        if (now - s_last_anim_ms < s_cooldown_ms) {
            /* 冷却期内启用空闲微表情，避免机器人完全静止 */
            if (s_zone != 3 && !anim_engine_is_idle_enabled()) {
                anim_engine_enable_idle(true);
            }
            goto debug_out;
        }

        /* ----------------------------------------------------------
         * 第 8 步：按能量区间选播动画
         * ---------------------------------------------------------- */
        pick_animation_by_energy(now);

debug_out:
        // if (tick % DEBUG_INTERVAL == 0) {
        //     print_debug();
        // }
    }
}

/* ==================================================================
 * 公开 API
 * ================================================================ */

void energy_manager_init(void) {
    BaseType_t ret = xTaskCreatePinnedToCore(
        energy_task, "energy_mgr", 4096, NULL, 6, NULL, 1
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create energy manager task");
    }
}

void energy_manager_notify_activity(void) {
    atomic_store(&s_last_activity_ms, now_ms());
}

void energy_manager_notify_event(void) {
    atomic_store(&s_pending_surprise, true);
    // ESP_LOGI(TAG, "External event recorded (surprise pending)");
}

uint8_t energy_manager_get_value(void) {
    return (uint8_t)clampf(s_energy, 0.0f, 100.0f);
}

int energy_manager_get_zone(void) {
    return energy_to_zone(s_energy);
}

energy_state_t energy_manager_get_state(void) {
    return s_state;
}
