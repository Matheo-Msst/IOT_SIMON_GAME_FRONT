#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Pins ---
const int ledPins[4] = {2, 4, 5, 6};     // LED0, LED1, LED2, LED3
const int buttonPins[4] = {7, 8, 9, 10}; // bouton0..3
const int buzzerPin = 3;                  // PWM pin pour buzzer
const int buzzerChannel = 0; // PWM channel

WiFiClient espClient;
PubSubClient mqtt(espClient);

String deviceId;
String pairedUsername = "";

// --- Config réseau ---
const char* DEFAULT_WIFI_SSID = "Teddy";
const char* DEFAULT_WIFI_PASSWORD = ""; // Wi-Fi ouvert possible

const char* MQTT_SERVER = "10.95.140.175";
const uint16_t MQTT_PORT = 1883;

// --- Game ---
const int MAX_SEQUENCE = 32;
int sequence[MAX_SEQUENCE];
int currentRound = 0;
int inputIndex = 0;
enum GameState { WAIT_START, SHOW_SEQUENCE, WAIT_INPUT, GAME_OVER, WAIT_PAIRING };
GameState gameState = WAIT_START;

volatile bool buttonFlags[4] = {false,false,false,false};
bool buttonLocked[4] = {false,false,false,false};
unsigned long lastPressTime[4] = {0,0,0,0};
const unsigned long debounceDelay = 200;

// --- Wi-Fi pairing ---
String pairingSSID = "";
String pairingPassword = "";
bool pairingInProgress = false;
unsigned long pairingStartTime = 0;
const unsigned long PAIRING_TIMEOUT = 10000; // 10s

// --- Timing démarrage ---
bool gameReady = false;
unsigned long gameStartTime = 0;
const unsigned long START_DELAY = 5000; // 5s

// --- Buzzer ---
void beep(int freq, int durationMs) { ledcWriteTone(buzzerChannel, freq); delay(durationMs); ledcWriteTone(buzzerChannel, 0); }
void beepWiFiConnected() { beep(1500, 500); }
void beepMQTTConnected() { beep(1800, 500); }
void beepGoodInput() { beep(2000, 100); }
void beepStartGame() { beep(1200, 500); }
void beepRoundWin() { beep(1800,100); delay(60); beep(2000,100); }
void beepGameOver() { for(int i=0;i<3;i++){ beep(600,100); delay(80); } }

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
  DeserializationError err = deserializeJson(doc, payloadStr);
  if(err) return;

  if(sTopic == "simon/pair"){
    pairingSSID = doc["ssid"] | "";
    pairingPassword = doc["password"] | "";
    pairedUsername = doc["username"] | "";
    pairingInProgress = true;
    pairingStartTime = millis();
    Serial.println("Appairage demandé pour SSID: " + pairingSSID);
  }
}

