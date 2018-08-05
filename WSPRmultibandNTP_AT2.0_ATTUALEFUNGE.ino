// Si5351_WSPR
//
// Simple WSPR beacon for Arduino Uno, with the Etherkit Si5351A Breakout
// Board, by Jason Milldrum NT7S.
// 
// Original code based on Feld Hell beacon for Arduino by Mark 
// Vandewettering K6HX, adapted for the Si5351A by Robert 
// Liesenfeld AK6L <ak6l@ak6l.org>.  Timer setup
// code by Thomas Knutsen LA3PNA.
//
// Time code adapted from the TimeSerial.ino example from the Time library.

// Hardware Requirements
// ---------------------
// This firmware must be run on an Arduino Zero capable microcontroller
//
// Required Libraries
// ------------------
// Etherkit Si5351 (Library Manager)
// Etherkit JTEncode (Library Manager)
// Time (Library Manager)
// Wire (Arduino Standard Library)
//
// License
// -------
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
// ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#include <si5351.h>
#include <JTEncode.h>
#include <int.h>
#include <TimeLib.h>

#include "Wire.h"

#define TONE_SPACING            146           // ~1.46 Hz
#define WSPR_CTC                10672         // CTC value for WSPR
#define SYMBOL_COUNT            WSPR_SYMBOL_COUNT
#define CORRECTION              -12000             // Change this for your ref osc

#define TX_LED_PIN              12
#define SYNC_LED_PIN            13

// serial monitor for debug output
#include <SoftwareSerial.h>
#define ARDUINO_RX_PIN 5    // set here the pin number you use for software Serial Rx
#define ARDUINO_TX_PIN 6    // set here the pin number you use for software Serial Tx
SoftwareSerial pcmonitor(ARDUINO_RX_PIN, ARDUINO_TX_PIN); // rxPin (the pin on which to receive serial data), txPin (the pin on which to transmit serial data)



// Global variables
Si5351 si5351;
JTEncode jtencode;
unsigned long freq0 = 28126158UL;                  // Change this
unsigned long freq1 =  7040158UL;                // Change this
unsigned long freq2 =  7040155UL;                // Change this
unsigned long freq2bis = 10140102UL;                // Change this (second tx frequency for clock2)

unsigned long freq2tx =  freq2;                
boolean select = false; 


char call[7] = "IW5EJM";                        // Change this
char loc[5] = "JN53";                           // Change this
uint8_t dbm = 10;
uint8_t tx_buffer[SYMBOL_COUNT];

//esp8266 routine variables and constants
#define hardRestPIN 11
#define SHORT_PAUSE 1000UL
#define LONG_PAUSE  30000UL
const char * OK_STR = "OK\r\n";
const char * MS_STR = "ms\r\n";
#define ESPSERIALBAUD 9600 // Set to whatever is the default for your ESP. 
#define ESPSEPRIAL Serial

// Global variables used in ISRs
volatile bool proceed = false;

// Timer interrupt vector.  This toggles the variable we use to gate
// each column of output to ensure accurate timing.  Called whenever
// Timer1 hits the count set below in setup().
ISR(TIMER1_COMPA_vect)
{
    proceed = true;
}

// --------------------------------------
// emptyESP_RX waits for duration ms
// and get rid of anything arriving
// on the ESP Serial port during that delay
// --------------------------------------

void emptyESP_RX(unsigned long duration)
{
  unsigned long currentTime;
  currentTime = millis();
  while (millis() - currentTime <= duration) {
    if (ESPSEPRIAL.available() > 0) ESPSEPRIAL.read();
  }
}

// --------------------------------------
// waitForString wait max for duration ms
// while checking if endMarker string is received
// on the ESP Serial port
// returns a boolean stating if the marker
// was found
// --------------------------------------

boolean waitForString(const char * endMarker, unsigned long duration)
{
  int localBufferSize = strlen(endMarker); // we won't need an \0 at the end
  char localBuffer[localBufferSize];
  int index = 0;
  boolean endMarkerFound = false;
  unsigned long currentTime;

  memset(localBuffer, '\0', localBufferSize); // clear buffer

  currentTime = millis();
  while (millis() - currentTime <= duration) {
    if (ESPSEPRIAL.available() > 0) {
      if (index == localBufferSize) index = 0;
      localBuffer[index] = (uint8_t) ESPSEPRIAL.read();
      endMarkerFound = true;
      for (int i = 0; i < localBufferSize; i++) {
        if (localBuffer[(index + 1 + i) % localBufferSize] != endMarker[i]) {
          endMarkerFound = false;
          break;
        }
      }
      index++;
    }
    if (endMarkerFound) break;
  }
  return endMarkerFound;
}


