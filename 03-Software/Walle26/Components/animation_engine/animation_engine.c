/**
 * @file animation_engine.c
 * @brief WALL-E 动画引擎
 *
 * ====== 架构思路 ======
 *
 * 这是一个基于 "专用任务 + 消息队列" 的动画播放系统：
 *
 *   anim_engine_play()  ──投信──┐
 *   anim_engine_enqueue()──投信──┤
 *   anim_engine_stop()  ──投信──┤
 *                               ▼
 *                       ┌───────────────┐
 *                       │   消息队列     │  (接收外部命令)
 *                       │  (4 槽环形)   │
 *                       └───────┬───────┘
 *                               │ 取信
 *                         ┌─────▼─────┐
 *                         │ 引擎任务   │  (独立 FreeRTOS 任务)
 *                         │ 状态机循环  │
 *                         │ IDLE/PLAY  │
 *                         └─────┬─────┘
 *                     ┌─────────┼─────────┐
 *                     ▼         ▼         ▼
 *                 舵机控制   音效播放   帧定时/可打断等待
 *
 * 关键设计：
 * - 所有公开 API 都是非阻塞的（投信即返回）
 * - 帧与帧之间的等待期通过 xQueueReceive 超时实现"可打断"
 * - 空闲时自动播随机微表情（生成器模式，不存帧数据存"生成方法"）
 * - 微表情和预定义动画共享同一套播放逻辑
 */

#include "animation_engine.h"
#include "Servo_app.h"
#include "DFPlayerMini.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_random.h"

static const char *TAG = "ANIM_ENGINE";

/* ==================================================================
 * 第一部分：预定义动画数据（剧本）
 *
 * 每套动画 = 一组关键帧（keyframe）序列。
 * 一个关键帧描述"这一刻要做什么"：
 *   - 发音效吗？（sound_folder / sound_file，-1 表示不发）
 *   - 持续多久？（duration_ms）
 *   - 7 个关节各要转到多少度？（0~100 百分比，-1 表示不动）
 *
 * 这些数据用 static const 存在 Flash（.rodata），不占 RAM。
 * ================================================================ */

/* 0. 开机唤醒 / WAKEUP
 *  帧0: 发动"启动音"    → 关节全部归零（初始姿态）
 *  帧1: 发"哔哔"声      → 头抬起来，脖子和手臂到位
 *  帧2~6: 左右看（眼睛交替 0↔100），制造"刚醒来在观察环境"的效果
 *  帧7: 发"完成音"      → 回到标准姿态 */
static const anim_keyframe_t anim_wakeup_frames[] = {
    { 1, 19, 1200, {50,  0,  0, 50, 50,  0,  0} },
    { 1,  1, 2500, {65, 50, 50, 50, 50, 30, 30} },
    {-1, -1,  500, {35, 55, 45,  0,100, 30, 30} },
    {-1, -1,  500, {55, 45, 55,100,  0, 30, 30} },
    {-1, -1,  300, {50, 50, 50, 50, 50, 30, 30} },
    {-1, -1,  500, {50, 50, 50,  0,100, 30, 30} },
    {-1, -1,  200, {50, 50, 50, 50, 50, 30, 30} },
    { 1, 27,  710, {50, 50, 50, 50, 50, 30, 30} },
};

/* 1. 好奇歪头 / CURIOUS
 *  帧0: 头歪向一边（60/35/65，头偏 + 脖子补偿），眼睛也偏
 *  帧1: 保持歪头+换音效
 *  帧2~3: 慢慢回正 */
static const anim_keyframe_t anim_curious_frames[] = {
    { 1, 17, 1800, {60, 35, 65, 65, 35, 30, 30} },
    { 1, 27,  800, {60, 35, 65, 65, 35, 30, 30} },
    {-1, -1,  600, {55, 40, 60, 55, 45, 30, 30} },
    {-1, -1,  500, {50, 50, 50, 50, 50, 30, 30} },
};

/* 2. 惊喜发现 / SURPRISE
 *  帧0: 眼睛睁大（关节3=0是右眼开到最大，4=100是左眼开到最大）
 *       手臂抬起（70/70），发音效
 *  帧1~3: 眼睛交替张合（快速眨眼，表示惊喜）
 *  帧4: 手臂举更高 + 发音效
 *  帧5: 收回来 */
