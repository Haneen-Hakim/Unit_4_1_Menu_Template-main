/* =============================================================
 * Game 1 – SHAPES
 * Find the one shape that is different from all the others.
 *
 * Controls  : Joystick LEFT / RIGHT to move cursor
 *             BT3  – confirm selection
 *             BT2  – cycle LED brightness (30 → 60 → 90 %)
 *
 * Structure : 3 levels × 3 rounds = 9 questions total
 *   Level 1 (Easy)   – 3 large shapes, 10 s
 *   Level 2 (Medium) – 5 medium shapes, 7 s
 *   Level 3 (Hard)   – 7 small shapes, 4 s
 * ============================================================= */

#include "Game_1.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "PWM.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "rng.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

/* ---- Peripherals declared in main.c ---- */
extern ST7789V2_cfg_t   cfg0;
extern PWM_cfg_t        pwm_cfg;
extern Buzzer_cfg_t     buzzer_cfg;
extern Joystick_cfg_t   joystick_cfg;
extern Joystick_t       joystick_data;
extern volatile uint32_t g_tim6_ticks;  /* 100 Hz tick from TIM6 */

/* ---- Palette colour indices (default palette) ---- */
#define C_BLACK   0
#define C_WHITE   1
#define C_RED     2
#define C_GREEN   3
#define C_BLUE    4
#define C_ORANGE  5
#define C_YELLOW  6

/* ---- Shape type IDs ---- */
#define SHAPE_CIRCLE   0
#define SHAPE_SQUARE   1
#define SHAPE_TRIANGLE 2
#define SHAPE_DIAMOND  3

/* ---- Game constants ---- */
#define NUM_LEVELS        3u
#define ROUNDS_PER_LEVEL  3u

/* All shapes are centred on this Y row (LCD is 240×240) */
#define SHAPE_CY  118u

/* Shapes per level */
static const uint8_t  lv_num[]   = { 3,   5,   7  };
/* Shape draw size (radius / half-side in pixels) */
static const uint8_t  lv_size[]  = { 25,  17,  12 };
/* Selection-box extra margin per level (shrinks for denser levels) */
static const uint8_t  lv_margin[]= { 8,   6,   4  };
/* Countdown in TIM6 ticks  (100 ticks = 1 s) */
static const uint32_t lv_ticks[] = { 1000, 700, 400 };
/* LED brightness per level */
static const uint8_t  lv_led[]   = { 30,  60,  90 };
/* Buzzer start-tone per level (Hz) */
static const uint32_t lv_freq[]  = { 500, 750, 1000 };

/* Shape X-centres for each level (up to 7 shapes, unused slots = 0) */
static const uint16_t lv_cx[3][7] = {
    { 55,  120, 185,   0,   0,   0,   0 },   /* Level 0 – 3 shapes */
    { 24,   72, 120, 168, 216,   0,   0 },   /* Level 1 – 5 shapes */
    { 17,   51,  85, 120, 154, 188, 222 },   /* Level 2 – 7 shapes */
};

/* ================================================================
 * Internal helpers
 * ============================================================== */

/* Safe hardware RNG with HAL_GetTick() fallback */
static uint32_t rng_get(void)
{
    uint32_t v;
    if (HAL_RNG_GenerateRandomNumber(&hrng, &v) != HAL_OK)
        v = HAL_GetTick();
    return v;
}

