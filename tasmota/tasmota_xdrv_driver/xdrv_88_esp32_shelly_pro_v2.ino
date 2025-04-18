/*
  xdrv_88_shelly_pro.ino - Shelly pro family support for Tasmota

  Copyright (C) 2022  Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef ESP32
#ifdef USE_SPI
#ifdef USE_SHELLY_PRO
/*********************************************************************************************\
 * Shelly Pro support
 *
 * {"NAME":"Shelly Pro 1","GPIO":[0,1,0,1,768,0,0,0,672,704,736,0,0,0,5600,6214,0,0,0,5568,0,0,0,0,0,0,0,0,0,0,0,32,4736,0,160,0],"FLAG":0,"BASE":1,"CMND":"AdcParam1 2,10000,10000,3350"}
 * {"NAME":"Shelly Pro 1PM","GPIO":[9568,1,9472,1,768,0,0,0,672,704,736,0,0,0,5600,6214,0,0,0,5568,0,0,0,0,0,0,0,0,3459,0,0,32,4736,0,160,0],"FLAG":0,"BASE":1,"CMND":"AdcParam1 2,10000,10000,3350"}
 * {"NAME":"Shelly Pro 2","GPIO":[0,1,0,1,768,0,0,0,672,704,736,0,0,0,5600,6214,0,0,0,5568,0,0,0,0,0,0,0,0,0,0,0,32,4736,4737,160,161],"FLAG":0,"BASE":1,"CMND":"AdcParam1 2,10000,10000,3350;AdcParam2 2,10000,10000,3350"}
 * {"NAME":"Shelly Pro 2PM","GPIO":[9568,1,9472,1,768,0,0,0,672,704,736,9569,0,0,5600,6214,0,0,0,5568,0,0,0,0,0,0,0,0,3460,0,0,32,4736,4737,160,161],"FLAG":0,"BASE":1,"CMND":"AdcParam1 2,10000,10000,3350;AdcParam2 2,10000,10000,3350"}
 *
 * {"NAME":"Shelly Pro 4PM","GPIO":[769,1,1,1,9568,0,0,0,1,705,9569,737,768,0,5600,0,0,0,0,5568,0,0,0,0,0,0,0,6214,736,704,3461,0,4736,1,0,672],"FLAG":0,"BASE":1,"CMND":"AdcParam1 2,5600,4700,3350"}
 * {"NAME":"Shelly Pro 4PM No display","GPIO":[1,1,1,1,9568,0,0,0,1,1,9569,1,768,0,5600,0,0,0,0,5568,0,0,0,0,0,0,0,6214,736,704,3461,0,4736,1,0,672],"FLAG":0,"BASE":1,"CMND":"AdcParam1 2,5600,4700,3350"}
 *
 * Shelly Pro 1/2 uses SPI to control one 74HC595 for relays/leds and one ADE7953 (1PM) or two ADE7953 (2PM) for energy monitoring
 * Shelly Pro 4 uses an SPI to control one MCP23S17 for buttons/switches/relays/leds and two ADE7953 for energy monitoring and a second SPI for the display
\*********************************************************************************************/

#define XDRV_88                        88

#define SHELLY_PRO_PIN_LAN8720_RESET   5
#define SHELLY_PRO_4_PIN_SPI_CS        16
#define SHELLY_PRO_4_PIN_MCP23S17_INT  35
#define SHELLY_PRO_4_MCP23S17_ADDRESS  0x40

struct SPro {
  uint32_t last_update;
  uint32_t probe_pin;
  uint16_t input_state;
  int8_t switch_offset;
  int8_t button_offset;
  uint8_t pin_register_cs;
  uint8_t pin_mcp23s17_int;
  uint8_t ledlink;
  uint8_t power;
  uint8_t detected;
} SPro;

/*********************************************************************************************\
 * Shelly Pro MCP23S17 support
\*********************************************************************************************/

