// For Arduino Pro Mini
// Andrew Lehman
// 2026/1/14 - Created new CaRGB code from scratch (using FastLED library instead this time)
// 2026/2/2 - Created a new STRUCT for the RGB Zones instead but didnt impliment it yet.
//            also updated the modes, added startup and shutoff animations, etc. I changed a lot since the last version.

// Im probably going to make the MISC button start data recording (OBD2 and IMU + other data) but I may want to move this to a different controller to handle processing all that?

#include <FastLED.h>
#include <Bounce2.h>
#include <SoftwareSerial.h>

// NOTE: The PUSHBUTTON lights are powered and controlled SEPERATELY from the light strips.
// The button RGB lights are in series (array of 4 lights). They are addressed individually with the Btn_RGB array.
// Power button simply switches the 12V power on / off to the arduino. If arduino is on, power button is pressed.


#define BT_RX_Pin     D0
#define BT_TX_Pin     D1
softwareSerial BT(BT_RX_Pin, BT_TX_Pin);


enum class Zone_State : uint8_t { OFF, STARTUP, ON, SHUTOFF };
enum class Zone_Run_Mode : uint8_t { SOLID, BREATH, CHASE, SHIMMER, SYNC_AUDIO, SYNC_RPM, SYNC_SPEED };
enum class Zone_Startup_Mode : uint8_t { NONE, FADE_IN, CHASE_IN, GLITCH_IN };
enum class Zone_Shutoff_Mode : uint8_t { NONE, FADE_OUT, CHASE_OUT, GLITCH_OFF };


bool Debug_En = false; // When true, it will print to command line for debugging (not yet implimented)
bool BT_En = false;


struct Zone {
  // --- Identity / hardware ---
  const char* Name = "";
  uint8_t Pwr_Pin = 255;
  uint8_t RGB_Pin = 255;         // optional (FastLED manages output pin internally)
  CRGB* leds = nullptr;          // NOT SURE WHAT THIS IS FOR, either th RGB type or the order? (WS2812 and GRB)
  uint16_t RGB_Qty = 0;

  // --- Button (debounced stable state externally, or you can store your debouncer object here) ---
  bool Btn = false;              // stable state: true = ON request
  bool Btn_Last = false;
  bool Trig_On = false;           // edge pulse
  bool Trig_Off = false;

  // --- Power / animation control ---
  Zone_State State = Zone_State::OFF;

  bool Startup_En = true;         // feature enable
  bool Shutoff_En = true;
  Zone_Startup_Mode Startup_Mode = Zone_Startup_Mode::FADE_IN;
  Zone_Shutoff_Mode Shutoff_Mode = Zone_Shutoff_Mode::FADE_OUT;

  bool Startup_Run = false;       // latched until animation finishes
  bool Shutoff_Run = false;       // latched until animation finishes

  // --- RGB settings ---
  Zone_Run_Mode Run_Mode = Zone_Run_Mode::STATIC;
  uint8_t Brt = 80;
  uint8_t Col[3][3] = { {255,255,255}, {255,0,0}, {0,255,0} }; // 3 colors, RGB

  // --- Animation state (per-zone) ---
  uint16_t Frame = 0;
  uint32_t tLast = 0;

  // ---------- Methods ----------
  // I/O Methods
  void setButton(bool newBtn) { //checks the button state to the last saved state, then toggles the on / off trigger bits and updates the Btn_Last bit
    Btn = newBtn;
    Trig_On  = Btn && !Btn_Last;
    Trig_Off = !Btn && Btn_Last;
    Btn_Last = Btn;
  }

  void handleTrans() { // If button transition is seen, toggle the startup or shutoff bits for the zone
    if (Trig_On) {
      if (Startup_En && Startup_Mode != Zone_Startup_Mode::NONE) {
        Startup_Run = true;
        Shutoff_Run = false;
        State = Zone_State::STARTING;
        Frame = 0;
      } else {
        State = Zone_State::ON;
      }
    }

    if (Trig_Off) {
      if (Shutoff_En && Shutoff_Mode != Zone_Shutoff_Mode::NONE) {
        Shutoff_Run = true;
        Startup_Run = false;
        State = Zone_State::SHUTTING;
        Frame = 0;
      } else {
        State = Zone_State::OFF;
      }
    }
  }

