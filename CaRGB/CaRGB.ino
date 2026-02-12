// For Arduino Pro Mini
// Andrew Lehman

// NOTE: The PUSHBUTTON lights are powered and controlled SEPERATELY from the light strips.

#include <FastLED.h>
#include <Bounce2.h>
#include <SoftwareSerial.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef D0
  #define D0 0
  #define D1 1
  #define D2 2
  #define D3 3
  #define D4 4
  #define D5 5
  #define D6 6
  #define D7 7
  #define D8 8
  #define D9 9
  #define D10 10
  #define D11 11
  #define D12 12
  #define D13 13
#endif


bool Debug_En = true;
bool BT_En = false;


#define BT_RX_Pin     D0
#define BT_TX_Pin     D1
SoftwareSerial BT(BT_RX_Pin, BT_TX_Pin);

// ---------- Forward declarations ----------
void read_BT();
void process_CMD(char *cmd);
void print_BT(const char *format, ...);
void print_Debug(const char *format, ...);
void applyBtnState(uint8_t state);
void btSend(const char *msg);
bool parseByte(const char *s, uint8_t &out);

// ---------- Enums ----------
enum class Zone_State : uint8_t { OFF, STARTUP, ON, SHUTOFF };
enum class Zone_Run_Mode : uint8_t { SOLID, BREATH, CHASE, SHIMMER, SYNC_AUDIO, SYNC_RPM, SYNC_SPEED };
enum class Zone_Startup_Mode : uint8_t { NONE, FADE_IN, CHASE_IN, GLITCH_IN };
enum class Zone_Shutoff_Mode : uint8_t { NONE, FADE_OUT, CHASE_OUT, GLITCH_OFF };

struct Zone {
  // --- Identity / hardware ---
  const char* Name = "";
  uint8_t Pwr_Pin = 255;
  uint8_t Btn_Pin = 255;

  // Button state
  Bounce Deb;
  bool Btn = false;
  bool Btn_Last = false;
  bool Trig_On = false;
  bool Trig_Off = false;

  // LED buffer
  CRGB* leds = nullptr;
  uint16_t Led_Count = 0;

  // --- Power / animation control ---
  Zone_State State = Zone_State::OFF;
  bool Startup_En = true;
  bool Shutoff_En = true;
  Zone_Startup_Mode Startup_Mode = Zone_Startup_Mode::FADE_IN;
  Zone_Shutoff_Mode Shutoff_Mode = Zone_Shutoff_Mode::FADE_OUT;
  bool Startup_Run = false;
  bool Shutoff_Run = false;

  // --- RGB settings ---
  Zone_Run_Mode Run_Mode = Zone_Run_Mode::SOLID;   // FIX: you don't have STATIC
  uint8_t Brt = 80;
  CRGB Col[3] = { CRGB::White, CRGB::Red, CRGB::Green };

  // --- Animation state ---
  uint16_t Frame = 0;

  void begin(uint8_t btnPin, uint8_t pwrPin, uint16_t debounceMs = 25) {
    Btn_Pin = btnPin;
    Pwr_Pin = pwrPin;

    pinMode(Btn_Pin, INPUT_PULLUP);
    Deb.attach(Btn_Pin);
    Deb.interval(debounceMs);

    if (Pwr_Pin != 255) {
      pinMode(Pwr_Pin, OUTPUT);
      digitalWrite(Pwr_Pin, LOW);
    }
  }

  void updateButton() {
    Deb.update();
    Btn = (Deb.read() == LOW);     // active low
    Trig_On  =  Btn && !Btn_Last;
    Trig_Off = !Btn &&  Btn_Last;
    Btn_Last = Btn;
  }

  void handleTransitions() {
    if (Trig_On) {
      if (Startup_En && Startup_Mode != Zone_Startup_Mode::NONE) {
        Startup_Run = true;
        Shutoff_Run = false;
        State = Zone_State::STARTUP;
        Frame = 0;
      } else {
        State = Zone_State::ON;
      }
    }

    if (Trig_Off) {
      if (Shutoff_En && Shutoff_Mode != Zone_Shutoff_Mode::NONE) {
        Shutoff_Run = true;
        Startup_Run = false;
        State = Zone_State::SHUTOFF;
        Frame = 0;
      } else {
        State = Zone_State::OFF;
      }
    }
  }

