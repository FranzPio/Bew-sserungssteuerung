#include "Timer0.h"
#include "FreqCounter.h"

#include <avr/io.h>
#include <avr/interrupt.h>

// Bibliotheken für den Betrieb des Displays
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Definition des Displays
#define OLED_WIDTH    128
#define OLED_HEIGHT    64
#define OLED_RESET     -1
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

#define HW_VERSION     10
#define SW_VERSION     10

// Definition der Taster(pins)
#define KEY_ROTARY     10
#define KEY_OK         11
#define KEY_ESC        12
#define ROTATE          1
#define NO_KEY          0

#define PUMP            2

// Definition der Menübildschirme
#define MENU_INFO                 22
#define MENU_SETTINGS              0
#define MENU_CLOCK                 1
#define MENU_CLOCK_SET_DAY         5
#define MENU_CLOCK_SET_HOURS       6
#define MENU_CLOCK_SET_MINS        7
#define MENU_CLOCK_SET_SECS        8
#define MENU_PLAN                  9
#define MENU_PLAN_TIME             2
#define MENU_PLAN_TIME_SET_1       3
#define MENU_PLAN_SET_HOURS_1     10
#define MENU_PLAN_SET_MINS_1      11
#define MENU_PLAN_SET_ACTIVE_1    12
#define MENU_PLAN_TIME_SET_2      13
#define MENU_PLAN_SET_HOURS_2     14
#define MENU_PLAN_SET_MINS_2      15
#define MENU_PLAN_SET_ACTIVE_2    16
#define MENU_PLAN_AMOUNT          17
#define MENU_PLAN_AMOUNT_SET      18
#define MENU_SENSOR               19
#define MENU_SENSOR_THRESHOLD     20
#define MENU_SENSOR_THRESHOLD_SET 21
#define MENU_MODE                  4

// Definition der Betriebsmodi
#define MODE_PLAN       0
#define MODE_AUTO       1


// ===================================

unsigned long frequency;
const unsigned long INTERVAL = 1000;  // in ms (kann auf 1000 gesetzt werden für 1s Messintervall)
const unsigned long MAX_FREQ = 200 * INTERVAL;
const unsigned long MIN_FREQ = 50 * INTERVAL;
int percentage;
int threshold_perc = 50, threshold_perc_temp;
unsigned char sensor_recheck_mins = 10;  // 10 Minuten nach dem Gießen wird erneut gemessen (im Auto-Modus)

char seconds = 0, seconds_temp;
char minutes = 0, minutes_temp;
char hours = 0, hours_temp;
char day = 0, day_temp;
const unsigned char SECONDS_MAX = 59, MINUTES_MAX = 59, HOURS_MAX = 23, DAY_MAX = 6;
const unsigned char SECONDS_MIN = 0, MINUTES_MIN = 0, HOURS_MIN = 0, DAY_MIN = 0;
const unsigned char weekdays[7][2] PROGMEM = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};

unsigned char plan_days = 0b00000000;  // ein sog. "Bitfeld" für die Gießtage
char plan_hours_1 = 8, plan_hours_2 = 20, plan_hours_1_temp, plan_hours_2_temp;
char plan_minutes_1 = 0, plan_minutes_2 = 0, plan_minutes_1_temp, plan_minutes_2_temp;
boolean plan_1_active = true;
boolean plan_2_active = false;

unsigned char key_state;
unsigned char ESC_old, OK_old, ROTARY_old;
unsigned char ESC_new = 0, OK_new = 0, ROTARY_new = 0;
unsigned char D9_old, D9_new, D8_old, D8_new;
volatile char pci_counter = 0;        // Variablen, die sowohl in der Endlosschleife als auch in einer ISR verwendet werden,
volatile unsigned char pci_flag = 0;  // müssen als volatile deklariert werden, damit der Compiler keine unerwünschte Optimierung vornimmt

unsigned char menu_page = MENU_SETTINGS;
char selection = 0;
char subselection = 0;
const unsigned char selector_pos[5][7] = {{15, 25, 35, 45}, {0, 66, 84, 102}, {0, 18, 36, 54, 72, 90, 108}, {60, 78, 96}, {15, 32}};
const unsigned char SELECTOR = 0x10;    // ⯈ 
const unsigned char ARROW_LEFT = 0x1B;  // ←
unsigned char selected_menu;
unsigned char _100ms = 0;
boolean blink = true;
boolean display_sleep = false;

boolean pump_on = false;
unsigned int pump_elapsed_secs = 0;
unsigned int pump_duration_secs = 180, pump_duration_secs_temp;  // 3 min Gießen entspricht ca. 100 ml
const unsigned char PUMP_SECS_MIN = 9;
const unsigned int PUMP_SECS_MAX = 65535;  // MAX_INT
unsigned char mode = MODE_PLAN;

