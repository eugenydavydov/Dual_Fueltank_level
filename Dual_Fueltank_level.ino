#include <EEPROM.h>
#define INIT_KEY 20   // Uniq key, to check is this already in EEPROM or not
#define ADDRESS 0     // address in eprom to store structures;

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define i2c_Address 0x3c // initialize with the I2C addr 0x3C Typically eBay OLED's
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
// Adafruit_SH1106 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, OLED_RESET);
#define WIDTH 64
#define HEIGHT 128 // parameters after OLED screen rotation
// font size: 1 - char 5*7, 2 - char 10*14

// buttons
#include <EncButton2.h> // by AlexGyver, v.2.0.0
#define BUTTONS 3
EncButton2<EB_BTN> button[BUTTONS];    // array of buttons

#define Relay1 4 // Fuel pump relay on digital pin
#define Vin1  A0 // Fuel level sensor 1, analog pin number
#define Vin2  A1 // Fuel level sensor 2, analog pin number
#define Vin3  A2 // 12v voltage input

// Number of samples taken from analog pins
#define NUM_SAMPLES 10

uint32_t timer1 = 0;    // main loop timer
unsigned int pos;  // counter
unsigned int raw0[NUM_SAMPLES];   // array for raw analog samples from input 1
unsigned int raw1[NUM_SAMPLES];   // array for raw analog samples from input 2
unsigned int sorted[NUM_SAMPLES]; // temporary array for sorting
unsigned long Aver;  // Average raw value calculated from all samples in array
int Summ ;           // Summary, liters

// Voltage splitter for 12v imput sensor.
#define VRef    5280 // Reference voltage in mV, 5v.
#define Rlo     9750 //ohm  (10 kOhm) between analog pin and gnd
#define Rhi    96000 //ohm (100 kOhm) between analog pin and voltage input
unsigned long SysVol;  // System voltage

struct FuelTankStruct {             // Variables calculated during work
  unsigned long V; // Voltage, mV
  int L;           // Volume, liter
  byte Perc;      // Percent of full load
} ;
FuelTankStruct tank[2];           // Use 2 fuel tanks.

struct FuelTankStoredStruct {       // Variables stored in EEPROM
  unsigned int  Vo[5]; // Voltage values for sensor, mV
  unsigned int  Li[5]; // Fuel tank values, liters
} ;
FuelTankStoredStruct tankVal[2];  // Use 2 fuel tanks.

bool led = false;               // Make onboard led blink to see that main loop is working.

const bool debug_osd = true;          // debug on OLED screen
static bool relayState = false; // Relay state, to control fuel pump.

void setup() {
  delay(1500);                        // For arduino leonardo, to allow serial usb up and connect

  // voltage inputs
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(Vin1, INPUT);
  pinMode(Vin2, INPUT);
  pinMode(Vin3, INPUT);

  // relay
  pinMode(Relay1, OUTPUT);
  digitalWrite(Relay1, LOW);
  relayState = false;

  // button setup
  button[0].setPins(INPUT_PULLUP, 7); // up (minus)
  button[1].setPins(INPUT_PULLUP, 9); // select
  button[2].setPins(INPUT_PULLUP, 8); // down (plus)

  // EEPROM.write(ADDRESS, 0);   // force reinit eeprom
  // for (int i = 0; i < 1024; i++) { EEPROM.write(i,255);}  // force clear all eeprom

  // read structure from eeprom
  if (EEPROM.read(ADDRESS) != INIT_KEY) { // check key
    EEPROM.write(ADDRESS, INIT_KEY);    // update key
    for (int t = 0; t < 2; t++) {
      for (int c = 0; c < 5; c++) {
        tankVal[t].Vo[c] = 2500 - c * 500;
        tankVal[t].Li[c] = c * 10;
      }
    }
    EEPROM.put(ADDRESS + 1, tankVal[0]);
    EEPROM.put(ADDRESS + 21, tankVal[1]);
  }
  EEPROM.get(ADDRESS + 1, tankVal[0]);
  EEPROM.get(ADDRESS + 21, tankVal[1]);

  display.begin(i2c_Address, true);
  display.setRotation(3);
  display.display();
  delay(500);

  // Clear the buffer.
  display.clearDisplay();

  // fill arrays with some default values
  for (int c = 0; c < NUM_SAMPLES; c++) {
    raw0[c] = analogRead(Vin1);
    raw1[c] = analogRead(Vin2);
  }

}

