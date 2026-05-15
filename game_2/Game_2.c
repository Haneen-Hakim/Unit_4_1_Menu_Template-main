/* =============================================================
 * Game 2 – COLOURS
 * Choose the coloured box that matches the displayed colour word.
 *
 * Controls  : Joystick LEFT / RIGHT to move cursor between boxes
 *             BT3 – confirm selection
 *             BT2 – cycle LED brightness (30 → 60 → 90 %)
 *
 * Structure : 3 levels × 3 rounds = 9 questions total
 *   Level 1 (Easy)   – 3 boxes, word colour matches meaning, 10 s
 *   Level 2 (Medium) – 5 boxes, word colour matches meaning, 7 s
 *   Level 3 (Stroop) – 5 boxes, word drawn in WRONG colour –
 *                       player must pick the colour NAMED, 4 s
 * ============================================================= */

#include "Game_2.h"
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
extern volatile uint32_t g_tim6_ticks;

/* ---- Palette colour indices ---- */
#define C_BLACK   0
#define C_WHITE   1
#define C_RED     2
#define C_GREEN   3
#define C_BLUE    4
#define C_ORANGE  5
#define C_YELLOW  6

/* ---- The 6 game colours (palette index + display name) ---- */
#define NUM_GAME_COLORS 6u
static const uint8_t color_pal[]  = { 2, 3, 4, 5, 6, 1 };
/*                                    R  G  B  O  Y  W     */
static const char *color_name[]   = { "RED","GREEN","BLUE",
                                       "ORANGE","YELLOW","WHITE" };

/* ---- Game constants ---- */
#define NUM_LEVELS        3u
#define ROUNDS_PER_LEVEL  3u

/* Number of coloured boxes per level */
static const uint8_t  lv_boxes[]  = { 3, 5, 5 };
/* Time limit in TIM6 ticks (100 Hz) */
static const uint32_t lv_ticks[]  = { 1000, 700, 400 };
/* LED brightness per level */
static const uint8_t  lv_led[]    = { 30, 60, 90 };
/* Buzzer start-tone per level (Hz) */
static const uint32_t lv_freq[]   = { 500, 750, 1000 };

/* ---- Box layout constants ---- */
/* Level 0 – 3 boxes: width=60, gap=20, first box x=10 */
#define BOX3_W    60u
#define BOX3_GAP  20u
#define BOX3_X0   10u
/* Level 1 / 2 – 5 boxes: width=36, gap=12, first box x=6 */
#define BOX5_W    36u
#define BOX5_GAP  12u
#define BOX5_X0    6u
/* Common vertical layout */
#define BOX_Y    115u   /* top of coloured boxes   */
#define BOX_H     60u   /* height of coloured boxes */

/* ================================================================
 * Internal helpers
 * ============================================================== */

static uint32_t rng_get(void)
{
    uint32_t v;
    if (HAL_RNG_GenerateRandomNumber(&hrng, &v) != HAL_OK)
        v = HAL_GetTick();
    return v;
}

/* Return left-edge X of box b for the given level */
static uint16_t box_x(uint8_t lv, uint8_t b)
{
    if (lv == 0u)
        return (uint16_t)(BOX3_X0 + b * (BOX3_W + BOX3_GAP));
    return (uint16_t)(BOX5_X0 + b * (BOX5_W + BOX5_GAP));
}

static uint16_t box_w(uint8_t lv)
{
    return (lv == 0u) ? (uint16_t)BOX3_W : (uint16_t)BOX5_W;
}

/* Generate a colour round:
 *   target    – index into color_pal/color_name for the word shown
 *   boxes[]   – colour index of each box
 *   correct   – which box position holds the target colour  */
static void gen_colour_q(uint8_t nb, uint8_t *target,
                         uint8_t *boxes, uint8_t *correct)
{
    *target  = (uint8_t)(rng_get() % NUM_GAME_COLORS);
    *correct = (uint8_t)(rng_get() % nb);

    /* Build pool of 5 colours that are NOT the target, then shuffle */
    uint8_t avail[5], na = 0;
    for (uint8_t i = 0; i < NUM_GAME_COLORS; i++)
        if (i != *target) avail[na++] = i;

    for (uint8_t i = na - 1u; i > 0u; i--) {
        uint8_t j  = (uint8_t)(rng_get() % (i + 1u));
        uint8_t tmp = avail[i]; avail[i] = avail[j]; avail[j] = tmp;
    }

    /* Assign: correct box = target, others = shuffled alternatives */
    uint8_t ai = 0;
    for (uint8_t b = 0; b < nb; b++)
        boxes[b] = (b == *correct) ? *target : avail[ai++];
}