// die Zeichenketten für Menütexte werden im Programmspeicher/Flash abgelegt,
// da sonst der SRAM (2 KB) des ATmega328P zur Neige geht und Programmfehler auftreten
const unsigned char settings_str[] PROGMEM =         "Einstellungen";
const unsigned char clock_str[] PROGMEM =            "Tag & Uhrzeit";
const unsigned char plan_str[] PROGMEM =             "Bew\204sserungsplan";  // ä ist \204 (ASCII-Code oktal)
const unsigned char plan_time_str[] PROGMEM =        "Zeitplan";
const unsigned char time_str[] PROGMEM =             "Zeit";
const unsigned char plan_amount_str[] PROGMEM =      "Wassermenge";
const unsigned char sensor_str[] PROGMEM =           "Feuchtesensor";
const unsigned char sensor_threshold_str[] PROGMEM = "Schwellenwert";
const unsigned char current_str[] PROGMEM =          "aktuell";
const unsigned char water_if_str[] PROGMEM =         "Gie\340 wenn";         // ß ist \340
const unsigned char mode_str[] PROGMEM =             "Modus";
const unsigned char mode_plan_str[] PROGMEM =        "Planm\204\340ig";
const unsigned char mode_auto_str[] PROGMEM =        "Intelligent";
const unsigned char active_str[] PROGMEM =           "aktiv";
const unsigned char inactive_str[] PROGMEM =         "aus";
const unsigned char mode_plan_expl_str[] PROGMEM =   "strikt nach Plan";
const unsigned char mode_auto_expl_str[] PROGMEM =   "Sensor entscheidet";
const unsigned char author_1_str[] PROGMEM =         "Piontek";
const unsigned char author_2_str[] PROGMEM =         "Spohr";
const unsigned char project_name_str[] PROGMEM =     "Pumpensteuerung";

// ===================================


void start_pump() {
  digitalWrite(PUMP, HIGH);
  pump_on = true;
}

void stop_pump() {
  digitalWrite(PUMP, LOW);
  pump_on = false;
  pump_elapsed_secs = 0;
}


boolean watering_time() {
  switch (mode) {
    case MODE_PLAN:
      if (plan_days & (1 << day)) {
        // heute ist ein Gießtag
        if ((hours == plan_hours_1) && (minutes == plan_minutes_1)) {
          // jetzt ist Gießzeit
          if (plan_1_active) return true;
        }
        else if ((hours == plan_hours_2) && (minutes == plan_minutes_2)) {
          // jetzt ist Gießzeit No. 2
          if (plan_2_active) return true;
        }
      }
    break;

    case MODE_AUTO:
      if (plan_days & (1 << day)) {
        // heute ist ein Messtag
        if ((hours == plan_hours_1) && (minutes == plan_minutes_1)) {
          // jetzt ist Messzeit
          if (plan_1_active) {
            if (percentage <= threshold_perc) return true;
          }
        }
        else if ((hours == plan_hours_2) && (minutes == plan_minutes_2)) {
          // jetzt ist Messzeit No. 2
          if (plan_2_active) {
            if (percentage <= threshold_perc) return true;
          }
        }

        else if ((hours == plan_hours_1) && (minutes == (plan_minutes_1 + sensor_recheck_mins))) {
          // ggf. erneut gießen, falls keine Besserung der Feuchtigkeit eingetreten ist
          if (plan_1_active) {
            if (percentage <= threshold_perc) return true;
          }
        }
        else if ((hours == plan_hours_2) && (minutes == (plan_minutes_2 + sensor_recheck_mins))) {
          // ggf. erneut gießen, falls keine Besserung der Feuchtigkeit eingetreten ist
          if (plan_2_active) {
            if (percentage <= threshold_perc) return true;
          }
        }
      }
    break;
  }
  return false;
}


void increment_time_1s() {
  seconds++;
    if (seconds > SECONDS_MAX) {
      seconds = SECONDS_MIN;
      minutes++;
      if (watering_time()) start_pump();
    
      if (minutes > MINUTES_MAX) {
        minutes = MINUTES_MIN;
        hours++;
  
        if (hours > HOURS_MAX) {
          hours = HOURS_MIN;
          minutes = MINUTES_MIN;
          seconds = SECONDS_MIN;
          day++;

          if (day > DAY_MAX) {
            day = DAY_MIN;
          }
        }
      }
    }
}


void keys_init() {
  pinMode(KEY_ESC, INPUT_PULLUP);  // Taster unten wird initialisiert (Pin auf Eingang gesetzt + interner Pull-up-Widerstand aktiviert)
  pinMode(KEY_OK, INPUT_PULLUP);   // Taster links wird intialisiert
  // ("Arduino-Abkürzung" für das Setzen der entsprechenden Bits in den Registern DDR und PORT)

  pinMode(9, INPUT);           // CLK (Pin A des Drehgebers)
  pinMode(8, INPUT);           // DT (Pin B des Drehgebers)
  pinMode(KEY_ROTARY, INPUT);  // SW (integrierter Druckknopf)

  D8_old = digitalRead(8);
  D9_old = digitalRead(9);

  cli();                    // alle Interrupts deaktiviert (setzt Global Interrupt Enable Bit im Register SREG)
  
  PCICR |= (1 << PCIE0);    // PCI für Pins PCINT0..7 aktiviert
  PCMSK0 |= (1 << PCINT1);  // für Pin D9 (PCINT1) wird ein Pin Change Interrupt gewünscht
  
  sei();                    // Interrupts freigegeben
}


