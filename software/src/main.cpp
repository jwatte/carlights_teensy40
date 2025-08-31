#include <Arduino.h>

#include <OctoWS2811.h>

// each channel is a longer strip of N LEDs for the side, and M LEDs for the front/back
// with N+M = 42
// I believe N = 25 and M = 17

const static int nleds = 42;
const static int nchans = 2;
static DMAMEM uint32_t frameBuffer[nleds * nchans * 3 / 4] = {};
static DMAMEM uint32_t drawBuffer[nleds * nchans * 3 / 4] = {};
OctoWS2811 LEDS(nleds, frameBuffer, drawBuffer, WS2811_800kHz | WS2811_GRB);

#define PIN_POWER 12
#define PIN_INDICATOR 13
#define APIN_VOLTAGE 14
#define PIN_LED_LEFT 2
#define PIN_LED_RIGHT 3
#define PIN_CAN_RX1 23
#define PIN_CAN_TX1 22

static bool measuring = false;
static bool animating = true;

static uint8_t ledPinList[] = {PIN_LED_LEFT, PIN_LED_RIGHT};

static uint8_t orderMap[] = {
    // left side, back to front
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
    24,
    25,
    26,
    // front side, left to right
    27,
    28,
    29,
    30,
    31,
    32,
    33,
    34,
    35,
    36,
    37,
    38,
    39,
    40,
    41,
    // right side, back to front (!)
    68,
    67,
    66,
    65,
    64,
    63,
    62,
    61,
    60,
    59,
    58,
    57,
    56,
    55,
    54,
    53,
    52,
    51,
    50,
    49,
    48,
    47,
    46,
    45,
    44,
    43,
    42,
    // back side, right to left
    69,
    70,
    71,
    72,
    73,
    74,
    75,
    76,
    77,
    78,
    79,
    80,
    81,
    82,
    83,
};

#define LEFT_START 0
#define LEFT_END 27
#define FRONT_START 27
#define FRONT_END 42
#define RIGHT_START 42
#define RIGHT_END 68
#define BACK_START 68
#define BACK_END 84

void setup()
{
    pinMode(PIN_POWER, OUTPUT);
    digitalWrite(PIN_POWER, 1);
    pinMode(PIN_INDICATOR, OUTPUT);
    pinMode(APIN_VOLTAGE, INPUT);
    pinMode(PIN_LED_LEFT, OUTPUT);
    pinMode(PIN_LED_RIGHT, OUTPUT);
    analogReadResolution(12);
    LEDS.begin(nleds, frameBuffer, drawBuffer, WS2811_800kHz | WS2811_GRB, 2, ledPinList);
}

uint32_t last = 0;
uint32_t readAnalog = 0;
uint32_t voltageCount = 0;
bool enabled = false;
bool suppressLights = false;
bool dimmed = false;
int phase = 0;

void clearLeds()
{
    for (int i = 0; i < nleds * 2; ++i)
    {
        LEDS.setPixel(orderMap[i], 0);
    }
}

unsigned int mode = 0;
#define STARTUP 0
#define RUNNING 1
#define ANIMATE_LEFT 2
#define ANIMATE_FRONT 3
#define ANIMATE_RIGHT 4
#define ANIMATE_BACK 5

int dimPhase(int phase)
{
    if (phase < 3)
    {
        return 255 >> phase;
    }
    int ret = 40 - phase;
    return ret < 1 ? 1 : ret;
}

void stepLedStartup(uint32_t now)
{
    phase += 1;
    if (phase == 256)
    {
        phase = 0;
        mode = RUNNING;
        return;
    }
    for (int i = 0; i < nleds; ++i)
    {
        int color = 0;
        if (i == phase)
        {
            color = 0x00ffffff;
        }
        if (i < phase)
        {
            int val = dimPhase(phase - i);
            color = (val << 16) | (val << 8) | val;
        }
        LEDS.setPixel(orderMap[i], color);
        LEDS.setPixel(orderMap[i + nleds], color);
    }
}

int gamma(int val)
{
    return val * val / 255;
}

void stepLedRunning(uint32_t now)
{
    phase += 1;
    int color = 0;
    if (phase < 64)
    {
        color = gamma(phase * 4 + 3);
    }
    else if (phase < (64 + 256 - 1))
    {
        color = gamma((64 + 256 - 1 - phase));
    }
    else
    {
        phase = 0;
    }
    if (color < 8)
    {
        color = 8;
    }
    if (dimmed)
    {
        color = color / 2;
    }
    color = (color << 8);
    for (int i = 0; i != nleds; i++)
    {
        LEDS.setPixel(orderMap[i], color);
        LEDS.setPixel(orderMap[i + nleds], color);
    }
}