enum SP4MCP23X17GPIORegisters {
  // A side
  SP4_MCP23S17_IODIRA = 0x00,
  SP4_MCP23S17_IPOLA = 0x02,
  SP4_MCP23S17_GPINTENA = 0x04,
  SP4_MCP23S17_DEFVALA = 0x06,
  SP4_MCP23S17_INTCONA = 0x08,
  SP4_MCP23S17_IOCONA = 0x0A,
  SP4_MCP23S17_GPPUA = 0x0C,
  SP4_MCP23S17_INTFA = 0x0E,
  SP4_MCP23S17_INTCAPA = 0x10,
  SP4_MCP23S17_GPIOA = 0x12,
  SP4_MCP23S17_OLATA = 0x14,
  // B side
  SP4_MCP23S17_IODIRB = 0x01,
  SP4_MCP23S17_IPOLB = 0x03,
  SP4_MCP23S17_GPINTENB = 0x05,
  SP4_MCP23S17_DEFVALB = 0x07,
  SP4_MCP23S17_INTCONB = 0x09,
  SP4_MCP23S17_IOCONB = 0x0B,
  SP4_MCP23S17_GPPUB = 0x0D,
  SP4_MCP23S17_INTFB = 0x0F,
  SP4_MCP23S17_INTCAPB = 0x11,
  SP4_MCP23S17_GPIOB = 0x13,
  SP4_MCP23S17_OLATB = 0x15,
};

uint8_t sp4_mcp23s17_olata = 0;
uint8_t sp4_mcp23s17_olatb = 0;

void SP4Mcp23S17Enable(void) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(SPro.pin_register_cs, 0);
}

void SP4Mcp23S17Disable(void) {
  SPI.endTransaction();
  digitalWrite(SPro.pin_register_cs, 1);
}

uint32_t SP4Mcp23S17Read16(uint8_t reg) {
  // Read 16-bit registers: (regb << 8) | rega
  SP4Mcp23S17Enable();
  SPI.transfer(SHELLY_PRO_4_MCP23S17_ADDRESS | 1);
  SPI.transfer(reg);
  uint32_t value = SPI.transfer(0xFF);  // RegA
  value |= (SPI.transfer(0xFF) << 8);   // RegB
  SP4Mcp23S17Disable();
  return value;
}

uint32_t SP4Mcp23S17Read(uint8_t reg) {
  SP4Mcp23S17Enable();
  SPI.transfer(SHELLY_PRO_4_MCP23S17_ADDRESS | 1);
  SPI.transfer(reg);
  uint32_t value = SPI.transfer(0xFF);
  SP4Mcp23S17Disable();
  return value;
}

void SP4Mcp23S17Write(uint8_t reg, uint8_t value) {
  SP4Mcp23S17Enable();
  SPI.transfer(SHELLY_PRO_4_MCP23S17_ADDRESS);
  SPI.transfer(reg);
  SPI.transfer(value);
  SP4Mcp23S17Disable();
}

void SP4Mcp23S17Update(uint8_t pin, bool pin_value, uint8_t reg_addr) {
  uint8_t bit = pin % 8;
  uint8_t reg_value = 0;
  if (reg_addr == SP4_MCP23S17_OLATA) {
    reg_value = sp4_mcp23s17_olata;
  } else if (reg_addr == SP4_MCP23S17_OLATB) {
    reg_value = sp4_mcp23s17_olatb;
  } else {
    reg_value = SP4Mcp23S17Read(reg_addr);
  }
  if (pin_value) {
    reg_value |= 1 << bit;
  } else {
    reg_value &= ~(1 << bit);
  }
  SP4Mcp23S17Write(reg_addr, reg_value);
  if (reg_addr == SP4_MCP23S17_OLATA) {
    sp4_mcp23s17_olata = reg_value;
  } else if (reg_addr == SP4_MCP23S17_OLATB) {
    sp4_mcp23s17_olatb = reg_value;
  }
}

void SP4Mcp23S17PinMode(uint8_t pin, uint8_t flags) {
  uint8_t iodir = pin < 8 ? SP4_MCP23S17_IODIRA : SP4_MCP23S17_IODIRB;
  uint8_t gppu = pin < 8 ? SP4_MCP23S17_GPPUA : SP4_MCP23S17_GPPUB;
  if (flags == INPUT) {
    SP4Mcp23S17Update(pin, true, iodir);
    SP4Mcp23S17Update(pin, false, gppu);
  } else if (flags == (INPUT | PULLUP)) {
    SP4Mcp23S17Update(pin, true, iodir);
    SP4Mcp23S17Update(pin, true, gppu);
  } else if (flags == OUTPUT) {
    SP4Mcp23S17Update(pin, false, iodir);
  }
}

bool SP4Mcp23S17DigitalRead(uint8_t pin) {
  uint8_t bit = pin % 8;
  uint8_t reg_addr = pin < 8 ? SP4_MCP23S17_GPIOA : SP4_MCP23S17_GPIOB;
  uint8_t value = SP4Mcp23S17Read(reg_addr);
  return value & (1 << bit);
}