void loop() {

  // once in 10 sec (10000 ms)
  if ( millis() - timer1 >= 1000) {
    timer1 = millis();                   // reset timer

    pos++;
    // cycle position
    if ( pos == NUM_SAMPLES ) {
      pos = 0;

      // check do we need to start fuel pump once in 10 cycles.
      // If first tank has less %, that second one - start pump. Hysteresis 10%
      if ( tank[0].Perc < (tank[1].Perc + 5) )        relayState = true;
        else if ( tank[0].Perc > (tank[1].Perc - 5) )   relayState = false;
      // don't allow pump to work if there too high level in Fuel tank 1 or if too low fuel in Fuel tank 2
      if ( tank[0].Perc > 90 or tank[1].Perc < 4 )      relayState = false;
      digitalWrite(Relay1, relayState);
    }

    // make led blink once in 10 second
    led = !led;
    if ( led ) { digitalWrite(LED_BUILTIN, HIGH);
    } else {     digitalWrite(LED_BUILTIN, LOW); }

    // get voltage from pins, store in arrays
    raw0[pos] = analogRead(Vin1);
    raw1[pos] = analogRead(Vin2);
    SysVol    = analogRead(Vin3);

    // calculate median value based on last 10 readings
    memcpy(sorted, raw0, sizeof(raw0[0])*NUM_SAMPLES );        // Copy to sorted array
    qsort(sorted, NUM_SAMPLES, sizeof(sorted[0]), ArrayCompare); // Sort sorted array
    Aver = (sorted[4] + sorted[5]) / 2;                          // Take 2 values from middle, calculate average
    tank[0].V = (long)Aver * VRef / 1024;                            // Convert average raw to voltage, mV

    memcpy(sorted, raw1, sizeof(raw1[0])*NUM_SAMPLES );
    qsort(sorted, NUM_SAMPLES, sizeof(sorted[0]), ArrayCompare);
    Aver = (sorted[4] + sorted[5]) / 2;
    tank[1].V = (long)Aver * VRef / 1024;

    // display
    ClearDisplay();
    display.drawLine(0, HEIGHT / 3 * 2, WIDTH - 1, HEIGHT / 3 * 2, SH110X_WHITE);

    display.setTextSize(2);
    display.setCursor(0, 5);  display.println("1:");
    display.setCursor(0, 35); display.println("2:");

    Summ = 0;

    // map volts to liters
    tank[0].L = CalcMapLiter(0, tank[0].V);
    // check limits
    if ( tank[0].L >= -10 and tank[0].L <= tankVal[0].Li[4] + 10 ) {
      // map value to percents
      tank[0].Perc = map(tank[0].L, 0, tankVal[0].Li[4], 0, 100 );

      // display
      display.setTextSize(2);
      if      ( tank[0].L >= 100 )        display.setCursor(WIDTH - 12 * 3, 5);
      else if ( tank[0].L < 100 and tank[0].L >= 10)        display.setCursor(WIDTH - 12 * 2, 5);
      else if ( tank[0].L < 10 and tank[0].L >= 0)        display.setCursor(WIDTH - 12, 5);
      else if ( tank[0].L < 0 )          display.setCursor(WIDTH - 12 * 2, 5);

      display.print(tank[0].L); 
      Summ += tank[0].L;

      // percent
      display.setTextSize(1);
      display.setCursor(0, 23);
      display.print(F("[")); display.print(tank[0].Perc); display.print(F("%]"));
    } else {
      tank[0].Perc = 0 ;
      display.setTextSize(1);
      display.setCursor(WIDTH - 14 * 3, 3);  display.print(F("Error"));
    }

    // map volts to liters
    tank[1].L = CalcMapLiter(1, tank[1].V);
    if ( tank[1].L >= -10 and tank[1].L <= tankVal[1].Li[4] + 10 ) {
      // map value to percents
      tank[1].Perc = map(tank[1].L, 0, tankVal[1].Li[4], 0, 100 );

      // display
      display.setTextSize(2);
      if      ( tank[1].L >= 100 )        display.setCursor(WIDTH - 12 * 3, 35);
      else if ( tank[1].L < 100 and tank[1].L >= 10)        display.setCursor(WIDTH - 12 * 2, 35);
      else if ( tank[1].L < 10 and tank[1].L >= 0 )        display.setCursor(WIDTH - 12, 35);
      else if ( tank[1].L < 0 )        display.setCursor(WIDTH - 12 * 2, 35);
      display.print(tank[1].L); 
      Summ += tank[1].L;

      // percent
      display.setTextSize(1);
      display.setCursor(0, 51);
      display.print(F("[")); display.print(tank[1].Perc); display.print(F("%]"));
    } else {
      tank[1].Perc = 0 ;
      display.setTextSize(1);
      display.setCursor(WIDTH - 14 * 3, 35);  display.print(F("Error"));
    }
    
    // Summary
    display.setTextSize(2);
    if        ( Summ >= 100 )      display.setCursor(WIDTH - 12 * 3, HEIGHT / 3 * 2 - 20);
    else if ( Summ < 100 and Summ >= 10)      display.setCursor(WIDTH - 12 * 2, HEIGHT / 3 * 2 - 20);
    else if ( Summ < 10 and Summ >= 0)      display.setCursor(WIDTH - 12,  HEIGHT / 3 * 2 - 20);
    else if ( Summ < 0 )      display.setCursor(WIDTH - 12 * 2, HEIGHT / 3 * 2 - 20);
    display.print(Summ);

    // show voltages
    display.setTextSize(1);
    display.setCursor(0, HEIGHT / 3 * 2 + 5);
    display.print(tank[0].V); display.print(F(" mV "));
    display.setCursor(0, HEIGHT / 3 * 2 + 15);
    display.print(tank[1].V); display.print(F(" mV "));
    display.setCursor(0, HEIGHT / 3 * 2 + 25);
    SysVol = (double) analogRead(Vin3) * VRef * (Rhi+Rlo) / 1024 / Rlo;
    display.print(SysVol/1000); display.print("."); display.print(SysVol%1000/100); display.print(F(" V"));
    //display.print(SysVol); display.print(F(" mV"));
    
    if ( relayState ) {
      // drive arrow, to indicate fuel pump working
      display.drawLine(9, 66, 15, 70, SH110X_WHITE);
      display.drawLine(0, 70, 15, 70, SH110X_WHITE);
      display.drawLine(9, 74, 15, 70, SH110X_WHITE);
    }

    display.display();
  } // end time cycle

  // get key state
  for (int i = 0; i < BUTTONS ; i++) {
    if ( button[i].tick() ) {
      if (button[i].click()) MainMenu();
    }
  } // end buttons
}

