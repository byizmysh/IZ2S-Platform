#include "SwitchController.h"

#include "Controller.pio.h"
#include "pico/stdlib.h"

//below this are my includes
#include <stdio.h>
#include "pico.h"
#include "hardware/structs/adc.h"
//#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"



void SwitchController::init() {
  // init led pin and set it to an outpit
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    //init all of the digital input button pins and have them be pulled high by defalt
    for(int i = 0; i <= 22; i++){
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);
    }
    
    adc_init(); //init adc functionality
    adc_gpio_init(ANALOG_X_PIN); // sets gpio as high impedance, no pullups ect
    //adc_select_input(0); // selects adc0 for analog X 
    adc_gpio_init(ANALOG_Y_PIN); // sets gpio as high impedance, no pullups ect
    //adc_select_input(1);  // selects adc1 for analog Y

    sleep_us(200); // not sure if this is needed
    
}

void SwitchController::ReadPins(){
   
}

void SwitchController::updateState() {
  //reads and stores all of the digitial GIOP pins 
  _ButtonReport = ~gpio_get_all(); // inverted because the pins are pulled high
  
  // reads and stores the values for the left stick analog values
  adc_select_input(0); // selects adc0 for analog X 
  _LS_X = adc_read();
  adc_select_input(1); // selects adc1 for analog Y 
  _LS_Y = adc_read();

}

