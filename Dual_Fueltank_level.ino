#include <EEPROM.h>
#define INIT_KEY 21                  // Uniq key, to check is this already in EEPROM or not
#define ADDRESS 0                    // address in eprom to store structures;

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define i2c_Address 0x3c              // initialize with the I2C addr 0x3C Typically eBay OLED's
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1             // Reset pin # (or -1 if sharing Arduino reset pin)
// Adafruit_SH1106 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, OLED_RESET);
#define WIDTH  128
#define HEIGHT  64
// font size: 1 - char 6*8, 2 - char 11*15
#define CHARW 11                      // character width
#define CHARH 15                      // character heigth

#define W1 0                          // start width  for tank1
#define H1 32                         // start heigth for tank1
#define W2 65                         // start width  for tank2
#define H2 32                         // start heigth for tank2

// buttons
#include <EncButton2.h>               // by AlexGyver, v.2.0.0
#define BUTTONS 3
EncButton2<EB_BTN> button[BUTTONS];   // array of buttons
	
//#include <Adafruit_ADS1X15.h>
//Adafruit_ADS1115 ads;  /* Use this for the 16-bit version */
//#define Resolution 16384            // adc resolution, 10bit=1024, 16bit=16384
#define Resolution 1024               // Analog pin resolution

#define Relay1 3                      // Fuel pump relay on digital pin

#define Vin1  A7 // Fuel level sensor 1, analog pin number, tank1, measure Volts. 
#define Vin2  A6 // Fuel level sensor 2, analog pin number, tank2, measure Ohms.

// Number of samples taken from analog pins
#define NUM_SAMPLES 10

uint32_t timer1 = 0;                  // main loop timer
unsigned int pos;                     // counter
unsigned int raw0[NUM_SAMPLES];       // array for raw analog samples from input 1
unsigned int raw1[NUM_SAMPLES];       // array for raw analog samples from input 2
unsigned int sorted[NUM_SAMPLES];     // temporary array for sorting
unsigned long Aver;                   // Average raw value calculated from all samples in array
int Summ ;                            // Summary, liters

// Voltage splitter for 12v input sensor.
#define VRef    5000                  // Reference voltage in mV, 5v.
#define Rlo     9780                  //ohm (10 kOhm) between analog pin and gnd
#define Rhi    19560                  //ohm (20 kOhm) between analog pin and voltage input
unsigned long SysVol;                 // System voltage

struct FuelTankStruct {               // Variables calculated during work
  unsigned long V;                    // Voltage, mV
  int L;                              // Volume, liter
  byte Perc;                          // Percent of full load
} ;
FuelTankStruct tank[2];               // Use 2 fuel tanks.

struct FuelTankStoredStruct {         // Variables stored in EEPROM
  unsigned int  Vo[2];                // Voltage values for sensor, mV
  unsigned int  Li[2];                // Fuel tank values, liters
} ;
FuelTankStoredStruct tankStored[2];   // Use 2 fuel tanks.

bool led = false;                     // Make onboard led blink to see that main loop is working.

const bool debug_osd = false;         // debug on OLED screen
static bool relayState = false;       // Relay state, to control fuel pump.

byte anim;                            // animation step

