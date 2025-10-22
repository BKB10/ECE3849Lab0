
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string>

extern "C" {
#include "driverlib/fpu.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "driverlib/timer.h"
#include "inc/hw_memmap.h"
#include "Crystalfontz128x128_ST7735.h"
#include "grlib/grlib.h"
#include "sysctl_pll.h"
}

#include "button.h"
#include "timerLib.h"
#include "elapsedTime.h"

// ===== Global configuration =====
static constexpr uint32_t BUTTON_TICK_MS     = 20U;
static constexpr uint32_t DISPLAY_REFRESH_MS = 50U;

uint32_t gSystemClock = 0;
volatile uint32_t gStopwatchMs = 0;
volatile bool gRunning = false;

// ============================================================================
// STRUCT: Simple GUI Button (for drawing)
// ============================================================================
struct MyButton {
    int x, y, w, h;
    const char* label;
    bool pressed;
};

// One on-screen button: Play / Pause
static MyButton btnStart = {0, 80, 50, 28, "PLAY", false};
static MyButton guiBtnReset = {60, 80, 50, 28, "RESET", false};

// ============================================================================
// Hardware button
// ============================================================================
static Button btnPlayPause(S1);  // S1 → Play/Pause
static Button btnReset(S2);  // S2 → second button

// ============================================================================
// Function prototypes
// ============================================================================
static void initializeDisplay(tContext &context);
static void configureTimer(Timer &timer);
static void setupButtons();
static void drawStopwatchScreen(tContext &context, uint32_t currentMs, bool running);
static void drawButton(tContext &context, const MyButton &btn);

static void onPlayPauseClick();
static void onPlayPauseRelease();
static void onResetClick();
static void onResetRelease();

// ============================================================================
// MAIN PROGRAM
// ============================================================================
int main(void)
{
    IntMasterDisable();
    FPUEnable();
    FPULazyStackingEnable();

    gSystemClock = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN |SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480,120000000);

    tContext sContext;
    initializeDisplay(sContext);

    Timer timer;
    configureTimer(timer);

    elapsedMillis buttonTick(timer);
    elapsedMillis displayTick(timer);
    elapsedMillis stopwatchTick(timer);

    setupButtons();
    IntMasterEnable();

    // Timer stuff
    uint32_t lastDisplayedSec = static_cast<uint32_t>(-1);
    uint32_t lastDisplayedMin = static_cast<uint32_t>(-1);
    uint32_t lastDisplayedHr = static_cast<uint32_t>(-1);
    uint32_t lastDisplayedMS = static_cast<uint32_t>(-1);

    bool lastRunning = !gRunning;

    while (true) {
        // --- Poll physical ` ---
        if (buttonTick >= BUTTON_TICK_MS) {
            btnPlayPause.tick();
            btnReset.tick();
            buttonTick = 0;
        }

        // --- Handle Play/Pause button ---
        if (btnPlayPause.wasPressed()) {
            btnStart.pressed = true;
            onPlayPauseClick();
        }
        if (btnPlayPause.wasReleased()) {
            btnStart.pressed = false;
            onPlayPauseRelease();
        }

        if (btnReset.wasPressed()) {
            guiBtnReset.pressed = true;
            onResetClick();
        }
        if (btnReset.wasReleased()) {
            guiBtnReset.pressed = false;
            onResetRelease();
        }

        // --- Stopwatch logic ---
        if (gRunning) {
            uint32_t delta = stopwatchTick;
            if (delta > 0U) {
                gStopwatchMs += delta;
                stopwatchTick = 0;
            }
        } else {
            stopwatchTick = 0;
        }

        // --- Update screen if needed ---
        // Seconds
        uint32_t currentSec = gStopwatchMs / 1000U;
        uint32_t currentMin = gStopwatchMs / 60000U;
        unint32_t currentHr = gStopwatchMs / 3600000U;
        uint32_t currentMS = gStopwatchMs;

        if ((currentSec != lastDisplayedSec) ||
            (gRunning != lastRunning) ||
            (displayTick >= DISPLAY_REFRESH_MS)) {

            drawStopwatchScreen(sContext, currentSec, currentMin, currentHr, currentMS, gRunning);
            drawStopwatchScreen(sContext, currentSec, gRunning);
            drawButton(sContext, btnStart);
            drawButton(sContext, guiBtnReset);

            #ifdef GrFlush
            GrFlush(&sContext);
            #endif

            lastDisplayedSec = currentSec;
            lastRunning = gRunning;
            displayTick = 0;
        }

        // Mins
        if ((currentMin != lastDisplayedMin) ||
            (gRunning != lastRunning) ||
            (displayTick >= DISPLAY_REFRESH_MS)) {

                lastDisplayedMin = currentMin;
                lastRunning = gRunning;
                displayTick = 0;
            }

        // Hours
        if ((currentHr != lastDisplayedHr) ||
            (gRunning != lastRunning) ||
            (displayTick >= DISPLAY_REFRESH_MS)) {

                lastDisplayedHr = currentHr;
                lastRunning = gRunning;
                displayTick = 0;
            }

        // Millisecond
        unint32_t currentMS = gStopwatchMs / 1U;
        if ((currentMS != lastDisplayedMS) ||
            (gRunning != lastRunning) ||
            (displayTick >= DISPLAY_REFRESH_MS)) {

                lastDisplayedMS = currentMS;
                lastRunning = gRunning;
                displayTick = 0;
            }
    }
}

