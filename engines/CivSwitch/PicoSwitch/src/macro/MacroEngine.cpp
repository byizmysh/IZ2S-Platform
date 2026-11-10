#include "MacroEngine.h"

#include "macro_script.h"

void MacroEngine::init() {
  gpio_init(PICO_DEFAULT_LED_PIN);
  gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
  gpio_put(PICO_DEFAULT_LED_PIN, false);
}

void MacroEngine::getSwitchUsbReport(SwitchUsbReport *switchUsbReport) {
  tick();
  *switchUsbReport = _pad;
}

void MacroEngine::getKeyboardReport(KeyboardReport *keyboardReport) {
  tick();
  *keyboardReport = _kbd;
}

void MacroEngine::neutralOutputs() {
  _kbd = KeyboardReport{0, 0, {0, 0, 0, 0, 0, 0}};
  _pad = SwitchUsbReport{0,
                         SWITCH_USB_HAT_NOTHING,
                         SWITCH_USB_JOYSTICK_MID,
                         SWITCH_USB_JOYSTICK_MID,
                         SWITCH_USB_JOYSTICK_MID,
                         SWITCH_USB_JOYSTICK_MID,
                         0};
}

void MacroEngine::loadStep(size_t index) {
  const MacroStep &step = MACRO_SCRIPT[index];
  neutralOutputs();
  if (step.op == MACRO_OP_KEYS || step.op == MACRO_OP_BOTH) {
    _kbd = step.keyboard;
  }
  if (step.op == MACRO_OP_PAD || step.op == MACRO_OP_BOTH) {
    _pad = step.gamepad;
  }
  if (step.op == MACRO_OP_KEY_TIMES) {
    _kbd = step.keyboard;  // first press starts immediately
    _pressesLeft = step.count;
    _keyDownPhase = true;
  }
}

// Handles the release/re-press phases of a KEY_TIMES step.
// Returns true while the step still has work (tick should not advance).
bool MacroEngine::tickRepeat(const MacroStep &step) {
  if (step.op != MACRO_OP_KEY_TIMES) return false;
  if (_keyDownPhase) {
    // release between presses (mandatory: same key must leave the report)
    _kbd = KeyboardReport{0, 0, {0, 0, 0, 0, 0, 0}};
    _keyDownPhase = false;
    _stepEnd += step.durationMs;
    return true;
  }
  _pressesLeft--;
  if (_pressesLeft == 0) return false;  // step done, currently released
  _kbd = step.keyboard;
  _keyDownPhase = true;
  _stepEnd += step.durationMs;
  return true;
}

void MacroEngine::tick() {
  if (_finished) return;

  uint32_t now = to_ms_since_boot(get_absolute_time());

  if (!_running) {
    if (now < MACRO_START_DELAY_MS) return;  // armed, outputs stay neutral
    _running = true;
    gpio_put(PICO_DEFAULT_LED_PIN, true);
    _step = 0;
    _stepEnd = now;  // makes the loop below load step 0 immediately
    _stepLoaded = false;
  }

  while (now >= _stepEnd) {
    const MacroStep &step = MACRO_SCRIPT[_step];
    if (step.op == MACRO_OP_END) {
      _finished = true;
      neutralOutputs();
      gpio_put(PICO_DEFAULT_LED_PIN, false);
      return;
    }
    if (!_stepLoaded) {
      loadStep(_step);
      _stepLoaded = true;
      _stepEnd += step.durationMs;
      continue;
    }
    if (tickRepeat(step)) continue;
    _stepLoaded = false;
    _step++;
  }
}