// --------------------------------------
// espATCommand executes an AT commmand
// checks if endMarker string is received
// on the ESP Serial port for max duration ms
// returns a boolean stating if the marker
// was found
// --------------------------------------

boolean espATCommand(const char * command, const char * endMarker, unsigned long duration)
{
  ESPSEPRIAL.println(command);
  return waitForString(endMarker, duration);
}


// --------------------------------------
// epochUnixNTP set the UNIX time
// number of seconds sice Jan 1 1970
// --------------------------------------

time_t epochUnixNTP()
{
  unsigned long secsSince1900 = 0UL;
  unsigned long epochUnix;
  String timeraw;
  int hr;
  int mins;
  int sec;
  int days;
  int months;
  int yr;
  
  pcmonitor.println(">>>>>>>> Time Sync function called <<<<<<<<<");
  emptyESP_RX(1000UL); // empty the buffer
  ESPSEPRIAL.println("AT+CIPSNTPTIME?"); // request ntp time
  delay(250);
  ESPSEPRIAL.readStringUntil('\n');
  timeraw=ESPSEPRIAL.readStringUntil('\n');
  pcmonitor.println(timeraw);
  hr=timeraw.substring(24,26).toInt();
  mins=timeraw.substring(27,29).toInt();
  sec=timeraw.substring(30,32).toInt();
  days=timeraw.substring(21,23).toInt();
  months=8;
  yr=timeraw.substring(33,37).toInt();
  
  setTime(hr,mins,sec,days,months,yr); 
//  pcmonitor.println("Time in unix format calculated:");
  pcmonitor.println(now());
  
//  if(timeStatus() == timeSet) pcmonitor.print("Time synchronized if 2 follows: ");
//  pcmonitor.println(timeStatus());

  return 0;
}


 
// Loop through the string, transmitting one character at a time.
void encode()
{
    uint8_t i;

    jtencode.wspr_encode(call, loc, dbm, tx_buffer);
    
    // Reset the tone to 0 and turn on the output
    si5351.set_clock_pwr(SI5351_CLK0, 1);
    // si5351.set_clock_pwr(SI5351_CLK1, 1);
    si5351.set_clock_pwr(SI5351_CLK2, 1);

    digitalWrite(TX_LED_PIN, HIGH);
    pcmonitor.println("TX ON");

    //choose the band for clock 2
    if (select) freq2tx = freq2bis; 
      else freq2tx=freq2;
      pcmonitor.println(freq2tx);

    // Now do the rest of the message
    for(i = 0; i < SYMBOL_COUNT; i++)
    {
        si5351.set_freq((freq0 * 100) + (tx_buffer[i] * TONE_SPACING), SI5351_CLK0);
        //si5351.set_freq((freq1 * 100) + (tx_buffer[i] * TONE_SPACING), SI5351_CLK1);
        si5351.set_freq((freq2tx * 100) + (tx_buffer[i] * TONE_SPACING), SI5351_CLK2);

        proceed = false;
        while(!proceed);
    }
        
    // Turn off the output
    si5351.set_clock_pwr(SI5351_CLK0, 0);
    //si5351.set_clock_pwr(SI5351_CLK1, 0);
    si5351.set_clock_pwr(SI5351_CLK2, 0);

    digitalWrite(TX_LED_PIN, LOW);
    select = !select;
    pcmonitor.println("TX OFF");
}
 
