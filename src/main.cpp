/* MyBuddyLED vs 1.0
  Date of this version: Oct 26th 2026
  Written by Troels
  
  NB: Before deploying the device, you must manually enter two specific time points below, the on/off clock times you want, and upload the code to the Arduino.

  Program that triggers two LEDs to start pulsing, one of which triggered once daily and one triggered according to 
  a 28-day specific schedule. The latter is specifically a pill-taking schedule for a user, in which they must take a pill every day
  for the first 14 days, then every third day for the next 14 days.
  The start and stop time of day of each pulsing sequence is triggered by an external DS1307 Real Time Clock module.
  Each LED can be turned off with an individual button, until next trigger point.

  This program is written for Arduino Uno, using the Arduino IDE and PlatformIO in VS Code. 
  It uses the RTClib library to interface with the DS1307 RTC module.
*/ 

#include <Arduino.h> // required for PlatformIO to recognize Arduino functions
#include "RTClib.h" // include the RTClib library to interface with the DS1307 RTC module
RTC_DS1307 rtc; // create an instance of the RTC_DS1307 class to interface with the DS1307 RTC module

// USER INPUT:
// Time of deployment and start of time taking with RTC:
// NB: Before plugging in the device, you should manually enter the current time and upload the code to the Arduino.   
int start_year = 2026;
int start_month = 10;
int start_day = 27;
int start_hour = 12; // NB: 24 hour format, i.e. 10 = 10 AM, 22 = 10 PM
int start_minute = 22;
int start_second = 40;

// The day that the first 28-day cycle started (before deployment:
// NB: Before plugging in the device, you should manually enter the current time and upload the code to the Arduino.   
int start_cycle0_year = 2026;
int start_cycle0_month = 10;
int start_cycle0_day = 22;

// Times of day that you want the LEDs to turn on and off on pill-taking days (24h time format)
int green_LED_on_hour = 19; // hour of day when green LED starts pulsing on pill-taking days
int green_LED_off_hour = 24; // hour of day when green LED stops pulsing on pill-taking days (unless button is pressed before that)
int blue_LED_on_hour = 6; // hour of day when blue LED starts pulsing 
int blue_LED_off_hour = 24; // hour of day when blue LED stops pulsing (unless button is pressed before that)

// Time variables initialization:
uint32_t secs_since_2000;     // seconds since Jan 1, 2000, as returned by the RTC module 
uint32_t start_of_cycle0;     // time at start of first day of first 28-day cycle (one sec past midnight)
uint32_t deployed_time;     // time at deployment of device
uint32_t seconds_2000_to_start_cycle0;   // seconds from Jan 1, 2000 to the time point of the start of the first 28-day cycle (one sec past midnight)
uint32_t seconds_since_time0; // seconds since the time point of the start of the first 28-day cycle (one sec past midnight)
uint32_t secs_in_24h = 24UL * 60 * 60; // number of seconds in 24 hours, used to calculate the current day number
unsigned int current_day_no_rollover; // running day number that does not roll over;
unsigned int current_cycle_day;       // the day in the current 28-day cycle, rolls over to 1 after 28
unsigned int current_day_temp;        // temporary variable used to check whether a new day has started, and if so, to update the current day number

// Status LED and test button:
// This yellow LED is always on, at a low duty cycle, to indicate that the device is powered and running. 
// The test button can be pressed to check whether all three LEDs are working. 
const int status_LED_pin_yellow = 11; // the "on" lamp of the device, connected with 220 Ohm resistor, on a pulse-width-modulation (PWM) pin
const int test_button_pin_yellow = 5; // the button that triggers a test in which all three LEDs light up three times, in order to be able to check for dead LEDs
const int status_LED_duty_cycle = 10; // PWM duty cycle of yellow status LED (255 is max)

// First LED and timer: intermittent days
const int LED_pin_green = 3; // LED is on a PWM pin. This is the LED that corresponds to the 28-day pill-taking schedule
const int button_pin_green = 6;  // button that allows the LED to be turned off
const uint32_t trigger_days[18] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 20, 23, 26}; 
  // the days in a 28-day cycle which are pill-taking days
const int n_trigger_days = 18; // number of pill-taking days in a 28 day cycle.

bool flag_turn_on_LED_green = false; // initial value of flag that becomes true on pill-taking days, and until button is pressed

// Blue LED and timer: triggers every day
const int LED_pin_blue = 9;        // LED is on a PWM pin. This is the LED that corresponds to the daily reminder
const int button_pin_blue = 8;     // button that allows the LED to be turned off
bool flag_turn_on_LED_blue = true; // flag that becomes true once every day