  void applyPower() { // Updates the power output pin
    if (Pwr_Pin == 255) return;
    // Decide when power should be on. Usually ON during STARTING/ON/SHUTTING.
    bool pwr = (State != Zone_State::OFF);
    digitalWrite(Pwr_Pin, pwr ? HIGH : LOW);
  }

  // Rendering Methods
  void render(uint32_t nowMs) {
    // 1) always render base frame first (unless OFF)
    if (State == Zone_State::OFF) {
      fill_solid(leds, RGB_Qty, CRGB::Black);
      return;
    }

    renderMode(); // <-- base frame for ON/STARTING/SHUTTING

    // 2) apply overlay transition if in STARTING/SHUTTING
    if (State == Zone_State::STARTUP) renderStartup(nowMs);
    if (State == Zone_State::SHUTOFF) renderShutoff(nowMs);
  }

  static inline uint8_t progress8(uint16_t frame, uint16_t N) {   // maps current frame and total frame to a 0-255 (8bit) scaled linear value (to indicate PROGRESS to then drive functions within animations)
    if (frame >= N) return 255;
    return (uint8_t)((frame * 255UL) / N);
  }

  void renderMode() {
    if (!leds) return;

    switch (Run_Mode) {
      case Zone_Run_Mode::STATIC: {
        CRGB c(col[0][0], col[0][1], col[0][2]);  //col[0][#] selects the FIRST color for that zone. col[1][#] = the second, col[2][#] = the third
        fill_solid(leds, RGB_Qty, c);
      } break;

      /*
      case Zone_Run_Mode::STATIC: {
        // Add aditional zone mode logic here. for animations you will have to call the code with the specified render frame number / variable Frame and tLast

        CRGB c(col[0][0], col[0][1], col[0][2]);
        fill_solid(leds, RGB_Qty, c);
      } break;
      */

      default:
        fill_solid(leds, RGB_Qty, CRGB::Black);
        break;
    }
  }

  void renderStartup(uint32_t nowMs) {
    if (!leds) { Startup_Run = false; return; }

    const uint16_t N = 40;                // placeholder duration in frames
    const uint8_t p = progress8(Frame, N); // 0..255

    switch (Startup_Mode)
    {
      case Zone_Startup_Mode::NONE:
        // No startup effect. Immediately consider startup "done".
        Startup_Run = false;
        break;

      case Zone_Startup_Mode::FADE_IN:
        // Overlay: scale current frame by p (0..255).
        for (uint16_t i = 0; i < RGB_Qty; i++)
          leds[i].nscale8_video(p);

        if (Frame++ >= N) Startup_Run = false;
        break;

      case Zone_Startup_Mode::CHASE_IN:
        // Placeholder: for now just treat like a fade, or leave TODO.
        // Later: reveal only first k pixels of the already-rendered base frame.
        // uint16_t k = (uint32_t)p * RGB_Qty / 255;
        // for (uint16_t i = k; i < RGB_Qty; i++) leds[i] = CRGB::Black;

        // TEMP: behave like FADE_IN until you implement CHASE
        for (uint16_t i = 0; i < RGB_Qty; i++)
          leds[i].nscale8_video(p);

        if (Frame++ >= N) Startup_Run = false;
        break;

      case Zone_Startup_Mode::GLITCH_IN:
        // Stub for now — keep structure, implement later.
        // Later idea (your concept): random reveal that starts fast and slows (logarithmic reveal gradient),
        // optionally weighted toward the "front" of the strip (linear gradient).
        //
        // TODO: implement glitch mask/reveal without extra RAM (likely with a deterministic PRNG + threshold).
        //
        // TEMP: behave like FADE_IN
        for (uint16_t i = 0; i < RGB_Qty; i++)
          leds[i].nscale8_video(p);

        if (Frame++ >= N) Startup_Run = false;
        break;
    }
  }