static const anim_keyframe_t anim_surprise_frames[] = {
    { 1, 13, 2000, {55, 60, 40,  0,100, 70, 70} },
    {-1, -1,  200, {45, 60, 40,100,  0, 70, 70} },
    {-1, -1,  200, {55, 60, 40,  0,100, 70, 70} },
    {-1, -1,  200, {45, 60, 40,100,  0, 70, 70} },
    { 1, 23, 1300, {50, 50, 50, 50, 50, 80, 80} },
    {-1, -1,  600, {50, 50, 50, 50, 50, 30, 30} },
};

/* 3. 害羞躲藏 / SHY
 *  特点：脖子往下缩（关节1=20→10），眼睛往下看（60/50→55/45）
 *       手臂收到底（10→0），整体姿态是"蜷缩" */
static const anim_keyframe_t anim_shy_frames[] = {
    { 1,  7, 1000, {45, 20, 45, 60, 40, 10, 10} },
    {-1, -1,  500, {45, 10, 45, 55, 45,  0,  0} },
    { 1, 15, 2100, {45, 20, 50, 55, 45, 10, 10} },
    {-1, -1,  600, {50, 50, 50, 50, 50, 20, 20} },
    {-1, -1,  400, {50, 50, 50, 50, 50, 30, 30} },
};

/* 4. 太空舞 / SPACE_DANCE
 *  左右摇摆身体 + 手臂交替上下，频率较快 */
static const anim_keyframe_t anim_space_dance_frames[] = {
    { 1, 18, 1000, {50, 50, 50, 50, 50, 50, 50} },
    {-1, -1,  800, {30, 55, 45, 50, 50, 80, 30} },
    {-1, -1,  800, {70, 45, 55, 50, 50, 30, 80} },
    {-1, -1,  700, {30, 55, 45, 50, 50, 80, 80} },
    {-1, -1,  700, {70, 45, 55, 50, 50, 80, 80} },
    {-1, -1,  500, {40, 50, 50, 50, 50, 60, 40} },
    {-1, -1,  500, {60, 50, 50, 50, 50, 40, 60} },
    {-1, -1,  800, {50, 50, 50, 50, 50, 30, 30} },
};

/* 5. 追踪小强 / BUG_TRACK
 *  头慢慢低下来（40→30），眼睛聚焦往下，模拟"盯住地上东西" */
static const anim_keyframe_t anim_bug_track_frames[] = {
    { 1, 22, 1500, {40, 20, 40, 50, 50,  0,  0} },
    {-1, -1,  600, {35, 25, 45, 55, 45,  0,  0} },
    {-1, -1,  800, {30, 30, 50, 60, 40,  0,  0} },
    {-1, -1,  600, {40, 25, 45, 50, 50,  0,  0} },
    {-1, -1,  500, {50, 30, 50, 50, 50,  0,  0} },
};

/* 6. 心情低落 / SAD
 *  头低（15），眼睛半闭（60/40），手臂落下（5/5），整体"没精神" */
static const anim_keyframe_t anim_sad_frames[] = {
    { 1, 16, 2500, {55, 15, 50, 60, 40,  5,  5} },
    {-1, -1, 1000, {45,  5, 50, 55, 45,  0,  0} },
    {-1, -1, 1500, {45,  5, 50, 55, 45,  0,  0} },
    {-1, -1, 1000, {50, 20, 50, 55, 45,  5,  5} },
    {-1, -1, 1000, {50, 50, 50, 50, 50, 30, 30} },
};

/* 7. 自我介绍 / INTRO
 *  帧0: 先摆好基础姿态
 *  帧1: 手臂抬起（80/80），发音效，持续 4 秒（可能在说话）
 *  帧2~4: 眼睛交替看左右，配合"说话"的感觉
 *  帧5: 收回来 */
static const anim_keyframe_t anim_intro_frames[] = {
    {-1, -1,  500, {50, 50, 50, 50, 50, 30, 30} },
    { 1, 24, 4000, {40, 40, 60, 50, 50, 80, 80} },
    {-1, -1,  600, {55, 40, 60,  0,100, 80, 80} },
    {-1, -1,  600, {55, 40, 60,100,  0, 80, 80} },
    {-1, -1,  400, {55, 40, 60, 50, 50, 80, 80} },
    {-1, -1,  600, {50, 50, 50, 50, 50, 30, 30} },
};

#define ANIM_COUNT 8