int ArrayCompare(const int *AFirst, const int *ASecond) {
  if (*AFirst < *ASecond) return -1;
  return (*AFirst == *ASecond) ? 0 : 1;
}

int CalcMapLiter (int N, int Resistance) {
  // resistance more Vo[4]
  if (Resistance < tankVal[N].Vo[4])
    return map(Resistance, tankVal[N].Vo[3], tankVal[N].Vo[4], tankVal[N].Li[3], tankVal[N].Li[4] );

  // for resistance in [0-4]
  for (int i = 4; i > 0 ; i--) {
    if (Resistance < tankVal[N].Vo[i - 1] )
      return map(Resistance, tankVal[N].Vo[i-1], tankVal[N].Vo[i], tankVal[N].Li[i-1], tankVal[N].Li[i] );
  }

  // resistance less Vo[0]
  return map(Resistance, tankVal[N].Vo[0], tankVal[N].Vo[1], tankVal[N].Li[0], tankVal[N].Li[1] );
}

void MainMenu () {
  String MItem[] = {F("Tank 1"), F("Tank 2"), F("Save"), F("Back")};
  byte pos = 0;
  byte MaxPos = 4;

  while ( true ) {
    ClearDisplay();
    // menu
    display.setTextSize(1);
    for ( byte i = 0; i < 4; i++) {
      display.setCursor(0, i * 10 + 5);
      if ( i == pos ) display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        else          display.setTextColor(SH110X_WHITE);
      display.println(MItem[i]);
    }
    display.display();

    // buttons
    for (int i = 0; i < BUTTONS ; i++) {
      if ( button[i].tick() ) {
        if (button[0].click()) { 
          if (pos > 0)        {  pos--; }
        }
        if (button[1].click()) {
          if (pos < MaxPos - 1) { pos++;  }
        }
        if (button[0].hold() and (pos > 2) )          {  pos = pos - 2; }
        if (button[1].hold() and (pos < MaxPos - 3))   { pos = pos + 2; }
        if (button[2].click()) {
          switch (pos) {
            case 0:
              TankCalibMenu(0);
              break;
            case 1:
              TankCalibMenu(1);
              break;
            case 2:
              EEPROM.put(ADDRESS + 1, tankVal[0]);
              EEPROM.put(ADDRESS + 21, tankVal[1]);
              return;
              break;
            case 3:
              return;
              break;
          }
        }
      }
    } // end buttons
  } /// end while;
}

