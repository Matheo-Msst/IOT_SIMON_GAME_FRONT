# ESP32 Simon + WiFiManager + MQTT + Flask (Python) Server

Ce document contient **deux** parties prêtes à l'emploi :

1. **Sketch Arduino (ESP32)** : ajoute WiFiManager (captive portal) pour l'appariement Wi‑Fi, MQTT (PubSubClient) et publication des scores au format JSON. Le device écoute aussi un topic de pairing pour associer un `username` à ce périphérique.

2. **Application Python (Flask)** : site web simple avec **register / login** (SQLite3), page d'appariement qui envoie la commande d'appariement via MQTT, et un **dashboard** qui affiche les scores. Un client MQTT côté serveur écoute le topic `simon/scores` et enregistre les scores dans `scores.json`.

---

## 1) ESP32 - Sketch (Arduino)

> **Fichiers / Librairies nécessaires côté Arduino** :
> - WiFiManager (tzapu/esp32-wifimanager compatible)
> - PubSubClient
> - ArduinoJson

**Remarques** :
- Change `MQTT_SERVER` pour l'adresse de ton broker Mosquitto (ex: `"192.168.1.100"`).
- Le périphérique publie sur `simon/scores` un JSON : `{ "device_id": "<mac>", "username": "<paired-user>", "score": <score> }`.
- Pour l'appariement, le serveur Flask publie sur `simon/pair` un JSON : `{ "device_id": "<mac>", "username": "<user>" }`. L'ESP32 écoute ce topic et s'associe si `device_id` correspond.