/* 动画注册表：名字 → 帧数组 → 帧数量
 * frame_count 用 sizeof/sizeof 编译期计算，没有运行时开销 */
static const anim_definition_t s_anim_defs[ANIM_COUNT] = {
    { "WAKEUP",       anim_wakeup_frames,      sizeof(anim_wakeup_frames)      / sizeof(anim_keyframe_t) },
    { "CURIOUS",      anim_curious_frames,     sizeof(anim_curious_frames)     / sizeof(anim_keyframe_t) },
    { "SURPRISE",     anim_surprise_frames,    sizeof(anim_surprise_frames)    / sizeof(anim_keyframe_t) },
    { "SHY",          anim_shy_frames,         sizeof(anim_shy_frames)         / sizeof(anim_keyframe_t) },
    { "SPACE_DANCE",  anim_space_dance_frames, sizeof(anim_space_dance_frames) / sizeof(anim_keyframe_t) },
    { "BUG_TRACK",    anim_bug_track_frames,   sizeof(anim_bug_track_frames)   / sizeof(anim_keyframe_t) },
    { "SAD",          anim_sad_frames,         sizeof(anim_sad_frames)         / sizeof(anim_keyframe_t) },
    { "INTRO",        anim_intro_frames,       sizeof(anim_intro_frames)       / sizeof(anim_keyframe_t) },
};

/* ==================================================================
 * 第二部分：空闲微表情生成器
 *
 * 设计思路：
 *   预定义动画是"写死的剧本"，适合完整、固定的表演。
 *   但空闲微表情需要"随机、不重复、有生命感"——所以不存帧数据，
 *   而是存"生成帧的方法"（生成器函数）。
 *
 *   每次空闲超时后，引擎随机选一个生成器，生成器往共享缓冲区
 *   s_idle_frames 里写 2~4 帧，然后按普通动画流程播放。
 *   这样同一套生成器每次调用的参数都随机，产生无穷变化。
 *
 *   缺点：生成器播完后帧数据在共享缓冲区被覆盖之前有效。
 *   如果需要在微表情播放途中被打断再播另一个微表情，可能会有问题——
 *   但当前设计不允许微表情打断微表情（只能被预定义动画打断），所以安全。
 * ================================================================ */

/* 生成器帧缓冲区（可打断的帧序列，由生成器在运行时填入） */
#define MAX_IDLE_FRAMES 16
static anim_keyframe_t s_idle_frames[MAX_IDLE_FRAMES];     /* 共享缓冲区 */
static int s_idle_frame_count;                              /* 本次实际写了多少帧 */
static anim_definition_t s_idle_runtime_def;                /* 运行时构造的动画定义 */

/* 生成器函数指针类型：
 *   buf   → 写入帧的缓冲区（s_idle_frames）
 *   count → 返回实际写了多少帧 */
typedef void (*idle_gen_t)(anim_keyframe_t *buf, int *count);

/* 安全的随机范围工具函数
 * 用 esp_random()（硬件真随机数发生器）而不是标准库 rand()
 * inline 提示编译器嵌入调用处，省去函数调用开销 */
static inline int rand_range(int min, int max) {
    if (max <= min) return min;
    return min + (int)(esp_random() % (uint32_t)(max - min + 1));
}

/* ---------- 各生成器函数 ---------- */

/* 0. 眨眼
 *   - 默认右眼眨（joint[3]=0 闭眼，joint[4]=100 睁眼 → 正确方向待确认）
 *     注意：如果 0=闭眼100=睁眼，那么正常是右0左100只闭右眼
 *   - 25% 概率变成左眼眨（交换方向）
 *   - 闭眼和睁眼的速度随机变化，避免机械感 */
static void gen_blink(anim_keyframe_t *buf, int *count) {
    int close_ms = rand_range(120, 280);
    int open_ms  = rand_range(200, 450);
    int eye_r = 0, eye_l = 100;
    if (esp_random() % 4 == 0) {
        eye_r = rand_range(0, 20);
        eye_l = rand_range(80, 100);
    }
    buf[0] = (anim_keyframe_t){ -1, -1, close_ms, { -1, -1, -1, eye_r, eye_l, -1, -1 } };
    buf[1] = (anim_keyframe_t){ -1, -1, open_ms,  { -1, -1, -1, 50,    50,    -1, -1 } };
    *count = 2;
}

