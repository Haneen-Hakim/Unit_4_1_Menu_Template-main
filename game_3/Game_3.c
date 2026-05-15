/* =============================================================
 * Game 3 – WORD SEARCH  (Odd Word Out)
 * Find the one word that does NOT belong to the group.
 *
 * Controls  : Joystick UP / DOWN to move cursor between words
 *             BT3 – confirm selection
 *             BT2 – cycle LED brightness (30 → 60 → 90 %)
 *
 * Structure : 3 levels × 3 rounds = 9 questions total
 *   Level 1 (Easy)   – 3 words, obvious categories, 10 s
 *   Level 2 (Medium) – 5 words, harder categories, 7 s
 *   Level 3 (Hard)   – 5 similar-looking words, 4 s
 *
 * Each level has a bank of 5 questions; 3 are chosen randomly
 * per play-through so the order varies each game.
 * ============================================================= */

#include "Game_3.h"
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
#define C_ORANGE  5
#define C_YELLOW  6

/* ---- Game constants ---- */
#define NUM_LEVELS        3u
#define ROUNDS_PER_LEVEL  3u
#define BANK_SIZE         5u   /* questions available per level */

/* ================================================================
 * Question data
 * ============================================================== */
typedef struct {
    const char *words[5];
    uint8_t     num_words;
    uint8_t     odd_idx;    /* index of the word that does NOT belong */
} WordQ;

/* --- Level 1: 3 words, obvious categories --- */
static const WordQ q_lv1[BANK_SIZE] = {
    { { "Apple",  "Banana", "Car"    }, 3, 2 },  /* Car – not a fruit  */
    { { "Dog",    "Table",  "Cat"    }, 3, 1 },  /* Table – not animal */
    { { "Red",    "Tiger",  "Blue"   }, 3, 1 },  /* Tiger – not colour */
    { { "Mango",  "Bus",    "Train"  }, 3, 0 },  /* Mango – not vehicle*/
    { { "Circle", "Egypt",  "Square" }, 3, 1 },  /* Egypt – not shape  */
};

/* --- Level 2: 5 words, harder mix --- */
static const WordQ q_lv2[BANK_SIZE] = {
    { { "Tiger",   "Lion",   "Eagle",  "Wolf",   "Tomato"  }, 5, 4 },
      /* Tomato – not an animal */
    { { "Bus",     "Car",    "Paris",  "Train",  "Boat"    }, 5, 2 },
      /* Paris – not a vehicle */
    { { "Apple",   "Mango",  "Grape",  "Hammer", "Lemon"   }, 5, 3 },
      /* Hammer – not a fruit */
    { { "Red",     "Blue",   "Knife",  "Green",  "Yellow"  }, 5, 2 },
      /* Knife – not a colour */
    { { "England", "France", "Table",  "Italy",  "Spain"   }, 5, 2 },
      /* Table – not a country */
};

/* --- Level 3: 5 words, similar categories (hard) --- */
static const WordQ q_lv3[BANK_SIZE] = {
    { { "Tiger",  "Lion",   "Carrot",     "Leopard", "Cheetah"  }, 5, 2 },
      /* Carrot – not a big cat */
    { { "Apple",  "Potato", "Mango",      "Grape",   "Orange"   }, 5, 1 },
      /* Potato – vegetable, not fruit */
    { { "Soccer", "Tennis", "Rice",       "Basket",  "Golf"     }, 5, 2 },
      /* Rice – not a sport */
    { { "Cairo",  "London", "Paris",      "Hammer",  "Rome"     }, 5, 3 },
      /* Hammer – not a capital city */
    { { "Shark",  "Eagle",  "Dolphin",    "Salmon",  "Whale"    }, 5, 1 },
      /* Eagle – not a sea creature */
};

static const WordQ *const level_bank[3] = { q_lv1, q_lv2, q_lv3 };

/* Time limits in TIM6 ticks (100 Hz) */
static const uint32_t lv_ticks[] = { 1000, 700, 400 };
/* LED brightness per level */
static const uint8_t  lv_led[]   = { 30,   60,  90 };
/* Buzzer start-tone per level (Hz) */
static const uint32_t lv_freq[]  = { 500,  750, 1000 };

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

/* Pick ROUNDS_PER_LEVEL distinct indices from [0, BANK_SIZE) */
static void pick_questions(uint8_t out[ROUNDS_PER_LEVEL])
{
    uint8_t used[BANK_SIZE] = { 0 };
    for (uint8_t i = 0; i < ROUNDS_PER_LEVEL; i++) {
        uint8_t q;
        do { q = (uint8_t)(rng_get() % BANK_SIZE); } while (used[q]);
        used[q] = 1;
        out[i]  = q;
    }
}