  void renderShutoff(uint32_t nowMs) {
    if (!leds) { Shutoff_Run = false; return; }

    const uint16_t N = 40;
    const uint8_t p = progress8(frame, N);     // 0..255
    const uint8_t p_inv = 255 - p;               // 255..0

    switch (Shutoff_Mode)
    {
      case Zone_Shutoff_Mode::NONE:
        Shutoff_Run = false;
        break;

      case Zone_Shutoff_Mode::FADE_OUT:
        for (uint16_t i = 0; i < RGB_Qty; i++)
          leds[i].nscale8_video(p_inv);

        if (Frame++ >= N) Shutoff_Run = false;
        break;

      case Zone_Shutoff_Mode::CHASE_OUT:
        // TODO: gradually black out more pixels each frame.
        // TEMP: behave like FADE_OUT
        for (uint16_t i = 0; i < RGB_Qty; i++)
          leds[i].nscale8_video(p_inv);

        if (Frame++ >= N) Shutoff_Run = false;
        break;

      case Zone_Shutoff_Mode::GLITCH_OFF:
        // TODO: glitchy disappearing / flicker-out
        // TEMP: behave like FADE_OUT
        for (uint16_t i = 0; i < RGB_Qty; i++)
          leds[i].nscale8_video(p_inv);

        if (Frame++ >= N) Shutoff_Run = false;
        break;
    }
  }
};



// ------------------------------- BUTTON + PWR zone variables -------------------------------
#define Btn_RGB_Pin     D6
#define Btn_RGB_Num     4
#define Btn_RGB_Type    WS2812
#define Btn_RGB_Order   GRB

CRGB Pwr_RGB_Col  = CRGB::Green;      // Sets the default solid power button color
uint8_t Pwr_RGB_Brt  60               // 0-255, the default power button brightness

CRGB Btn_RGB[Btn_RGB_Num];            // Create the LED array

bounce Int_Btn_DB;
bounce Ext_Btn_DB;
bounce Misc_Btn_DB;

// use these to track the DEBONCED LOGIC button state and enable strip power + Mode
bool Int_Btn; // current debounced button state
bool Ext_Btn;
bool Misc_Btn;

bool Int_Btn_Last = 0; // previous debounced button state
bool Ext_Btn_Last = 0;
bool Misc_Btn_Last = 0;

// ------------------------------- INTERIOR zone variables -------------------------------
// Man looks like using classes would be good about now...
#define Int_Btn_Pin     A0
#define Int_Pwr_On_Pin  D13

#define Int_RGB_Pin     D5
#define Int_RGB_Num     150
#define Int_RGB_Type    WS2812
#define Int_RGB_Order   GRB

bool Int_Trig_On = false;
bool Int_Trig_Off = false;

bool Int_Startup_En = false;              // Enables startup animation
bool Int_Shutoff_En = false;              // Enables shutoff animation
bool Int_Startup_Run = false;
bool Int_Shutoff_Run = false;
RGB_Startup Int_RGB_Startup = FADE_IN;    // Startup animation
RGB_Shutoff Int_RGB_Shutoff = FADE_OUT;   // Shutoff animation

RGB_Mode Int_RGB_Mode = SOLID;        // Mode = Animation type
CRGB Int_RGB_Mode_Col[3] = {CRGB::Orange, CRGB::Yellow, CRGB::Black}; //specifies the primary [0], secondary [2], and teritary [3] colors for the animation mode. Excess colors wont be used by some animations.
CRGB Int_RGB_Col  = CRGB(255, 35, 0); // Sets the default interior SOLID color (for int button and strips)
uint8_t Int_RGB_Brt  255              // 0-255, the default strip + button brightness

CRGB Int_RGB[Int_RGB_Num];            // Create the LED array


// ------------------------------- EXTERIOR zone variables -------------------------------
#define Ext_Btn_Pin     A1
#define Ext_Pwr_On_Pin  D12

#define Ext_RGB_Pin     D4
#define Ext_RGB_Num     150
#define Ext_RGB_Type    WS2812
#define Ext_RGB_Order   GRB

bool Ext_Trig_On = false;
bool Ext_Trig_Off = false;