  void applyPower() {
    if (Pwr_Pin == 255) return;
    const bool pwr = (State != Zone_State::OFF);
    digitalWrite(Pwr_Pin, pwr ? HIGH : LOW);
  }

  static inline uint8_t progress8(uint16_t frame, uint16_t N) {
    if (frame >= N) return 255;
    return (uint8_t)((frame * 255UL) / N);
  }

  void renderMode() {
    if (!leds || Led_Count == 0) return;

    switch (Run_Mode) {
      case Zone_Run_Mode::SOLID: {
        fill_solid(leds, Led_Count, Col[0]);
      } break;

      default:
        fill_solid(leds, Led_Count, CRGB::Black);
        break;
    }

    // Apply per-zone brightness
    if (Brt < 255) {
      for (uint16_t i = 0; i < Led_Count; i++) leds[i].nscale8_video(Brt);
    }
  }

  void renderStartup(uint32_t /*nowMs*/) {
    if (!leds || Led_Count == 0) { Startup_Run = false; State = Zone_State::ON; return; }

    const uint16_t N = 200;
    const uint8_t p = progress8(Frame, N);

    switch (Startup_Mode) {
      case Zone_Startup_Mode::NONE:
        Startup_Run = false;
        break;

      default:
        for (uint16_t i = 0; i < Led_Count; i++) leds[i].nscale8_video(p);
        if (Frame++ >= N) Startup_Run = false;
        break;
    }

    if (!Startup_Run) {
      State = Zone_State::ON;
      Frame = 0;
    }
  }

  void renderShutoff(uint32_t /*nowMs*/) {
    if (!leds || Led_Count == 0) { Shutoff_Run = false; State = Zone_State::OFF; return; }

    const uint16_t N = 200;
    const uint8_t p = progress8(Frame, N);
    const uint8_t inv = 255 - p;

    switch (Shutoff_Mode) {
      case Zone_Shutoff_Mode::NONE:
        Shutoff_Run = false;
        break;

      default:
        for (uint16_t i = 0; i < Led_Count; i++) leds[i].nscale8_video(inv);
        if (Frame++ >= N) Shutoff_Run = false;
        break;
    }

    if (!Shutoff_Run) {
      State = Zone_State::OFF;
      Frame = 0;
    }
  }

  void render(uint32_t nowMs) {
    if (!leds || Led_Count == 0) return;

    if (State == Zone_State::OFF) {
      fill_solid(leds, Led_Count, CRGB::Black);
      return;
    }

    renderMode();

    if (State == Zone_State::STARTUP) renderStartup(nowMs);
    if (State == Zone_State::SHUTOFF) renderShutoff(nowMs);
  }

  CRGB frameFirstPixel() const {
    if (!leds || Led_Count == 0) return CRGB::Black;
    return leds[0];
  }
};

// ------------------------------- BUTTON + PWR zone variables -------------------------------
#define Btn_RGB_Pin     D6
#define Btn_RGB_Num     4
#define Btn_RGB_Type    WS2812
#define Btn_RGB_Order   GRB

CRGB Btn_RGB[Btn_RGB_Num];

CRGB Pwr_RGB_Col  = CRGB::Green;
uint8_t Pwr_RGB_Brt = 60;

static uint8_t Btn_State = 254;
static uint8_t Btn_State_Last = 254;

// -------------------- Zones: pins + LED arrays --------------------
#define Int_Btn_Pin    A0
#define Int_Pwr_Pin    D13
#define Int_RGB_Pin    D5

#define Int_RGB_Num    50
#define Int_RGB_Type   WS2812
#define Int_RGB_Order  GRB
CRGB Int_RGB[Int_RGB_Num];
Zone Z_Int;

#define Ext_Btn_Pin    A1
#define Ext_Pwr_Pin    D12
#define Ext_RGB_Pin    D4

