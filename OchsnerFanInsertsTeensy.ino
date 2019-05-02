#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <FastLED.h>

FASTLED_USING_NAMESPACE

#define TEST_PIN    7

#define DEBUG 1

#define DATA_PIN    5
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS    400

#define BRIGHTNESS          96
#define FRAMES_PER_SECOND  120

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

int LED_state = 0;

// Initialize the current state ON or OFF
int currentState = IDLE_STATE;
int tempState;

// Initialize the ethernet library
EthernetClient net;
// Initialize the MQTT library
PubSubClient mqttClient(net);

// const char* mqttServer = "192.168.1.97";
const int mqttPort = 1883;
const char* mqttServer = "192.168.2.10";

// Station states, used as MQTT Messages
const char states[4][30] = {"IDLE", "PRESENT", "ACTIVE", "COMPLETE"};

boolean reconnect_non_blocking() {
  if (mqttClient.connect(CLIENT_ID)) {
      // Serial.println("Connected!");
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

// List of patterns to cycle through.
typedef void (*EffectsList[])();
EffectsList effects = { rainbow, rainbowWithGlitter, confetti, sinelon, juggle, bpm };

uint8_t currentEffect = 0; // Index number of which pattern is current
uint8_t gHue = 0; // rotating "base color" used by many of the patterns

long lastReconnectAttempt = 0;

void nextPattern();

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

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(messageReceived);
  
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS);

  pinMode(TEST_PIN, INPUT);
  // Set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
  lastReconnectAttempt = 0;
  LED_state = 0;
}

int testModeTimer = 0;
void loop() {
  boolean testMode = digitalRead(TEST_PIN) == HIGH;
  if (testMode) {
    if (millis() - testModeTimer > 8000) {
      testModeTimer = millis();
      currentState = currentState + 1;
      if (currentState > 3) currentState = 0;
    }
  }
  else {
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
  Serial.println(LED_state);
  if (LED_state == 0) {
      // fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.clear();
  }
  else {
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
    mqttClient.publish("chromaFaceStatus", states[currentState], true);
  }
}

/********** SET GIVEN STATION STATE TO IDLE **********/
void setIdle () {
#if DEBUG == 1
  Serial.println("Set IDLE");
#endif
  fill_solid(leds, NUM_LEDS, CRGB::Black);
}

/********* SET GIVEN STATION STATE TO PRESENT *********/
void setPresent () {
#if DEBUG == 1
  Serial.println("Set PRESENT");
#endif
  fill_solid(leds, NUM_LEDS, CRGB::White);
}

/********** SET GIVEN STATION STATE TO ACTIVE **********/
void setActive () {
#if DEBUG == 1  
  Serial.println("ACTIVE");
#endif
  static uint8_t startIndex = 0;
  startIndex = startIndex + 2; /* motion speed */
    
  FillLEDsFromPaletteColors(startIndex);
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
}

void testCycle () {

}


/*
 * 
 * LED EFFECTS FUNCTIONS
 *
 *
 */
void rainbow() 
{
  // FastLED's built-in rainbow generator
  fill_rainbow( leds, NUM_LEDS, gHue, 7);
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

void confetti() 
{
  // random colored speckles that blink in and fade smoothly
  fadeToBlackBy( leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV( gHue + random8(64), 200, 255);
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


void FillLEDsFromPaletteColors( uint8_t colorIndex)
{
    for( int i = 0; i < NUM_LEDS; i++) {
        leds[i] = ColorFromPalette( SetupActivePalette(), colorIndex, BRIGHTNESS, LINEARBLEND);
        colorIndex += 1;
    }
}

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
