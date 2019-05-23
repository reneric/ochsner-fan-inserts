#define FASTLED_ALLOW_INTERRUPTS 0
#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <FastLED.h>

FASTLED_USING_NAMESPACE

// MUST SET CPU SPEED @ 24MHz TO AVOID FLICKER [AND SO AS NOT TO OVERCLOCK]

// #define TEST_PIN    7
#define DEBUG 1
// #define LOCAL true

#define DATA_PIN    5
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS    400

#define BRIGHTNESS         150
#define FRAMES_PER_SECOND  96

CRGB leds[NUM_LEDS];

// Update these with values suitable for the hardware/network.
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEA };
IPAddress ip(192, 168, 2, 106);


// byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
// IPAddress ip(192, 168, 1, 82);

void stateMachine (int state);

// The Client ID for connecting to the MQTT Broker
const char* CLIENT_ID = "LED_T";

// The Topic for mqtt messaging
const char* EFFECTS_TOPIC = "command/LED_T/effects";
const char* STATE_TOPIC = "command/LED_T/state";
const char* TOGGLE_TOPIC = "command/LED_T";

// States
const int IDLE_STATE = 0;
const int PRESENT_STATE = 1;
const int ACTIVE_STATE = 2;
const int COMPLETE_STATE = 3;

int LED_state = 1;

// Initialize the current state of LIGHT EFFECT
int currentState = IDLE_STATE;
int tempState;

// Initialize the ethernet library
EthernetClient net;
// Initialize the MQTT library
PubSubClient mqttClient(net);

#ifdef LOCAL
const char* mqttServer = "192.168.1.97";
#else
const char* mqttServer = "192.168.2.10";
#endif
const int mqttPort = 1883;


// Station states, used as MQTT Messages
const char states[4][30] = {"IDLE", "PRESENT", "ACTIVE", "COMPLETE"};

/*********** RECONNECT WITHOUT BLOCKING LOOP ************/
long lastReconnectAttempt = 0;
boolean reconnect_non_blocking() {
  if (mqttClient.connect(CLIENT_ID)) {
       Serial.print("Connected!");
      // Once connected, publish an announcement...
      mqttClient.publish(CLIENT_ID, "CONNECTED", true);

      // Subscribe to topic
      mqttClient.subscribe(STATE_TOPIC);
      mqttClient.subscribe(EFFECTS_TOPIC);
      mqttClient.subscribe(TOGGLE_TOPIC);

  } else {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
  }
  return mqttClient.connected();
}

/****************** LIST OF PATTERNS TO CYCLE THRU **************/
typedef void (*EffectsList[])();
EffectsList effects = { blue_pulse, blue_pulse_split, blue_pulse_white, blue_pulse_split_white, rainbow, confetti, sinelon, juggle, bpm, ochsnerSparkle};

uint8_t currentEffect = 0; // Index number of which pattern is current
uint8_t gHue = 0; // rotating "base color" used by many of the patterns


/**** CALLBACK RUN WHEN AN MQTT MESSAGE IS RECEIVED *****/
void messageReceived(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);  
  Serial.println("] ");
  char payloadArr[length+1];
  
  for (unsigned int i=0;i<length;i++)
  {
    payloadArr[i] = (char)payload[i];
  }
  payloadArr[length] = 0;

  Serial.println(payloadArr);  // null terminated array
  if (strcmp(topic, EFFECTS_TOPIC) == 0) {
    Serial.println("nextPattern");
    nextPattern();
  }
  else if (strcmp(topic, STATE_TOPIC) == 0) {
    if (strcmp(payloadArr, states[0]) == 0) tempState = IDLE_STATE;
    if (strcmp(payloadArr, states[1]) == 0) tempState = PRESENT_STATE;
    if (strcmp(payloadArr, states[2]) == 0) tempState = ACTIVE_STATE;
    if (strcmp(payloadArr, states[3]) == 0) tempState = COMPLETE_STATE;

    Serial.println(tempState);
    stateMachine(tempState);
  }
  else if (strcmp(topic, TOGGLE_TOPIC) == 0) {
    if (strcmp(payloadArr, "stop") == 0) LED_state = 0;
    if (strcmp(payloadArr, "start") == 0) LED_state = 1;
    Serial.println(LED_state);
  }
}