```cpp
// --- ARDUINO SKETCH ---
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager (esp32 fork)

// --- Pins & Sound/LEDs (from ton code original) ---
const int ledPins[4] = {2, 3, 0, 1};
#define BUTTON1_PIN 7
#define BUTTON2_PIN 6
#define BUTTON3_PIN 5
#define BUTTON4_PIN 4
const int buttonPins[4] = { BUTTON1_PIN, BUTTON2_PIN, BUTTON3_PIN, BUTTON4_PIN };
#define BUZZER_PIN 10
#define LOW_VOLUME 8

// --- MQTT Broker (modifier) ---
const char* MQTT_SERVER = "192.168.1.100"; // <- change this to your mosquitto broker
const uint16_t MQTT_PORT = 1883;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

String pairedUsername = ""; // username apparié via MQTT
String deviceId; // mac

// maximums & game variables -- copie de ton code
const int MAX_SEQUENCE = 32;
int sequence[MAX_SEQUENCE];
int currentRound = 1;
int inputIndex = 0;

enum GameState { SHOW_SEQUENCE, WAIT_INPUT, GAME_OVER };
GameState gameState = SHOW_SEQUENCE;

volatile bool buttonFlags[4] = {false, false, false, false};
unsigned long lastPressTime[4] = {0,0,0,0};
const unsigned long debounceDelay = 200;
bool buttonLocked[4] = {false,false,false,false};

// forward declarations (adaptées)
void startNewGame();
void showSequence();
void handleUserInput(int buttonIndex);
void gameOverAnimation();

void callback(char* topic, byte* payload, unsigned int length) {
  // on reçoit pairing ou autres messages
  String sTopic = String(topic);
  String payloadStr;
  for (unsigned int i=0;i<length;i++) payloadStr += (char)payload[i];

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, payloadStr);
  if (err) {
    // mauvaise payload
    return;
  }

  if (sTopic == "simon/pair") {
    const char* tDevice = doc["device_id"] | "";
    const char* tUser = doc["username"] | "";
    if (String(tDevice) == deviceId) {
      pairedUsername = String(tUser);
      // ack back
      StaticJsonDocument<128> ack;
      ack["device_id"] = deviceId;
      ack["username"] = pairedUsername;
      ack["status"] = "paired";
      char buf[128];
      size_t n = serializeJson(ack, buf);
      mqttClient.publish("simon/pair/ack", buf, n);
    }
  }
}

void ensureMqtt() {
  if (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    while (!mqttClient.connected()) {
      String clientId = "ESP32-Simon-" + deviceId;
      if (mqttClient.connect(clientId.c_str())) {
        Serial.println("connected");
        mqttClient.subscribe("simon/pair");
      } else {
        Serial.print("failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" try again in 2s");
        delay(2000);
      }
    }
  }
}

void publishScore(int score) {
  if (!mqttClient.connected()) ensureMqtt();
  StaticJsonDocument<256> doc;
  doc["device_id"] = deviceId;
  doc["username"] = pairedUsername;
  doc["score"] = score;
  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqttClient.publish("simon/scores", buf, n);
}

// --- ISR pour boutons (simplifié) ---
void IRAM_ATTR isrButton1() { buttonFlags[0] = true; }
void IRAM_ATTR isrButton2() { buttonFlags[1] = true; }
void IRAM_ATTR isrButton3() { buttonFlags[2] = true; }
void IRAM_ATTR isrButton4() { buttonFlags[3] = true; }

// --- (Simplifie play/leds) ---
void lightLed(int index, int onTime = 400, int offTime = 100) {
  digitalWrite(ledPins[index], HIGH);
  delay(onTime);
  digitalWrite(ledPins[index], LOW);
  delay(offTime);
}

void startNewGame() {
  randomSeed(micros());
  sequence[0] = random(0,4);
  currentRound = 1;
  inputIndex = 0;
  gameState = SHOW_SEQUENCE;
  for (int i=0;i<4;i++) { buttonFlags[i]=false; buttonLocked[i]=false; lastPressTime[i]=millis(); }
}

void showSequence() {
  delay(400);
  for (int i=0;i<4;i++) { buttonFlags[i]=false; buttonLocked[i]=false; lastPressTime[i]=millis(); }
  for (int i=0;i<currentRound;i++) lightLed(sequence[i]);
  inputIndex = 0;
  gameState = WAIT_INPUT;
}

void handleUserInput(int buttonIndex) {
  if (buttonIndex != sequence[inputIndex]) {
    gameState = GAME_OVER;
    // publish score 0 or currentRound? on erreur on envoie currentRound-1
    publishScore(currentRound-1);
    return;
  }
  lightLed(buttonIndex,200,50);
  inputIndex++;
  if (inputIndex >= currentRound) {
    if (currentRound < MAX_SEQUENCE) {
      sequence[currentRound] = random(0,4);
      currentRound++;
      gameState = SHOW_SEQUENCE;
    } else {
      // win
      publishScore(currentRound);
      startNewGame();
    }
  }
}

void gameOverAnimation() {
  for (int i=0;i<3;i++) {
    for (int j=0;j<4;j++) digitalWrite(ledPins[j], HIGH);
    delay(150);
    for (int j=0;j<4;j++) digitalWrite(ledPins[j], LOW);
    delay(150);
  }
}

void setup() {
  Serial.begin(115200);
  for (int i=0;i<4;i++) { pinMode(ledPins[i], OUTPUT); digitalWrite(ledPins[i], LOW); }
  for (int i=0;i<4;i++) { pinMode(buttonPins[i], INPUT_PULLUP); }
  attachInterrupt(BUTTON1_PIN, isrButton1, FALLING);
  attachInterrupt(BUTTON2_PIN, isrButton2, FALLING);
  attachInterrupt(BUTTON3_PIN, isrButton3, FALLING);
  attachInterrupt(BUTTON4_PIN, isrButton4, FALLING);

  // Device ID as MAC without ':'
  deviceId = WiFi.macAddress();
  deviceId.replace(":", "");

  // WiFiManager: start AP if needed, captive portal
  WiFiManager wm;
  String apName = "Simon-PAIR-" + deviceId;
  wm.autoConnect(apName.c_str()); // lance l'AP si pas de config

  // une fois connecté
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callback);
  ensureMqtt();

  startNewGame();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    // attempt reconnect (WiFiManager generally gère)
  }
  if (!mqttClient.connected()) ensureMqtt();
  mqttClient.loop();

  for (int i=0;i<4;i++) {
    if (buttonLocked[i] && digitalRead(buttonPins[i]) == HIGH) buttonLocked[i] = false;
    if (buttonFlags[i]) {
      buttonFlags[i] = false;
      unsigned long now = millis();
      if (buttonLocked[i]) continue;
      if (digitalRead(buttonPins[i]) == LOW && (now - lastPressTime[i] > debounceDelay)) {
        lastPressTime[i] = now;
        buttonLocked[i] = true;
        if (gameState == WAIT_INPUT) {
          handleUserInput(i);
        } else if (gameState == GAME_OVER) {
          startNewGame();
        }
      }
    }
  }

  switch (gameState) {
    case SHOW_SEQUENCE: showSequence(); break;
    case WAIT_INPUT: break;
    case GAME_OVER: gameOverAnimation(); gameState = GAME_OVER; break;
  }
}
```