/* Draw the word list; highlighted word shown in yellow with ">" */
static void render_words(const WordQ *q, uint8_t sel)
{
    uint8_t  n        = q->num_words;
    uint16_t y_start  = (n == 3u) ? 95u  : 88u;
    uint16_t y_space  = (n == 3u) ? 30u  : 24u;

    for (uint8_t i = 0; i < n; i++) {
        uint16_t wy = y_start + (uint16_t)(i * y_space);
        if (i == sel) {
            LCD_printString(">",         5,  wy, C_YELLOW, 2);
            LCD_printString(q->words[i], 28, wy, C_YELLOW, 2);
        } else {
            LCD_printString(q->words[i], 28, wy, C_WHITE,  2);
        }
    }
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
static void show_feedback(uint8_t correct, const WordQ *q)
{
    LCD_Fill_Buffer(C_BLACK);
    if (correct) {
        LCD_printString("CORRECT!", 50, 95, C_GREEN, 2);
        snd_correct();
    } else {
        LCD_printString("WRONG!", 65, 82, C_RED, 2);
        char buf[22];
        sprintf(buf, "Odd: %s", q->words[q->odd_idx]);
        LCD_printString(buf, 10, 114, C_WHITE, 1);
        snd_wrong();
    }
    LCD_Refresh(&cfg0);
    HAL_Delay(900);
}

static void show_timeout(const WordQ *q)
{
    LCD_Fill_Buffer(C_BLACK);
    LCD_printString("TIME UP!", 55, 82, C_ORANGE, 2);
    char buf[22];
    sprintf(buf, "Odd: %s", q->words[q->odd_idx]);
    LCD_printString(buf, 10, 114, C_WHITE, 1);
    snd_timeout();
    LCD_Refresh(&cfg0);
    HAL_Delay(1000);
}

/* ================================================================
 * Game 3 main entry point
 * ============================================================== */
MenuState Game3_Run(void)
{
    uint8_t total_score = 0;
    uint8_t led_duty    = lv_led[0];
    PWM_SetDuty(&pwm_cfg, led_duty);

    /* ---- Intro screen ---- */
    LCD_Fill_Buffer(C_BLACK);
    LCD_printString("WORD SEARCH",       20, 65,  C_YELLOW, 2);
    LCD_printString("Find the odd word!", 5,  98,  C_WHITE,  1);
    LCD_printString("Joystick: Up/Down", 15, 118, C_WHITE,  1);
    LCD_printString("BT3: Select",       45, 133, C_WHITE,  1);
    LCD_printString("BT2: Brightness",   25, 148, C_WHITE,  1);
    LCD_Refresh(&cfg0);
    HAL_Delay(1800);

    static const char *lv_name[] = {
        "Level 1 - Easy",
        "Level 2 - Medium",
        "Level 3 - Hard"
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
        LCD_printString("WORD SEARCH", 20, 65,  C_YELLOW, 2);
        LCD_printString(lv_name[lv],   15, 105, C_WHITE,  1);
        LCD_Refresh(&cfg0);
        HAL_Delay(1200);

        uint8_t lv_score = 0;

        /* Select 3 unique questions from the bank for this level */
        uint8_t qidx[ROUNDS_PER_LEVEL];
        pick_questions(qidx);

        /* ================================================================
         * Round loop
         * ============================================================== */
        for (uint8_t rnd = 0; rnd < ROUNDS_PER_LEVEL; rnd++) {

            const WordQ *q = &level_bank[lv][qidx[rnd]];
            uint8_t  n     = q->num_words;

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

                /* Joystick UP / DOWN – debounced */
                if (dir == N && last_dir != N && sel > 0u)      sel--;
                if (dir == S && last_dir != S && sel < n - 1u)  sel++;
                last_dir = dir;

                if (current_input.btn3_pressed) {
                    uint8_t ok = (sel == q->odd_idx);
                    if (ok) { total_score++; lv_score++; }
                    show_feedback(ok, q);
                    break;
                }

                uint32_t elapsed = g_tim6_ticks - t_start;
                if (elapsed >= t_limit) {
                    show_timeout(q);
                    break;
                }

                uint32_t rem_s = (t_limit - elapsed + 99u) / 100u;

                /* --- RENDER --- */
                LCD_Fill_Buffer(C_BLACK);

                /* Header */
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

                LCD_printString("Find the odd word!", 8, 58, C_GREEN, 1);

                /* Word list */
                render_words(q, sel);

                /* Controls */
                uint16_t ctrl_y = (n == 3u) ? 190u : 212u;
                LCD_printString("Joy:Up/Dn BT3:Select", 5,
                                ctrl_y,       C_WHITE, 1);
                LCD_printString("BT2: Brightness", 38,
                                ctrl_y + 15u, C_WHITE, 1);

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
    LCD_printString("GAME OVER",   35, 48,  C_YELLOW, 2);
    LCD_printString("WORD SEARCH", 20, 76,  C_WHITE,  2);
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