void setup()
{
  ESPSEPRIAL.begin(ESPSERIALBAUD); while (!ESPSEPRIAL);
  pcmonitor.begin(9600); while (!pcmonitor);
  pcmonitor.println("COMs setup successful");
  
  //reset the device (pull the RST pin to ground)
  digitalWrite(hardRestPIN, LOW);
  delay(100);
  digitalWrite(hardRestPIN, HIGH);

  // connect to my WiFi network
  espATCommand("AT+RST", "ready", LONG_PAUSE); // reset
  emptyESP_RX(SHORT_PAUSE);
  if (espATCommand("AT", OK_STR, SHORT_PAUSE)) pcmonitor.println("AT with ESP OK");; //is all OK?
//  if (espATCommand("AT+CWMODE_CUR=1", OK_STR, SHORT_PAUSE)) pcmonitor.println("Wireless client mode selected"); //Set the wireless mode
//  espATCommand("AT+CWQAP", OK_STR, SHORT_PAUSE);   //disconnect  - it shouldn't be but just to make sure
  pcmonitor.print("Trying to connect to WIFI");
  while (!espATCommand("AT+CWJAP_CUR=\"TIM-82408153\",\"GGjyCC2yMyuYUrg4\"", OK_STR, LONG_PAUSE)) 
  
    pcmonitor.print("."); // connect to wifi
  
  pcmonitor.println("WIFI connected");
  if (espATCommand("AT+CIPSNTPCFG=1,0,\"time.google.com\",\"it.pool.ntp.org\",\"ntp.sjtu.edu.cn\"", OK_STR, LONG_PAUSE)) pcmonitor.println("NTP server connected");
    else  pcmonitor.println("ERROR in NTP server connection");
  
  // Use the LED as a keying indicator.
  pinMode(TX_LED_PIN, OUTPUT);
  pinMode(SYNC_LED_PIN, OUTPUT);
  digitalWrite(TX_LED_PIN, LOW);
  digitalWrite(SYNC_LED_PIN, LOW);

  // Set time sync provider
  setSyncProvider(epochUnixNTP);  //set function to call when sync required 
    
  // Initialize the Si5351
  // Change the 2nd parameter in init if using a ref osc other
  // than 25 MHz
  pcmonitor.println("start radio module setup");
  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 27000000UL, CORRECTION);
  pcmonitor.println("Module intializated");

  
  // Set CLK0 output
  si5351.set_freq(freq1 * 100, SI5351_CLK0);
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA); // Set for max power
  si5351.set_clock_pwr(SI5351_CLK0, 0); // Disable the clock initially

  // Set CLK1 output
  si5351.set_freq(freq1 * 100, SI5351_CLK1);
  si5351.drive_strength(SI5351_CLK1, SI5351_DRIVE_8MA); // Set for max power
  si5351.set_clock_pwr(SI5351_CLK1, 0); // Disable the clock initially

  // Set CLK2 output
  si5351.set_freq(freq2tx * 100, SI5351_CLK2);
  si5351.drive_strength(SI5351_CLK2, SI5351_DRIVE_8MA); // Set for max power
  si5351.set_clock_pwr(SI5351_CLK2, 0); // Disable the clock initially
  
  pcmonitor.println("Radio Module setup successful");

  // Set up Timer1 for interrupts every symbol period.
  noInterrupts();          // Turn off interrupts.
  TCCR1A = 0;              // Set entire TCCR1A register to 0; disconnects
                           //   interrupt output pins, sets normal waveform
                           //   mode.  We're just using Timer1 as a counter.
  TCNT1  = 0;              // Initialize counter value to 0.
  TCCR1B = (1 << CS12) |   // Set CS12 and CS10 bit to set prescale
    (1 << CS10) |          //   to /1024
    (1 << WGM12);          //   turn on CTC
                           //   which gives, 64 us ticks
  TIMSK1 = (1 << OCIE1A);  // Enable timer compare interrupt.
  OCR1A = WSPR_CTC;       // Set up interrupt trigger count;
  interrupts();            // Re-enable interrupts.
  pcmonitor.println("Entering loop...");
}
 
void loop()
{  
  
//  if(timeStatus() == timeSet)  
//    {
//     digitalWrite(SYNC_LED_PIN, HIGH); // LED on if synced
//    }
//  else
//    {
//      digitalWrite(SYNC_LED_PIN, LOW);  // LED off if needs refresh
//     }

  // Trigger every 10 minute
  // WSPR should start on the 1st second of the minute, but there's a slight delay
  // in this code because it is limited to 1 second resolution.
  if(minute() % 10 == 0 && second() == 0)
    {
      pcmonitor.print(hour());
      pcmonitor.print(":");
      pcmonitor.println(minute());
      encode();
      delay(1000);
     }
  }