void SwitchController::getSwitchUsbReport(SwitchUsbReport *switchUsbReport) {

  updateState();
  gpio_put(LED_PIN, false);
 // if(SWITCH_C_MASK & _ButtonReport ){gpio_put(LED_PIN, true);}
  if(PIN_MASK_2 & _ButtonReport ){gpio_put(LED_PIN, true);}

  switchUsbReport->hat = SWITCH_USB_HAT_NOTHING;
  switchUsbReport->rx = SWITCH_USB_JOYSTICK_MID;
  switchUsbReport->ry = SWITCH_USB_JOYSTICK_MID;

  /*
  uint16_t test = 3;

  switchUsbReport->buttons = 0;
  gpio_put(led_pin, false);

    if(gpio_get(16) == false){
      switchUsbReport->buttons += 4;
      gpio_put(led_pin, true);
    }
    if (gpio_get(17) == false){
      switchUsbReport->buttons += 2;
      gpio_put(led_pin, true);
    }
    */
  
  // Updating digital buttons 
  switchUsbReport->buttons =
      (PIN_MASK_20 & _ButtonReport ? SWITCH_USB_MASK_ZL : 0) |
      (PIN_MASK_21 & _ButtonReport ? SWITCH_USB_MASK_L : 0) |
      (PIN_MASK_4 & _ButtonReport ? SWITCH_USB_MASK_ZR : 0) |
      (PIN_MASK_3 & _ButtonReport ? SWITCH_USB_MASK_R : 0) |
      (PIN_MASK_9 & _ButtonReport ? SWITCH_USB_MASK_Y : 0) |
      (PIN_MASK_8 & _ButtonReport ? SWITCH_USB_MASK_X : 0) |
      (PIN_MASK_11 & _ButtonReport ? SWITCH_USB_MASK_B : 0) |
      (PIN_MASK_10 & _ButtonReport ? SWITCH_USB_MASK_A : 0) |
      (PIN_MASK_7 & _ButtonReport ? SWITCH_USB_MASK_HOME : 0) |
      (PIN_MASK_5 & _ButtonReport ? SWITCH_USB_MASK_MINUS : 0) |
      (PIN_MASK_6 & _ButtonReport ? SWITCH_USB_MASK_PLUS : 0) |
      (PIN_MASK_22 & _ButtonReport ? SWITCH_USB_MASK_L3 : 0);
      

      
  // Setting D PAD
  switch (SWITCH_DPAD_MASK & _ButtonReport) {
    case SWITCH_DPAD_UP:
      switchUsbReport->hat = SWITCH_USB_HAT_UP;
      break;
    case SWITCH_DPAD_UP_RIGHT:
      switchUsbReport->hat = SWITCH_USB_HAT_UPRIGHT;
      break;
    case SWITCH_DPAD_RIGHT:
      switchUsbReport->hat = SWITCH_USB_HAT_RIGHT;
      break;
    case SWTICH_DPAD_DOWN_RIGHT:
      switchUsbReport->hat = SWITCH_USB_HAT_DOWNRIGHT;
      break;
    case SWITCH_DPAD_DOWN:
      switchUsbReport->hat = SWITCH_USB_HAT_DOWN;
      break;
    case SWITCH_DPAD_DOWN_LEFT:
      switchUsbReport->hat = SWITCH_USB_HAT_DOWNLEFT;
      break;
    case SWITCH_DPAD_LEFT:
      switchUsbReport->hat = SWITCH_USB_HAT_LEFT;
      break;
    case SWITCH_DPAD_UP_LEFT:
      switchUsbReport->hat = SWITCH_USB_HAT_UPLEFT;
      break;
  }
  // Updating C Stick
  //switchUsbReport->rx = SWITCH_USB_JOYSTICK_MID;
  //switchUsbReport->ry = SWITCH_USB_JOYSTICK_MID;
  switch (SWITCH_C_MASK & _ButtonReport) {
    case PIN_MASK_12:
      switchUsbReport->ry = SWITCH_USB_JOYSTICK_MIN;
      break;
    case PIN_MASK_15:
      switchUsbReport->ry = SWITCH_USB_JOYSTICK_MAX;
      break;
    case PIN_MASK_13:
      switchUsbReport->rx = SWITCH_USB_JOYSTICK_MIN;
      break;
    case PIN_MASK_14:
      switchUsbReport->rx = SWITCH_USB_JOYSTICK_MAX;
      break;
  }

  //updating Left Stick

    uint16_t _LS_X_pos = 255-(_LS_Zero_Factor * _LS_Y); // not sure what error lead to these needing to be switched byt x is y and y is x
    uint16_t _LS_Y_pos = 255-(_LS_Zero_Factor * _LS_X);
/*
    if(_LS_X_pos > (SWITCH_USB_JOYSTICK_MID + _LS_Dead_Zone))
    {
      _LS_X_pos = _LS_X_pos * (1 + _LS_X_Scale_Factor_P);
    }

    else if(_LS_X_pos < (SWITCH_USB_JOYSTICK_MID - _LS_Dead_Zone))
    {
      _LS_X_pos = _LS_X_pos * (1 - _LS_X_Scale_Factor_N);
    }

    else {_LS_X_pos = SWITCH_USB_JOYSTICK_MID;}

    if(_LS_Y_pos > (SWITCH_USB_JOYSTICK_MID + _LS_Dead_Zone))
    {
      _LS_Y_pos = _LS_Y_pos * (1 + _LS_Y_Scale_Factor_P);
    }

    else if(_LS_Y_pos < (SWITCH_USB_JOYSTICK_MID - _LS_Dead_Zone))
    {
      _LS_Y_pos = _LS_Y_pos * (1 - _LS_Y_Scale_Factor_N);
    }

    else {_LS_Y_pos = SWITCH_USB_JOYSTICK_MID;}
*/
    if(_LS_X_pos > (SWITCH_USB_JOYSTICK_MID + _LS_Dead_Zone))
    {
      _LS_X_pos = _LS_X_pos + ((_LS_X_pos - SWITCH_USB_JOYSTICK_MID ) * _LS_X_Scale_Factor_P); 
    }
     else if(_LS_X_pos < (SWITCH_USB_JOYSTICK_MID - _LS_Dead_Zone))
    {
      _LS_X_pos = _LS_X_pos + ((_LS_X_pos - SWITCH_USB_JOYSTICK_MID) * _LS_X_Scale_Factor_N); 
    }
    else {_LS_X_pos = SWITCH_USB_JOYSTICK_MID;}

    

    if(_LS_Y_pos > (SWITCH_USB_JOYSTICK_MID + _LS_Dead_Zone))
    {
      _LS_Y_pos = _LS_Y_pos + ((_LS_Y_pos - SWITCH_USB_JOYSTICK_MID ) * _LS_Y_Scale_Factor_P); 
    }
    else if(_LS_Y_pos < (SWITCH_USB_JOYSTICK_MID - _LS_Dead_Zone))
    {
      _LS_Y_pos = _LS_Y_pos + ((_LS_Y_pos - SWITCH_USB_JOYSTICK_MID ) * _LS_Y_Scale_Factor_N); 
    }
    else {_LS_Y_pos = SWITCH_USB_JOYSTICK_MID;}


    if (_LS_X_pos > SWITCH_USB_JOYSTICK_MAX)
    {
        _LS_X_pos = SWITCH_USB_JOYSTICK_MAX;
    }

    if (_LS_X_pos < SWITCH_USB_JOYSTICK_MIN)
    {
        _LS_X_pos = SWITCH_USB_JOYSTICK_MIN;
    }

    if (_LS_Y_pos > SWITCH_USB_JOYSTICK_MAX)
    {
        _LS_Y_pos = SWITCH_USB_JOYSTICK_MAX;
    }

    if (_LS_Y_pos < SWITCH_USB_JOYSTICK_MIN)
    {
        _LS_Y_pos = SWITCH_USB_JOYSTICK_MIN;
    }


    switchUsbReport->ly = _LS_Y_pos;
    switchUsbReport->lx = _LS_X_pos;
  /*
  // scale for joystick insensitivity if needed
  // https://n64squid.com/n64-joystick-360-degrees/
  switchUsbReport->lx =
      convertToSwitchUsbJoystick(_controllerState[2], &_minX, &_maxX);
  switchUsbReport->ly =
      convertToSwitchUsbJoystick(-_controllerState[3], &_minY, &_maxY);*/
  //return;
  
}

