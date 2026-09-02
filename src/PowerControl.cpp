#include "PowerControl.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "Product.h"
#include "ui/EinkCanvas.h"

[[noreturn]] void powerOff(EinkCanvas& canvas) {
  Serial.println(LOG_TAG "Power button held - shutting down");
  Serial.flush();

  // E-ink keeps its last image with the rails down, so this is literally what
  // the device looks like while it's off. Full refresh, not fast: a partial
  // update leaves the dashboard ghosted underneath for however long it's off.
  canvas.clear();
  canvas.drawText(24, 60, "POWERED OFF", 3, true);
  canvas.drawText(24, 120, "PRESS THE POWER BUTTON TO TURN BACK ON", 1, true);
  canvas.present(EInkDisplay::FULL_REFRESH);

  // Radio down first: an associated station keeps PHY power domains alive.
  WiFi.disconnect(/*wifioff=*/true, /*eraseap=*/false);
  WiFi.mode(WIFI_OFF);

  // The panel controller gets its own deep-sleep command while its rail is
  // still up - powerDownRailsForSleep() below may cut that rail, and a
  // controller that never heard DSLP can keep its analog booster running.
  // Same ordering as CrossInk's enterDeepSleep() (its src/main.cpp).
  canvas.sleepPanel();

  // Release the battery latch. On the X4 BoardConfig maps power.latch0 to
  // GPIO13; driving it LOW is the actual battery cutoff on the hardware
  // revisions that don't self-latch. On self-latching units, and on the X3
  // (which declares no latch at all), this is a no-op and deep sleep is what
  // "off" means. latchConflictsWithBus() is the SDK's guard against a
  // mis-set profile driving a display/SD bus pin here.
  //
  // CrossInk wraps this in `#if !SOC_PM_SUPPORT_EXT1_WAKEUP`; that guard is
  // always true on the ESP32-C3 this project targets, so it's dropped rather
  // than carried as dead preprocessor weight.
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0 || BoardConfig::latchConflictsWithBus(pin)) continue;
    const auto latch = static_cast<gpio_num_t>(pin);
    gpio_set_direction(latch, GPIO_MODE_OUTPUT);
    gpio_set_level(latch, 0);
    gpio_hold_en(latch);
  }

  // Cut switched peripheral rails and hold them off through sleep (on the X3
  // that's the SD rail on GPIO13, which otherwise stays powered all through
  // "off"). No-op on boards with no switched rails.
  freeink::PowerManager::powerDownRailsForSleep();

  // Deliberately NOT PowerManager::deepSleepUntilPowerButton(). Its deepSleep()
  // calls esp_sleep_config_gpio_isolate() *after* arming the wake source, and on
  // the ESP32-C3 that overwrites the power pin's sleep input configuration, so
  // short presses get missed and the device looks dead until you hold it. Wait
  // for release, isolate, then arm - the order CrossInk's
  // HalPowerManager::startDeepSleep() settled on for this same board.
  freeink::PowerManager::waitForPowerButtonRelease();
  esp_sleep_config_gpio_isolate();
  freeink::PowerManager::armPowerButtonWakeup();
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();

  while (true) {
  }  // esp_deep_sleep_start() does not return; satisfy [[noreturn]]
}