/* 1. 连眨（快速眨两次，制造"嗯？"的效果）
 *   - 4 帧：闭→睁→闭→睁，每次持续 150~350ms */
static void gen_double_blink(anim_keyframe_t *buf, int *count) {
    int t[4];
    for (int i = 0; i < 4; i++) t[i] = rand_range(150, 350);
    buf[0] = (anim_keyframe_t){ -1, -1, t[0], { -1, -1, -1,  0,100, -1, -1 } };
    buf[1] = (anim_keyframe_t){ -1, -1, t[1], { -1, -1, -1, 50, 50, -1, -1 } };
    buf[2] = (anim_keyframe_t){ -1, -1, t[2], { -1, -1, -1,  0,100, -1, -1 } };
    buf[3] = (anim_keyframe_t){ -1, -1, t[3], { -1, -1, -1, 50, 50, -1, -1 } };
    *count = 4;
}

/* 2. 眼睛扫左
 *   - 向左转到 pos（10~30），盯住 2~4 秒，转回来
 *   - sound_folder=1, sound_file=19 触发 Processing 音效 */
static void gen_eye_left(anim_keyframe_t *buf, int *count) {
    int spd = rand_range(400, 700);
    int hold = rand_range(2000, 4000);
    int ret = rand_range(400, 700);
    int pos = rand_range(10, 30);
    buf[0] = (anim_keyframe_t){ 1, 19, spd,  { -1, -1, -1, pos, pos, -1, -1 } };
    buf[1] = (anim_keyframe_t){ -1, -1, hold, { -1, -1, -1, pos, pos, -1, -1 } };
    buf[2] = (anim_keyframe_t){ -1, -1, ret,  { -1, -1, -1, 50,  50,  -1, -1 } };
    *count = 3;
}

/* 3. 眼睛扫右 — 逻辑同上，pos 范围不同（70~90） */
static void gen_eye_right(anim_keyframe_t *buf, int *count) {
    int spd = rand_range(400, 700);
    int hold = rand_range(2000, 4000);
    int ret = rand_range(400, 700);
    int pos = rand_range(70, 90);
    buf[0] = (anim_keyframe_t){ 1, 19, spd, { -1, -1, -1, pos, pos, -1, -1 } };
    buf[1] = (anim_keyframe_t){ -1, -1, hold, { -1, -1, -1, pos, pos, -1, -1 } };
    buf[2] = (anim_keyframe_t){ -1, -1, ret,  { -1, -1, -1, 50,  50,  -1, -1 } };
    *count = 3;
}

/* 4. 歪头
 *   - 三个关节联动：头(h) + 脖子顶(nt) + 脖子底(nb)
 *   - 数学关系：
 *       头向一边偏 amt (5~15%)
 *       脖子顶反向补偿 2/3（模拟颈椎弯曲）
 *       脖子底随头同向偏 2/3
 *   - 方向随机（左/右），幅度随机
 *   - 动作较慢（1000~1800ms），因为"歪头"是柔和的 */
static void gen_head_tilt(anim_keyframe_t *buf, int *count) {
    int spd = rand_range(1000, 1800);
    int hold = rand_range(2500, 5000);
    int ret = rand_range(1000, 1800);
    int amt = rand_range(5, 15);
    int dir = (esp_random() % 2) ? 1 : -1;
    int h  = 50 + dir * amt;
    int nt = 50 - dir * amt * 2 / 3;
    int nb = 50 + dir * amt * 2 / 3;
    buf[0] = (anim_keyframe_t){ 1, 5, spd,  { h, nt, nb, -1, -1, -1, -1 } };
    buf[1] = (anim_keyframe_t){ -1, -1, hold, { h, nt, nb, -1, -1, -1, -1 } };
    buf[2] = (anim_keyframe_t){ -1, -1, ret,  { 50, 50, 50, -1, -1, -1, -1 } };
    *count = 3;
}

/* 5. 全景扫视（眼睛从最左扫到最右再回中）
 *   - 模拟 WALL-E 在观察整个房间
 *   - 速度较慢（600~1200ms），让扫视看起来从容 */
static void gen_scan(anim_keyframe_t *buf, int *count) {
    int spd = rand_range(600, 1200);
    int pos_l = rand_range(10, 25);
    int pos_r = rand_range(75, 90);
    buf[0] = (anim_keyframe_t){ 1, 21, spd, { -1, -1, -1, pos_l, pos_l, -1, -1 } };
    buf[1] = (anim_keyframe_t){ -1, -1, spd, { -1, -1, -1, pos_r, pos_r, -1, -1 } };
    buf[2] = (anim_keyframe_t){ -1, -1, spd, { -1, -1, -1, 50,    50,    -1, -1 } };
    *count = 3;
}

