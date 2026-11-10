#ifndef MacroEngine_h
#define MacroEngine_h

#include "Controller.h"
#include "tusb.h"  // HID_KEY_* keycodes used by macro scripts

// Playback starts this many milliseconds after boot.
#ifndef MACRO_START_DELAY_MS
#define MACRO_START_DELAY_MS 1000
#endif

enum MacroOp : uint8_t {
  MACRO_OP_WAIT,       // everything released
  MACRO_OP_KEYS,       // keyboard report active, gamepad neutral
  MACRO_OP_PAD,        // gamepad report active, keyboard released
  MACRO_OP_BOTH,       // keyboard and gamepad active together
  MACRO_OP_KEY_TIMES,  // press one keyboard key `count` times (hold+release each)
  MACRO_OP_END,        // stop playback
};

typedef struct {
  MacroOp op;
  uint32_t durationMs;  // per-step (or per-press for KEY_TIMES) hold time
  uint32_t count;       // KEY_TIMES: number of presses; 1 for other ops
  KeyboardReport keyboard;
  SwitchUsbReport gamepad;
} MacroStep;

#define MACRO_KBD_RELEASED \
  { 0, 0, { 0, 0, 0, 0, 0, 0 } }
#define MACRO_PAD_NEUTRAL                                             \
  {                                                                   \
    0, SWITCH_USB_HAT_NOTHING, SWITCH_USB_JOYSTICK_MID,               \
        SWITCH_USB_JOYSTICK_MID, SWITCH_USB_JOYSTICK_MID,             \
        SWITCH_USB_JOYSTICK_MID, 0                                    \
  }

// Script helpers. Every step fully sets the outputs for its duration;
// anything a step does not hold is released.
#define WAIT(ms) { MACRO_OP_WAIT, ms, 1, MACRO_KBD_RELEASED, MACRO_PAD_NEUTRAL }
#define KEYS(ms, mod, k1, k2, k3, k4, k5, k6) \
  { MACRO_OP_KEYS, ms, 1, { mod, 0, { k1, k2, k3, k4, k5, k6 } }, MACRO_PAD_NEUTRAL }
// Press a single key n times, ms hold + ms release per press.
#define KEY_TIMES(n, ms, mod, k) \
  { MACRO_OP_KEY_TIMES, ms, n, { mod, 0, { k, 0, 0, 0, 0, 0 } }, MACRO_PAD_NEUTRAL }
#define PAD(ms, buttons, hat, lx, ly, rx, ry) \
  { MACRO_OP_PAD, ms, 1, MACRO_KBD_RELEASED, { buttons, hat, lx, ly, rx, ry, 0 } }
#define PAD_BTN(ms, buttons)                                             \
  PAD(ms, buttons, SWITCH_USB_HAT_NOTHING, SWITCH_USB_JOYSTICK_MID,      \
      SWITCH_USB_JOYSTICK_MID, SWITCH_USB_JOYSTICK_MID,                  \
      SWITCH_USB_JOYSTICK_MID)
#define PAD_DPAD(ms, hat)                                                \
  PAD(ms, 0, hat, SWITCH_USB_JOYSTICK_MID, SWITCH_USB_JOYSTICK_MID,      \
      SWITCH_USB_JOYSTICK_MID, SWITCH_USB_JOYSTICK_MID)
#define PAD_LSTICK(ms, x, y)                                             \
  PAD(ms, 0, SWITCH_USB_HAT_NOTHING, x, y, SWITCH_USB_JOYSTICK_MID,      \
      SWITCH_USB_JOYSTICK_MID)
#define PAD_RSTICK(ms, x, y)                                             \
  PAD(ms, 0, SWITCH_USB_HAT_NOTHING, SWITCH_USB_JOYSTICK_MID,            \
      SWITCH_USB_JOYSTICK_MID, x, y)
#define BOTH(ms, mod, k1, k2, k3, k4, k5, k6, buttons, hat, lx, ly, rx, ry) \
  { MACRO_OP_BOTH, ms, 1, { mod, 0, { k1, k2, k3, k4, k5, k6 } },             \
    { buttons, hat, lx, ly, rx, ry, 0 } }
#define END() { MACRO_OP_END, 0, 1, MACRO_KBD_RELEASED, MACRO_PAD_NEUTRAL }

class MacroEngine : public Controller {
 public:
  MacroEngine() : Controller(0, 0){};
  void init();

  void getSwitchUsbReport(SwitchUsbReport *switchUsbReport);
  void getSwitchBtReport(SwitchBtReport *switchBtReport) {
    (void)switchBtReport;
  }
  void getKeyboardReport(KeyboardReport *keyboardReport);

 private:
  void updateState() {}
  void tick();
  void loadStep(size_t index);
  bool tickRepeat(const MacroStep &step);
  void neutralOutputs();

  bool _running = false;
  bool _finished = false;
  bool _stepLoaded = false;
  bool _keyDownPhase = false;   // KEY_TIMES: currently holding the key down
  uint32_t _pressesLeft = 0;    // KEY_TIMES: presses remaining
  size_t _step = 0;
  uint32_t _stepEnd = 0;
  SwitchUsbReport _pad{0,
                       SWITCH_USB_HAT_NOTHING,
                       SWITCH_USB_JOYSTICK_MID,
                       SWITCH_USB_JOYSTICK_MID,
                       SWITCH_USB_JOYSTICK_MID,
                       SWITCH_USB_JOYSTICK_MID,
                       0};
  KeyboardReport _kbd{0, 0, {0, 0, 0, 0, 0, 0}};
};

#endif
