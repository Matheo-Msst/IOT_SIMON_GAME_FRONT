from flask import Flask, render_template, request, redirect, url_for, session
import sqlite3, json, threading, time, os
import paho.mqtt.client as mqtt
from werkzeug.security import generate_password_hash, check_password_hash

# --- Config ---
APP_SECRET = 'change-me'
MQTT_BROKER = '127.0.0.1'
MQTT_PORT = 1883
DB_FILE = 'users.db'
SCORES_DIR = './json'
SCORES_FILE = os.path.join(SCORES_DIR, 'scores.json')

# --- Flask app ---
app = Flask(__name__)
app.secret_key = APP_SECRET

# --- Jinja2 filter ---
@app.template_filter('timestamp_to_date')
def timestamp_to_date(timestamp):
    return time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(timestamp))

# --- DB init ---
def init_db():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY, 
        username TEXT UNIQUE, 
        password TEXT
    )''')
    conn.commit()
    conn.close()
init_db()

# --- MQTT ---
mqtt_client = mqtt.Client(client_id="FlaskServer", protocol=mqtt.MQTTv311)
mqtt_connected = False

# Variables pour appairage
pair_result = None
pair_event = threading.Event()

def on_connect(client, userdata, flags, rc):
    global mqtt_connected
    if rc == 0:
        mqtt_connected = True
        print("MQTT connecté avec succès !")
        client.subscribe("simon/scores")
        client.subscribe("simon/pair/ack")
    else:
        print(f"Erreur de connexion MQTT, code: {rc}")

def on_message(client, userdata, msg):
    global pair_result
    payload = msg.payload.decode()
    try:
        data = json.loads(payload)
    except:
        print("Message MQTT invalide:", payload)
        return

    if msg.topic == "simon/scores":
        os.makedirs(SCORES_DIR, exist_ok=True)
        try:
            with open(SCORES_FILE, 'r') as f:
                scores = json.load(f)
        except FileNotFoundError:
            scores = []

        timestamp = int(time.time())
        scores.append({
            "ssid": data.get("ssid"),
            "username": data.get("username"),
            "score": data.get("score"),
            "ts": timestamp,
            "date": time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(timestamp))
        })

        with open(SCORES_FILE, 'w') as f:
            json.dump(scores, f, indent=2)

    elif msg.topic == "simon/pair/ack":
        print(f"{data.get('ssid')} paired with {data.get('username')} - status: {data.get('status')}")
        pair_result = data
        pair_event.set()  # Débloque l'attente dans /pair

mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
threading.Thread(target=mqtt_client.loop_forever, daemon=True).start()

# --- Routes ---
@app.route('/')
def index():
    return redirect(url_for('login'))

@app.route('/register', methods=['GET','POST'])
def register():
    if request.method == 'POST':
        username = request.form['username']
        password = generate_password_hash(request.form['password'])
        conn = sqlite3.connect(DB_FILE)
        try:
            conn.execute("INSERT INTO users(username, password) VALUES(?,?)", (username,password))
            conn.commit()
        except sqlite3.IntegrityError:
            conn.close()
            return render_template('register.html', error="Ce nom d'utilisateur existe déjà")
        conn.close()
        return redirect(url_for('login'))
    return render_template('register.html')

@app.route('/login', methods=['GET','POST'])
def login():
    if request.method == 'POST':
        username = request.form['username']
        password = request.form['password']
        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute("SELECT password FROM users WHERE username=?", (username,))
        r = c.fetchone()
        conn.close()
        if r and check_password_hash(r[0], password):
            session['username'] = username
            return redirect(url_for('dashboard'))
        return render_template('login.html', error="Identifiants incorrects")
    return render_template('login.html')

@app.route('/pair', methods=['GET','POST'])
def pair():
    if 'username' not in session:
        return redirect(url_for('login'))

    success_msg = None
    error_msg = None

    if request.method == 'POST':
        ssid = request.form['ssid'].strip()
        pwd = request.form.get('password','').strip()  # <-- mot de passe optionnel

        if mqtt_connected:
            pair_event.clear()
            global pair_result
            pair_result = None

            payload = json.dumps({
                "ssid": ssid,
                "password": pwd,  # vide si Wi-Fi ouvert
                "username": session['username']
            })
            mqtt_client.publish("simon/pair", payload)

            # Attente de la réponse MQTT max 10 secondes
            if pair_event.wait(timeout=10):
                if pair_result.get("status") == "paired":
                    return redirect(url_for('dashboard'))
                else:
                    error_msg = f"Appairage échoué pour {pair_result.get('ssid')}"
            else:
                error_msg = "Aucune réponse de l'ESP32, réessayez."
        else:
            error_msg = "Serveur MQTT non connecté, réessayez plus tard"

    return render_template('pair.html', username=session['username'], success=success_msg, error=error_msg)


@app.route('/dashboard')
def dashboard():
    if 'username' not in session:
        return redirect(url_for('login'))

    try:
        with open(SCORES_FILE,'r') as f:
            scores = json.load(f)
    except:
        scores = []

    recent_scores = list(reversed(scores[-200:]))  # plus récent en premier
    return render_template('dashboard.html', username=session['username'], scores=recent_scores)

@app.route('/logout')
def logout():
    session.clear()
    return redirect(url_for('login'))

if __name__ == "__main__":
    os.makedirs(SCORES_DIR, exist_ok=True)
    # Eviter debug=True pour MQTT stable
    app.run(host="0.0.0.0", port=5000, debug=False)