unsigned char check_key_state() {
  if (pci_flag == 1) {
    pci_flag = 0;
    return ROTATE;
  }
  
  //ROTARY_old = ROTARY_new;  // der im Drehgeber integrierte Druckknopf funktioniert nicht wirklich gut, daher ungenutzt
  ESC_old = ESC_new;
  OK_old = OK_new;

  //ROTARY_new = digitalRead(KEY_ROTARY);
  ESC_new = digitalRead(KEY_ESC);
  OK_new = digitalRead(KEY_OK);

  if ((ESC_new == LOW) && (ESC_old != LOW)) return KEY_ESC;
  else if ((OK_new == LOW) && (OK_old != LOW)) return KEY_OK;
  //else if ((ROTARY_new == LOW) && (ROTARY_old != LOW)) return KEY_ROTARY;
  else return NO_KEY;
}


void display_init() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  //display.setTextWrap(false);
}


void show_splash(int duration_ms) {
  display.setTextSize(2);
  display_print(project_name_str, 6);
  display.write('-');
  display.setCursor(0, 16);
  display_print(&project_name_str[6], 9);
  display.setTextSize(1);
  display.setCursor(0, 35);
  display.print("Hw");
  display.write(' ');
  display.write('v');
  display.print(HW_VERSION / 10);
  display.write('.');
  display.print(HW_VERSION % 10);
  display.setCursor(0, 45);
  display.print("Sw");
  display.write(' ');
  display.write('v');
  display.print(SW_VERSION / 10);
  display.write('.');
  display.print(SW_VERSION % 10);
  display.setCursor(56, 35);
  display.write('(');
  display.write('c');
  display.write(')');
  display.write(' ');
  display_print(author_1_str, strlen_P(author_1_str));
  display.write(',');
  display.setCursor(80, 45);
  display_print(author_2_str, strlen_P(author_2_str));
  display.display();
  delay(duration_ms);
}


void display_print(unsigned char arr[], unsigned char len) {
  for (unsigned char cha=0; cha < len; cha++) {
    display.write(pgm_read_byte_near(&arr[cha]));    // & ist der Dereferenzierungs-Operator (gibt Speicheradresse an)
  }                                                  // pgm_read_byte_near() wird zum Auslesen von PROGMEM Variablen benötigt
}


