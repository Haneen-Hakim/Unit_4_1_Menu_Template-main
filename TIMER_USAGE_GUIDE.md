# Timer Usage Guide

## Available Timers

The system provides several timers for different purposes:

### TIM2 (PWM - Buzzer)
- **Purpose**: PWM for buzzer control
- **Configuration**: 1MHz tick frequency
- **Usage**: Managed by Buzzer library (`buzzer_cfg`)
- **Status**: Already initialized and managed

### TIM4 (PWM - LED)
- **Purpose**: PWM for LED control
- **Configuration**: 1MHz tick frequency
- **Usage**: Managed by PWM library (`pwm_cfg`)
- **Status**: Already initialized and managed

### TIM6 (General Purpose Timer - Fast)
- **Purpose**: General purpose timer with interrupt
- **Frequency**: 100Hz (10ms period)
- **Configuration**: Prescaler=7999, Period=99
- **Calculation**: 80MHz / (7999+1) / (99+1) = 100Hz
- **Status**: Initialized and started in `main.c`
- **Use cases**: 
    - Fixed-rate game updates
    - Debounced periodic checks
    - Short interval counters and cooldowns

### TIM7 (General Purpose Timer - Slow)
- **Purpose**: General purpose timer with interrupt
- **Frequency**: 1Hz (1000ms / 1s period)
- **Configuration**: Prescaler=7999, Period=9999
- **Calculation**: 80MHz / (7999+1) / (9999+1) = 1Hz
- **Status**: Initialized, optional start is commented in `main.c`
- **Use cases**:
  - Countdown timers
  - Score updates every second
  - Game state changes
  - Periodic score increments

---

## Recommended Timer Pattern (Centralized ISR + Game Loop Logic)

`main.c` provides a single `HAL_TIM_PeriodElapsedCallback()` for TIM6/TIM7.

Students should:
1. **Not** define their own `HAL_TIM_PeriodElapsedCallback` in game files.
2. Use timer tick counters/flags to trigger work in the game loop.
3. Keep ISR work minimal (set flags/counters only).

### Minimal Game Example (copy/paste)

```c
#include "tim.h"

extern volatile uint32_t g_tim6_ticks;

MenuState Game1_Run(void) {
    uint32_t last_tick = g_tim6_ticks;

    while (1) {
        Input_Read();

        if (current_input.btn3_pressed) {
            break;
        }

        // Run update every 10ms when TIM6 advances
        if (g_tim6_ticks != last_tick) {
            last_tick = g_tim6_ticks;

            // game update work here...
        }

        // render work here...
    }

    return MENU_STATE_HOME;
}
```

At 100Hz tick, **1 tick = 10ms**.
Example: update every 5 ticks for a 50ms fixed-step loop.

---

## Important Notes

### ⚠️ Always Stop Timers!
**Only stop timers that your game explicitly started.**

In this template, TIM6 is started in `main.c` and remains active globally.
TIM7 is optional and can be started/stopped by a game if needed.

```c
if (game_over) {
    HAL_TIM_Base_Stop_IT(&htim7);  // Example: stop optional timer you started
    break;
}
```

### ⚠️ Callback Best Practices
1. Keep callback code **SHORT and FAST**
2. Use `volatile` for variables shared with interrupts
3. Set flags in callback, do heavy work in main loop
4. Don't call LCD functions or long delays in callbacks

### ⚠️ Shared Callback
Both TIM6 and TIM7 use the **same** callback function `HAL_TIM_PeriodElapsedCallback()`. Use `if (htim == &htim6)` to distinguish which timer triggered.

### Multiple Games
If different students use different timers:
- Game 1 uses TIM6 (fast events)
- Game 2 uses TIM7 (slow countdown)
- Game 3 uses both

Each game is responsible for starting/stopping its own timers!

---

## Timer Calculations

If you want to modify timer frequencies, use this formula:

```
Interrupt Frequency = System Clock / (Prescaler + 1) / (Period + 1)
```

For STM32L476 at 80MHz:
- **TIM6**: 80MHz / 8000 / 100 = 100Hz (10ms)
- **TIM7**: 80MHz / 8000 / 10000 = 1Hz (1000ms)

To change frequencies, modify in STM32CubeMX and regenerate code.