#define Ext_RGB_Num    50
#define Ext_RGB_Type   WS2812
#define Ext_RGB_Order  GRB
CRGB Ext_RGB[Ext_RGB_Num];
Zone Z_Ext;

#define Misc_Btn_Pin   A2
#define Misc_Pwr_Pin   D11
#define Misc_RGB_Pin   D3

#define Misc_RGB_Num   1
#define Misc_RGB_Type  WS2812
#define Misc_RGB_Order GRB
CRGB Misc_RGB[Misc_RGB_Num];
Zone Z_Misc;

// Placeholder for old variable referenced in process_CMD
uint8_t Int_RGB_Brt = 255;

// -------------------- Setup / Loop --------------------
void setup() {
  if (Debug_En) {
    Serial.begin(115200);
    print_Debug("Debug Enabled");
  }

  if (BT_En) {
    BT.begin(9600);
    print_BT("Bluetooth Enabled");
    if (Debug_En) {
      print_Debug("Debug Enabled");
    }
  }

  Z_Int.Name = "Interior";
  Z_Int.leds = Int_RGB;
  Z_Int.Led_Count = Int_RGB_Num;
  Z_Int.Col[0]  = CRGB(255, 35, 0);
  Z_Int.Col[1]  = CRGB::Black;
  Z_Int.Col[2]  = CRGB::Black;
  Z_Int.Brt = 255;
  Z_Int.begin(Int_Btn_Pin, Int_Pwr_Pin);


  Z_Ext.Name = "Exterior";
  Z_Ext.leds = Ext_RGB;
  Z_Ext.Led_Count = Ext_RGB_Num;
  Z_Ext.Col[0]  = CRGB(255, 255, 255);
  Z_Ext.Col[1]  = CRGB::Black;
  Z_Ext.Col[2]  = CRGB::Black;
  Z_Ext.Brt = 125;
  Z_Ext.begin(Ext_Btn_Pin, Ext_Pwr_Pin);


  Z_Misc.Name = "Misc";
  Z_Misc.leds = Misc_RGB;
  Z_Misc.Led_Count = Misc_RGB_Num;
  Z_Misc.Col[0]  = CRGB::Blue;
  Z_Misc.Col[1]  = CRGB::Black;
  Z_Misc.Col[2]  = CRGB::Black;
  Z_Misc.Brt = 255;
  Z_Misc.begin(Misc_Btn_Pin, Misc_Pwr_Pin);


  FastLED.addLeds<Btn_RGB_Type, Btn_RGB_Pin, Btn_RGB_Order>(Btn_RGB, Btn_RGB_Num);
  FastLED.addLeds<Int_RGB_Type, Int_RGB_Pin, Int_RGB_Order>(Int_RGB, Int_RGB_Num);
  FastLED.addLeds<Ext_RGB_Type, Ext_RGB_Pin, Ext_RGB_Order>(Ext_RGB, Ext_RGB_Num);
  FastLED.addLeds<Misc_RGB_Type, Misc_RGB_Pin, Misc_RGB_Order>(Misc_RGB, Misc_RGB_Num);

  FastLED.setBrightness(255);
  FastLED.clear(true);

  // Just to get code working with solid colors
  Z_Int.Run_Mode  = Zone_Run_Mode::SOLID;
  Z_Ext.Run_Mode  = Zone_Run_Mode::SOLID;
  Z_Misc.Run_Mode = Zone_Run_Mode::SOLID;

  applyBtnState(254);
  print_Debug("Initialized");
}