bool Ext_Startup_En = false;              // Enables startup animation
bool Ext_Shutoff_En = false;              // Enables shutoff animation
bool Ext_Startup_Run = false;
bool Ext_Shutoff_Run = false;
RGB_Startup Ext_RGB_Startup = FADE_IN;    // Startup animation
RGB_Shutoff Ext_RGB_Shutoff = FADE_OUT;   // Shutoff animation

RGB_Mode Ext_RGB_Mode = SOLID;        // Mode = Animation type
CRGB Ext_RGB_Mode_Col[3] = {CRGB::Orange, CRGB::Yellow, CRGB::Black}; //specifies the primary [0], secondary [2], and teritary [3] colors for the animation mode. Excess colors wont be used by some animations.
CRGB Ext_RGB_Col  = CRGB::White;      // Sets the default exterior SOLID color (for int button and strips)
uint8_t Ext_RGB_Brt  125              // 0-255, the default strip + button brightness

CRGB Ext_RGB[Ext_RGB_Num];            // Create the LED array


// ------------------------------- MISC zone variables -------------------------------
// Misc currently has no RGB STRIP output. The misc button light is managed by the Btn_RGB array.
#define Misc_Btn_Pin    A2
#define Misc_Pwr_On_Pin D11

#define Misc_RGB_Pin     D3
#define Misc_RGB_Num     1
#define Misc_RGB_Type    WS2812
#define Misc_RGB_Order   GRB

bool Misc_Trig_On = false;
bool Misc_Trig_Off = false;
bool Misc_Startup_Run = false;
bool Misc_Shutoff_Run = false;
bool Misc_Startup_En = false;             // Enables startup animation
bool Misc_Shutoff_En = false;             // Enables shutoff animation

RGB_Startup Misc_RGB_Startup = FADE_IN;   // Startup animation
RGB_Shutoff Misc_RGB_Shutoff = FADE_OUT;  // Shutoff animation

RGB_Mode Misc_RGB_Mode = SOLID;       // Mode = Animation type
CRGB Misc_RGB_Mode_Col[3] = {CRGB::Orange, CRGB::Yellow, CRGB::Black}; //specifies the primary [0], secondary [2], and teritary [3] colors for the animation mode. Excess colors wont be used by some animations.
CRGB Misc_RGB_Col = CRGB::Blue;       // Sets the default misc SOLID color (for int button and strips)
uint8_t Misc_RGB_Brt  255             // 0-255, the default strip + button brightness

CRGB Misc_RGB[Misc_RGB_Num];          // Create the LED array


// ------------------------------- general variables -------------------------------
// Stores button combination state (to detect changes). 3-bit value of Int, Ext, Misc buttons respectively (000; MSB --> LSB)
uint8_t Btn_State;
uint8_t Btn_State_Last;   // Stores previous button state combination

void setup() {
  // Initialize general I/O Pins
  // NOTE: the FastLED library automatically sets the pinmodes for the RGB data pins using the FastLED.addLeds.
  pinMode(Int_Btn_Pin, INPUT_PULLUP);
  Int_Btn_DB.attach(Int_Btn_Pin);
  Int_Btn_DB.INTERVAL(25);
  pinMode(Int_Pwr_On_Pin, OUTPUT);

  pinMode(Ext_Btn_Pin, INPUT_PULLUP);
  Ext_Btn_DB.attach(Ext_Btn_Pin);
  Ext_Btn_DB.INTERVAL(25);
  pinMode(Ext_Pwr_On_Pin, OUTPUT);

  pinMode(Misc_Btn_Pin, INPUT_PULLUP);
  Misc_Btn_DB.attach(Misc_Btn_Pin);
  Misc_Btn_DB.INTERVAL(25);
  pinMode(Misc_Pwr_On_Pin, OUTPUT);


  if (BT_En) {
    BT.begin(9600);
    print_BT("Bluetooth Enabled");
  }

  if (Debug_En) {
    Serial.begin(115200);
    print_Debug("Debugging Enabled");
  }


  // Optional safety - limits current draw (potentially good for car wiring)
  // FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000);
  digitalWrite(Int_Pwr_On_Pin, LOW);
  digitalWrite(Ext_Pwr_On_Pin, LOW);
  digitalWrite(Misc_Pwr_On_Pin, LOW);

  // Initialize the LED strips
  FastLED.addLeds<Btn_RGB_Type,  Btn_RGB_Pin,  Btn_RGB_Order>(Btn_RGB,  Btn_RGB_Num);
  FastLED.addLeds<Int_RGB_Type,  Int_RGB_Pin,  Int_RGB_Order>(Int_RGB,  Int_RGB_Num);
  FastLED.addLeds<Ext_RGB_Type,  Ext_RGB_Pin,  Ext_RGB_Order>(Ext_RGB,  Ext_RGB_Num);
  FastLED.addLeds<Misc_RGB_Type,  Misc_RGB_Pin,  Misc_RGB_Order>(Misc_RGB,  Misc_RGB_Num);
  FastLED.setBrightness(255);  // Set OVERALL brightness (0-255)

  // Initialize all strips off
  // immediatley clears the LED array buffers and writes it to the strips.
  // Use "false" to only clear the buffers but keep the strips on the last value instead.
  FastLED.clear(true);

  Btn_State = 254;
  apply_Btn_State(Btn_State);
  print_Debug("Initialized");
}

