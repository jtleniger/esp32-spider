#pragma once

#include <cstdint>

// All hardware and tuning knobs for the LED rig live here.
namespace ledfx {

// --- TLC5947 breakout wiring (see PINS.md) ---
constexpr uint8_t kTlcClockPin = 25;  // GPIO25 / Pin 9
constexpr uint8_t kTlcDataPin = 27;   // GPIO27 / Pin 11
constexpr uint8_t kTlcLatchPin = 32;  // GPIO32 / Pin 7

// --- Adafruit TLC5947 breakout, not chained ---
constexpr uint8_t kChannelsPerDriver = 24;
constexpr uint8_t kTlcDriverCount = 1;
constexpr uint8_t kChannelCount = kChannelsPerDriver * kTlcDriverCount;

// --- HLK-LD1020 radar: output is driven high while motion is detected ---
constexpr uint8_t kRadarPin = 2;  // GPIO2 / Pin 24

// --- Fitted LEDs: only these channels have an LED soldered on so far ---
constexpr uint8_t kFirstChannel = 6;
constexpr uint8_t kLastChannel = 17;  // 0-5 and 18-23 are empty for now
constexpr uint8_t kConnectedChannelCount = kLastChannel - kFirstChannel + 1;

// --- Planned colour inventory. Which channel carries which colour is not
// mapped yet, so both modes treat every fitted channel identically. ---
constexpr uint8_t kRedLedCount = 8;
constexpr uint8_t kGreenLedCount = 8;
constexpr uint8_t kOrangeLedCount = 8;

// --- TLC5947 PWM resolution (12 bit) ---
constexpr uint16_t kMaxPwm = 4095;

// --- Mode 1 tuning: one PWM refresh every kFadeStepMs ---
constexpr uint32_t kFadeStepMs = 8;
constexpr uint16_t kFadeSteps = 64;                         // steps per fade-in
constexpr uint32_t kHalfCycleMs = kFadeSteps * kFadeStepMs;  // 512 ms
constexpr uint32_t kCycleMs = 2 * kHalfCycleMs;              // 1024 ms in+out

// --- Mode 0 (pulse) tuning: a full in+out cycle is twice the half cycle ---
constexpr uint32_t kPulseHalfSlowMs = 1000;  // 2 s cycle while the radar is idle
constexpr uint32_t kPulseHalfFastMs = 250;   // 0.5 s cycle while it sees motion
constexpr uint16_t kPulseLevels = 64;        // brightness rungs per half cycle

}  // namespace ledfx