void SP4Mcp23S17DigitalWrite(uint8_t pin, bool value) {
  uint8_t reg_addr = pin < 8 ? SP4_MCP23S17_OLATA : SP4_MCP23S17_OLATB;
  SP4Mcp23S17Update(pin, value, reg_addr);
}

/*********************************************************************************************\
 * Shelly Pro 4
\*********************************************************************************************/

const uint8_t sp4_relay_pin[] = { 8, 13, 14, 12 };
const uint8_t sp4_switch_pin[] = { 6, 1, 0, 15 };
const uint8_t sp4_button_pin[] = { 5, 2, 3 };

void ShellyPro4Init(void) {
  /*
  Shelly Pro 4PM MCP23S17 registers
   bit 0 = input - Switch3
   bit 1 = input - Switch2
   bit 2 = input, pullup, inverted - Button Down
   bit 3 = input, pullup, inverted - Button OK
   bit 4 = output - Reset, display, ADE7953
   bit 5 = input, pullup, inverted - Button Up
   bit 6 = input - Switch1
   bit 7
   bit 8 = output - Relay O1
   bit 9
   bit 10
   bit 11
   bit 12 = output - Relay O4
   bit 13 = output - Relay O2
   bit 14 = output - Relay O3
   bit 15 = input - Switch4
  */
  SP4Mcp23S17Write(SP4_MCP23S17_IOCONA, 0b01011000);   // Enable INT mirror, Slew rate disabled, HAEN pins for addressing
  SP4Mcp23S17Write(SP4_MCP23S17_GPINTENA, 0x6F);   // Enable interrupt on change
  SP4Mcp23S17Write(SP4_MCP23S17_GPINTENB, 0x80);   // Enable interrupt on change

  // Read current output register state
  sp4_mcp23s17_olata = SP4Mcp23S17Read(SP4_MCP23S17_OLATA);
  sp4_mcp23s17_olatb = SP4Mcp23S17Read(SP4_MCP23S17_OLATB);

  for (uint32_t i = 0; i < 4; i++) {
    SP4Mcp23S17PinMode(sp4_switch_pin[i], INPUT);   // Switch1..4
    SP4Mcp23S17PinMode(sp4_relay_pin[i], OUTPUT);   // Relay O1..O4
  }
  SPro.switch_offset = -1;

  for (uint32_t i = 0; i < 3; i++) {
    SP4Mcp23S17PinMode(sp4_button_pin[i], PULLUP);  // Button Up, Down, OK
  }
  SPro.button_offset = -1;

  SP4Mcp23S17PinMode(4, OUTPUT);                    // Reset display, ADE7943
  SP4Mcp23S17DigitalWrite(4, 1);

  attachInterrupt(SPro.pin_mcp23s17_int, ShellyProUpdateIsr, CHANGE);
}

void ShellyPro4Reset(void) {
  SP4Mcp23S17DigitalWrite(4, 0);                    // Reset pin display, ADE7953
  delay(1);                                         // To initiate a hardware reset, this pin must be brought low for a minimum of 10 μs.
  SP4Mcp23S17DigitalWrite(4, 1);
}

bool ShellyProAddButton(void) {
  if (SPro.detected != 4) { return false; }         // Only support Shelly Pro 4
  if (SPro.button_offset < 0) { SPro.button_offset = XdrvMailbox.index; }
  uint32_t index = XdrvMailbox.index - SPro.button_offset;
  if (index > 2) { return false; }                  // Support three buttons
  uint32_t state = SP4Mcp23S17DigitalRead(sp4_button_pin[index]);
  bitWrite(SPro.input_state, sp4_button_pin[index], state);
  XdrvMailbox.index = state;
  return true;
}

bool ShellyProAddSwitch(void) {
  if (SPro.detected != 4) { return false; }         // Only support Shelly Pro 4
  if (SPro.switch_offset < 0) { SPro.switch_offset = XdrvMailbox.index; }
  uint32_t index = XdrvMailbox.index - SPro.switch_offset;
  if (index > 3) { return false; }                  // Support four switches
  uint32_t state = SP4Mcp23S17DigitalRead(sp4_switch_pin[index]);
  bitWrite(SPro.input_state, sp4_switch_pin[index], state);
  XdrvMailbox.index = state ^1;                     // Invert
  return true;
}