void update_display() {
  display.clearDisplay();

  if (display_sleep) {
    display.fillScreen(BLACK);  // leider kein echter Stromsparmodus, aber immerhin
    display.display();
    return;
  }
  
  switch (menu_page) {
    case MENU_SETTINGS:
      display.setTextColor(BLACK, WHITE);
      display.setCursor(0, 0);
      display.write(' ');
      display_print(settings_str, strlen_P(settings_str));
      display.setTextColor(WHITE, BLACK);
      display.setCursor(10, 15);
      display_print(clock_str, strlen_P(clock_str));
      display.setCursor(10, 25);
      display_print(plan_str, strlen_P(plan_str));
      display.setCursor(10, 35);
      display_print(sensor_str, strlen_P(sensor_str));
      display.setCursor(10, 45);
      display_print(mode_str, strlen_P(mode_str));
    break;

    case MENU_CLOCK:
    case MENU_CLOCK_SET_DAY:
    case MENU_CLOCK_SET_HOURS:
    case MENU_CLOCK_SET_MINS:
    case MENU_CLOCK_SET_SECS:
      display.setTextColor(BLACK, WHITE);
      display.setCursor(0, 0);
      display.write(ARROW_LEFT);
      display.write(' ');
      display_print(clock_str, strlen_P(clock_str));
      display.setTextColor(WHITE, BLACK);
      display.setCursor(0, 20);
      display_print(weekdays[day_temp], 2);
      display.write(', ');
      display.print(hours_temp / 10);
      display.print(hours_temp % 10);
      display.print(":");
      display.print(minutes_temp / 10);
      display.print(minutes_temp % 10);
      display.print(":");
      display.print(seconds_temp / 10);
      display.print(seconds_temp % 10);
    break;

    case MENU_PLAN:
      display.setTextColor(BLACK, WHITE);
      display.setCursor(0, 0);
      display.write(ARROW_LEFT);
      display.write(' ');
      display_print(plan_str, strlen_P(plan_str));
      display.setTextColor(WHITE, BLACK);
      display.setCursor(10, 15);
      display_print(plan_time_str, strlen_P(plan_time_str));
      display.setCursor(10, 25);
      display_print(plan_amount_str, strlen_P(plan_amount_str));
    break;

    case MENU_PLAN_TIME:
    case MENU_PLAN_TIME_SET_1:
    case MENU_PLAN_SET_HOURS_1:
    case MENU_PLAN_SET_MINS_1:
    case MENU_PLAN_SET_ACTIVE_1:
    case MENU_PLAN_TIME_SET_2:
    case MENU_PLAN_SET_HOURS_2:
    case MENU_PLAN_SET_MINS_2:
    case MENU_PLAN_SET_ACTIVE_2:
      display.setTextColor(BLACK, WHITE);
      display.setCursor(0, 0);
      display.write(ARROW_LEFT);
      display.write(' ');
      display_print(plan_time_str, strlen_P(plan_time_str));
      display.setTextColor(WHITE, BLACK);
      display.setCursor(0, 15);
      for (unsigned char i=0; i<7; i++) {
        display_print(weekdays[i], 2);
        display.write(' ');
      }
      display.setCursor(10, 30);
      display_print(time_str, strlen_P(time_str));
      display.write(' ');
      display.write('1');
      display.write(':');
      display.write(' ');
      display.print(plan_hours_1_temp / 10);
      display.print(plan_hours_1_temp % 10);
      display.print(':');
      display.print(plan_minutes_1_temp / 10);
      display.print(plan_minutes_1_temp % 10);
      display.setCursor(10, 45);
      display_print(time_str, strlen_P(time_str));
      display.write(' ');
      display.write('1');
      display.write(':');
      display.write(' ');
      display.print(plan_hours_2_temp / 10);
      display.print(plan_hours_2_temp % 10);
      display.print(':');
      display.print(plan_minutes_2_temp / 10);
      display.print(plan_minutes_2_temp % 10);
    break;

    case MENU_PLAN_AMOUNT:
    case MENU_PLAN_AMOUNT_SET:
      display.setTextColor(BLACK, WHITE);
      display.setCursor(0, 0);
      display.write(ARROW_LEFT);
      display.write(' ');
      display_print(plan_amount_str, strlen_P(plan_amount_str));
      display.setTextColor(WHITE, BLACK);
      display.setCursor(10, 25);
      display.print(pump_duration_secs_temp*5/9);
      display.setCursor(35, 25);
      display.print("ml");
      display.setCursor(70, 25);
      display.write(0xF6);  // Rundungszeichen
      display.write(' ');
      display.print(pump_duration_secs_temp);
      display.write(' ');
      display.write('s');
    break;

    case MENU_SENSOR:
      display.setTextColor(BLACK, WHITE);
      display.setCursor(0, 0);
      display.write(ARROW_LEFT);
      display.write(' ');
      display_print(sensor_str, strlen_P(sensor_str));
      display.setTextColor(WHITE, BLACK);
      display.setCursor(10, 15);
      display_print(sensor_threshold_str, strlen_P(sensor_threshold_str));
    break;

    case MENU_SENSOR_THRESHOLD:
    case MENU_SENSOR_THRESHOLD_SET:
      display.setTextColor(BLACK, WHITE);
      display.setCursor(0, 0);
      display.write(ARROW_LEFT);
      display.write(' ');
      display_print(sensor_threshold_str, strlen_P(sensor_threshold_str));
      display.setTextColor(WHITE, BLACK);
      display.setCursor(10, 15);
      display_print(current_str, strlen_P(current_str));
      display.write(':');
      display.setCursor(80, 15);
      display.print(percentage);
      display.setCursor(102, 15);
      display.write('%');
      display.setCursor(10, 35);
      display_print(water_if_str, strlen_P(water_if_str));
      display.write(' ');
      display.write('<');
      display.setCursor(80, 35);
      display.print(threshold_perc_temp);
      display.setCursor(102, 35);
      display.write('%');
    break;

    case MENU_MODE:
      display.setTextColor(BLACK, WHITE);
      display.setCursor(0, 0);
      display.write(ARROW_LEFT);
      display.write(' ');
      display_print(mode_str, strlen_P(mode_str));
      display.setTextColor(WHITE, BLACK);
      display.setCursor(10, 15);
      display_print(mode_plan_str, strlen_P(mode_plan_str));
      display.setCursor(10, 32);
      display_print(mode_auto_str, strlen_P(mode_auto_str));
    break;

    case MENU_INFO:
      display.setCursor(0, 0);
      display.println("rel. Feuchte:");
      display.setCursor(0, 10);
      display.print(percentage);
      display.write(' ');
      display.write('%');
      display.setCursor(0, 20);
      display_print(weekdays[day], 2);
      display.write(',');
      display.write(' ');
      display.print(hours / 10);
      display.print(hours % 10);
      display.write(':');
      display.print(minutes / 10);
      display.print(minutes % 10);
      display.write(':');
      display.print(seconds / 10);
      display.print(seconds % 10);
    break;
  }

  // Anzeige des Auswahlindikators / Cursors
  switch (menu_page) {
    case MENU_SETTINGS:
    case MENU_PLAN:
    case MENU_SENSOR:
      display.setCursor(0, selector_pos[MENU_SETTINGS][selection]);
      display.write(SELECTOR);
    break;
    
    case MENU_CLOCK:
        display.drawFastHLine(selector_pos[MENU_PLAN_TIME][selection], 30, 11, WHITE);
    break;
    
    case MENU_CLOCK_SET_DAY:
    case MENU_CLOCK_SET_HOURS:
    case MENU_CLOCK_SET_MINS:
    case MENU_CLOCK_SET_SECS:
      if (blink) display.drawFastHLine(selector_pos[MENU_PLAN_TIME][selection], 30, 11, WHITE);
      else display.drawFastHLine(selector_pos[MENU_PLAN_TIME][selection], 30, 11, BLACK);
    break;

    case MENU_PLAN_TIME:
    case MENU_PLAN_TIME_SET_1:
    case MENU_PLAN_SET_HOURS_1:
    case MENU_PLAN_SET_MINS_1:
    case MENU_PLAN_SET_ACTIVE_1:
    case MENU_PLAN_TIME_SET_2:
    case MENU_PLAN_SET_HOURS_2:
    case MENU_PLAN_SET_MINS_2:
    case MENU_PLAN_SET_ACTIVE_2:
      for (unsigned char i=0; i<7; i++) {
        if (plan_days & (1 << i)) {
          display.setTextColor(BLACK, WHITE);
          display.setCursor(selector_pos[MENU_PLAN_TIME][i], 15);
          display_print(weekdays[i], 2);
        }
        else {
          display.setTextColor(WHITE, BLACK);
          display.setCursor(selector_pos[MENU_PLAN_TIME][i], 15);
          display_print(weekdays[i], 2);
        }
      }
      display.setTextColor(WHITE, BLACK);

      display.setCursor(96, 30);
      if (plan_1_active) display_print(active_str, strlen_P(active_str));
      else display_print(inactive_str, strlen_P(inactive_str));
      display.setCursor(96, 45);
      if (plan_2_active) display_print(active_str, strlen_P(active_str));
      else display_print(inactive_str, strlen_P(inactive_str));

      if (selection == 8) {
        display.setCursor(0, 45);
        display.write(SELECTOR);
      }
      else if (selection == 7) {
        display.setCursor(0, 30);
        display.write(SELECTOR);
      }
      else {
        display.drawFastHLine(selector_pos[MENU_PLAN_TIME][selection], 25, 11, WHITE);
      }

      switch (menu_page) {
        case MENU_PLAN_TIME_SET_1:
          display.drawFastHLine(selector_pos[MENU_PLAN_TIME_SET_1][subselection], 40, 11, WHITE);
        break;
        
        case MENU_PLAN_TIME_SET_2:
          display.drawFastHLine(selector_pos[MENU_PLAN_TIME_SET_1][subselection], 55, 11, WHITE);
        break;

        case MENU_PLAN_SET_HOURS_1:
        case MENU_PLAN_SET_MINS_1:
        case MENU_PLAN_SET_ACTIVE_1:
          if (blink) display.drawFastHLine(selector_pos[MENU_PLAN_TIME_SET_1][subselection], 40, 11, WHITE);
          else display.drawFastHLine(selector_pos[MENU_PLAN_TIME_SET_1][subselection], 40, 11, BLACK);
        break;

        case MENU_PLAN_SET_HOURS_2:
        case MENU_PLAN_SET_MINS_2:
        case MENU_PLAN_SET_ACTIVE_2:
          if (blink) display.drawFastHLine(selector_pos[MENU_PLAN_TIME_SET_1][subselection], 55, 11, WHITE);
          else display.drawFastHLine(selector_pos[MENU_PLAN_TIME_SET_1][subselection], 55, 11, BLACK);
        break;
      }
    break;

    case MENU_PLAN_AMOUNT:
      display.drawFastHLine(10, selector_pos[MENU_SETTINGS][2], 16, WHITE);
    break;

    case MENU_PLAN_AMOUNT_SET:
      if (blink) display.drawFastHLine(10, selector_pos[MENU_SETTINGS][2], 16, WHITE);
      else display.drawFastHLine(10, selector_pos[MENU_SETTINGS][2], 16, BLACK);
    break;

    case MENU_SENSOR_THRESHOLD:
      display.drawFastHLine(80, selector_pos[MENU_SETTINGS][2]+10, 11, WHITE);
    break;

    case MENU_SENSOR_THRESHOLD_SET:
      if (blink) display.drawFastHLine(80, selector_pos[MENU_SETTINGS][2]+10, 11, WHITE);
      else display.drawFastHLine(80, selector_pos[MENU_SETTINGS][2]+10, 11, BLACK);
    break;

    case MENU_MODE:
      display.drawFastHLine(10, selector_pos[MENU_MODE][mode]+10, 56, WHITE);
      display.setCursor(86, selector_pos[MENU_MODE][mode]);
      display.write('(');
      display_print(active_str, strlen_P(active_str));
      display.write(')');
      
      display.setCursor(0, selector_pos[MENU_MODE][selection]);
      display.write(SELECTOR);
      display.setCursor(0, 55);
      display.write('(');
      if (selection == MODE_PLAN) display_print(mode_plan_expl_str, strlen_P(mode_plan_expl_str));
      else if (selection == MODE_AUTO) display_print(mode_auto_expl_str, strlen_P(mode_auto_expl_str));
      display.write(')');
    break;
  }

  display.display();
}