void setup() {
  // Initialize serial communication:
  Serial.begin(9600);
  
  Ethernet.init(10);
  
  // Initialize the ethernet connection
  Ethernet.begin(mac, ip);
//   Ethernet.begin(mac, ip, myDns); // Connect with DNS 

 

  mqttClient.setServer(mqttServer, 1883);
  mqttClient.setCallback(messageReceived);
  
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // pinMode(TEST_PIN, INPUT);

  FastLED.setDither(0);
  // Set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
  lastReconnectAttempt = 0;
}

int testModeTimer = 0;

void loop() {
/******************  
  boolean testMode = digitalRead(TEST_PIN) == HIGH;
  if (testMode) {
    if (millis() - testModeTimer > 8000) {
      testModeTimer = millis();
      currentState = currentState + 1;
      if (currentState > 3) currentState = 0;
    }
  }
  else ****/ 
  { 
    if (!mqttClient.connected()) {
      long now = millis();
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        // Attempt to reconnect
        if (reconnect_non_blocking()) {
          lastReconnectAttempt = 0;
        }
      }
    } else {
      mqttClient.loop();
    }
  }
 
  if (LED_state == 0) {
      Serial.println("OFF");
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.clear();
  }
  else {
    
    Serial.print("Current State: ");
    Serial.println(currentState);

    switch (currentState) {
      case PRESENT_STATE:
        setPresent();
        break;
      case ACTIVE_STATE:
        setActive();
        break;
      case COMPLETE_STATE:
        setComplete();
        break;
      default:
        setIdle();
        break;
    }
  }
  FastLED.show();
  FastLED.delay(1000/FRAMES_PER_SECOND);
}

void stateMachine (int state) {
  // Get the current sensor state
  
  /*
   * Only send the state update on the first loop.
   *
   * IF the current (temporary) PS sensor state is not equal to the actual current broadcasted state,
   * THEN we can safely change the actual state and broadcast it.
   *
   */
  if (state != currentState) {
#if DEBUG == 1    
    Serial.print("State Changed: ");
    Serial.println();
    Serial.print("Last State: ");
    Serial.println(currentState);
    Serial.print("New State: ");
    Serial.println(state);
#endif
    currentState = state;
    mqttClient.publish("FanArrayStatus", states[currentState], true);
  }
}

/********** SET GIVEN STATION STATE TO IDLE **********/
void setIdle () {
#if DEBUG == 1
  Serial.println("Set IDLE");
#endif
  currentEffect = 0;  // blue pulse
  effects[currentEffect]();
}

/********* SET GIVEN STATION STATE TO PRESENT *********/
void setPresent () {
#if DEBUG == 1
  Serial.println("Set PRESENT");
#endif
  fill_solid(leds, NUM_LEDS, CRGB::Yellow);
}

/********** SET GIVEN STATION STATE TO ACTIVE **********/
void setActive () {
#if DEBUG == 1  
  Serial.println("ACTIVE");
#endif
  currentEffect = 9;  // Ochsner Sparkle (custom)
  effects[currentEffect]();
}

/********** SET GIVEN STATION STATE TO COMPLETE **********/
void setComplete () {
#if DEBUG == 1
  Serial.println("Set COMPLETE");
#endif
  fill_solid(leds, NUM_LEDS, CRGB::Green);
}


#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

void nextPattern()
{
  // add one to the current pattern number, and wrap around at the end
  currentEffect = (currentEffect + 1) % ARRAY_SIZE(effects);
  Serial.print("CURRENT EFFECT: ");
  Serial.println(currentEffect);
}

void testCycle () {

}


/*********** LED EFFECTS FUNCTIONS **********/

void rainbow() 
{
  // FastLED's built-in rainbow generator
  fill_rainbow( leds, NUM_LEDS, gHue, 7);
}

int speed = 3;
void blue_pulse() 
{
  static uint8_t startIndex = 0;
  startIndex = startIndex + 4; /* motion speed */

  FillLEDsFromPaletteColors(startIndex, CRGB::Blue, CRGB::DarkBlue);
}

