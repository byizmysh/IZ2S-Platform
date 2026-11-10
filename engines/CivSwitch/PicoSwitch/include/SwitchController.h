#ifndef SwitchController_h
#define SwitchController_h

#include "Controller.h"

#define ANALOG_Y_PIN 26
#define ANALOG_X_PIN 27

#define LED_PIN 25

#define LS_PIN 22
#define LB_PIN 21
#define LT_PIN 20

#define D_UP_PIN 19
#define D_RIGHT_PIN 18 
#define D_LEFT_PIN 17
#define D_DOWN_PIN 16 

#define C_DOWN_PIN 15 
#define C_RIGHT_PIN 14
#define C_LEFT_PIN 13
#define C_UP_PIN 12

#define B_PIN 11
#define A_PIN 10
#define Y_PIN 9
#define X_PIN 8

#define HOME_PIN 7
#define START_PIN 6
#define SELECT_PIN 5

#define RT_PIN 4
#define RB_PIN 3

#define SPARE_PIN 2

#define PIN_MASK_0  (1U << 0)
#define PIN_MASK_1  (1U << 1)
#define PIN_MASK_2  (1U << 2)
#define PIN_MASK_3  (1U << 3)
#define PIN_MASK_4  (1U << 4)
#define PIN_MASK_5  (1U << 5)
#define PIN_MASK_6  (1U << 6)
#define PIN_MASK_7  (1U << 7)
#define PIN_MASK_8  (1U << 8)
#define PIN_MASK_9  (1U << 9)
#define PIN_MASK_10 (1U << 10)
#define PIN_MASK_11 (1U << 11)
#define PIN_MASK_12 (1U << 12)
#define PIN_MASK_13 (1U << 13)
#define PIN_MASK_14 (1U << 14)
#define PIN_MASK_15 (1U << 15)
#define PIN_MASK_16 (1U << 16)
#define PIN_MASK_17 (1U << 17)
#define PIN_MASK_18 (1U << 18)
#define PIN_MASK_19 (1U << 19)
#define PIN_MASK_20 (1U << 20)
#define PIN_MASK_21 (1U << 21)
#define PIN_MASK_22 (1U << 22)

#define SWITCH_DPAD_MASK 0xF0000
#define SWITCH_DPAD_UP 0x80000
#define SWITCH_DPAD_UP_RIGHT 0xC0000
#define SWITCH_DPAD_UP_LEFT 0xA0000
#define SWITCH_DPAD_DOWN 0x10000
#define SWTICH_DPAD_DOWN_RIGHT 0x50000
#define SWITCH_DPAD_DOWN_LEFT 0x30000
#define SWITCH_DPAD_RIGHT 0x40000
#define SWITCH_DPAD_LEFT 0x20000

#define SWITCH_C_MASK 0xF000







class SwitchController : public Controller {
 public:
  SwitchController(uint8_t pin) : Controller(pin, 4){};
  void init();

  void getSwitchUsbReport(SwitchUsbReport *switchUsbReport);
  uint16_t convertToSwitchUsbJoystick(int8_t axisPos, double *minAxis,
                                      double *maxAxis);
  void getSwitchBtReport(SwitchBtReport *switchBtReport);
  uint16_t convertToSwitchBtJoystick(int8_t axisPos, double *minAxis,
                                     double *maxAxis);



  void ReadPins();

 private:
  void updateState();


  const float _LS_Scale_Factor = .06; // was 0.06227
  const float _LS_Zero_Factor = .0605; // was 0.06227
  const float _LS_X_Scale_Factor_P = .54; // you may need to adjust these values, you want your analaog stick to hit the max 
  const float _LS_Y_Scale_Factor_P = .51; // you may need to adjust these values, you want your analaog stick to hit the max
  const float _LS_X_Scale_Factor_N = .39; //you may need to adjust these values, you want your analaog stick to hit the min
  const float _LS_Y_Scale_Factor_N = .45; //you may need to adjust these values, you want your analaog stick to hit the min
  const float _LS_Dead_Zone = 9; 
  uint32_t _ButtonReport;
  uint16_t _LS_X = 0;
  uint16_t _LS_Y = 0; 
  
};



#endif
