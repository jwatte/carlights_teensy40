#include <Arduino.h>

#include <OctoWS2811.h>

const static int nleds = 42;
const static int nchans = 2;
static DMAMEM uint32_t frameBuffer[nleds*nchans*3/4] = {};
static DMAMEM uint32_t drawBuffer[nleds*nchans*3/4] = {};
OctoWS2811 LEDS(nleds, frameBuffer, drawBuffer, WS2811_800kHz | WS2811_GRB);

#define PIN_POWER 12
#define PIN_INDICATOR 13
#define APIN_VOLTAGE 14
#define PIN_LED_LEFT 2
#define PIN_LED_RIGHT 3
#define PIN_CAN_RX1 23
#define PIN_CAN_TX1 22

uint8_t ledPinList[] = {PIN_LED_LEFT, PIN_LED_RIGHT};

void setup() {
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
bool lovolt = false;
int phase = 0;

void clearLeds() {
  for (int i = 0; i < nleds*2; ++i) {
    LEDS.setPixel(i, 0);
  }
}

int mode = 0;
#define STARTUP 0
#define RUNNING 1

int dimPhase(int phase) {
  if (phase < 3) {
    return 255 >> phase;
  }
  int ret = 40 - phase;
  return ret < 1 ? 1 : ret;
}

void stepLedStartup() {
  phase += 1;
  if (phase == 256) {
    phase = 0;
    mode = RUNNING;
    return;
  }
  for (int i = 0; i < nleds; ++i) {
    int color = 0;
    if (i == phase) {
      color = 0x00ffffff;
    }
    if (i < phase) {
      int val = dimPhase(phase - i);
      color = (val << 16) | (val << 8) | val;
    }
    LEDS.setPixel(i, color);
    LEDS.setPixel(i+nleds, color);
  }
}

int gamma(int val) {
  return val * val / 255;
}

void stepLedRunning() {
  phase += 1;
  int color = 0;
  if (phase < 64) {
    color = gamma(phase * 4 + 3);
  } else if (phase < (64 + 256 - 1)){
    color = gamma((64 + 256 - 1 - phase));
  } else {
    phase = 0;
  }
  if (color < 1) {
    color = 1;
  }
  color = (color << 8);
  for (int i = 0; i != nleds; i++) {
    LEDS.setPixel(i, color);
    LEDS.setPixel(i+nleds, color);
  }
}

void stepLeds(uint32_t now) {
  while (LEDS.busy()) {
  }
  if (lovolt) {
    clearLeds();
    return;
  }
  switch (mode) {
    case STARTUP:
      stepLedStartup();
      break;
    case RUNNING:
      stepLedRunning();
      break;
    default:
      clearLeds();
      mode = STARTUP;
      break;
  }
}

void stepLowVoltage() {
  mode = 0;
  phase = 0;
  voltageCount += 1;
  if (enabled && voltageCount > 5) {
    enabled = false;
    voltageCount = 0;
  } else if (!enabled && voltageCount > 10) {
    enabled = true;
    voltageCount = 4; // check for more voltage
  }
}

void stepVoltageCheck() {
    const int32_t avolt = analogRead(APIN_VOLTAGE);
    if (SerialUSB) {
      SerialUSB.printf("%.2f\r\n", avolt * 12.9 / 719.0);
    }

    if (avolt < 650) {
      lovolt = true;
      stepLowVoltage();
    } else {
      lovolt = false;
      enabled = true;
      voltageCount = 0;
    }
    digitalWrite(PIN_POWER, enabled ? 1 : 0);
}

void showLeds() {
    digitalWrite(PIN_INDICATOR, 1);
    LEDS.show();
    digitalWrite(PIN_INDICATOR, 0);
}

void loop() {
  const uint32_t now = micros();
  if (now - last > 16600) {
    last = now;
    if (enabled) {
      stepLeds(now);
      showLeds();
    }
  } else if (now - readAnalog > 1000000) {
    readAnalog = now;
    stepVoltageCheck();
  }
}