/* 6. 手臂微动
 *   - 随机选左臂(j5)或右臂(j6), 抬一下再放下
 *   - 选中手臂的关节值 = base(30) + amt(3~15), 没选中的填 -1
 *   - 动作快（300~600ms），像"抽动"一下 */
static void gen_arm_twitch(anim_keyframe_t *buf, int *count) {
    int spd = rand_range(300, 600);
    int amt = rand_range(3, 15);
    int arm = (esp_random() % 2) ? 5 : 6;
    int base = 30;
    int j5 = (arm == 5) ? base + amt : -1;
    int j6 = (arm == 6) ? base + amt : -1;
    buf[0] = (anim_keyframe_t){ 1, 3, spd, { -1, -1, -1, -1, -1, j5, j6 } };
    buf[1] = (anim_keyframe_t){ -1, -1, spd, { -1, -1, -1, -1, -1, 30, 30 } };
    *count = 2;
}

/* ---------- 加权随机选择表 ---------- */

#define IDLE_EXPR_COUNT 7

/* 每个微表情的"出现概率"由 weight 控制
 * 权重越大，被选中的概率越高
 * 总权重 = 100（刚好等于 100%，方便理解概率分布） */
static const struct {
    const char *name;
    idle_gen_t  generate;
    uint8_t     weight;
} s_idle_exprs[IDLE_EXPR_COUNT] = {
    { "BLINK",       gen_blink,       30 },  /* 30% — 最频繁 */
    { "BLINK2",      gen_double_blink,12 },  /* 12% */
    { "EYE_LEFT",    gen_eye_left,    12 },  /* 12% */
    { "EYE_RIGHT",   gen_eye_right,   12 },  /* 12% */
    { "HEAD_TILT",   gen_head_tilt,   20 },  /* 20% — 观赏性好 */
    { "SCAN",        gen_scan,         8 },  /*  8% — 动作大，少些 */
    { "ARM_TWITCH",  gen_arm_twitch,   6 },  /*  6% — 最不频繁 */
};

/* 加权随机选择算法：
 *   1. 算总权重
 *   2. 随机 0~（总权重-1）
 *   3. 累加权重，首次超过随机值的位置就是选中结果
 *
 * 视觉化：把总权重画成一条线段，每个表情占一段
 *   [眨眼  ][连眨 ][左][右][歪头    ][扫 ][手]
 *   0      30     42  54  66        86  94  100
 *
 * 注意：每次调用都重新遍历求和 total，对于固定权重来说有微小浪费
 * （可以编译期求值或缓存）。但这里只有 7 个元素，影响忽略不计，
 * 保留遍历写法更清晰，且支持未来改成动态权重。 */
static int pick_weighted_idle(void) {
    uint32_t total = 0;
    for (int i = 0; i < IDLE_EXPR_COUNT; i++)
        total += s_idle_exprs[i].weight;

    uint32_t r = esp_random() % total;
    uint32_t cum = 0;
    for (int i = 0; i < IDLE_EXPR_COUNT; i++) {
        cum += s_idle_exprs[i].weight;
        if (r < cum) return i;
    }
    return 0;
}

/* ==================================================================
 * 第三部分：引擎状态与内部函数
 *
 * 这套状态变量相当于"管家的工作台"：
 *   g_state          → 管家在闲着还是忙着
 *   g_current_anim   → 手里拿着的剧本
 *   g_current_frame  → 剧本翻到第几页
 *   g_current_id     → 剧本编号（-1 表示是微表情）
 *   g_anim_queue[]   → 待办清单（最多 4 项）
 * ================================================================ */

#define ANIM_QUEUE_SIZE 4

/* 消息队列句柄 — 邮箱 */
static QueueHandle_t        g_cmd_queue       = NULL;

/* 播放状态 */
static anim_state_t         g_state           = ANIM_STATE_IDLE;
static int                  g_current_id      = -1;          /* -1 = idle 微表情 */
static const anim_definition_t *g_current_anim = NULL;       /* 指向当前播放的剧 */
static uint16_t             g_current_frame   = 0;           /* 当前帧索引 */