void blue_pulse_split() 
{
  static uint8_t startIndex = 0;
  startIndex = startIndex + 4; /* motion speed */
  
  FillLEDsFromPaletteColorsSplit(startIndex, CRGB::Blue, CRGB::DarkBlue);
}

void blue_pulse_white() 
{
  static uint8_t startIndex = 0;
  startIndex = startIndex + 4; /* motion speed */
  
  FillLEDsFromPaletteColors(startIndex, CRGB::RoyalBlue, CRGB::LightSkyBlue);
}

void blue_pulse_split_white() 
{
  static uint8_t startIndex = 0;
  startIndex = startIndex + 6; /* motion speed */
  
  FillLEDsFromPaletteColorsSplit(startIndex, CRGB::RoyalBlue, CRGB::LightSkyBlue);
}

void rainbowWithGlitter() 
{
  // built-in FastLED rainbow, plus some random sparkly glitter
  rainbow();
  addGlitter(80);
}

void addGlitter( fract8 chanceOfGlitter) 
{
  if( random8() < chanceOfGlitter) {
    leds[ random16(NUM_LEDS) ] += CRGB::White;
  }
}


void addGlitterBlue( fract8 chanceOfGlitter) 
{
  if( random8() < chanceOfGlitter) {
    leds[ random16(NUM_LEDS) ] += CRGB::Blue;
  }
}

void addGlitterYellow( fract8 chanceOfGlitter) 
{
  if( random8() < chanceOfGlitter) {
    leds[ random16(NUM_LEDS) ] += CRGB::Yellow;
  }
}