void loop() {
  // need to track frame start time and then end time
  read_IO(); // This updates the xxx_Btn and Btn_State variables with debouncing

  read_BT();

  // run the zone handlers for zone states. Zones that are on first generate the frame, then the startup animation overlays / modifies the static mode frame (if startup is triggered AND enabled)
  handler_Int();
  handler_Ext();
  handler_Misc();

  if (Btn_State != Btn_State_Last) { // If the button state has changed since last loop run, change the state and update I/O
    // Toggle Power Outputs
    digitalWrite(Int_Pwr_On_Pin, Int_Btn);
    digitalWrite(Ext_Pwr_On_Pin, Ext_Btn);
    digitalWrite(Misc_Pwr_On_Pin, Misc_Btn);
    delay(10); // wait for relays / power to turn on



    apply_Btn_State(Btn_State); // this mainly updates the pushbutton color buffers. it also contains the very simple on/off initialization animation for the pushbuttons
    // Also if a zone has startup animations enabled, toggle a bit here to then trigger that in the zone_Startup() function below.
    // This function sets the corresponding zone pushbutton to a SOLID color. it doesnt currently match the button color to the first LED of the current animation frame for that zone.
    //        that would have to be done outside this logic? not too sure how to do that. maybe i just later call apply_Btn_State(Btn_State_Last) to reload the buttons but change the code to load the current frames LED 1 color?

    // zone_Startup(); // prospective function, Insert logic to trigger startup animations for newly pressed button zones? and if a zone's mode is a solid color, set that here.

  }

  //Mode_Next_Frame();  // prospective function, loads the next frames for each zone.

  FastLED.show(); // finally update all with the current buffer. I removed the .show from the end of the apply_Btn_State function and moved it here at the end of the main loop instead (so its synchrynously updated with the frames)
}


/*
 EDITS:
 These button states / cases currently DIRECTLY edit the outputs, Ideally they should set bits for that zone.
    The main loop then checks what zone bits are set high, and then it looks at their set animation and last active frame, it then updates each zone to the next frame in their respective animations
    (based on the set animation it calls that animations function and passes through the last frame value if needed)
    THEN at the end of the main loop it updates/shows the LED outputs.

    Variables for animations:
      Mode
      Base Color
      Acc Color 1, 2, etc.
      target_Brtness (or "master" brightness? animation will vary it)
      trans_on
      trans_off
      Last_Frame (or Last_Frame_mS?)

    NOTE: 24fps = 42mS/f, 30fps=33mS/f, 60fps = 17mS/f
      [aka; frame_time_ms = 1000 / fps]. WS2812 takes 30uS per LED, so 4.5mS for a 150 LED strip
      I will need to record how long each main loop takes to run (t_start at start of loop and t_end at the end) only print the frame execution time periodically, not every frame.
      Its important to set the total LEDs per strip correctly for maximum headroom. overshooting the number eats up execution time.

 need to figure out how it will make the button light match that zone as well

 If i want on/off transitions, how would I do that?
    somehow set a "transition on" or "transisiton off" bit for that zone when the button is pressed / changes, and while that bit is active,
    whenever a frame is updated it will scale the output brightness (up / down, can nscale_8 scale brightness up? what happens if it exceeds 255?
    but also if the ON color is like 255, 35, 0 then it could maybe scale it to 255, 255, 0 instead???), that way it stacks. once it reaches max or min brightness it stops

 Eventually need to add bluetooth setup and loop function for configuring zone colors, brightnesses, animations, on/off, etc.

*/