/* 环形队列 — 动画排队机制
 *  head == tail → 空
 *  (tail+1) % N == head → 满
 *  固定 4 槽，满了丢弃最早的排队请求 */
static int                  g_anim_queue[ANIM_QUEUE_SIZE];
static int                  g_queue_head      = 0;
static int                  g_queue_tail      = 0;

/* 空闲微表情控制 */
static bool                 g_idle_enabled    = true;          /* 默认开启 */
static uint32_t             g_idle_base_ms    = 10000;        /* 基础间隔 10s */
static uint32_t             g_idle_jitter_ms  = 15000;        /* 随机抖动 0~15s */

/* ---------- 内部辅助函数 ---------- */

/* 校验动画 ID 是否有效（0~7） */
static bool is_valid_id(int id) {
    return (id >= 0 && id < ANIM_COUNT);
}

/* 启动预定义动画：重置帧索引，切到 PLAYING 状态 */
static void start_anim(int anim_id) {
    g_current_id    = anim_id;
    g_current_anim  = &s_anim_defs[anim_id];
    g_current_frame = 0;
    g_state         = ANIM_STATE_PLAYING;
}

/* 启动空闲微表情：
 *   1. 调用生成器在 s_idle_frames 缓冲区生成帧
 *   2. 构造一个临时的 anim_definition_t（名字+帧指针+帧数）
 *   3. g_current_anim 指向这个临时定义
 *   4. 按正常 PLAYING 流程播放
 *
 *   注意：g_current_id 设为 -1，外部查询当前动画 ID 时
 *   通过 -1 知道"现在在播微表情，不是预定义动画" */
static void start_idle_anim(int idx) {
    /* 调用生成器填充帧缓冲区 */
    s_idle_exprs[idx].generate(s_idle_frames, &s_idle_frame_count);
    s_idle_runtime_def.name       = s_idle_exprs[idx].name;
    s_idle_runtime_def.keyframes  = s_idle_frames;
    s_idle_runtime_def.frame_count = (uint16_t)s_idle_frame_count;

    g_current_id    = -1;          /* 标记为内部 idle 动画 */
    g_current_anim  = &s_idle_runtime_def;
    g_current_frame = 0;
    g_state         = ANIM_STATE_PLAYING;
}

/* 入队：往环形队列尾部写入
 *  (tail+1) % N == head 时说明满了 → 丢弃并日志警告 */
static void enqueue_anim(int anim_id) {
    int next = (g_queue_tail + 1) % ANIM_QUEUE_SIZE;
    if (next == g_queue_head) {
        ESP_LOGW(TAG, "动画队列已满，丢弃 #%d", anim_id);
        return;
    }
    g_anim_queue[g_queue_tail] = anim_id;
    g_queue_tail = next;
}

/* 出队：从环形队列头部取出
 *  head == tail 时说明空了 → 返回 false */
static bool dequeue_anim(int *out_id) {
    if (g_queue_head == g_queue_tail)
        return false;
    *out_id = g_anim_queue[g_queue_head];
    g_queue_head = (g_queue_head + 1) % ANIM_QUEUE_SIZE;
    return true;
}

/* 清空队列：头尾归零即可，数据不用擦 */
static void clear_queue(void) {
    g_queue_head = 0;
    g_queue_tail = 0;
}

/* 播放中收到命令的处理器：
 *   PLAY  → 清空排队，强行打断当前动画，播新的
 *   STOP  → 清空排队，回到空闲
 *   QUEUE → 不打断，排到队尾等当前播完再说
 *
 *   为什么 PLAY 要清空排队？
 *     因为 PLAY 表达的是"立刻看这个"，
 *     之前排队的都不重要了，清掉避免干扰。 */
static void handle_cmd_while_playing(const anim_engine_cmd_t *cmd) {
    switch (cmd->type) {
        case ANIM_CMD_PLAY:
            if (!is_valid_id(cmd->anim_id)) return;
            clear_queue();
            start_anim(cmd->anim_id);
            break;

        case ANIM_CMD_STOP:
            clear_queue();
            g_state      = ANIM_STATE_IDLE;
            g_current_id = -1;
            g_current_anim = NULL;
            break;

        case ANIM_CMD_QUEUE:
            if (is_valid_id(cmd->anim_id))
                enqueue_anim(cmd->anim_id);
            break;
    }
}

