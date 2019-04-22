#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <FastLED.h>

FASTLED_USING_NAMESPACE

#define DATA_PIN    5
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS    400

#define BRIGHTNESS          96
#define FRAMES_PER_SECOND  120

CRGB leds[NUM_LEDS];

// Update these with values suitable for the hardware/network.
// byte mac[] = { 0xB3, 0x5C, 0xED, 0xF8, 0x15, 0xD6 };
byte mac[] = { 0x4D, 0x26, 0xB5, 0x45, 0xF6, 0xD8 };
IPAddress ip(192, 168, 2, 106);
// IPAddress myDns(192, 168, 0, 1);

void stateMachine (int state);

// The Client ID for connecting to the MQTT Broker
const char* CLIENT_ID = "fanInsertsClient";

// The Topic for mqtt messaging
const char* TOPIC = "fanInserts/set";

// States
const int IDLE_STATE = 0;
const int PRESENT_STATE = 1;
const int ACTIVE_STATE = 2;
const int COMPLETE_STATE = 3;

// Initialize the current state ON or OFF
int currentState = IDLE_STATE;
int tempState;

// Initialize the ethernet library
EthernetClient net;
// Initialize the MQTT library
PubSubClient mqttClient(net);

const char* mqttServer = "192.168.2.101";

// Station states, used as MQTT Messages
const char states[4][10] = {"IDLE", "PRESENT", "ACTIVE", "COMPLETE"};

// Reconnect to the MQTT broker when the connection is lost
void reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect with the client ID
    if (mqttClient.connect("chromaFaceClient")) {
        Serial.println("Connected!");
        // Once connected, publish an announcement...
        mqttClient.publish("chromaFace", "CONNECTED");

        // Subscribe to topic
        mqttClient.subscribe("chromaFace");
        mqttClient.subscribe("chromaFaceStatus");
        mqttClient.subscribe("chromaFaceEffects");

    } else {
        Serial.print("failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" try again in 5 seconds");
        // Wait 5 seconds before retrying
        delay(5000);
    }
  }
}

boolean reconnect_non_blocking() {
  if (mqttClient.connect("chromaFaceClient")) {
      Serial.println("Connected!");
      // Once connected, publish an announcement...
      mqttClient.publish("chromaFace", "CONNECTED");

      // Subscribe to topic
      mqttClient.subscribe("chromaFace");
      mqttClient.subscribe("chromaFaceStatus");
      mqttClient.subscribe("chromaFaceEffects");

  } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
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
  if (strcmp(topic, "fanInsertsEffects") == 0) {
    Serial.println("nextPattern");
    nextPattern();
  }
  else {  
    if (strcmp(payloadArr, states[0]) == 0) tempState = IDLE_STATE;
    if (strcmp(payloadArr, states[1]) == 0) tempState = PRESENT_STATE;
    if (strcmp(payloadArr, states[2]) == 0) tempState = ACTIVE_STATE;
    if (strcmp(payloadArr, states[3]) == 0) tempState = COMPLETE_STATE;


    Serial.println(tempState);
    stateMachine(tempState);
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
  
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS);

  // Set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
  lastReconnectAttempt = 0;
}

void loop() {
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
    mqttClient.publish("chromaFaceStatus", states[currentState]);
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
  Serial.println("Set ACTIVE");
#endif
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
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