/* Draw one shape at (cx, cy) with given size and palette colour */
static void draw_shape(uint8_t type, uint16_t cx, uint16_t cy,
                       uint8_t sz, uint8_t col)
{
    switch (type) {
        case SHAPE_CIRCLE:
            LCD_Draw_Circle(cx, cy, sz, col, 0);
            break;
        case SHAPE_SQUARE:
            LCD_Draw_Rect(cx - sz, cy - sz, sz * 2u, sz * 2u, col, 0);
            break;
        case SHAPE_TRIANGLE:
            LCD_Draw_Line(cx,      cy - sz, cx - sz, cy + sz, col);
            LCD_Draw_Line(cx - sz, cy + sz, cx + sz, cy + sz, col);
            LCD_Draw_Line(cx + sz, cy + sz, cx,      cy - sz, col);
            break;
        case SHAPE_DIAMOND:
            LCD_Draw_Line(cx,      cy - sz, cx + sz, cy,      col);
            LCD_Draw_Line(cx + sz, cy,      cx,      cy + sz, col);
            LCD_Draw_Line(cx,      cy + sz, cx - sz, cy,      col);
            LCD_Draw_Line(cx - sz, cy,      cx,      cy - sz, col);
            break;
        default:
            break;
    }
}

/* Draw all shapes and a yellow selection box around the chosen one */
static void render_shapes(uint8_t lv, uint8_t types[],
                          uint8_t sel, uint8_t sz)
{
    uint8_t n = lv_num[lv];
    for (uint8_t i = 0; i < n; i++)
        draw_shape(types[i], lv_cx[lv][i], SHAPE_CY, sz, C_WHITE);

    /* Yellow selection rectangle – margin chosen so it never goes off-screen */
    uint16_t m  = (uint16_t)(sz + lv_margin[lv]);
    uint16_t cx = lv_cx[lv][sel];
    LCD_Draw_Rect(cx - m, SHAPE_CY - m, m * 2u, m * 2u, C_YELLOW, 0);
}

/* Fill types[] so that all shapes are 'majority' except one at *odd_idx */
static void gen_question(uint8_t lv, uint8_t types[], uint8_t *odd_idx)
{
    uint8_t n        = lv_num[lv];
    uint8_t majority = (uint8_t)(rng_get() % 4u);
    /* odd_type is guaranteed ≠ majority */
    uint8_t odd_type = (uint8_t)((majority + 1u + rng_get() % 3u) % 4u);
    *odd_idx         = (uint8_t)(rng_get() % n);

    for (uint8_t i = 0; i < n; i++)
        types[i] = majority;
    types[*odd_idx] = odd_type;
}

/* ---- Buzzer helpers ---- */
static void snd_correct(void)
{
    buzzer_tone(&buzzer_cfg, 523, 60); HAL_Delay(100);
    buzzer_tone(&buzzer_cfg, 659, 60); HAL_Delay(100);
    buzzer_tone(&buzzer_cfg, 784, 60); HAL_Delay(150);
    buzzer_off(&buzzer_cfg);
}
static void snd_wrong(void)
{
    buzzer_tone(&buzzer_cfg, 200, 70); HAL_Delay(350);
    buzzer_off(&buzzer_cfg);
}
static void snd_timeout(void)
{
    buzzer_tone(&buzzer_cfg, 150, 60); HAL_Delay(500);
    buzzer_off(&buzzer_cfg);
}
static void snd_win(void)
{
    static const uint32_t melody[] = { 523, 659, 784, 1047 };
    for (int i = 0; i < 4; i++) {
        buzzer_tone(&buzzer_cfg, melody[i], 60);
        HAL_Delay(150);
    }
    buzzer_off(&buzzer_cfg);
}

/* ---- Brief feedback screens (blocking) ---- */
static void show_feedback(uint8_t correct, uint8_t odd_idx)
{
    LCD_Fill_Buffer(C_BLACK);
    if (correct) {
        LCD_printString("CORRECT!", 50, 95, C_GREEN, 2);
        snd_correct();
    } else {
        LCD_printString("WRONG!", 65, 85, C_RED, 2);
        char buf[18];
        sprintf(buf, "Odd was: #%d", odd_idx + 1u);
        LCD_printString(buf, 45, 115, C_WHITE, 1);
        snd_wrong();
    }
    LCD_Refresh(&cfg0);
    HAL_Delay(900);
}