void confetti() 
{
  // random colored speckles that blink in and fade smoothly
  fadeToBlackBy( leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  int pos2 = random16(NUM_LEDS);
//  leds[pos] += CHSV( gHue + random8(64), 200, 255);
  leds[pos] += CHSV( HUE_BLUE, 170 + random8(64), 255);
  leds[pos2] += CHSV( HUE_YELLOW, 170 + random8(64), 255);
}

void sinelon()
{
  // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy( leds, NUM_LEDS, 20);
  int pos = beatsin16( 13, 0, NUM_LEDS-1 );
  leds[pos] += CHSV( gHue, 255, 192);
}

void bpm()
{
  // colored stripes pulsing at a defined Beats-Per-Minute (BPM)
  uint8_t BeatsPerMinute = 62;
  CRGBPalette16 palette = PartyColors_p;
  uint8_t beat = beatsin8( BeatsPerMinute, 64, 255);
  for( int i = 0; i < NUM_LEDS; i++) { //9948
    leds[i] = ColorFromPalette(palette, gHue+(i*2), beat-gHue+(i*10));
  }
}

void juggle() {
  // eight colored dots, weaving in and out of sync with each other
  fadeToBlackBy( leds, NUM_LEDS, 20);
  byte dothue = 0;
  for( int i = 0; i < 8; i++) {
    leds[beatsin16( i+7, 0, NUM_LEDS-1 )] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}

void ochsnerSparkle() {
  // built specifically for Active state of the AR experience 
  // confetti x3 x5
  fadeToBlackBy( leds, NUM_LEDS, 35);
  int pos = random16(NUM_LEDS);
  int pos2 = random16(NUM_LEDS);
  int pos3 = random16(NUM_LEDS);
  int pos4 = random16(NUM_LEDS);
  int pos5 = random16(NUM_LEDS);
  int pos6 = random16(NUM_LEDS);
  int pos7 = random16(NUM_LEDS);
  int pos8 = random16(NUM_LEDS);
  int pos9 = random16(NUM_LEDS);
  int pos10 = random16(NUM_LEDS);
  int pos11 = random16(NUM_LEDS);
  int pos12 = random16(NUM_LEDS);
  int pos13 = random16(NUM_LEDS);
  int pos14 = random16(NUM_LEDS);
  int pos15 = random16(NUM_LEDS);
  leds[pos] += CHSV( HUE_BLUE, 170 + random8(64), 255);
  leds[pos2] += CHSV( HUE_YELLOW, 170 + random8(64), 255);
  leds[pos3] += CHSV( HUE_YELLOW, 1, 255);
  leds[pos4] += CHSV( HUE_BLUE, 170 + random8(64), 255);
  leds[pos5] += CHSV( HUE_YELLOW, 170 + random8(64), 255);
  leds[pos6] += CHSV( HUE_YELLOW, 1, 255);
  leds[pos7] += CHSV( HUE_BLUE, 170 + random8(64), 255);
  leds[pos8] += CHSV( HUE_YELLOW, 170 + random8(64), 255);
  leds[pos9] += CHSV( HUE_YELLOW, 1, 255);
  leds[pos10] += CHSV( HUE_BLUE, 170 + random8(64), 255);
  leds[pos11] += CHSV( HUE_YELLOW, 170 + random8(64), 255);
  leds[pos12] += CHSV( HUE_YELLOW, 1, 255);
  leds[pos13] += CHSV( HUE_BLUE, 170 + random8(64), 255);
  leds[pos14] += CHSV( HUE_YELLOW, 170 + random8(64), 255);
  leds[pos15] += CHSV( HUE_YELLOW, 1, 255);  

}

long start = random(NUM_LEDS);
void FillLEDsFromPaletteColors( uint8_t colorIndex, CRGB blue, CRGB darkBlue)
{
    for( int i = 0; i < NUM_LEDS; i++) {
        leds[i] = ColorFromPalette(SetupActivePalette(blue, darkBlue), colorIndex, BRIGHTNESS, LINEARBLEND);
        colorIndex += speed;
    }
}

void FillLEDsFromPaletteColorsSplit( uint8_t colorIndex, CRGB blue, CRGB darkBlue)
{
    for( int i = 0; i < 52; i++) {
        leds[i] = ColorFromPalette(SetupActivePalette(blue, darkBlue), colorIndex, BRIGHTNESS, LINEARBLEND);
        colorIndex += speed;
    }
    for(int i = 0; i < 53; i++) {
        leds[i+51] = ColorFromPalette(SetupActivePalette(blue, darkBlue), colorIndex, BRIGHTNESS, LINEARBLEND);
        colorIndex -= speed;
    }
}

void FillLEDsFromPaletteColorsWhite( uint8_t colorIndex, CRGB blue, CRGB darkBlue)
{
    for( int i = 0; i < NUM_LEDS; i++) {
        leds[i] = ColorFromPalette(SetupActivePalette(blue, darkBlue), colorIndex, BRIGHTNESS, LINEARBLEND);
        colorIndex += speed;
    }
}

void FillLEDsFromPaletteColorsSplitWhite( uint8_t colorIndex, CRGB blue, CRGB darkBlue)
{
    for( int i = 0; i < 52; i++) {
        leds[i] = ColorFromPalette(SetupActivePalette(blue, darkBlue), colorIndex, BRIGHTNESS, LINEARBLEND);
        colorIndex += speed;
    }
    for(int i = 0; i < 53; i++) {
        leds[i+51] = ColorFromPalette(SetupActivePalette(blue, darkBlue), colorIndex, BRIGHTNESS, LINEARBLEND);
        colorIndex -= speed;
    }
}

CRGBPalette16 SetupActivePalette(CRGB blue, CRGB darkBlue)
{
  CRGB black  = darkBlue;
    
    return CRGBPalette16(
                                   blue,  blue,  black,  black,
                                   blue,  blue,  blue,  blue,
                                   blue,  blue,  black,  black,
                                   darkBlue, darkBlue, black, black);
}

/****************** UNUSED PALETTES *********************
CRGBPalette16 SetupActivePalette()
{
  
    CRGB blue = CHSV( HUE_BLUE, 255, 255);
    CRGB darkBlue  = CRGB::DarkBlue;
    CRGB black  = CRGB::DarkBlue;
 
    return CRGBPalette16(
                                   blue,  blue,  black,  black,
                                   darkBlue, darkBlue, black,  black,
                                   blue,  blue,  black,  black,
                                   darkBlue, darkBlue, black,  black );
}


CRGBPalette16 SetupActivePalette(CRGB blue, CRGB darkBlue)
{
   CRGB black  = darkBlue;
 
   return CRGBPalette16(
                                   blue,  blue,  black,  black,
                                   black,  blue,  blue,  blue,
                                   blue,  blue,  black,  black,
                                   darkBlue, darkBlue, black, black);
}
******/
