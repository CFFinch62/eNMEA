#pragma once

class EinkCanvas;

// Software power-off: leaves a "POWERED OFF" image on the panel (e-ink holds
// it with no power), puts the panel controller to sleep, releases the battery
// latch and enters deep sleep armed to wake on the power button.
//
// Does not return - the chip resets when the user presses power again, so wake
// comes back through setup() as a normal cold boot.
[[noreturn]] void powerOff(EinkCanvas& canvas);