void ShellyProUpdateIsr(void) {
  /*
  The goal if this function is to minimize SPI and SetVirtualPinState calls
  */

  uint32_t input_state = SP4Mcp23S17Read16(SP4_MCP23S17_INTCAPA);  // Read intcap and clear interrupt
  input_state &= 0x806F;                            // Only test input bits

//  AddLog(LOG_LEVEL_DEBUG, PSTR("SHP: Input detected %04X, was %04X"), input_state, SPro.input_state);

  uint32_t mask = 1;
  for (uint32_t j = 0; j < 16; j++) {
    if ((input_state & mask) != (SPro.input_state & mask)) {
      uint32_t state = (input_state >> j) &1;

//      AddLog(LOG_LEVEL_DEBUG, PSTR("SHP: Change pin %d to %d"), j, state);

      for (uint32_t i = 0; i < 4; i++) {
        if (j == sp4_switch_pin[i]) {
          SwitchSetVirtualPinState(SPro.switch_offset +i, state ^1);  // Invert
        }
        else if ((i < 3) && (j == sp4_button_pin[i])) {
          ButtonSetVirtualPinState(SPro.button_offset +i, state);
        }
      }
    }
    mask <<= 1;
  }
  SPro.input_state = input_state;
}

bool ShellyProButton(void) {
  if (SPro.detected != 4) { return false; }         // Only support Shelly Pro 4

  uint32_t button_index = XdrvMailbox.index - SPro.button_offset;
  if (button_index > 2) { return false; }           // Only support Up, Down, Ok

  uint32_t button = XdrvMailbox.payload;
  uint32_t last_state = XdrvMailbox.command_code;
  if ((PRESSED == button) && (NOT_PRESSED == last_state)) {  // Button pressed

    AddLog(LOG_LEVEL_DEBUG, PSTR("SHP: Button %d pressed"), button_index +1);

    // Do something with the Up,Down,Ok button
    switch (button_index) {
      case 0:  // Up
        break;
      case 1:  // Down
        break;
      case 2:  // Ok
        break;
    }
  }
  return true;                                      // Disable further button processing
}

/*********************************************************************************************\
 * Shelly Pro 1/2
\*********************************************************************************************/

void ShellyProUpdate(void) {
  /*
  Shelly Pro 1/2/PM 74HC595 register
   bit 0 = relay/led 1
   bit 1 = relay/led 2
   bit 2 = wifi led blue
   bit 3 = wifi led green
   bit 4 = wifi led red
   bit 5 - 7 = nc
   OE is connected to Gnd with 470 ohm resistor R62 AND a capacitor C81 to 3V3
   - this inhibits output of signals (also relay state) during power on for a few seconds
  */
  uint8_t val = SPro.power | SPro.ledlink;
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(val);                                // Write 74HC595 shift register
  SPI.endTransaction();
//  delayMicroseconds(2);                             // Wait for SPI clock to stop
  digitalWrite(SPro.pin_register_cs, 1);            // Latch data
  delayMicroseconds(1);                             // Shelly 10mS
  digitalWrite(SPro.pin_register_cs, 0);
}

/*********************************************************************************************\
 * Shelly Pro
\*********************************************************************************************/

void ShellyProPreInit(void) {
  if ((SPI_MOSI_MISO == TasmotaGlobal.spi_enabled) &&
      PinUsed(GPIO_SPI_CS) &&                       // 74HC595 rclk / MCP23S17
      TasmotaGlobal.gpio_optiona.shelly_pro) {      // Option_A7

    if (PinUsed(GPIO_SWT1) || PinUsed(GPIO_KEY1)) {
      SPro.detected = 1;                            // Shelly Pro 1
      if (PinUsed(GPIO_SWT1, 1) || PinUsed(GPIO_KEY1, 1)) {
        SPro.detected = 2;                          // Shelly Pro 2
      }
      SPro.ledlink = 0x18;                          // Blue led on - set by first call ShellyProPower() - Shelly 1/2
    }
    if (SHELLY_PRO_4_PIN_SPI_CS == Pin(GPIO_SPI_CS)) {
      SPro.detected = 4;                            // Shelly Pro 4PM (No SWT or KEY)
    }

    if (SPro.detected) {
      TasmotaGlobal.devices_present += SPro.detected;

      SPro.pin_register_cs = Pin(GPIO_SPI_CS);
      pinMode(SPro.pin_register_cs, OUTPUT);
      // Does nothing if SPI is already initiated (by ADE7953) so no harm done
      SPI.begin(Pin(GPIO_SPI_CLK), Pin(GPIO_SPI_MISO), Pin(GPIO_SPI_MOSI), -1);

      if (4 == SPro.detected) {
        digitalWrite(SPro.pin_register_cs, 1);      // Prep MCP23S17 chip select
        SPro.pin_mcp23s17_int = SHELLY_PRO_4_PIN_MCP23S17_INT;  // GPIO35 = MCP23S17 common interrupt
        pinMode(SPro.pin_mcp23s17_int, INPUT);
        ShellyPro4Init();                           // Init MCP23S17
      } else {
        digitalWrite(SPro.pin_register_cs, 0);      // Prep 74HC595 rclk
      }
    }
  }
}