void read_IO() { // Updates the xxx_Btn and Btn_State variables with debouncing
  Int_Btn_DB.update();
  Ext_Btn_DB.update();
  Misc_Btn_DB.update();

  Int_Btn = (Int_Btn_DB.read() == LOW);
  Ext_Btn = (Ext_Btn_DB.read() == LOW);
  Misc_Btn = (Misc_Btn_DB.read() == LOW);

  // --- Clear triggers ---
  Int_Trig_On = Int_Trig_Off = false;
  Ext_Trig_On = Ext_Trig_Off = false;
  Misc_Trig_On = Misc_Trig_Off = false;

  // --- Update triggers ---
  Int_Trig_On = Int_Btn && !Int_Btn_Last;
  Int_Trig_Off = !Int_Btn && Int_Btn_Last;

  Ext_Trig_On = Ext_Btn && !Ext_Btn_Last;
  Ext_Trig_Off = !Ext_Btn && Ext_Btn_Last;

  Misc_Trig_On = Misc_Btn && !Misc_Btn_Last;
  Misc_Trig_Off = !Misc_Btn && Misc_Btn_Last;

  // --- Save previous states ---
  Int_Btn_Last  = Int_Btn;
  Ext_Btn_Last  = Ext_Btn;
  Misc_Btn_Last = Misc_Btn;

  // Convert the button combo to a 3 bit binary state value; Shifts the status of each button to the corresponding bit
  Btn_State = (Int_Btn  << 2) | (Ext_Btn  << 1) | (Misc_Btn << 0);
}

void apply_Btn_State(uint8_t state_ID) { // This changes the state based on the button state when function is run.
  // Case 254 is the default initialization state

  // Set default color for power button
  // Only gets overwritten during initialization and when interior lighting is on.
  // NOTE: nscale8 scales the currently LOADED color value by the scale amount x/255. Multiple executions without resetting the color to max will STACK the scaling.
  fill_solid(Btn_RGB, Btn_RGB_Num, CRGB::Black);
  Btn_RGB[0] = Pwr_RGB_Col;
  Btn_RGB[0].nscale8(Pwr_RGB_Brt);

  switch (state_ID) {
    // NOTE: Power buttom is always on while code is running
    // This currently doesnt track WHAT buttons changed. It only updates things based on the new state.

    case 0: // 000 - All off
      // no logic. Interior buttons already filled with black at start of function
      break;

    case 1: // 001 - Misc On
      Btn_RGB[3] = Misc_RGB_Col;
      Btn_RGB[3].nscale8(Misc_RGB_Brt);
      
      break;

    case 2: // 010 - Ext On
      Btn_RGB[2] = Ext_RGB_Col;
      Btn_RGB[2].nscale8(Ext_RGB_Brt);

      break;
    
    case 3: // 011 - Ext + Misc On
      Btn_RGB[2] = Ext_RGB_Col;
      Btn_RGB[3] = Misc_RGB_Col;
      Btn_RGB[2].nscale8(Ext_RGB_Brt);_Brt
      Btn_RGB[3].nscale8(Misc_RGB_Brt);

      break;
    
    case 4: // 100 - Int On
      // This logic may change so the buttons first match the current interior (LED 1) animation frame color / brightness
      fill_solid(BTN_RGB, BTN_RGB_Num, Int_RGB_Col);
      nscale8_video(BTN_RGB, BTN_RGB_Num, Int_RGB_Brt);

      break;

    case 5: // 101 - Int + Misc On
      // This logic may change so the buttons first match the current interior (LED 1) animation frame color / brightness
      fill_solid(BTN_RGB, BTN_RGB_Num, Int_RGB_Col);
      nscale8_video(BTN_RGB, BTN_RGB_Num, Int_RGB_Brt);
      
      Btn_RGB[3] = Misc_RGB_Col;
      Btn_RGB[3].nscale8(Misc_RGB_Brt);

      break;

    case 6: // 110 - Int + Ext On
      // This logic may change so the buttons first match the current interior (LED 1) animation frame color / brightness
      fill_solid(Btn_RGB, Btn_RGB_Num, Int_RGB_Col);
      nscale8_video(Btn_RGB, Btn_RGB_Num, Int_RGB_Brt);

      Btn_RGB[2] = Ext_RGB_Col;
      Btn_RGB[2].nscale8(Ext_RGB_Brt);

      break;

    case 7: // 111 - ALL On
      // This logic may change so the buttons first match the current interior (LED 1) animation frame color / brightness
      fill_solid(Btn_RGB, Btn_RGB_Num, Int_RGB_Col);
      nscale8_video(Btn_RGB, Btn_RGB_Num, Int_RGB_Brt);

      Btn_RGB[2] = Ext_RGB_Col;
      Btn_RGB[3] = Misc_RGB_Col;
      Btn_RGB[2].nscale8(Ext_RGB_Brt);
      Btn_RGB[3].nscale8(Misc_RGB_Brt);

      break;

    case 254: // Initialization
      // Turn all button LEDs yellow for 1sec, then off all lights for 250mS.
      // After apply_Btn_State is done (post initialization), it will read the button state and run it again (since Btn_State_Last = 254, which isnt a button combo)
      fill_solid(Btn_RGB, Btn_RGB_Num, CRGB::Yellow);
      nscale8_video(Btn_RGB, Btn_RGB_Num, 255);

      FastLED.show();
      delay(1000);
      FastLED.clear(true);
      delay(250);
      break;

    // default:
    //  break;
  }

  Btn_State_Last = Btn_State;
}