// Photoresistor. The point of this is to sense the ambient light intensity, and adjust the brightness of the LEDs accordingly.
// By doing so, the LEDs will not be too bright when the ambient light is dim (e.g. disturbing sleep), and not too dim when the ambient light is bright. 
// The photoresistor is connected in series with a 100k Ohm resistor, this resistor being downstream of the 5V pin and upstream of the photoresistor, i.e. a voltage divider.
// Voltage between resistances is read on pin A0.

// Alternative code to be used if switching points between brightness levels are based on voltages: (as opposed to ADC readings on port A0)
/*
const int V0_switch_point_max_med_mV = 150;  // i.e if V0 is below this it means that there is high ambient light and that the signal LEDs should have maximum PWM duty cycle to be visible.
const int V0_switch_point_med_min_mV = 2000;
  if V0 is below this (but above the switch_point_max_med cutoff), there is medium ambient brightness and LED PWM should be medium
  if V0 is above this cutoff, there is very little ambient and light so LEDs should be dim
int switch_point_max_med = map(V0_switch_point_max_med_mV, 0, 5000, 0, 1023);
// transposes the V0 "switch from max to medium LED brightness" value from volts to the reading range of the analog pin
int switch_point_med_min = map(V0_switch_point_med_min_mV, 0, 5000, 0, 1023);
// transposes the V0 "switch from medium to min LED brightness" value from volts to the reading range of the analog pin
*/

// Code to be used if switching points between brightness levels are based on ADC readings on port A0 directly, and not first entered as voltages
// these values were found by trial and error at different levels of ambient light
int switch_point_max_med = 100; // if A0 reading is below this, it means that there is high ambient light and that the signal LEDs should have maximum PWM duty cycle to be visible.
int switch_point_med_min = 300; // if A0 reading is below this (but above the switch_point_max_med cutoff), there is medium ambient brightness and LED PWM should be medium
   // if A0 reading is above this cutoff, there is very little ambient and light so LEDs should be dim

int duty_cycle_top_out;         // at a given ambient light, this is the highest PWM duty cycle that an LED reaches during its pulsing cycle
const int duty_cycle_max = 200; // maximum PWM input to green and blue LED (when lots of ambient light)
const int duty_cycle_med = 70; // medium PWM input to green and blue LED (when medium ambient light)
const int duty_cycle_min = 7; // lowest PWM input, when ambient light is dim
                               // these values were found by trial and error at different levels of ambient light
int n_bright_levels = 13;      // the number of discrete brightness levels as the LEDs pulse

void setup()
{
  pinMode(LED_pin_green, OUTPUT);                // the green LED pin
  pinMode(button_pin_green, INPUT_PULLUP);       // the green button pin, that when pressed goes to LOW state and triggers that the LED turns off (flag becomes false)
  pinMode(LED_pin_blue, OUTPUT);                 // the blue LED pin
  pinMode(button_pin_blue, INPUT_PULLUP);        // the blue button pin, that when pressed goes to LOW state and triggers that the LED turns off (flag becomes false)
  pinMode(status_LED_pin_yellow, OUTPUT);        // status LED (yellow)
  pinMode(test_button_pin_yellow, INPUT_PULLUP); // the yellow test button, that when pressed turns off all LEDs, then flashes them three times, then turns of, then resumes program
  Serial.begin(9600);
  analogWrite(status_LED_pin_yellow, status_LED_duty_cycle); // Status LED is always on, at this duty cycle

#ifndef ESP8266 // Code from RTClib library, to ensure that the serial port is connected before proceeding. This is only relevant for boards with native USB, such as the Arduino Leonardo or Micro. The Uno does not have native USB, so this code is not needed for it.
  while (!Serial)
    ; // wait for serial port to connect. Needed for native USB
#endif

  if (!rtc.begin()) // check whether the RTC module is connected and working. If not, the program will stop here and the yellow status LED will remain on.
  {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1)
      delay(10);
  }

  rtc.adjust(DateTime(start_year, start_month, start_day, start_hour, start_minute, start_second)); // set the RTC module to the time of deployment, as entered above. 
  DateTime deployed_time = rtc.now();
  secs_since_2000 = deployed_time.secondstime(); // seconds since Jan 1, 2000, as returned by the RTC module
  DateTime start_of_cycle0(start_cycle0_year, start_cycle0_month, start_cycle0_day, 0, 0, 1); 
    // the time point (year, month, day, hour, minute, second) for the start of the first day of the cycle (one second past midnight)
  seconds_2000_to_start_cycle0 = start_of_cycle0.secondstime();         // seconds from Jan 1, 2000 to start of the first 28-day cycle (one sec past midnight)
  seconds_since_time0 = secs_since_2000 - seconds_2000_to_start_cycle0; // seconds elapsed since the time point of the start of the first 28-day cycle
  current_day_no_rollover = seconds_since_time0 / secs_in_24h + 1; // running day number that does not roll over; Day 1 is the day that device was deployed
  current_cycle_day = current_day_no_rollover % 28; // the day in the current 28-day cycle, rolls over to 1 after 28 and cycle restarts
  current_day_temp = current_day_no_rollover; // temporary variable used to check whether a new day has started

  for (int i = 0; i < n_trigger_days; i++)
  { // determine whether the deployment day is a pill-taking day for user A and if so, flag for turning on green LED becomes true
    if (trigger_days[i] == current_cycle_day)
    {
      flag_turn_on_LED_green = true;
      break; // breaks out of for loop if it has been established that the deployment day is a pilltaking day
    }
    flag_turn_on_LED_green = false; // turns off flag on non-pilltaking days
  }
}