void loop() {
  const uint32_t tStart = millis();

  Z_Int.updateButton();
  Z_Ext.updateButton();
  Z_Misc.updateButton();

  read_BT();

  Z_Int.handleTransitions();
  Z_Ext.handleTransitions();
  Z_Misc.handleTransitions();

  Z_Int.applyPower();
  Z_Ext.applyPower();
  Z_Misc.applyPower();

  Btn_State = (Z_Int.Btn << 2) | (Z_Ext.Btn << 1) | (Z_Misc.Btn << 0);

  Z_Int.render(tStart);
  Z_Ext.render(tStart);
  Z_Misc.render(tStart);

  if (Btn_State != Btn_State_Last) {
    applyBtnState(Btn_State);
    Btn_State_Last = Btn_State;

    print_Debug("raw A0=%d A1=%d A2=%d | Btn I=%d E=%d M=%d | On I=%d E=%d M=%d | Off I=%d E=%d M=%d",
      digitalRead(A0), digitalRead(A1), digitalRead(A2),
      Z_Int.Btn, Z_Ext.Btn, Z_Misc.Btn,
      Z_Int.Trig_On, Z_Ext.Trig_On, Z_Misc.Trig_On,
      Z_Int.Trig_Off, Z_Ext.Trig_Off, Z_Misc.Trig_Off
    );
    delay(200);
  }

  FastLED.show();

  uint32_t tEnd = millis();
  uint32_t tFrame = tEnd - tStart;
  // print_Debug("Frame Time (mS): %lu", (unsigned long)tFrame);
}

// -------------------- General Functions --------------------
void applyBtnState(uint8_t state) {
  // Sample rendered frame colors (brightness already baked in)
  const CRGB cInt  = (Z_Int.leds  && Z_Int.Led_Count  > 0) ? Z_Int.leds[0]  : CRGB::Black;
  const CRGB cExt  = (Z_Ext.leds  && Z_Ext.Led_Count  > 0) ? Z_Ext.leds[0]  : CRGB::Black;
  const CRGB cMisc = (Z_Misc.leds && Z_Misc.Led_Count > 0) ? Z_Misc.leds[0] : CRGB::Black;

  // Clear all button LEDs
  fill_solid(Btn_RGB, Btn_RGB_Num, CRGB::Black);
  Btn_RGB[3] = Pwr_RGB_Col;

  // Power button base color
  if (state & 0b100) {
    fill_solid(Btn_RGB, Btn_RGB_Num, cInt);
  }

  if (state & 0b010) Btn_RGB[1] = cExt;

  if (state & 0b001) Btn_RGB[2] = cMisc;

  // Apply brightness ONLY to the power button pixel
  if (Pwr_RGB_Brt < 255) {
    Btn_RGB[3].nscale8_video(Pwr_RGB_Brt);
  }
}

// -------------------- BT command handling --------------------
void read_BT() {
  if (!BT_En) return;

  static char cmdBuf[32];
  static uint8_t idx = 0;

  while (BT.available()) {
    char c = (char)BT.read();

    if (c == '\n' || c == '\r') {
      if (idx > 0) {
        cmdBuf[idx] = '\0';
        process_CMD(cmdBuf);
        idx = 0;
      }
    } else if (idx < sizeof(cmdBuf) - 1) {
      cmdBuf[idx++] = c;
    }
  }
}

void process_CMD(char *cmd) {
  char *zone  = strtok(cmd, " ");
  char *param = strtok(nullptr, " ");
  char *val   = strtok(nullptr, " ");

  if (!zone || !param) { btSend("ERR"); return; }

  if (strcmp(zone, "INT") == 0 && strcmp(param, "BRIGHTNESS") == 0) {
    uint8_t b;
    if (!parseByte(val, b)) { btSend("ERR BAD VALUE"); return; }
    Int_RGB_Brt = b;   // placeholder until you map this into Z_Int.Brt etc.
    btSend("OK");
    return;
  }

  btSend("ERR UNKNOWN");
}

// -------------------- Print helpers --------------------
void print_BT(const char *format, ...) {
  if (!BT_En) return;

  char buffer[64];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  BT.println(buffer);
}

void print_Debug(const char *format, ...) {
  if (!Debug_En) return;

  char buffer[64];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  Serial.println(buffer);
  if (BT_En) BT.println(buffer);
}

// -------------------- Stubs / placeholders --------------------
void btSend(const char *msg) { // what is this for???? seems redundant to print_BT()
  // Keep it simple: send over BT if enabled, else do nothing
  if (BT_En) BT.println(msg);
}

bool parseByte(const char *s, uint8_t &out) {
  if (!s) return false;
  char *end = nullptr;
  long v = strtol(s, &end, 10);
  if (end == s) return false;
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  out = (uint8_t)v;
  return true;
}