// ============================================================================
// System configuration
// ============================================================================

static void initializeDisplay(tContext &context)
{
    Crystalfontz128x128_Init();
    Crystalfontz128x128_SetOrientation(LCD_ORIENTATION_UP);
    GrContextInit(&context, &g_sCrystalfontz128x128);
    GrContextFontSet(&context, &g_sFontFixed6x8);

    tRectangle full = {0, 0, 127, 127};
    GrContextForegroundSet(&context, ClrBlack);
    GrRectFill(&context, &full);
}

static void configureTimer(Timer &timer)
{
    timer.begin(gSystemClock, TIMER0_BASE);
}

static void setupButtons()
{
    btnPlayPause.begin();
    btnPlayPause.setTickIntervalMs(BUTTON_TICK_MS);
    btnPlayPause.setDebounceMs(30);

    btnReset.begin();
    btnReset.setTickIntervalMs(BUTTON_TICK_MS);
    btnReset.setDebounceMs(30);
}

// ============================================================================
// Drawing functions
// ============================================================================
// Update function to display HH:MM:SS:MS
static void drawStopwatchScreen(tContext &context, uint32_t currentSec, uint32_t currentMin, uint32_t currentHr, uint32_t currentMS , bool running)
{
    tRectangle rectFull = {0, 0, 127, 127};
    GrContextForegroundSet(&context, ClrBlack);
    GrRectFill(&context, &rectFull);

    // === Draw title "STOPWATCH" at the top ===
    GrContextForegroundSet(&context, ClrCyan);
    GrStringDrawCentered(&context, "STOPWATCH", -1, 64, 15, false);

    // Draw seconds counter and state centered
    char str[10];
    snprintf(str, sizeof(str), "%02u:%02u:%02u:%02u", currentHr, currentMin, currentSec, currentMS);
    snprintf(str, sizeof(str), "%02u s", currentSec);
    std::string str2 = running ? "RUNNING" : "STOPPED";

    GrContextForegroundSet(&context, running ? ClrYellow : ClrOlive);
    GrStringDrawCentered(&context, str, -1, 64, 50, false);

    GrStringDrawCentered(&context, str2.c_str(), -1, 64, 40, false);
}

static void drawButton(tContext &context, const MyButton &btn)
{
    uint16_t bgColor = btn.pressed ? ClrBlack : ClrGray;
    uint16_t textColor = btn.pressed ? ClrWhite : ClrBlack;

    tRectangle rect = {btn.x, btn.y, btn.x + btn.w - 1, btn.y + btn.h - 1};
    GrContextForegroundSet(&context, bgColor);
    GrRectFill(&context, &rect);

    GrContextForegroundSet(&context, ClrBlack);
    GrRectDraw(&context, &rect);

    GrContextForegroundSet(&context, textColor);
    GrStringDrawCentered(&context, btn.label, -1,
                         btn.x + btn.w / 2, btn.y + btn.h / 2, false);
}

// ============================================================================
// Button callbacks
// ============================================================================
static void onPlayPauseClick()
{
    gRunning = !gRunning;
    btnStart.label = gRunning ? "PAUSE" : "PLAY";
}

static void onPlayPauseRelease()
{
    // Optional visual or sound feedback
}

static void onResetClick()
{
    gStopwatchMs = 0U;
}

static void onResetRelease()
{
    // Optional visual or sound feedback
}
