#include <stdio.h>
#include <string.h>

#include "GamecubeController.h"
#include "MacroEngine.h"
#include "N64Controller.h"
#include "SwitchController.h"
#include "pico/stdlib.h"

#ifdef SWITCH_BLUETOOTH
#include "SwitchBluetooth.h"
#else
#include "SwitchUsb.h"
#endif

#ifdef CONTROLLER_TYPE
#define controllerType CONTROLLER_TYPE
#else
#define controllerType "Switch"
#endif

int main() {
  stdio_init_all();

  Controller *controller;



  if (strcmp(controllerType, "N64") == 0) {
    controller = new N64Controller(18);
  } else if (strcmp(controllerType, "Gamecube") == 0) {
    controller = new GamecubeController(18);
  } else if (strcmp(controllerType, "Switch") == 0) {
    controller = new SwitchController(18);
  } else if (strcmp(controllerType, "Macro") == 0) {
    controller = new MacroEngine();
  }
  controller->init();

#ifdef SWITCH_BLUETOOTH
  SwitchBluetooth::init(controller);
#else
  SwitchUsb switchUsb(controller);
  switchUsb.init();
#endif
}
