#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Pins ---
const int ledPins[4] = {2,3,4,5};
const int buttonPins[4] = {12,13,14,15};

WiFiClient espClient;
PubSubClient mqtt(espClient);

String deviceId;
String pairedUsername = "";

const char* WIFI_SSID = "SIMON_123";
const char* WIFI_PASSWORD = "123";

const char* MQTT_SERVER = "192.168.1.100"; 
const uint16_t MQTT_PORT = 1883;

// --- Game ---
const int MAX_SEQUENCE = 32;
int sequence[MAX_SEQUENCE];
int currentRound = 1;
int inputIndex = 0;
enum GameState { SHOW_SEQUENCE, WAIT_INPUT, GAME_OVER };
GameState gameState = SHOW_SEQUENCE;

volatile bool buttonFlags[4] = {false,false,false,false};
bool buttonLocked[4] = {false,false,false,false};
unsigned long lastPressTime[4] = {0,0,0,0};
const unsigned long debounceDelay = 200;

// --- ISR ---
void IRAM_ATTR isrButton0() { buttonFlags[0]=true; }
void IRAM_ATTR isrButton1() { buttonFlags[1]=true; }
void IRAM_ATTR isrButton2() { buttonFlags[2]=true; }
void IRAM_ATTR isrButton3() { buttonFlags[3]=true; }

// --- MQTT callback ---
void callback(char* topic, byte* payload, unsigned int length){
  String sTopic = String(topic);
  String payloadStr;
  for(unsigned int i=0;i<length;i++) payloadStr += (char)payload[i];

  StaticJsonDocument<256> doc;
  if(deserializeJson(doc,payloadStr)) return;

  if(sTopic == "simon/pair"){
    const char* ssid = doc["ssid"] | "";
    const char* pwd  = doc["password"] | "";
    const char* user = doc["username"] | "";

    // On tente de connecter l'ESP32 au SSID indiqué
    WiFi.begin(ssid,pwd);
    unsigned long start = millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-start<10000){
      delay(500);
    }
    StaticJsonDocument<128> ack;
    ack["ssid"] = String(ssid);
    ack["username"] = String(user);
    ack["status"] = WiFi.status()==WL_CONNECTED ? "paired" : "failed";
    char buf[128];
    size_t n = serializeJson(ack,buf);
    mqtt.publish("simon/pair/ack",buf,n);
    if(WiFi.status()==WL_CONNECTED) pairedUsername = String(user);
  }
}

// --- MQTT connect ---
void ensureMqtt(){
  while(!mqtt.connected()){
    String clientId = "ESP32-" + deviceId;
    if(mqtt.connect(clientId.c_str())){
      mqtt.subscribe("simon/pair");
    } else { delay(2000); }
  }
}

void publishScore(int score){
  if(!mqtt.connected()) ensureMqtt();
  if(pairedUsername=="") return;
  StaticJsonDocument<256> doc;
  doc["ssid"] = WiFi.SSID();
  doc["username"] = pairedUsername;
  doc["score"] = score;
  char buf[256];
  size_t n = serializeJson(doc,buf);
  mqtt.publish("simon/scores",buf,n);
}

// --- Game functions ---
void lightLed(int idx,int onTime=400,int offTime=100){
  digitalWrite(ledPins[idx],HIGH);
  delay(onTime);
  digitalWrite(ledPins[idx],LOW);
  delay(offTime);
}

void startNewGame(){
  randomSeed(micros());
  sequence[0]=random(0,4);
  currentRound=1; inputIndex=0; gameState=SHOW_SEQUENCE;
  for(int i=0;i<4;i++){ buttonFlags[i]=false; buttonLocked[i]=false; lastPressTime[i]=millis();}
}

void showSequence(){
  delay(400);
  for(int i=0;i<currentRound;i++) lightLed(sequence[i]);
  inputIndex=0; gameState=WAIT_INPUT;
}

void handleUserInput(int idx){
  if(idx!=sequence[inputIndex]){
    gameState=GAME_OVER; publishScore(currentRound-1); return;
  }
  lightLed(idx,200,50); inputIndex++;
  if(inputIndex>=currentRound){
    if(currentRound<MAX_SEQUENCE){
      sequence[currentRound]=random(0,4); currentRound++; gameState=SHOW_SEQUENCE;
    } else { publishScore(currentRound); startNewGame();}
  }
}

void gameOverAnimation(){
  for(int i=0;i<3;i++){
    for(int j=0;j<4;j++) digitalWrite(ledPins[j],HIGH);
    delay(150);
    for(int j=0;j<4;j++) digitalWrite(ledPins[j],LOW);
    delay(150);
  }
}

void setup(){
  Serial.begin(115200);
  for(int i=0;i<4;i++){ pinMode(ledPins[i],OUTPUT); digitalWrite(ledPins[i],LOW);}
  for(int i=0;i<4;i++){ pinMode(buttonPins[i],INPUT_PULLUP);}
  attachInterrupt(buttonPins[0],isrButton0,FALLING);
  attachInterrupt(buttonPins[1],isrButton1,FALLING);
  attachInterrupt(buttonPins[2],isrButton2,FALLING);
  attachInterrupt(buttonPins[3],isrButton3,FALLING);

  deviceId = WiFi.macAddress(); deviceId.replace(":","");

  // Connexion Wi-Fi automatique
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ");
  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected, IP: "+WiFi.localIP().toString());

  mqtt.setServer(MQTT_SERVER,MQTT_PORT);
  mqtt.setCallback(callback);
  ensureMqtt();

  startNewGame();
}

void loop(){
  if(!mqtt.connected()) ensureMqtt();
  mqtt.loop();

  for(int i=0;i<4;i++){
    if(buttonLocked[i] && digitalRead(buttonPins[i])==HIGH) buttonLocked[i]=false;
    if(buttonFlags[i]){
      buttonFlags[i]=false;
      unsigned long now=millis();
      if(buttonLocked[i]) continue;
      if(digitalRead(buttonPins[i])==LOW && now-lastPressTime[i]>debounceDelay){
        lastPressTime[i]=now;
        buttonLocked[i]=true;
        if(gameState==WAIT_INPUT) handleUserInput(i);
        else if(gameState==GAME_OVER) startNewGame();
      }
    }
  }
  switch(gameState){
    case SHOW_SEQUENCE: showSequence(); break;
    case WAIT_INPUT: break;
    case GAME_OVER: gameOverAnimation(); gameState=GAME_OVER; break;
  }
}