// --- MQTT connect ---
void ensureMqtt(){
  while(!mqtt.connected()){
    String clientId = "ESP32-" + deviceId;
    if(mqtt.connect(clientId.c_str())){
      mqtt.subscribe("simon/pair");
      beepMQTTConnected();
      Serial.println("MQTT connecté");
    } else {
      Serial.print("Erreur MQTT, retry in 2s: ");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

// --- Publish score ---
void publishScore(int score){
  if(!mqtt.connected()) ensureMqtt();
  if(pairedUsername=="") return;

  StaticJsonDocument<256> doc;
  doc["ssid"] = WiFi.SSID();
  doc["username"] = pairedUsername;
  doc["score"] = score;

  char buf[256];
  size_t n = serializeJson(doc,buf);
  mqtt.publish("simon/scores", buf, n);
}

// --- Game Logic ---
void lightLed(int idx,int onTime=400,int offTime=100){
  digitalWrite(ledPins[idx],HIGH);
  delay(onTime);
  digitalWrite(ledPins[idx],LOW);
  delay(offTime);
}

void startNewGame(){
  randomSeed(micros());
  sequence[0] = random(0,4);
  currentRound = 1;
  inputIndex = 0;
  gameState = SHOW_SEQUENCE;

  for(int i=0;i<4;i++){
    buttonFlags[i]=false;
    buttonLocked[i]=false;
    lastPressTime[i]=millis();
  }

  beepStartGame();
}

void showSequence(){
  delay(400);
  for(int i=0;i<currentRound;i++){
    lightLed(sequence[i]);
  }
  inputIndex = 0;
  gameState = WAIT_INPUT;
}

void handleUserInput(int idx){
  if(idx != sequence[inputIndex]){
    // Game Over → passage en WAIT_PAIRING
    beepGameOver();
    gameState = WAIT_PAIRING;
    publishScore(currentRound-1);

    // Éteindre toutes les LEDs et buzzer
    for(int i=0;i<4;i++) digitalWrite(ledPins[i], LOW);
    ledcWriteTone(buzzerChannel, 0);
    Serial.println("Game over, en attente d'un nouvel appairage...");
    return;
  }

  beepGoodInput();
  lightLed(idx,200,50);
  inputIndex++;

  if(inputIndex >= currentRound){
    if(currentRound < MAX_SEQUENCE){
      sequence[currentRound] = random(0,4);
      currentRound++;
      beepRoundWin();
      gameState = SHOW_SEQUENCE;
    } else {
      publishScore(currentRound);
      startNewGame();
    }
  }
}

// --- Wi-Fi connect ---
void connectWiFi(){
  Serial.print("Connexion au Wi-Fi...");
  WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD);

  int attempts = 0;
  while(WiFi.status() != WL_CONNECTED && attempts < 20){
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if(WiFi.status() == WL_CONNECTED){
    Serial.println("\nConnecté au Wi-Fi!");
    Serial.print("IP locale: ");
    Serial.println(WiFi.localIP());
    beepWiFiConnected();
  } else {
    Serial.println("\nÉchec de connexion au Wi-Fi");
  }
}

// --- Setup ---
void setup(){
  Serial.begin(115200);

  for(int i=0;i<4;i++){ pinMode(ledPins[i], OUTPUT); digitalWrite(ledPins[i], LOW); }
  for(int i=0;i<4;i++){ pinMode(buttonPins[i], INPUT_PULLUP); }

  attachInterrupt(buttonPins[0], isrButton0, FALLING);
  attachInterrupt(buttonPins[1], isrButton1, FALLING);
  attachInterrupt(buttonPins[2], isrButton2, FALLING);
  attachInterrupt(buttonPins[3], isrButton3, FALLING);

  ledcAttachPin(buzzerPin, buzzerChannel);
  ledcSetup(buzzerChannel, 2000, 10);

  deviceId = WiFi.macAddress();
  deviceId.replace(":", "");

  connectWiFi();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(callback);
  ensureMqtt();

  // Démarrage différé du jeu
  gameStartTime = millis();
}

// --- Loop ---
void loop(){
  mqtt.loop();

  // --- Gestion appairage ---
  if(pairingInProgress){
    if(WiFi.status()!=WL_CONNECTED){
      WiFi.begin(pairingSSID.c_str(), pairingPassword.c_str());
    }

    if(WiFi.status()==WL_CONNECTED){
      pairingInProgress = false;
      Serial.println("Appairage réussi avec: " + pairedUsername);
      beepWiFiConnected();

      StaticJsonDocument<128> ack;
      ack["ssid"] = pairingSSID;
      ack["username"] = pairedUsername;
      ack["status"] = "paired";
      char buf[128];
      size_t n = serializeJson(ack, buf);
      mqtt.publish("simon/pair/ack", buf, n);

      // Démarrage du jeu après appairage
      startNewGame();
      gameReady = true;
    } else if(millis() - pairingStartTime > PAIRING_TIMEOUT){
      pairingInProgress = false;
      Serial.println("Appairage échoué");
      StaticJsonDocument<128> ack;
      ack["ssid"] = pairingSSID;
      ack["username"] = pairedUsername;
      ack["status"] = "failed";
      char buf[128];
      size_t n = serializeJson(ack, buf);
      mqtt.publish("simon/pair/ack", buf, n);
    }
  }

  // --- Si le jeu est en attente, ne rien faire ---
  if(gameState == WAIT_PAIRING) return;

  // --- Attente démarrage 5s ---
  if(!gameReady && millis() - gameStartTime >= START_DELAY){
    startNewGame();
    gameReady = true;
  }

  // --- Gestion boutons ---
  for(int i=0;i<4;i++){
    if(buttonLocked[i] && digitalRead(buttonPins[i])==HIGH) buttonLocked[i]=false;
    if(buttonFlags[i]){
      buttonFlags[i]=false;
      unsigned long now = millis();
      if(buttonLocked[i]) continue;

      if(digitalRead(buttonPins[i])==LOW && now-lastPressTime[i] > debounceDelay){
        lastPressTime[i]=now;
        buttonLocked[i]=true;

        if(gameState==WAIT_INPUT) handleUserInput(i);
      }
    }
  }

  // --- Game state ---
  switch(gameState){
    case SHOW_SEQUENCE: showSequence(); break;
    case WAIT_INPUT: break;
    case GAME_OVER: break; // plus utilisé
    case WAIT_START: break;
    case WAIT_PAIRING: break; // ne rien faire
  }
}