void TankCalibMenu (int tankNumber) {
  byte posit = 0; // changed by buttons, 0-11
  byte cur = 0;  // half of posit, 0-5
  byte MaxPos = 11;

  while ( true ) {
    ClearDisplay();
    // menu
    display.setTextSize(1);
    display.setCursor(0, 5);
    display.print(F("> Tank ")); display.print(tankNumber + 1);

    for ( byte i = 0; i < 5; i++) {
      display.setCursor(0, i * 10 + 15);
      display.setTextColor(SH110X_WHITE); display.print(i + 1); display.print(":");

      if ( i == cur and posit % 2 == 0 )  display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        else                              display.setTextColor(SH110X_WHITE);
      
      display.print(tankVal[tankNumber].Li[i]);
      display.setTextColor(SH110X_WHITE); display.print("-");
      if ( i == cur and posit % 2 > 0 )  display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        else                             display.setTextColor(SH110X_WHITE);
      display.print(tankVal[tankNumber].Vo[i]);
    }

    display.setCursor(0, 5 * 10 + 15);
    if ( posit == 10 )                 display.setTextColor(SH110X_BLACK, SH110X_WHITE);
      else                             display.setTextColor(SH110X_WHITE);
    display.print(F("Back"));
    display.setTextColor(SH110X_WHITE);

    display.display();

    // buttons
    for (int i = 0; i < BUTTONS ; i++) {
      if ( button[i].tick() ) {
        if (button[0].click()) {
          if (posit > 0)        {  posit--; cur = posit / 2;   }
        }
        if (button[1].click()) {
          if (posit < MaxPos - 1) { posit++;  cur = posit / 2;  }
        }
        if (button[2].click()) {
          if  ( posit < 10) {
            if ( posit % 2 == 0 ) {
              tankVal[tankNumber].Li[cur] = SetValueMenu(tankNumber, "Liters", tankVal[tankNumber].Li[cur]);
            }
            else                  {
              SetVoltageMenu(tankNumber, cur, "mV", tankVal[tankNumber].Vo[cur] );
            }
          } else if ( posit == 10 ) {
            return;
          }
        }
      }
    } // end buttons
  } /// end while;
}

int SetValueMenu (int tankN, String Value, int defValue) {
  int MaxPos = 5000;

  while ( true ) {
    ClearDisplay();
    // menu
    display.setTextSize(1);

    display.setCursor(0, 5);
    display.print(F("> Tank ")); display.print(tankN + 1);

    display.setCursor(0, 15);
    display.print(F(">> "));

    display.setTextColor(SH110X_BLACK, SH110X_WHITE);
    display.setCursor(0, 25);
    display.println(defValue);

    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 35);
    display.println(Value);

    display.display();

    // buttons
    for (int i = 0; i < BUTTONS ; i++) {
      if ( button[i].tick() ) {
        if ( button[0].click() and (defValue > 0) )       { defValue--;  }
        if ( button[0].hold()  and (defValue > 2) )       { defValue = defValue - 2; }
        if ( button[1].click() and (defValue < MaxPos - 1)) { defValue++;   }
        if ( button[1].hold()  and (defValue < MaxPos - 3)) { defValue = defValue + 2;  }
        if ( button[2].click() )                          { return defValue;    }
      }
    } // end buttons
  } /// end while;

}

