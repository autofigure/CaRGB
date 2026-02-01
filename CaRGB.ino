// For Arduino Pro Mini
// Andrew Lehman
// 2026/1/14 - Created new CaRGB code from scratch (using FastLED library instead this time)

#include <FastLED.h>

// NOTE: The PUSHBUTTON lights are powered and controlled SEPERATELY from the light strips.
// The button RGB lights are in series (array of 4 lights). They are addressed individually with the Btn_RGB array.
// Power button simply switches the 12V power on / off to the arduino. If arduino is on, power button is pressed.

bool debug_En = false; // When true, it will print to command line for debugging

// ------------------------------- BUTTON + PWR zone variables -------------------------------
#define Btn_RGB_Pin     D6
#define Btn_RGB_Num     4
#define Btn_RGB_Type    WS2812
#define Btn_RGB_Order   GRB

CRGB Pwr_RGB_Col  = CRGB::Green;      // Sets the default solid power button color
uint8_t Pwr_RGB_Brt  60               // 0-255, the default power button brightness

CRGB Btn_RGB[Btn_RGB_Num];            // Create the LED array


// ------------------------------- INTERIOR zone variables -------------------------------
#define Int_Btn_Pin     A0
#define Int_Pwr_On_Pin  D13

#define Int_RGB_Pin     D5
#define Int_RGB_Num     150
#define Int_RGB_Type    WS2812
#define Int_RGB_Order   GRB

bool Int_Btn;                         // use this to track button state and enable strip power + Mode
bool Int_Startup_En = 0;              // Enables startup animation
bool Int_Shutoff_En = 0;              // Enables shutoff animation
char Int_RGB_Startup[10] = "FADE IN"; // Startup animation
char Int_RGB_Shutoff[10] = "FADE OUT"; // shutoff animation

char Int_RGB_Mode[10] = "SOLID";      // Mode = Animation type
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

bool Ext_Btn;                         // use this to track button state and enable strip power + Mode
bool Ext_Startup_En = 0;              // Enables startup animation
bool Ext_Shutoff_En = 0;              // Enables shutoff animation
char Ext_RGB_Startup[10] = "FADE IN"; // Startup animation
char Ext_RGB_Shutoff[10] = "FADE OUT"; // shutoff animation

char Ext_RGB_Mode[10] = "SOLID";      // Mode = Animation type
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

bool Misc_Btn;                        // use this to track button state and enable strip power + Mode
bool Misc_Startup_En = 0;             // Enables startup animation
bool Misc_Shutoff_En = 0;             // Enables shutoff animation
char Misc_RGB_Startup[10] = "FADE IN"; // Startup animation
char Misc_RGB_Shutoff[10] = "FADE OUT"; // shutoff animation

char Misc_RGB_Mode[10] = "SOLID";     // Mode = Animation type
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
  pinMode(Ext_Btn_Pin, INPUT_PULLUP);
  pinMode(Misc_Btn_Pin, INPUT_PULLUP);

  pinMode(Int_Pwr_On_Pin, OUTPUT);
  pinMode(Ext_Pwr_On_Pin, OUTPUT);
  pinMode(Misc_Pwr_On_Pin, OUTPUT);


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
}

void loop() {
  // need to track frame start time and then end time

  // change this to a dedicated function with debounce handling.
  // add function to check for bluetooth commands
  Int_Btn  = (digitalRead(Int_Btn_Pin) == LOW);
  Ext_Btn  = (digitalRead(Ext_Btn_Pin) == LOW);
  Misc_Btn  = (digitalRead(Misc_Btn_Pin) == LOW);
  
  
  // Convert the button combo to a 3 bit binary state value; Shifts the status of each button to the corresponding bit
  Btn_State = (Int_Btn  << 2) | (Ext_Btn  << 1) | (Misc_Btn << 0);

  // If the button state has changed since last loop run, change the state and update I/O
  if (Btn_State_Last != Btn_State) {
    // Toggle Power Outputs
    digitalWrite(Int_Pwr_On_Pin, Int_Btn);
    digitalWrite(Ext_Pwr_On_Pin, Ext_Btn);
    digitalWrite(Misc_Pwr_On_Pin, Misc_Btn);
    delay(10); // Delay for relays / power

    apply_Btn_State(Btn_State); // this mainly updates the pushbutton color buffers. it also contains the very simple on/off initialization animation for the pushbuttons
    // Also if a zone has startup animations enabled, toggle a bit here to then trigger that in the zone_Startup() function below.
    // This function sets the corresponding zone pushbutton to a SOLID color. it doesnt currently match the button color to the first LED of the current animation frame for that zone.
    //        that would have to be done outside this logic? not too sure how to do that. maybe i just later call apply_Btn_State(Btn_State_Last) to reload the buttons but change the code to load the current frames LED 1 color?

    // zone_Startup(); // prospective function, Insert logic to trigger startup animations for newly pressed button zones? and if a zone's mode is a solid color, set that here.

  }

  // Mode_Change(); // prospective function, requires bluetooth or command line commands to change modes and colors during operation. This would also need to trigger apply_Btn_State(Btn_State_Last) to update the pusbutton colors?

  // Mode_Next_Frame();  // prospective function, loads the next frames for each zone.

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


// This changes the state based on the button state when function is run
//Case 254 is the default initialization state
void apply_Btn_State(uint8_t state_ID) {
  // Set default color for power button
  // Only gets overwritten during initialization and when interior lighting is on.
  // NOTE: nscale8 scales the currently LOADED color value by the scale amount x/255. Multiple executions without resetting the color to max will STACK the scaling.
  fill_solid(Btn_RGB, Btn_RGB_Num, CRGB::Black);
  Btn_RGB[0] = Pwr_RGB_Col;
  Btn_RGB[0].nscale8(Pwr_RGB_Brt);

  switch (state_ID) {
    // NOTE: Power buttom is always on while code is running
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