/* ==================================================================
 * 第四部分：引擎主任务（核心状态机）
 *
 * 这是一个永不退出的 while(1) 循环，状态在两个模式间切换：
 *
 *   IDLE ──收到命令──→ PLAYING
 *   PLAYING ──播完且队列空──→ IDLE
 *
 * 关键技巧：
 *   用 xQueueReceive 的超时参数代替 delay()，
 *   这样每一帧的等待期都是"可打断"的——
 *   如果等待期间来了新命令，能立即响应而不是死等。
 * ================================================================ */

static void anim_engine_task(void *arg) {
    anim_engine_cmd_t cmd;

    while (1) {
        /* ---------- 空闲态 ---------- */
        if (g_state == ANIM_STATE_IDLE) {
            /* 默认：无限期等待（portMAX_DELAY = 永远等下去） */
            uint32_t wait_ticks = portMAX_DELAY;

            /* 如果空闲微表情开启，等一段时间没命令就自己加戏
             * 等待时间 = base + 随机 jitter，范围 10~25 秒
             * 随机 jitter 的目的是让动作间隔不规律，更有"活物感" */
            if (g_idle_enabled) {
                uint32_t idle_wait_ms = g_idle_base_ms
                    + (esp_random() % (g_idle_jitter_ms + 1));
                wait_ticks = pdMS_TO_TICKS(idle_wait_ms);
                if (wait_ticks == 0) wait_ticks = 1;
            }

            /* 在信箱前等待：
             *   pdTRUE → 来命令了，处理
             *   pdFALSE → 超时，播微表情 */
            BaseType_t ret = xQueueReceive(g_cmd_queue, &cmd, wait_ticks);

            if (ret == pdTRUE) {
                if ((cmd.type == ANIM_CMD_PLAY || cmd.type == ANIM_CMD_QUEUE)
                    && is_valid_id(cmd.anim_id)) {
                    start_anim(cmd.anim_id);
                }
                /* IDLE 收到 STOP → 无操作（已经在空闲了） */
            } else if (g_idle_enabled) {
                /* 超时：播一段随机微表情 */
                int idx = pick_weighted_idle();
                // ESP_LOGI(TAG, "idle → %s", s_idle_exprs[idx].name);
                start_idle_anim(idx);
            }
        }
        /* ---------- 播放态 ---------- */
        else if (g_state == ANIM_STATE_PLAYING) {
            const anim_keyframe_t *frame = &g_current_anim->keyframes[g_current_frame];

            /* 1. 发当前帧的音效（非阻塞） */
            if (frame->sound_folder >= 0 && frame->sound_file >= 0)
                DFPlayerMini_PlayFolder(frame->sound_folder, frame->sound_file);

            /* 2. 发当前帧的舵机命令（跳过 -1 的关节）
             *    walle_joint_move() 应该是非阻塞的，启动斜坡运动后立即返回 */
            for (int j = 0; j < ANIM_JOINT_COUNT; j++) {
                if (frame->joint[j] >= 0)
                    walle_joint_move(j, (float)frame->joint[j], frame->duration_ms);
            }

            /* 3. 等待这一帧持续时长，同时监听信箱
             *    这是核心设计：用 xQueueReceive 超时代替 delay()
             *    效果："最多等 duration_ms，但有命令立刻醒来"
             *
             *    如果这帧是 2000ms：
             *      - 第 500ms 来了 STOP → 立即停止，不等剩下 1500ms
             *      - 第 2000ms 没任何事  → 自动播下一帧 */
            uint32_t ticks = pdMS_TO_TICKS(frame->duration_ms);
            if (ticks == 0) ticks = 1;

            if (xQueueReceive(g_cmd_queue, &cmd, ticks) == pdTRUE) {
                /* 来了新命令 */
                handle_cmd_while_playing(&cmd);
                /* 回到循环顶部，由新 state 决定下一步 */
            } else {
                /* 超时：这一帧播完了 */
                g_current_frame++;

                if (g_current_frame >= g_current_anim->frame_count) {
                    /* 整段动画播完 */
                    int next_id;
                    if (dequeue_anim(&next_id)) {
                        /* 队列里有下一个 → 继续播，不切回 IDLE */
                        start_anim(next_id);
                    } else {
                        /* 队列空了 → 回空闲态 */
                        g_state      = ANIM_STATE_IDLE;
                        g_current_id = -1;
                        g_current_anim = NULL;
                        // ESP_LOGI(TAG, "动画队列已空，回到空闲");
                    }
                }
            }
        }
    }
}