int AutoResistanceMenu(int tankN) {
  unsigned int out;
  unsigned int count = 0;
  uint32_t timer2 = 0;
  unsigned int tmp[NUM_SAMPLES];
  unsigned int Av;

  for (int c = 0; c < NUM_SAMPLES; c++) {
    if (tankN == 0 ) { tmp[c] = analogRead(Vin1); }
    else           {   tmp[c] = analogRead(Vin2); }
  }

  timer2 = millis();

  while (! button[2].click() )  {
    // once in 1 sec (1000 ms
    if (millis() - timer2 >= 1000) {
      timer2 = millis();                   // reset timer

      ClearDisplay();
      // menu
      display.setTextSize(1);
      display.setCursor(0, 5);
      display.print("> Tank "); display.print(tankN + 1);

      count++;
      if ( count >= NUM_SAMPLES ) {
        count = 0;
      }

      if (tankN == 0 ) {  tmp[count] = analogRead(Vin1);  }
      else           {    tmp[count] = analogRead(Vin2);  }

      for (int c = 0; c < NUM_SAMPLES; c++) {
        display.setCursor(0, 15 + 10 * c);
        display.println(tmp[c]);
      }

      memcpy(sorted, tmp, sizeof(raw0[0])*NUM_SAMPLES );
      qsort(sorted, NUM_SAMPLES, sizeof(sorted[0]), ArrayCompare); // Sort sorted array
      Av = (sorted[4] + sorted[5]) / 2;                          // Take 2 values from middle, calculate average
      out = (long)Av * VRef / 1024;                             // Convert average raw to voltage, mV
      display.setCursor(27, 55);
      display.println("Avg.");
      display.setCursor(27, 65);
      display.println(Av);
      display.setCursor(27, 85);
      display.setTextColor(SH110X_BLACK, SH110X_WHITE);
      display.print(out);
      display.setTextColor(SH110X_WHITE);
      display.print("mV");
      display.display();

    }

    button[2].tick();
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 5);
  display.print(F("> Tank ")); display.print(tankN + 1);
  display.setCursor(0, 10 + 25);
  display.print(out);
  display.display();
  delay(1000);
  return out;
}

int SetVoltageMenu (int tankN, int cur, String Value, int defValue) {
  String MItem[] = {F("Automatic"), F("Manual"), F("Back")};
  byte pos = 0;
  byte MaxPos = 3;

  while ( true ) {
    ClearDisplay();
    // menu
    display.setTextSize(1);

    display.setCursor(0, 5);
    display.print(F("> Tank ")); display.print(tankN + 1);

    display.setCursor(0, 15);
    display.print(F(">> "));

    for ( byte i = 0; i < MaxPos; i++) {
      display.setCursor(0, i * 10 + 25);
      if ( i == pos ) { display.setTextColor(SH110X_BLACK, SH110X_WHITE);
      } else {          display.setTextColor(SH110X_WHITE);  }
      display.println(MItem[i]);
    }

    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 35);
    display.println(Value);

    display.display();

    // buttons
    for (int i = 0; i < BUTTONS ; i++) {
      if ( button[i].tick() ) {
        if ( button[0].click() and (pos > 0) )       {
          pos--;
        }
        if ( button[1].click() and (pos < MaxPos - 1)) {
          pos++;
        }
        if ( button[2].click() )
          switch (pos) {
            case 0:
              tankVal[tankN].Vo[cur] = AutoResistanceMenu(tankN);
              break;
            case 1:
              tankVal[tankN].Vo[cur] = SetValueMenu(tankVal[tankN].Li[cur], "mV",  tankVal[tankN].Vo[cur]);
              break;
            case 2:
              return;
              break;
          }
      }
    } // end buttons
  } /// end while;
}

void ClearDisplay () {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  // lines
  display.drawLine(0,        0, WIDTH - 1, 0,        SH110X_WHITE);
  if ( debug_osd ) {
    display.setCursor(0, HEIGHT - 8);
    display.print(memoryFree()); display.print(F("b "));
    // uptime
    display.print(  millis()/1000 );
  } else {
    display.drawLine(0, HEIGHT - 1, WIDTH - 1, HEIGHT - 1, SH110X_WHITE);
  }
}

extern int __bss_end;
extern void *__brkval;
// Calculate free Ram used by variables function
int memoryFree() {
  int freeValue;
  if ((int)__brkval == 0)
    freeValue = ((int)&freeValue) - ((int)&__bss_end);
  else
    freeValue = ((int)&freeValue) - ((int)__brkval);
  return freeValue;
}