void setup() {
  delay(1500);                        // For arduino leonardo, to allow serial usb up and connect

  // voltage inputs
  pinMode(LED_BUILTIN, OUTPUT);

  // relay (first init with "high", then pinmode, avoid short "on" on power on)
  relayState = false;
  digitalWrite(Relay1, ! relayState);
  pinMode(Relay1, OUTPUT);


  // button setup
  button[0].setPins(INPUT_PULLUP, 7);   // up (minus)
  button[1].setPins(INPUT_PULLUP, 9);   // down (plus)
  button[2].setPins(INPUT_PULLUP, 8);   // select

  //EEPROM.begin(4096);                 // ESP8266 only: define eeprom size
  // EEPROM.write(ADDRESS, 0);          // force reinit eeprom
  // for (int i = 0; i < Resolution; i++) { EEPROM.write(i,255);}  // force clear all eeprom

  // read structure from eeprom
  if (EEPROM.read(ADDRESS) != INIT_KEY) { // check key
    EEPROM.write(ADDRESS, INIT_KEY);      // update key
    for (int t = 0; t < 2; t++) {
      for (int c = 0; c < 2; c++) {
        tankStored[t].Vo[c] = 2500 - c * 500;
        tankStored[t].Li[c] = c * 10;
      }
    }
    EEPROM.put(ADDRESS + 1, tankStored[0]);
    EEPROM.put(ADDRESS + 21, tankStored[1]);
    //EEPROM.commit();                     // ESP8266 only: commit changes from shadow ram to flash.
  }
  EEPROM.get(ADDRESS + 1, tankStored[0]);
  EEPROM.get(ADDRESS + 21, tankStored[1]);

  // init AD1115 converter 
  //if (!ads.begin())
  //{
  //  Serial.println("Failed to initialize ADS.");
  //  while (1);
  //}

  display.begin(i2c_Address, true);
  display.setRotation(0);
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
  if ( millis() - timer1 >= 500) {
    timer1 = millis();                // reset timer

    pos++;
    // cycle position
    if ( pos == NUM_SAMPLES ) {
      pos = 0;

      // check do we need to start fuel pump once in 10 cycles.
      // If first tank has less 50%, start pump
      if (  tank[0].L <  tankStored[0].Li[1] / 2 )        relayState = true;
        else if (  tank[0].L > tankStored[0].Li[1] - 5 )   relayState = false;
      // don't allow pump to work if there too high level in Fuel tank 1 or if too low fuel in Fuel tank 2
      if (  tank[1].L < 6 )                     relayState = false;
      digitalWrite(Relay1, ! relayState);
    }

    // make led blink once in 10 second
    led = !led;
    if ( led ) { digitalWrite(LED_BUILTIN, HIGH);
    } else {     digitalWrite(LED_BUILTIN, LOW); }

    // get voltage from pins, store in arrays
    raw0[pos] = analogRead(Vin1);
    raw1[pos] = analogRead(Vin2);
    //SysVol    = ads.readADC_SingleEnded(2);

    // calculate median value based on last 10 readings
    memcpy(sorted, raw0, sizeof(raw0[0])*NUM_SAMPLES );          // Copy to sorted array
    //qsort(sorted, NUM_SAMPLES, sizeof(sorted[0]), ArrayCompare); // Sort sorted array
    Aver = (sorted[4] + sorted[5]) / 2;                          // Take 2 values from middle, calculate average
    tank[0].V = (long)Aver * VRef / Resolution;                  // Convert average raw to voltage, mV

    memcpy(sorted, raw1, sizeof(raw1[0])*NUM_SAMPLES );
    //qsort(sorted, NUM_SAMPLES, sizeof(sorted[0]), ArrayCompare);
    Aver = (sorted[4] + sorted[5]) / 2;
    tank[1].V = (long)Aver * VRef / Resolution;

    // display
    ClearDisplay();
    display.drawLine(0, H1, WIDTH - 1, H1, SH110X_WHITE); // second line
    display.drawLine(63, 32, 63, 64, SH110X_WHITE); // third line
    
    display.setTextSize(2);
    display.setCursor(0, 2); display.print(F("Fuel"));
    display.setTextSize(1);
    display.setCursor(W1, H1+2); display.print(F("Main"));
    display.setCursor(W2, H2+2); display.print(F("Aux"));

    Summ = 0;

    // tank 1 **********************************************************************************
    // map volts to liters
    tank[0].L = CalcMapLiter(0, tank[0].V);
    // check limits
    if ( tank[0].L >= -10 and tank[0].L <= tankStored[0].Li[1] + 10 ) {
      // map value to percents
      tank[0].Perc = map(tank[0].L, 0, tankStored[0].Li[1], 0, 100 );

      // display tank1
      display.setTextSize(2);
      if      ( tank[0].L >= 100 )                     display.setCursor(WIDTH/2 - 12 * 3 - 2, H1+2);
      else if ( tank[0].L < 100 and tank[0].L >= 10)   display.setCursor(WIDTH/2 - 12 * 2 - 2, H1+2);
      else if ( tank[0].L < 10 and tank[0].L >= 0)     display.setCursor(WIDTH/2 - 12     - 2, H1+2);
      else if ( tank[0].L < 0 )                        display.setCursor(WIDTH/2 - 12 * 2 - 2, H1+2);
      display.print(tank[0].L);
      Summ += tank[0].L;

      // percent
      display.setTextSize(1);
      display.setCursor(W1, H1+2+18); display.print(F("[________]"));
      display.fillRect(W1+6, H1+2+18, map(tank[0].L,1,tankStored[0].Li[1],1,47), 7, SH110X_WHITE);
    } else {
      tank[0].Perc = 0 ;
      display.setTextSize(1);
      display.setCursor(W1+4+24, H1+2);  display.print(F("Error"));
      display.setCursor(W1+4+8, H1+2+8); display.print(tank[0].V); display.print(F(" mV "));
    }

    // tank 2 **********************************************************************************
    // map volts to liters
    tank[1].L = CalcMapLiter(1, tank[1].V);
    if ( tank[1].L >= -10 and tank[1].L <= tankStored[1].Li[1] + 10 ) {
      // map value to percents
      tank[1].Perc = map(tank[1].L, 0, tankStored[1].Li[1], 0, 100 );

      // display tank2
      display.setTextSize(2);
      if      ( tank[1].L >= 100 )                     display.setCursor(WIDTH - 12 * 3 - 2, H2+2);
      else if ( tank[1].L < 100 and tank[1].L >= 10)   display.setCursor(WIDTH - 12 * 2 - 2, H2+2);
      else if ( tank[1].L < 10 and tank[1].L >= 0 )    display.setCursor(WIDTH - 12     - 2, H2+2);
      else if ( tank[1].L < 0 )                        display.setCursor(WIDTH - 12 * 2 - 2, H2+2);
      display.print(tank[1].L);
      Summ += tank[1].L;

      // percent
      display.setTextSize(1);
      display.setCursor(W2, H2+2+18); display.print(F("[________]"));
      display.fillRect(W2+6, H2+2+18, map(tank[1].L,1,tankStored[1].Li[1],1,47), 7, SH110X_WHITE);
    } else {
      tank[1].Perc = 0 ;
      display.setTextSize(1);
      display.setCursor(W2+4+24, H2+2);  display.print(F("Error"));
      display.setCursor(W2+4+8, H2+2+8); display.print(tank[1].V); display.print(F(" mV "));
    }
    
    // Summary **********************************************************************************
    display.setTextSize(2);
    if        ( Summ >= 100 )                          display.setCursor(WIDTH - 12 * 3 - 20, 2);
    else if ( Summ < 100 and Summ >= 10)               display.setCursor(WIDTH - 12 * 2 - 20, 2);
    else if ( Summ < 10 and Summ >= 0)                 display.setCursor(WIDTH - 12     - 20, 2);
    else if ( Summ < 0 )                               display.setCursor(WIDTH - 12 * 2 - 20, 2);
    display.print(Summ); 
    display.setTextSize(1);  display.setCursor(WIDTH -  20, 9); display.print("lit");

    // show voltages
    display.setTextSize(1); 
    display.setCursor(3, 1*CHARH+6); display.print(F("[__________________]"));
    display.fillRect(9, 1*CHARH+6, map(Summ,1,tankStored[0].Li[1]+tankStored[1].Li[1],1,111), 7, SH110X_WHITE);
    
    //display.setCursor(0, HEIGHT / 3 * 2 + 25); 
    //SysVol = (double) analogRead(Vin3) * VRef * (Rhi+Rlo) / Resolution / Rlo;
    //display.print(SysVol/1000); display.print("."); display.print(SysVol%1000/100); display.print(F(" V"));
    //display.print(SysVol); display.print(F(" mV"));
  
    if ( relayState ) {
      anim++;
      if ( anim>3 ) { anim = 0; }      
      // drive arrow, to indicate fuel pump working
      // animation
      display.fillTriangle(W2-15-anim*3, H2+20-3, W2-15-10-anim*3, H2+20+5-3, W2-15-anim*3, H2+20+10-3, SH110X_WHITE);
      display.fillRect(W2-15-anim*3, H2+20+3-3, 10, 6, SH110X_WHITE);
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
  // resistance less Vo[0]
  return map(Resistance, tankStored[N].Vo[0], tankStored[N].Vo[1], tankStored[N].Li[0], tankStored[N].Li[1] );
}

void MainMenu () {
  String MItem[] = {F("Tank 1"), F("Tank 2"), F("Save"), F("Back")};
  byte pos = 0;
  byte MaxPos = 4;

  while ( true ) {
    ClearDisplay();
    // menu
    display.setTextSize(1);
    display.setCursor(0, 5);
    display.print(F("Menu"));
    display.drawLine(0, 13, WIDTH - 1, 13, SH110X_WHITE); // menu line

    for ( byte i = 0; i < 4; i++) {
      display.setCursor(0, i * 10 + 15);
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
              EEPROM.put(ADDRESS + 1, tankStored[0]);
              EEPROM.put(ADDRESS + 21, tankStored[1]);
              //EEPROM.commit();      // ESP8266 only: commit changes from shadow ram to flash.
              return;
              break;
            case 3:
              return;
              break;
          }
        }
      }
    }                                 // end buttons
  }                                   // end while;
}