void setup() {
  pinMode(LED_BUILTIN, OUTPUT);  // integrierte LED wird initialisiert (Pin auf Ausgang gesetzt)
  pinMode(PUMP, OUTPUT);

  //Serial.begin(9600);  // zum Debuggen

  display_init();
  show_splash(2500);
  
  Timer0_Init();
  keys_init();
  
  FreqCounter::f_comp = INTERVAL / 10;  // Cal Value / Calibrate with professional Freq Counter
  FreqCounter::start(INTERVAL);         // 100 ms Gate Time (Zählintervall)
}


void loop() {
  if (FreqCounter::f_ready == 1) {
    frequency = FreqCounter::f_freq;
    percentage = (frequency - MAX_FREQ) / ((MAX_FREQ - MIN_FREQ) / 100);  // Umrechnung Frequenz in Prozent relative Luftfeuchte (antiproportional)

    FreqCounter::start(INTERVAL);  // neues Zeitintervall beginnt
  }

  
  if (Timer0_Get_1sState() == TIMER_TRIGGERED) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    increment_time_1s();
    
    if (pump_on) {
      pump_elapsed_secs++;
       if (pump_elapsed_secs >= pump_duration_secs) {
         stop_pump();
       }
    }
  }

  
  if (Timer0_Get_100msState() == TIMER_TRIGGERED) {

    _100ms++;
    if (_100ms == 5) {
      blink = !blink;
      _100ms = 0;
    }

    key_state = check_key_state();

    switch (menu_page) {
      case MENU_SETTINGS:
        switch (key_state) {
          case KEY_OK:
            menu_page = selected_menu;
            selection = 0;
            seconds_temp = seconds;
            minutes_temp = minutes;
            hours_temp = hours;
            day_temp = day; 
          break;
          case KEY_ESC:
            menu_page = MENU_INFO;
            selection = 0;
          break;
          case ROTATE:
            selection += pci_counter;
            pci_counter = 0;
          break;
          default:
          break;
        }
        if (selection <= 0) {
          selection = 0;
          selected_menu = MENU_CLOCK;
        }
        else if (selection >= 3) {
          selection = 3;
          selected_menu = MENU_MODE;
        }
        else if (selection == 2) {
          selected_menu = MENU_SENSOR;
        }
        else {
          selected_menu = MENU_PLAN;
        }
      break;

      case MENU_CLOCK:
        switch (key_state) {
          case KEY_OK:
            menu_page = selected_menu;
          break;
          case KEY_ESC:
            menu_page = MENU_SETTINGS;
            selection = 0;
          break;
          case ROTATE:
            selection += pci_counter;
            pci_counter = 0;
          break;
          default:
          break;
        }
        if (selection <= 0) {
          selection = 0;
          selected_menu = MENU_CLOCK_SET_DAY;
        }
        else if (selection >= 3) {
          selection = 3;
          selected_menu = MENU_CLOCK_SET_SECS;
        }
        else if (selection == 2) {
          selected_menu = MENU_CLOCK_SET_MINS;
        }
        else {
          selected_menu = MENU_CLOCK_SET_HOURS;
        }
      break;

      case MENU_CLOCK_SET_DAY:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_CLOCK;
            day = day_temp;
          break;
          case KEY_ESC:
            menu_page = MENU_CLOCK;
            day_temp = day;
          break;
          case ROTATE:
            day_temp += pci_counter;
            pci_counter = 0;
            if (day_temp < DAY_MIN) day_temp = DAY_MAX;
            else if (day_temp > DAY_MAX) day_temp = DAY_MIN;
          break;
          default:
          break;
        }
      break;
      
      case MENU_CLOCK_SET_HOURS:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_CLOCK;
            hours = hours_temp;
          break;
          case KEY_ESC:
            menu_page = MENU_CLOCK;
            hours_temp = hours;
          break;
          case ROTATE:
            hours_temp += pci_counter;
            pci_counter = 0;
            if (hours_temp < HOURS_MIN) hours_temp = HOURS_MAX;
            else if (hours_temp > HOURS_MAX) hours_temp = HOURS_MIN;
          break;
          default:
          break;
        }
      break;

      case MENU_CLOCK_SET_MINS:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_CLOCK;
            minutes = minutes_temp;
          break;
          case KEY_ESC:
            menu_page = MENU_CLOCK;
            minutes_temp = minutes;
          break;
          case ROTATE:
            minutes_temp += pci_counter;
            pci_counter = 0;
            if (minutes_temp < MINUTES_MIN) minutes_temp = MINUTES_MAX;
            else if (minutes_temp > MINUTES_MAX) minutes_temp = MINUTES_MIN;
          break;
          default:
          break;
        }
      break;

      case MENU_CLOCK_SET_SECS:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_CLOCK;
            seconds = seconds_temp;
          break;
          case KEY_ESC:
            menu_page = MENU_CLOCK;
            seconds_temp = seconds;
          break;
          case ROTATE:
            seconds_temp += pci_counter;
            pci_counter = 0;
            if (seconds_temp < SECONDS_MIN) seconds_temp = SECONDS_MAX;
            else if (seconds_temp > SECONDS_MAX) seconds_temp = SECONDS_MIN;
          break;
          default:
          break;
        }
      break;

      case MENU_PLAN:
        switch (key_state) {
          case KEY_OK:
            menu_page = selected_menu;
            selection = 0;
            plan_hours_1_temp = plan_hours_1;
            plan_hours_2_temp = plan_hours_2;
            plan_minutes_1_temp = plan_minutes_1;
            plan_minutes_2_temp = plan_minutes_2;
            pump_duration_secs_temp = pump_duration_secs;
          break;
          case KEY_ESC:
            menu_page = MENU_SETTINGS;
            selection = 1;
          break;
          case ROTATE:
            selection += pci_counter;
            pci_counter = 0;
          break;
          default:
          break;
        }
        if (selection <= 0) {
          selection = 0;
          selected_menu = MENU_PLAN_TIME;
        }
        else {
          selection = 1;
          selected_menu = MENU_PLAN_AMOUNT;
        }
      break;

      case MENU_PLAN_TIME:
        switch (key_state) {
          case KEY_OK:
            menu_page = selected_menu;
            if (selection <= 6) {
              plan_days ^= (1 << selection);  // ^ ist XOR
            }
          break;
          case KEY_ESC:
            menu_page = MENU_PLAN;
            selection = 0;
          break;
          case ROTATE:
            selection += pci_counter;
            pci_counter = 0;
          break;
          default:
          break;
        }
        if (selection <= 0) {
          selection = 0;
          selected_menu = MENU_PLAN_TIME;
        }
        else if (selection <= 6) {
          selected_menu = MENU_PLAN_TIME;
        }
        else if (selection >= 8) {
          selection = 8;
          selected_menu = MENU_PLAN_TIME_SET_2;
        }
        else {
          selected_menu = MENU_PLAN_TIME_SET_1;
        }
      break;

      case MENU_PLAN_TIME_SET_1:
        switch (key_state) {
          case KEY_OK:
            menu_page = selected_menu;
          break;
          case KEY_ESC:
            menu_page = MENU_PLAN_TIME;
            subselection = 0;
          break;
          case ROTATE:
            subselection += pci_counter;
            pci_counter = 0;
          break;
          default:
          break;
        }
        if (subselection <= 0) {
          subselection = 0;
          selected_menu = MENU_PLAN_SET_HOURS_1;
        }
        else if (subselection >= 2) {
          subselection = 2;
          selected_menu = MENU_PLAN_SET_ACTIVE_1;
        }
        else {
          selected_menu = MENU_PLAN_SET_MINS_1;
        }
      break;

      case MENU_PLAN_TIME_SET_2:
        switch (key_state) {
          case KEY_OK:
            menu_page = selected_menu;
          break;
          case KEY_ESC:
            menu_page = MENU_PLAN_TIME;
            subselection = 0;
          break;
          case ROTATE:
            subselection += pci_counter;
            pci_counter = 0;
          break;
          default:
          break;
        }
        if (subselection <= 0) {
          subselection = 0;
          selected_menu = MENU_PLAN_SET_HOURS_2;
        }
        else if (subselection >= 2) {
          subselection = 2;
          selected_menu = MENU_PLAN_SET_ACTIVE_2;
        }
        else {
          selected_menu = MENU_PLAN_SET_MINS_2;
        }
      break;

      case MENU_PLAN_SET_HOURS_1:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_PLAN_TIME_SET_1;
            plan_hours_1 = plan_hours_1_temp;
          break;
          case KEY_ESC:
            menu_page = MENU_PLAN_TIME_SET_1;
            plan_hours_1_temp = plan_hours_1;
          break;
          case ROTATE:
            plan_hours_1_temp += pci_counter;
            pci_counter = 0;
            if (plan_hours_1_temp < HOURS_MIN) plan_hours_1_temp = HOURS_MAX;
            else if (plan_hours_1_temp > HOURS_MAX) plan_hours_1_temp = HOURS_MIN;
          break;
          default:
          break;
        }
      break;
      
      case MENU_PLAN_SET_HOURS_2:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_PLAN_TIME_SET_2;
            plan_hours_2 = plan_hours_2_temp;
          break;
          case KEY_ESC:
            menu_page = MENU_PLAN_TIME_SET_2;
            plan_hours_2_temp = plan_hours_2;
          break;
          case ROTATE:
            plan_hours_2_temp += pci_counter;
            pci_counter = 0;
            if (plan_hours_2_temp < HOURS_MIN) plan_hours_2_temp = HOURS_MAX;
            else if (plan_hours_2_temp > HOURS_MAX) plan_hours_2_temp = HOURS_MIN;
          break;
          default:
          break;
        }
      break;

      case MENU_PLAN_SET_MINS_1:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_PLAN_TIME_SET_1;
            plan_minutes_1 = plan_minutes_1_temp;
          break;
          case KEY_ESC:
            menu_page = MENU_CLOCK;
            plan_minutes_1_temp = plan_minutes_1;
          break;
          case ROTATE:
            plan_minutes_1_temp += pci_counter;
            pci_counter = 0;
            if (plan_minutes_1_temp < MINUTES_MIN) plan_minutes_1_temp = MINUTES_MAX;
            else if (plan_minutes_1_temp > MINUTES_MAX) plan_minutes_1_temp = MINUTES_MIN;
          break;
          default:
          break;
        }
      break;

      case MENU_PLAN_SET_MINS_2:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_PLAN_TIME_SET_2;
            plan_minutes_2 = plan_minutes_2_temp;
          break;
          case KEY_ESC:
            menu_page = MENU_CLOCK;
            plan_minutes_2_temp = plan_minutes_2;
          break;
          case ROTATE:
            plan_minutes_2_temp += pci_counter;
            pci_counter = 0;
            if (plan_minutes_2_temp < MINUTES_MIN) plan_minutes_2_temp = MINUTES_MAX;
            else if (plan_minutes_2_temp > MINUTES_MAX) plan_minutes_2_temp = MINUTES_MIN;
          break;
          default:
          break;
        }
      break;

      case MENU_PLAN_SET_ACTIVE_1:
        switch (key_state) {
          case KEY_OK:
          case KEY_ESC:
            menu_page = MENU_PLAN_TIME_SET_1;
          break;
          case ROTATE:
            plan_1_active = !plan_1_active;
            pci_counter = 0;
          break;
          default:
          break;
        }
      break;

      case MENU_PLAN_SET_ACTIVE_2:
        switch (key_state) {
          case KEY_OK:
          case KEY_ESC:
            menu_page = MENU_PLAN_TIME_SET_2;
          break;
          case ROTATE:
            plan_2_active = !plan_2_active;
            pci_counter = 0;
          break;
          default:
          break;
        }
      break;

      case MENU_PLAN_AMOUNT:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_PLAN_AMOUNT_SET;
          break;
          case KEY_ESC:
            menu_page = MENU_PLAN;
            selection = 1;
          break;
          case ROTATE:
            pci_counter = 0;
          break;
          default:
          break;
        }
      break;

      case MENU_PLAN_AMOUNT_SET:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_PLAN_AMOUNT;
            pump_duration_secs = pump_duration_secs_temp;
          break;
          case KEY_ESC:
            menu_page = MENU_PLAN_AMOUNT;
            pump_duration_secs_temp = pump_duration_secs;
          break;
          case ROTATE:
            pump_duration_secs_temp += pci_counter*9;
            pci_counter = 0;
            if (pump_duration_secs_temp < PUMP_SECS_MIN) pump_duration_secs_temp = PUMP_SECS_MIN;
            else if (pump_duration_secs_temp > PUMP_SECS_MAX) pump_duration_secs_temp = PUMP_SECS_MAX;
          break;
          default:
          break;
        }
      break;

      case MENU_SENSOR:
        switch (key_state) {
          case KEY_OK:
            menu_page = selected_menu;
            threshold_perc_temp = threshold_perc;
          break;
          case KEY_ESC:
            menu_page = MENU_SETTINGS;
            selection = 2;
          break;
          case ROTATE:
          break;
          default:
          break;
        }
        selected_menu = MENU_SENSOR_THRESHOLD;
      break;

      case MENU_SENSOR_THRESHOLD:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_SENSOR_THRESHOLD_SET;
          break;
          case KEY_ESC:
            menu_page = MENU_SENSOR;
            selection = 0;
          break;
          case ROTATE:
          break;
          default:
          break;
        }
      break;

      case MENU_SENSOR_THRESHOLD_SET:
        switch (key_state) {
          case KEY_OK:
            menu_page = MENU_SENSOR_THRESHOLD;
            threshold_perc = threshold_perc_temp;
          break;
          case KEY_ESC:
            menu_page = MENU_SENSOR_THRESHOLD;
            threshold_perc_temp = threshold_perc;
          break;
          case ROTATE:
            threshold_perc_temp += pci_counter;
            pci_counter = 0;
          break;
          default:
          break;
        }
      break;

      case MENU_MODE:
        switch (key_state) {
          case KEY_OK:
            mode = selection;
          break;
          case KEY_ESC:
            menu_page = MENU_SETTINGS;
            selection = 3;
          break;
          case ROTATE:
            selection += pci_counter;
            pci_counter = 0;
            if (selection <= 0) selection = 0;
            else selection = 1;
          break;
          default:
          break;
        }
      break;      
          
      case MENU_INFO:
        switch (key_state) {
          case KEY_OK:
            display_sleep = !display_sleep;
          break;
          case KEY_ESC:
            if (!display_sleep) menu_page = MENU_SETTINGS;
          break;
          case ROTATE:
            pci_counter = 0;
          break;
          default:
          break;
        }
      break;
    }

    update_display();
  }
}


// Interrupt Service Routine für den Pin Change Interrupt
ISR(PCINT0_vect)
{
  D9_new = digitalRead(9);
  D8_new = digitalRead(8);

  if (D9_new != D9_old) {
    // CLK (D9) hat Interrupt ausgelöst
    if ((D9_new == HIGH) && (D9_old == LOW)) {
      // steigende Flanke
      if (D8_new == HIGH) pci_counter--;  // CLK (an D9) eilt DT (an D8) hinterher -> gegen Uhrzeigersinn
      else pci_counter++;                 // DT eilt CLK hinterher -> im Uhrzeigersinn
    }
  }

  D9_old = D9_new;
  D8_old = D8_new;

  pci_flag = 1;
}