void SwitchController::getSwitchBtReport(SwitchBtReport *switchBtReport) {
  updateState();
/*
  switchBtReport->buttons[0] =
      (N64_MASK_R & _controllerState[1] ? SWITCH_BT_MASK_R : 0) |
      (N64_MASK_A & _controllerState[0] ? SWITCH_BT_MASK_A : 0) |
      (N64_MASK_B & _controllerState[0] ? SWITCH_BT_MASK_B : 0);

  switchBtReport->buttons[1] =
      (N64_MASK_RESET & _controllerState[1] ? SWITCH_BT_MASK_HOME : 0) |
      (N64_MASK_START & _controllerState[0] ? SWITCH_BT_MASK_PLUS : 0);

  switchBtReport->buttons[2] = 0x00;
  switch (N64_MASK_DPAD & _controllerState[0]) {
    case N64_MASK_DPAD_UP:
      switchBtReport->buttons[2] |= SWITCH_BT_HAT_UP;
      break;
    case N64_MASK_DPAD_UPRIGHT:
      switchBtReport->buttons[2] |= SWITCH_BT_HAT_UPRIGHT;
      break;
    case N64_MASK_DPAD_RIGHT:
      switchBtReport->buttons[2] |= SWITCH_BT_HAT_RIGHT;
      break;
    case N64_MASK_DPAD_DOWNRIGHT:
      switchBtReport->buttons[2] |= SWITCH_BT_HAT_DOWNRIGHT;
      break;
    case N64_MASK_DPAD_DOWN:
      switchBtReport->buttons[2] |= SWITCH_BT_HAT_DOWN;
      break;
    case N64_MASK_DPAD_DOWNLEFT:
      switchBtReport->buttons[2] |= SWITCH_BT_HAT_DOWNLEFT;
      break;
    case N64_MASK_DPAD_LEFT:
      switchBtReport->buttons[2] |= SWITCH_BT_HAT_LEFT;
      break;
    case N64_MASK_DPAD_UPLEFT:
      switchBtReport->buttons[2] |= SWITCH_BT_HAT_UPLEFT;
      break;
  }

  switchBtReport->buttons[2] |=
      (N64_MASK_L & _controllerState[1] ? SWITCH_BT_MASK_L : 0) |
      (N64_MASK_Z & _controllerState[0] ? SWITCH_BT_MASK_ZL : 0);

  // Scale for joystick insensitivity if needed
  uint16_t lx = convertToSwitchBtJoystick(_controllerState[2], &_minX, &_maxX);
  uint16_t ly = convertToSwitchBtJoystick(_controllerState[3], &_minY, &_maxY);
  switchBtReport->l[0] = lx & 0xff;
  switchBtReport->l[1] = ((ly & 0xf) << 4) | (lx >> 8);
  switchBtReport->l[2] = ly >> 4;

  uint16_t rx = SWITCH_BT_JOYSTICK_MID;
  uint16_t ry = SWITCH_BT_JOYSTICK_MID;
  switch (N64_MASK_C & _controllerState[1]) {
    case N64_MASK_C_UP:
      ry = SWITCH_BT_JOYSTICK_MAX;
      break;
    case N64_MASK_C_DOWN:
      ry = SWITCH_BT_JOYSTICK_MIN;
      break;
    case N64_MASK_C_LEFT:
      rx = SWITCH_BT_JOYSTICK_MIN;
      break;
    case N64_MASK_C_RIGHT:
      rx = SWITCH_BT_JOYSTICK_MAX;
      break;
  }
  switchBtReport->r[0] = rx & 0xff;
  switchBtReport->r[1] = ((ry & 0xf) << 4) | (rx >> 8);
  switchBtReport->r[2] = ry >> 4;
*/
  return;
}