void TankCalibMenu (int tankNumber) {
  byte posit = 0;                     // Cursor position, changed by buttons, 0-4
  byte cur = 0;                       // half of posit, 0-2
  byte MaxPos = 5;

  while ( true ) {
    ClearDisplay();
    // menu
    display.setTextSize(1);
    display.setCursor(0, 5);
    display.print(F("M>Tank ")); display.print(tankNumber + 1);
    display.drawLine(0, 13, WIDTH - 1, 13, SH110X_WHITE); // menu line

    for ( byte i = 0; i < 2; i++) {
      display.setCursor(0, i * 10 + 15);
      display.setTextColor(SH110X_WHITE); display.print(i + 1); display.print(":");

      if ( i == cur and posit % 2 == 0 )  display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        else                              display.setTextColor(SH110X_WHITE);
      
      display.print(tankStored[tankNumber].Li[i]);
      display.setTextColor(SH110X_WHITE); display.print("-");
      if ( i == cur and posit % 2 > 0 )  display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        else                             display.setTextColor(SH110X_WHITE);
      display.print(tankStored[tankNumber].Vo[i]);
    }

    display.setCursor(0, 25+10);
    if ( posit == MaxPos - 1 )                  display.setTextColor(SH110X_BLACK, SH110X_WHITE);
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
          if  ( posit < 4) {
            if ( posit % 2 == 0 ) {
              tankStored[tankNumber].Li[cur] = SetValueMenu(tankNumber, "Liters", tankStored[tankNumber].Li[cur]);
            }
            else                  {
              SetVoltageMenu(tankNumber, cur, "mV", tankStored[tankNumber].Vo[cur] );
            }
          } else if ( posit == 4 ) {
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
    display.print(F("M>T")); display.print(tankN + 1); display.print(F(">Set")); 
    display.drawLine(0, 13, WIDTH - 1, 13, SH110X_WHITE); // menu line

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
      timer2 = millis();              // reset timer

      ClearDisplay();
      // menu
      display.setTextSize(1);
      display.setCursor(0, 5);
      display.print("M>T"); display.print(tankN + 1); display.print(">mV>Auto set");
      display.drawLine(0, 13, WIDTH - 1, 13, SH110X_WHITE); // menu line

      count++;
      if ( count >= NUM_SAMPLES ) {
        count = 0;
      }

      if (tankN == 0 ) {  tmp[count] = analogRead(Vin1);  }
      else           {    tmp[count] = analogRead(Vin2);  }

      for (int c = 0; c < NUM_SAMPLES; c++) {
        if ( c > 4 ) { display.setCursor(30, 15 + 10*c-50); }
         else { display.setCursor(0, 15 + 10 * c); }
        display.println(tmp[c]);
      }

      memcpy(sorted, tmp, sizeof(raw0[0])*NUM_SAMPLES );
     // qsort(sorted, NUM_SAMPLES, sizeof(sorted[0]), ArrayCompare); // Sort sorted array
      Av = (sorted[4] + sorted[5]) / 2;           // Take 2 values from middle, calculate average
      out = (long)Av * VRef / Resolution;         // Convert average raw to voltage, mV
      display.setCursor(70, 20);
      display.println("Avg.");
      display.setCursor(70, 30);
      display.println(Av);
      display.setCursor(70, 40);
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
  display.print(F(">Tank ")); display.print(tankN + 1);
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
    display.print(F("M>T")); display.print(tankN + 1); display.print(F(">mV"));
    display.drawLine(0, 13, WIDTH - 1, 13, SH110X_WHITE); // menu line

    for ( byte i = 0; i < MaxPos; i++) {
      display.setCursor(0, i * 10 + 25);
      if ( i == pos ) { display.setTextColor(SH110X_BLACK, SH110X_WHITE);
      } else {          display.setTextColor(SH110X_WHITE);  }
      display.println(MItem[i]);
    }

    //display.setTextColor(SH110X_WHITE);
    //display.setCursor(0, 35);
    //display.println(Value);

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
              tankStored[tankN].Vo[cur] = AutoResistanceMenu(tankN);
              break;
            case 1:
              tankStored[tankN].Vo[cur] = SetValueMenu(tankN, "mV",  tankStored[tankN].Vo[cur]);
              break;
            case 2:
              return(0);
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
  display.drawLine(0,        0, WIDTH - 1, 0,        SH110X_WHITE); //top
  if ( debug_osd ) {
    display.setCursor(0, HEIGHT - 8);
    display.print(memoryFree()); display.print(F("b "));
    // uptime
    display.print(  millis()/1000 );
  } else {
    display.drawLine(0, HEIGHT - 1, WIDTH - 1, HEIGHT - 1, SH110X_WHITE); // bottom
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