void ShellyProInit(void) {
  int pin_lan_reset = SHELLY_PRO_PIN_LAN8720_RESET;  // GPIO5 = LAN8720 nRST
//  delay(30);                                        // (t-purstd) This pin must be brought low for a minimum of 25 mS after power on
  digitalWrite(pin_lan_reset, 0);
  pinMode(pin_lan_reset, OUTPUT);
  delay(1);                                         // (t-rstia) This pin must be brought low for a minimum of 100 uS
  digitalWrite(pin_lan_reset, 1);

  AddLog(LOG_LEVEL_INFO, PSTR("HDW: Shelly Pro %d%s initialized"), SPro.detected, (PinUsed(GPIO_ADE7953_CS))?"PM":"");
}

void ShellyProPower(void) {
  if (4 == SPro.detected) {

//    AddLog(LOG_LEVEL_DEBUG, PSTR("SHP: Set Power 0x%08X"), XdrvMailbox.index);

    power_t rpower = XdrvMailbox.index;
    for (uint32_t i = 0; i < 4; i++) {
      power_t state = rpower &1;
      SP4Mcp23S17DigitalWrite(sp4_relay_pin[i], state);
      rpower >>= 1;                                 // Select next power
    }
  } else {
    SPro.power = XdrvMailbox.index &3;
    ShellyProUpdate();
  }
}

void ShellyProUpdateLedLink(uint32_t ledlink) {
  if (4 == SPro.detected) {


  } else {
    if (ledlink != SPro.ledlink) {
      SPro.ledlink = ledlink;
      ShellyProUpdate();
    }
  }
}

void ShellyProLedLink(void) {
  if (4 == SPro.detected) {


  } else {
    /*
    bit 2 = blue, 3 = green, 4 = red
    Shelly Pro documentation
    - Blue light indicator will be on if in AP mode.
    - Red light indicator will be on if in STA mode and not connected to a Wi-Fi network.
    - Yellow light indicator will be on if in STA mode and connected to a Wi-Fi network.
    - Green light indicator will be on if in STA mode and connected to a Wi-Fi network and to the Shelly Cloud.
    - The light indicator will be flashing Red/Blue if OTA update is in progress.
    Tasmota behaviour
    - Blue light indicator will blink if no wifi or mqtt.
    - Green light indicator will be on if in STA mode and connected to a Wi-Fi network.
    */
    SPro.last_update = TasmotaGlobal.uptime;
    uint32_t ledlink = 0x1C;                        // All leds off
    if (XdrvMailbox.index) {
      ledlink &= 0xFB;                              // Blue blinks if wifi/mqtt lost
    }
    else if (!TasmotaGlobal.global_state.wifi_down) {
      ledlink &= 0xF7;                              // Green On
    }
    ShellyProUpdateLedLink(ledlink);
  }
}

void ShellyProLedLinkWifiOff(void) {
  if (4 == SPro.detected) {


  } else {
    /*
    bit 2 = blue, 3 = green, 4 = red
    - Green light indicator will be on if in STA mode and connected to a Wi-Fi network.
    */
    if (SPro.last_update +1 < TasmotaGlobal.uptime) {
      ShellyProUpdateLedLink((TasmotaGlobal.global_state.wifi_down) ? 0x1C : 0x14);  // Green off if wifi OFF
    }
  }
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv88(uint32_t function) {
  bool result = false;

  if (FUNC_MODULE_INIT == function) {
    ShellyProPreInit();
    } else if (SPro.detected) {
      switch (function) {
/*
        case FUNC_BUTTON_PRESSED:
          result = ShellyProButton();
          break;
*/
        case FUNC_EVERY_SECOND:
          ShellyProLedLinkWifiOff();
          break;
        case FUNC_SET_DEVICE_POWER:
          ShellyProPower();
          return true;
        case FUNC_LED_LINK:
          ShellyProLedLink();
          break;
        case FUNC_INIT:
          ShellyProInit();
          break;
        case FUNC_ADD_BUTTON:
          result = ShellyProAddButton();
          break;
        case FUNC_ADD_SWITCH:
          result = ShellyProAddSwitch();
          break;
      }
    }
  return result;
}

#endif  // USE_SHELLY_PRO
#endif  // USE_SPI
#endif  // ESP32