void loop()
{
  DateTime current_time = rtc.now(); // get the current time from the RTC module
  current_day_temp = (current_time.secondstime() - seconds_2000_to_start_cycle0) / secs_in_24h + 1; // updates the temporary variable holding the current day
  if (current_day_temp != current_day_no_rollover) // if a new day has started since last loop
  {
    // The code below will be executed once per day, at the start of a new day.
    current_day_no_rollover = current_day_temp; // updates the current day number that does not roll over
    current_cycle_day = current_day_no_rollover % 28; // updates day number in the current 28-day cycle, rolls over to 1 after 28

    for (int i = 0; i <= (n_trigger_days - 1); i++)
    { // checks whether current day is a pill-taking day for user A and if so, flag for turning on green LED becomes true
      // (though code also checks if the time of day is correct for the LED to be on
      if (trigger_days[i] == current_cycle_day)
      {
        flag_turn_on_LED_green = true;
        break; // breaks out of for loop if it has been established that today is a pilltaking day
      }
      flag_turn_on_LED_green = false; // turns off flag on non-pilltaking days
    }

    flag_turn_on_LED_blue = true; // blue LED turns on every day
  }
  
  // Sensing ambient light intensity in order to give matching LED brightness:
  if (analogRead(A0) <= switch_point_max_med) // case where ambient light is bright, so LED should be at maximum brightness
  {
    duty_cycle_top_out = duty_cycle_max;
  }
  else if (analogRead(A0) > switch_point_max_med && analogRead(A0) <= switch_point_med_min)  // case where ambient light is medium, so LED should be at medium brightness
  {
    duty_cycle_top_out = duty_cycle_med;
  }
  else if (analogRead(A0) > switch_point_med_min) // case where ambient light is dim, so LED should be at minimum brightness
  {
    duty_cycle_top_out = duty_cycle_min;
  }
  int duty_cycles_array[13] = {0, duty_cycle_top_out * 1 / 12, duty_cycle_top_out * 2 / 12, duty_cycle_top_out * 3 / 12, duty_cycle_top_out * 4 / 12, duty_cycle_top_out * 5 / 12, duty_cycle_top_out * 6 / 12, duty_cycle_top_out * 7 / 12, duty_cycle_top_out * 8 / 12, duty_cycle_top_out * 9 / 12, duty_cycle_top_out * 10 / 12, duty_cycle_top_out * 11 / 12, duty_cycle_top_out};
  // LED will cycle through these 13 values when pulsing
  // There are only 13 discrete brightness levels. When brightness is dim, some of the levels will be the same.

  // GREEN LED fading up, if activated:
  current_time = rtc.now(); // get the current time from the RTC module
  if (flag_turn_on_LED_green == true && current_time.hour() >= green_LED_on_hour && current_time.hour() < green_LED_off_hour)
  { // on pill-taking days, in a given time frame, and until button is pressed, the LED will pulse
    analogWrite(LED_pin_green, 0);
    for (int i = 0; i < n_bright_levels; i++)
    { // fading up
      analogWrite(LED_pin_green, duty_cycles_array[i]); // brightness cycles up through the 13 discrete levels, with the top level being determined by the ambient light intensity
      delay(70);
    }
  }

  if (digitalRead(button_pin_green) == LOW)
  { // check whether LED-specific button is pressed. If so, turn off LED until next daily trigger
    analogWrite(LED_pin_green, 0);
    flag_turn_on_LED_green = false;
  }

  if (digitalRead(button_pin_blue) == LOW)
  { // check whether LED-specific button is pressed. If so, turn off LED until next daily trigger
    analogWrite(LED_pin_blue, 0);
    flag_turn_on_LED_blue = false;
  }

  // BLUE LED fading up, if activated:
  if (flag_turn_on_LED_blue == true && current_time.hour() >= blue_LED_on_hour && current_time.hour() < blue_LED_off_hour)
  { // on all days, and until button is pressed, the LED will pulse
    analogWrite(LED_pin_blue, 0);
    for (int i = 0; i < n_bright_levels; i++)
    { // fading up
      analogWrite(LED_pin_blue, duty_cycles_array[i]);
      delay(70);
    }
  }

  if (digitalRead(button_pin_green) == LOW)
  { // check whether LED-specific button is pressed. If so, turn of LED until next daily trigger
    analogWrite(LED_pin_green, 0);
    flag_turn_on_LED_green = false;
  }

  if (digitalRead(button_pin_blue) == LOW)
  { // check whether LED-specific button is pressed. If so, turn of LED until next daily trigger
    analogWrite(LED_pin_blue, 0);
    flag_turn_on_LED_blue = false;
  }

  // GREEN LED fading down, if activated:
  if (flag_turn_on_LED_green == true && current_time.hour() >= green_LED_on_hour && current_time.hour() < green_LED_off_hour)
  { // on pill-taking days, and until button is pressed, the LED will pulse
    for (int i = (n_bright_levels - 1); i >= 0; i--)
    { // fading down
      analogWrite(LED_pin_green, duty_cycles_array[i]);
      delay(70);
      //  Serial.println(duty_cycles_array[i]);
    }
    analogWrite(LED_pin_green, 0);
  }

  if (digitalRead(button_pin_green) == LOW)
  { // check whether LED-specific button is pressed. If so, turn of LED until next daily trigger
    analogWrite(LED_pin_green, 0);
    flag_turn_on_LED_green = false;
  }

  if (digitalRead(button_pin_blue) == LOW)
  { // check whether LED-specific button is pressed. If so, turn of LED until next daily trigger
    analogWrite(LED_pin_blue, 0);
    flag_turn_on_LED_blue = false;
  }

  // BLUE LED fading down, if activated:
  if (flag_turn_on_LED_blue == true && current_time.hour() >= blue_LED_on_hour && current_time.hour() < blue_LED_off_hour)
  { // on all days, and until button is pressed, the LED will pulse
    for (int i = (n_bright_levels - 1); i >= 0; i--)
    { // fading down
      analogWrite(LED_pin_blue, duty_cycles_array[i]);
      delay(70);
    }
    analogWrite(LED_pin_blue, 0);
  }

  if (digitalRead(button_pin_green) == LOW)
  { // check whether LED-specific button is pressed. If so, turn of LED until next daily trigger
    analogWrite(LED_pin_green, 0);
    flag_turn_on_LED_green = false;
  }

  if (digitalRead(button_pin_blue) == LOW)
  { // check whether LED-specific button is pressed. If so, turn of LED until next daily trigger
    analogWrite(LED_pin_blue, 0);
    flag_turn_on_LED_blue = false;
  }

  // TEST BUTTON CODE: if the test button is pressed, all three LEDs will blink three times, to check whether they are working.
  if (digitalRead(test_button_pin_yellow) == LOW)
  {
    // Code for the test procedure to check if all LEDs are working. If "TEST" button is pressed for approx 4 seconds (or less) it triggers three blinks from all three LEDs
    analogWrite(LED_pin_green, 0);
    analogWrite(LED_pin_blue, 0);
    analogWrite(status_LED_pin_yellow, 0);
    delay(1000);
    analogWrite(LED_pin_green, duty_cycle_max);
    analogWrite(LED_pin_blue, duty_cycle_max);
    analogWrite(status_LED_pin_yellow, duty_cycle_max);
    delay(500);
    analogWrite(LED_pin_green, 0);
    analogWrite(LED_pin_blue, 0);
    analogWrite(status_LED_pin_yellow, 0);
    delay(500);
    analogWrite(LED_pin_green, duty_cycle_max);
    analogWrite(LED_pin_blue, duty_cycle_max);
    analogWrite(status_LED_pin_yellow, duty_cycle_max);
    delay(500);
    analogWrite(LED_pin_green, 0);
    analogWrite(LED_pin_blue, 0);
    analogWrite(status_LED_pin_yellow, 0);
    delay(500);
    analogWrite(LED_pin_green, duty_cycle_max);
    analogWrite(LED_pin_blue, duty_cycle_max);
    analogWrite(status_LED_pin_yellow, duty_cycle_max);
    delay(500);
    analogWrite(LED_pin_green, 0);
    analogWrite(LED_pin_blue, 0);
    analogWrite(status_LED_pin_yellow, 0);
    delay(1000);
    analogWrite(status_LED_pin_yellow, status_LED_duty_cycle);
  }
}