/* ==================================================================
 * 第五部分：公开 API
 *
 * 这些函数都是"非阻塞"的——投信即返回，不等处理结果。
 * 调用者（main.c 或其他任务）不需要等管家忙完。
 * ================================================================ */

/* 初始化引擎：创建消息队列 + 创建管家任务
 *
 * 任务参数：
 *   - 栈 4096 字节（存局部变量和函数调用链，对纯控制逻辑够用）
 *   - 优先级 8（ESP-IDF 0-24，8 是中等偏上）
 *   - 固定核心 1（核心 0 通常跑 Wi-Fi/蓝牙协议栈）
 *
 * 创建失败只打日志不阻塞系统——没动画 WALL-E 也能动。 */
void anim_engine_init(void) {
    g_cmd_queue = xQueueCreate(4, sizeof(anim_engine_cmd_t));
    if (g_cmd_queue == NULL) {
        ESP_LOGE(TAG, "命令队列创建失败");
        return;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        anim_engine_task, "anim_engine", 4096, NULL, 8, NULL, 1
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "引擎任务创建失败");
        return;
    }

    ESP_LOGI(TAG, "动画引擎初始化完成，%d 个动画已加载", ANIM_COUNT);
}

/* 播放动画（打断当前）
 *   投信超时设为 0（不等待），如果队列已满立刻返回失败
 *   调用者通过返回值知道是否投信成功 */
esp_err_t anim_engine_play(int anim_id) {
    if (!is_valid_id(anim_id)) {
        ESP_LOGE(TAG, "无效动画 ID: %d", anim_id);
        return ESP_ERR_INVALID_ARG;
    }

    anim_engine_cmd_t cmd = { .type = ANIM_CMD_PLAY, .anim_id = (int8_t)anim_id };
    if (xQueueSend(g_cmd_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGE(TAG, "命令队列满，无法发送 PLAY");
        return ESP_ERR_TIMEOUT;
    }

    // ESP_LOGI(TAG, ">> PLAY [%d] %s", anim_id, s_anim_defs[anim_id].name);
    return ESP_OK;
}

/* 排队播放（当前播完后自动播这个）
 *   不打断正在播的动画，只是排到待播队列尾部 */
esp_err_t anim_engine_enqueue(int anim_id) {
    if (!is_valid_id(anim_id)) {
        ESP_LOGE(TAG, "无效动画 ID: %d", anim_id);
        return ESP_ERR_INVALID_ARG;
    }

    anim_engine_cmd_t cmd = { .type = ANIM_CMD_QUEUE, .anim_id = (int8_t)anim_id };
    if (xQueueSend(g_cmd_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGE(TAG, "命令队列满，无法发送 QUEUE");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/* 立即停止所有动画
 *   队列满时重试最多 3 次（间隔 5ms），因为 stop 必须生效以确保
 *   WebSocket 用户指令不被动画帧覆盖 */
void anim_engine_stop(void) {
    anim_engine_cmd_t cmd = { .type = ANIM_CMD_STOP, .anim_id = -1 };
    for (int i = 0; i < 3; i++) {
        if (xQueueSend(g_cmd_queue, &cmd, 0) == pdTRUE) return;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGW(TAG, "anim_engine_stop: 重试 3 次后仍无法投信（队列满）");
}

/* ----- 查询函数（直接读全局变量） ----- */

bool anim_engine_is_playing(void) {
    return (g_state == ANIM_STATE_PLAYING);
}

/* 返回 -1 表示当前在播空闲微表情 */
int anim_engine_current_id(void) {
    return g_current_id;
}

int anim_engine_get_count(void) {
    return ANIM_COUNT;
}

/* 获取指定动画的定义（给 CLI -l 列表显示用） */
const anim_definition_t *anim_engine_get_def(int anim_id) {
    if (!is_valid_id(anim_id)) return NULL;
    return &s_anim_defs[anim_id];
}

/* 开关空闲微表情 */
void anim_engine_enable_idle(bool enable) {
    g_idle_enabled = enable;
    // ESP_LOGI(TAG, "空闲微表情 %s", enable ? "已开启" : "已关闭");
}

bool anim_engine_is_idle_enabled(void) {
    return g_idle_enabled;
}