template <int START, int END>
void stepAnimateStrip(uint32_t now)
{
    phase += 1;
    if (phase >= (END - START) * 2)
    {
        phase = 0;
    }
    if (phase < END - START)
    {
        LEDS.setPixel(orderMap[phase + START], 0x00ff0000);
    }
    else
    {
        LEDS.setPixel(orderMap[phase - (END - START) + START], 0x00020000);
    }
}

static void (*const animations[])(uint32_t) = {
    stepLedStartup,
    stepLedRunning,
    stepAnimateStrip<LEFT_START, LEFT_END>,
    stepAnimateStrip<FRONT_START, FRONT_END>,
    stepAnimateStrip<RIGHT_START, RIGHT_END>,
    stepAnimateStrip<BACK_START, BACK_END>,
};

void stepLeds(uint32_t now)
{
    while (LEDS.busy())
    {
    }
    if (!animating)
    {
        return;
    }
    if (mode < sizeof(animations) / sizeof(animations[0]))
    {
        animations[mode](now);
    }
    else
    {
        clearLeds();
        mode = STARTUP;
        phase = 0;
    }
}

void stepLowVoltage()
{
    mode = 0;
    phase = 0;
    voltageCount += 1;
    if (enabled)
    {
        if (suppressLights)
        {
            clearLeds();
        }
        if (voltageCount > 5)
        {
            if (SerialUSB && measuring)
            {
                SerialUSB.printf("enabled: 0\n");
            }
            enabled = false;
            voltageCount = 0;
        }
    }
    else
    {
        if (voltageCount > 10)
        {
            if (SerialUSB && measuring)
            {
                SerialUSB.printf("enabled: 1\n");
            }
            enabled = true;
            voltageCount = 4; // check for more voltage
        }
    }
}

float lastVoltFloat = 0.0;

void stepVoltageCheck()
{
    const int32_t avolt = analogRead(APIN_VOLTAGE);
    lastVoltFloat = avolt * 12.9 / 719.0;
    if (SerialUSB && measuring)
    {
        SerialUSB.printf("%.2f\r\n", lastVoltFloat);
    }

    if (lastVoltFloat < 11.8)
    {
        suppressLights = true;
        stepLowVoltage();
    }
    else
    {
        suppressLights = false;
        enabled = true;
        voltageCount = 0;
        dimmed = lastVoltFloat < 14.0;
    }
    digitalWrite(PIN_POWER, enabled ? 1 : 0);
}

void showLeds()
{
    digitalWrite(PIN_INDICATOR, 1);
    LEDS.show();
    digitalWrite(PIN_INDICATOR, 0);
}

struct Command
{
    char c;
    const char *help;
    void (*f)();
};

void printHelp();

static const Command commands[] = {
    {'?', "print this help", []()
     { printHelp(); }},
    {'m', "toggle measuring", []()
     { measuring = !measuring; SerialUSB.printf("measuring: %d\n", measuring); }},
    {'e', "toggle enabled", []()
     { enabled = !enabled; suppressLights = !enabled; SerialUSB.printf("enabled: %d\n", enabled); }},
    {' ', "space to toggle animation", []()
     { animating = !animating; SerialUSB.printf("animating: %d\n", animating); }},
    {'1', "animate left", []()
     { mode = ANIMATE_LEFT; phase = 0; SerialUSB.printf("animate left\n"); }},
    {'2', "animate front", []()
     { mode = ANIMATE_FRONT; phase = 0; SerialUSB.printf("animate front\n"); }},
    {'3', "animate right", []()
     { mode = ANIMATE_RIGHT; phase = 0; SerialUSB.printf("animate right\n"); }},
    {'4', "animate back", []()
     { mode = ANIMATE_BACK; phase = 0; SerialUSB.printf("animate back\n"); }},
    {'0', "animate all", []()
     { mode = 0; phase = 0; SerialUSB.printf("animate all\n"); }},
    {0, 0},
};

void printHelp()
{
    SerialUSB.print("\n");
    for (int i = 0; commands[i].c; ++i)
    {
        SerialUSB.printf("%c: %s\n", commands[i].c, commands[i].help);
    }
    SerialUSB.printf("\nenabled: %d lights: %d animating: %d dimmed %d mode %d lastVolt: %.2f\n",
                     enabled, !suppressLights, animating, dimmed, mode, lastVoltFloat);
}

void handleCli(int c)
{
    for (int i = 0; commands[i].c; ++i)
    {
        if (commands[i].c == c)
        {
            commands[i].f();
            return;
        }
    }
    SerialUSB.printf("Press ? for help\n");
}

void stepCli()
{
    if (SerialUSB.available())
    {
        const char c = SerialUSB.read();
        handleCli(c);
    }
}

void loop()
{
    const uint32_t now = micros();
    if (now - last > 16600)
    {
        last = now;
        if (enabled)
        {
            stepLeds(now);
            showLeds();
        }
    }
    else if (now - readAnalog > 1000000)
    {
        readAnalog = now;
        stepVoltageCheck();
    }
    else
    {
        stepCli();
    }
}