---

## 2) Python - Flask app (server web + MQTT client)

> **Dépendances** :
> ```bash
> pip install flask flask-bcrypt paho-mqtt
> ```

Le serveur fait :
- `register` & `login` avec SQLite3 (`users.db`).
- Après login, redirection vers `/pair` : page d'appariement. L'utilisateur entre `device_id` (ou le select si tu veux) et clique `Pair`.
- Le serveur publie sur `simon/pair` un JSON `{ device_id, username }`.
- Serveur écoute `simon/scores` et sauvegarde chaque score dans `scores.json` (liste d'objets), puis le dashboard lit `scores.json` pour afficher.

```python
# --- flask_server.py ---
from flask import Flask, render_template_string, request, redirect, url_for, session
import sqlite3
from werkzeug.security import generate_password_hash, check_password_hash
import threading
import json
import paho.mqtt.client as mqtt
import time

APP_SECRET = 'change-me'
MQTT_BROKER = '192.168.1.100'  # <- changer aussi
MQTT_PORT = 1883

app = Flask(__name__)
app.secret_key = APP_SECRET

DATABASE = 'users.db'
SCORES_FILE = 'scores.json'

# --- DB helpers ---
def init_db():
    conn = sqlite3.connect(DATABASE)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password TEXT)''')
    conn.commit()
    conn.close()

init_db()

# --- MQTT client en thread ---
mqtt_client = mqtt.Client()

def on_connect(client, userdata, flags, rc):
    print('MQTT connected', rc)
    client.subscribe('simon/scores')

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode()
        data = json.loads(payload)
        # store into scores.json
        try:
            with open(SCORES_FILE, 'r') as f:
                arr = json.load(f)
        except FileNotFoundError:
            arr = []
        arr.append({'device_id': data.get('device_id'), 'username': data.get('username'), 'score': data.get('score'), 'ts': int(time.time())})
        with open(SCORES_FILE, 'w') as f:
            json.dump(arr, f, indent=2)
    except Exception as e:
        print('Error processing MQTT message', e)

mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
threading.Thread(target=mqtt_client.loop_forever, daemon=True).start()

# --- routes ---
TEMPLATE_BASE = '''
<!doctype html><html><head><meta charset="utf-8"><title>{{title}}</title></head><body>
<h1>{{title}}</h1>
{% block body %}{% endblock %}
</body></html>'''

@app.route('/')
def index():
    if 'username' in session:
        return redirect(url_for('dashboard'))
    return redirect(url_for('login'))

@app.route('/register', methods=['GET','POST'])
def register():
    if request.method == 'POST':
        username = request.form['username']
        password = request.form['password']
        hashed = generate_password_hash(password)
        conn = sqlite3.connect(DATABASE)
        c = conn.cursor()
        try:
            c.execute('INSERT INTO users (username, password) VALUES (?,?)', (username, hashed))
            conn.commit()
        except sqlite3.IntegrityError:
            conn.close()
            return 'Username exists'
        conn.close()
        return redirect(url_for('login'))
    return render_template_string(TEMPLATE_BASE + '''{% block body %}
<form method="post"><input name="username" placeholder="username"><br><input name="password" type="password" placeholder="password"><br><button>Register</button></form>
{% endblock %}''', title='Register')

@app.route('/login', methods=['GET','POST'])
def login():
    if request.method == 'POST':
        username = request.form['username']
        password = request.form['password']
        conn = sqlite3.connect(DATABASE)
        c = conn.cursor()
        c.execute('SELECT password FROM users WHERE username=?', (username,))
        r = c.fetchone()
        conn.close()
        if r and check_password_hash(r[0], password):
            session['username'] = username
            return redirect(url_for('pair'))
        return 'Bad credentials'
    return render_template_string(TEMPLATE_BASE + '''{% block body %}
<form method="post"><input name="username" placeholder="username"><br><input name="password" type="password" placeholder="password"><br><button>Login</button></form>
{% endblock %}''', title='Login')

@app.route('/pair', methods=['GET','POST'])
def pair():
    if 'username' not in session: return redirect(url_for('login'))
    if request.method == 'POST':
        device_id = request.form['device_id'].strip()
        # publish to MQTT topic simon/pair
        payload = json.dumps({'device_id': device_id, 'username': session['username']})
        mqtt_client.publish('simon/pair', payload)
        return 'Pair request sent. Votre device devrait répondre si l'ID correspond.'
    return render_template_string(TEMPLATE_BASE + '''{% block body %}
<form method="post"><input name="device_id" placeholder="device id (MAC sans :) e.g. AABBCCDDEEFF"><br><button>Pair</button></form>
<p>Après pairing, scores envoyés par l'ESP apparaîtront dans le dashboard.</p>
{% endblock %}''', title='Pair device')

@app.route('/dashboard')
def dashboard():
    if 'username' not in session: return redirect(url_for('login'))
    try:
        with open(SCORES_FILE,'r') as f:
            scores = json.load(f)
    except FileNotFoundError:
        scores = []
    # simple table
    table = '<table border=1><tr><th>Username</th><th>Device</th><th>Score</th><th>Time</th></tr>'
    for s in reversed(scores[-200:]):
        table += f"<tr><td>{s.get('username')}</td><td>{s.get('device_id')}</td><td>{s.get('score')}</td><td>{time.ctime(s.get('ts'))}</td></tr>"
    table += '</table>'
    return render_template_string(TEMPLATE_BASE + '''{% block body %}
<p>Bonjour {{user}} — <a href="/pair">Pair a device</a> — <a href="/logout">Logout</a></p>
''' + table + '{% endblock %}', title='Dashboard', user=session['username'])

@app.route('/logout')
def logout():
    session.clear()
    return redirect(url_for('login'))

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
```

---

## Notes d'utilisation / étapes rapides

1. **Mosquitto** doit être accessible depuis le réseau. Mets son IP dans `MQTT_SERVER` (Arduino) et `MQTT_BROKER` (Flask).
2. Flash l'ESP32 avec le sketch. Au premier démarrage, il ouvrira un AP `Simon-PAIR-<MAC>` via WiFiManager pour configurer le WiFi local.
3. Lance le serveur Flask (`python flask_server.py`). Crée un compte (`/register`) puis login. Tu seras redirigé vers la page d'appariement.
4. Sur la page d'appariement, entre l'`device_id` (MAC de l'ESP32 sans deux-points, par ex `AABBCCDDEEFF`). Le serveur publiera le message MQTT et l'ESP32 se configurera localement (variable `pairedUsername`).
5. Quand une partie se termine, l'ESP32 publiera un JSON sur `simon/scores`. Le serveur l'enregistrera dans `scores.json` et le `dashboard` l'affichera.

---

## Améliorations possibles
- Auth via tokens JWT ou sessions plus robustes.
- Liste automatique des devices (via discovery MQTT ou stockage côté serveur des devices découverts).
- Retour visuel côté serveur quand l'ESP ack le pairing (le serveur peut s'abonner à `simon/pair/ack`).
- Sécurité MQTT (user/pass, TLS).

---

Si tu veux, je peux :
- Générer une version plus complète du front HTML/CSS (template séparé) ;
- Ajouter la gestion du `simon/pair/ack` pour montrer l'état d'appariement en temps réel ;
- Fournir une version Docker pour le serveur Flask + Mosquitto.

Dis-moi ce que tu veux améliorer ou si tu veux que j'ajoute la gestion du token/session persistante côté ESP (ex : stocker `pairedUsername` en SPIFFS/non-volatile).