static void show_timeout(uint8_t odd_idx)
{
    LCD_Fill_Buffer(C_BLACK);
    LCD_printString("TIME UP!", 55, 85, C_ORANGE, 2);
    char buf[18];
    sprintf(buf, "Odd was: #%d", odd_idx + 1u);
    LCD_printString(buf, 45, 115, C_WHITE, 1);
    snd_timeout();
    LCD_Refresh(&cfg0);
    HAL_Delay(1000);
}

/* ================================================================
 * Game 1 main entry point
 * Called by main.c state machine when MENU_STATE_GAME_1 is active.
 * Returns MENU_STATE_HOME when the player finishes or quits.
 * ============================================================== */
MenuState Game1_Run(void)
{
    uint8_t total_score = 0;
    uint8_t led_duty    = lv_led[0];
    PWM_SetDuty(&pwm_cfg, led_duty);

    /* ---- Intro screen ---- */
    LCD_Fill_Buffer(C_BLACK);
    LCD_printString("SHAPES",              55, 70,  C_YELLOW, 3);
    LCD_printString("Find the odd shape!", 5,  112, C_WHITE,  1);
    LCD_printString("Joystick: Move",      25, 132, C_WHITE,  1);
    LCD_printString("BT3: Select",         45, 147, C_WHITE,  1);
    LCD_printString("BT2: Brightness",     25, 162, C_WHITE,  1);
    LCD_Refresh(&cfg0);
    HAL_Delay(1800);

    static const char *lv_name[] = {
        "Level 1 - Easy",
        "Level 2 - Medium",
        "Level 3 - Hard"
    };

    /* ================================================================
     * Level loop (0, 1, 2)
     * ============================================================== */
    for (uint8_t lv = 0; lv < NUM_LEVELS; lv++) {

        /* LED brightness and start beep for this level */
        led_duty = lv_led[lv];
        PWM_SetDuty(&pwm_cfg, led_duty);
        buzzer_tone(&buzzer_cfg, lv_freq[lv], 40);
        HAL_Delay(120);
        buzzer_off(&buzzer_cfg);

        /* Level intro */
        LCD_Fill_Buffer(C_BLACK);
        LCD_printString("SHAPES",    55, 68,  C_YELLOW, 3);
        LCD_printString(lv_name[lv], 15, 115, C_WHITE,  1);
        LCD_Refresh(&cfg0);
        HAL_Delay(1200);

        uint8_t lv_score = 0;

        /* ==============================================================
         * Round loop (0, 1, 2)
         * ============================================================ */
        for (uint8_t rnd = 0; rnd < ROUNDS_PER_LEVEL; rnd++) {

            /* Generate question */
            uint8_t shape_types[7];
            uint8_t odd_idx;
            gen_question(lv, shape_types, &odd_idx);

            uint8_t   sel      = 0;
            Direction last_dir = CENTRE;
            uint32_t  t_start  = g_tim6_ticks;
            uint32_t  t_limit  = lv_ticks[lv];
            uint8_t   sz       = lv_size[lv];
            uint8_t   n        = lv_num[lv];

            /* ---- Game loop ---- */
            while (1) {
                uint32_t frame_ms = HAL_GetTick();

                /* --- INPUT --- */
                Input_Read();
                Joystick_Read(&joystick_cfg, &joystick_data);
                Direction dir = joystick_data.direction;

                /* BT2 – cycle LED brightness */
                if (current_input.btn2_pressed) {
                    led_duty = (led_duty >= 90u) ? 30u : led_duty + 30u;
                    PWM_SetDuty(&pwm_cfg, led_duty);
                }

                /* Joystick LEFT / RIGHT – debounced */
                if (dir == E && last_dir != E && sel < n - 1u) sel++;
                if (dir == W && last_dir != W && sel > 0u)      sel--;
                last_dir = dir;

                /* BT3 – confirm answer */
                if (current_input.btn3_pressed) {
                    uint8_t ok = (sel == odd_idx);
                    if (ok) { total_score++; lv_score++; }
                    show_feedback(ok, odd_idx);
                    break;
                }

                /* Timer check */
                uint32_t elapsed = g_tim6_ticks - t_start;
                if (elapsed >= t_limit) {
                    show_timeout(odd_idx);
                    break;
                }

                uint32_t rem_s = (t_limit - elapsed + 99u) / 100u;

                /* --- RENDER --- */
                LCD_Fill_Buffer(C_BLACK);

                /* Header: level name + timer + score */
                LCD_printString(lv_name[lv], 5, 3, C_WHITE, 1);

                char tbuf[14];
                sprintf(tbuf, "Time: %lus", rem_s);
                LCD_printString(tbuf, 5, 16,
                    (rem_s <= 2u) ? C_RED : C_YELLOW, 2);

                char sbuf[22];
                sprintf(sbuf, "Score:%lu/9  Rnd:%lu/3",
                        (unsigned long)total_score,
                        (unsigned long)(rnd + 1u));
                LCD_printString(sbuf, 5, 44, C_WHITE, 1);

                LCD_printString("Find the ODD shape!", 5, 58, C_GREEN, 1);

                /* Shapes + selection highlight */
                render_shapes(lv, shape_types, sel, sz);

                /* Current selection indicator */
                char nbuf[12];
                sprintf(nbuf, "< %lu / %lu >",
                        (unsigned long)(sel + 1u),
                        (unsigned long)n);
                LCD_printString(nbuf, 70, 168, C_YELLOW, 1);

                /* Controls hint */
                LCD_printString("Joy:Move  BT3:Select", 5,  185, C_WHITE, 1);
                LCD_printString("BT2: Brightness",      38, 200, C_WHITE, 1);

                LCD_Refresh(&cfg0);

                /* Frame cap ~30 ms */
                uint32_t ft = HAL_GetTick() - frame_ms;
                if (ft < 30u) HAL_Delay(30u - ft);
            }
        } /* rounds */

        /* Level complete summary */
        LCD_Fill_Buffer(C_BLACK);
        LCD_printString("Level Done!", 35, 85, C_GREEN, 2);
        char lbuf[20];
        sprintf(lbuf, "Score this level: %lu/3", (unsigned long)lv_score);
        LCD_printString(lbuf, 5, 120, C_WHITE, 1);
        LCD_Refresh(&cfg0);
        HAL_Delay(1400);

    } /* levels */

    /* ================================================================
     * Game-over screen
     * ============================================================== */
    LCD_Fill_Buffer(C_BLACK);
    LCD_printString("GAME OVER",   35, 48,  C_YELLOW, 2);
    LCD_printString("SHAPES",      70, 76,  C_WHITE,  2);
    char fbuf[18];
    sprintf(fbuf, "Score: %lu / 9", (unsigned long)total_score);
    LCD_printString(fbuf, 30, 110, C_GREEN, 2);

    if (total_score >= 7u) {
        LCD_printString("EXCELLENT!", 35, 148, C_YELLOW, 2);
        snd_win();
    } else if (total_score >= 4u) {
        LCD_printString("GOOD JOB!",  40, 148, C_GREEN,  2);
        buzzer_tone(&buzzer_cfg, 523, 50); HAL_Delay(250);
        buzzer_off(&buzzer_cfg);
    } else {
        LCD_printString("TRY AGAIN!", 35, 148, C_RED,    2);
        buzzer_tone(&buzzer_cfg, 280, 60); HAL_Delay(350);
        buzzer_off(&buzzer_cfg);
    }

    LCD_printString("BT3 to return", 30, 210, C_WHITE, 1);
    LCD_Refresh(&cfg0);

    /* Wait for BT3 to return to menu */
    while (1) {
        Input_Read();
        if (current_input.btn3_pressed) break;
        HAL_Delay(30);
    }

    /* Restore defaults before handing control back */
    PWM_SetDuty(&pwm_cfg, 50);
    return MENU_STATE_HOME;
}