/* Draw all coloured boxes + yellow selection border */
static void render_boxes(uint8_t lv, uint8_t boxes[], uint8_t sel)
{
    uint8_t  nb = lv_boxes[lv];
    uint16_t bw = box_w(lv);

    for (uint8_t b = 0; b < nb; b++) {
        uint16_t bx = box_x(lv, b);
        /* Filled colour box */
        LCD_Draw_Rect(bx, BOX_Y, bw, BOX_H, color_pal[boxes[b]], 1);
        /* White outline */
        LCD_Draw_Rect(bx, BOX_Y, bw, BOX_H, C_WHITE, 0);
    }

    /* Yellow double-border around selected box */
    uint16_t sx = box_x(lv, sel);
    LCD_Draw_Rect(sx - 3u, BOX_Y - 3u, bw + 6u,  BOX_H + 6u,  C_YELLOW, 0);
    LCD_Draw_Rect(sx - 4u, BOX_Y - 4u, bw + 8u,  BOX_H + 8u,  C_YELLOW, 0);
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

/* ---- Feedback screens (blocking) ---- */
static void show_feedback(uint8_t correct, uint8_t target)
{
    LCD_Fill_Buffer(C_BLACK);
    if (correct) {
        LCD_printString("CORRECT!", 50, 90, C_GREEN, 2);
        /* Show the colour name in its own colour */
        LCD_printString(color_name[target], 80, 120,
                        color_pal[target], 1);
        snd_correct();
    } else {
        LCD_printString("WRONG!", 65, 80, C_RED, 2);
        char buf[20];
        sprintf(buf, "Was: %s", color_name[target]);
        LCD_printString(buf, 40, 112, C_WHITE, 1);
        snd_wrong();
    }
    LCD_Refresh(&cfg0);
    HAL_Delay(900);
}

static void show_timeout(uint8_t target)
{
    LCD_Fill_Buffer(C_BLACK);
    LCD_printString("TIME UP!", 55, 80, C_ORANGE, 2);
    char buf[20];
    sprintf(buf, "Was: %s", color_name[target]);
    LCD_printString(buf, 40, 112, C_WHITE, 1);
    snd_timeout();
    LCD_Refresh(&cfg0);
    HAL_Delay(1000);
}

/* ================================================================
 * Game 2 main entry point
 * ============================================================== */
MenuState Game2_Run(void)
{
    uint8_t total_score = 0;
    uint8_t led_duty    = lv_led[0];
    PWM_SetDuty(&pwm_cfg, led_duty);

    /* ---- Intro screen ---- */
    LCD_Fill_Buffer(C_BLACK);
    LCD_printString("COLOURS",          48, 68,  C_YELLOW, 3);
    LCD_printString("Match the colour!", 10, 110, C_WHITE,  1);
    LCD_printString("Joystick: Move",   25, 130, C_WHITE,  1);
    LCD_printString("BT3: Select",      45, 145, C_WHITE,  1);
    LCD_printString("BT2: Brightness",  25, 160, C_WHITE,  1);
    LCD_Refresh(&cfg0);
    HAL_Delay(1800);

    static const char *lv_name[] = {
        "Level 1 - Easy",
        "Level 2 - Medium",
        "Level 3 - Stroop"
    };

    /* ================================================================
     * Level loop
     * ============================================================== */
    for (uint8_t lv = 0; lv < NUM_LEVELS; lv++) {

        led_duty = lv_led[lv];
        PWM_SetDuty(&pwm_cfg, led_duty);
        buzzer_tone(&buzzer_cfg, lv_freq[lv], 40);
        HAL_Delay(120);
        buzzer_off(&buzzer_cfg);

        /* Level intro */
        LCD_Fill_Buffer(C_BLACK);
        LCD_printString("COLOURS",    48, 65,  C_YELLOW, 3);
        LCD_printString(lv_name[lv], 15, 112,  C_WHITE,  1);
        if (lv == 2u)
            LCD_printString("Pick the MEANING!", 10, 128, C_ORANGE, 1);
        LCD_Refresh(&cfg0);
        HAL_Delay(1400);

        uint8_t lv_score = 0;

        /* ================================================================
         * Round loop
         * ============================================================== */
        for (uint8_t rnd = 0; rnd < ROUNDS_PER_LEVEL; rnd++) {

            uint8_t target, correct_box;
            uint8_t boxes[5];
            uint8_t nb = lv_boxes[lv];
            gen_colour_q(nb, &target, boxes, &correct_box);

            /* Level 3 Stroop: display word in a DIFFERENT palette colour */
            uint8_t text_pal = C_WHITE;
            if (lv == 2u) {
                uint8_t diff;
                do {
                    diff = (uint8_t)(rng_get() % NUM_GAME_COLORS);
                } while (diff == target);
                text_pal = color_pal[diff];
            }

            uint8_t   sel      = 0;
            Direction last_dir = CENTRE;
            uint32_t  t_start  = g_tim6_ticks;
            uint32_t  t_limit  = lv_ticks[lv];

            /* ---- Game loop ---- */
            while (1) {
                uint32_t frame_ms = HAL_GetTick();

                /* --- INPUT --- */
                Input_Read();
                Joystick_Read(&joystick_cfg, &joystick_data);
                Direction dir = joystick_data.direction;

                if (current_input.btn2_pressed) {
                    led_duty = (led_duty >= 90u) ? 30u : led_duty + 30u;
                    PWM_SetDuty(&pwm_cfg, led_duty);
                }

                if (dir == E && last_dir != E && sel < nb - 1u) sel++;
                if (dir == W && last_dir != W && sel > 0u)       sel--;
                last_dir = dir;

                if (current_input.btn3_pressed) {
                    uint8_t ok = (sel == correct_box);
                    if (ok) { total_score++; lv_score++; }
                    show_feedback(ok, target);
                    break;
                }

                uint32_t elapsed = g_tim6_ticks - t_start;
                if (elapsed >= t_limit) {
                    show_timeout(target);
                    break;
                }

                uint32_t rem_s = (t_limit - elapsed + 99u) / 100u;

                /* --- RENDER --- */
                LCD_Fill_Buffer(C_BLACK);

                LCD_printString(lv_name[lv], 5, 3, C_WHITE, 1);

                char tbuf[14];
                sprintf(tbuf, "Time: %lus", rem_s);
                LCD_printString(tbuf, 5, 16,
                    (rem_s <= 2u) ? C_RED : C_YELLOW, 2);

                char sbuf[22];
                sprintf(sbuf, "Score:%lu/9  R:%lu/3",
                        (unsigned long)total_score,
                        (unsigned long)(rnd + 1u));
                LCD_printString(sbuf, 5, 44, C_WHITE, 1);

                /* Instruction line */
                if (lv == 2u)
                    LCD_printString("Pick MEANING:", 5, 59, C_ORANGE, 1);
                else
                    LCD_printString("Match this:", 30, 59, C_WHITE,  1);

                /* Colour word – Level 3: rendered in distractor colour */
                LCD_printString(color_name[target], 72, 73, text_pal, 2);

                /* Coloured boxes + selection highlight */
                render_boxes(lv, boxes, sel);

                /* Box number labels below each box */
                uint16_t bw = box_w(lv);
                for (uint8_t b = 0; b < nb; b++) {
                    char num[3];
                    sprintf(num, "%u", (unsigned)(b + 1u));
                    uint16_t bx = box_x(lv, b);
                    LCD_printString(num,
                                    bx + bw / 2u - 3u,
                                    BOX_Y + BOX_H + 4u,
                                    C_WHITE, 1);
                }

                /* Current selection indicator */
                char nbuf[12];
                sprintf(nbuf, "< %lu / %lu >",
                        (unsigned long)(sel + 1u),
                        (unsigned long)nb);
                LCD_printString(nbuf, 72, 192, C_YELLOW, 1);

                LCD_printString("Joy:Move  BT3:Select", 5,  207, C_WHITE, 1);
                LCD_printString("BT2: Brightness",      38, 222, C_WHITE, 1);

                LCD_Refresh(&cfg0);

                uint32_t ft = HAL_GetTick() - frame_ms;
                if (ft < 30u) HAL_Delay(30u - ft);
            }
        } /* rounds */

        /* Level complete */
        LCD_Fill_Buffer(C_BLACK);
        LCD_printString("Level Done!", 35, 85, C_GREEN, 2);
        char lbuf[22];
        sprintf(lbuf, "Score this level: %lu/3", (unsigned long)lv_score);
        LCD_printString(lbuf, 5, 118, C_WHITE, 1);
        LCD_Refresh(&cfg0);
        HAL_Delay(1400);

    } /* levels */

    /* ================================================================
     * Game-over screen
     * ============================================================== */
    LCD_Fill_Buffer(C_BLACK);
    LCD_printString("GAME OVER", 35, 48,  C_YELLOW, 2);
    LCD_printString("COLOURS",   55, 76,  C_WHITE,  2);
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

    while (1) {
        Input_Read();
        if (current_input.btn3_pressed) break;
        HAL_Delay(30);
    }

    PWM_SetDuty(&pwm_cfg, 50);
    return MENU_STATE_HOME;
}