void print_BT(const char *msg) { // If BT is enabled, print to BT output
  if (!BT_En) return;

  BT.println(msg);
}

void print_Debug(const char *msg) { // If Debugging is enabled, print to serial output. Also prints to BT output if enabled.
  if (!Debug_En) return;

  Serial.println(msg);
  if (BT_En) { BT.println(msg); }
}

void read_BT() { // Read BT commands: <ZONE> <PARAM> <VAR1> <VAR2> <VAR3>
  if (!BT_En) return;

  static char cmdBuf[32];
  static uint8_t idx = 0;

  while (BT.available()) {
    char c = BT.read();

    if (c == '\n' || c == '\r') {
      if (idx > 0) {
        cmdBuf[idx] = '\0';          // make it a real C-string
        process_CMD(cmdBuf);     // hand it off
        idx = 0;                     // reset for next command
      }
    } else if (idx < sizeof(cmdBuf) - 1) {
      cmdBuf[idx++] = c;             // store character
    }
  }
}

// I JUST COPIED THIS NEED TO EDIT IT / SET MY COMMANDS
void process_CMD(char *cmd) {
  char *zone  = strtok(cmd, " ");
  char *param = strtok(nullptr, " ");
  char *val   = strtok(nullptr, " ");

  if (!zone || !param) { btSend("ERR"); return; }

  if (strcmp(zone, "INT") == 0 && strcmp(param, "BRIGHTNESS") == 0) {
    uint8_t b;
    if (!parseByte(val, b)) { btSend("ERR BAD VALUE"); return; }

    Int_RGB_Brt = b;
    btSend("OK");
    return;
  }

  btSend("ERR UNKNOWN");
}

void handler_Int(char *cmd) {
  if ((Int_Trig_On = true) && (Int_Startup_En == true)) {
    Int_Startup_Run = true;
  } else {
    Int_Startup_Run = false;
  }

  if ((Int_Shutoff_En == true) && (Int_Trig_Off = true)) {
    Int_Startup_Run = false;
    Int_Shutoff_Run = true;
  } else {
    Int_Shutoff_Run = false;
  }

  if (cmd != ""){
    process_CMD();
  }

}