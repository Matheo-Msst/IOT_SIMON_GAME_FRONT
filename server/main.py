from flask import Flask, render_template_string, request, redirect, url_for, session
import sqlite3, json, threading, time, os
import paho.mqtt.client as mqtt
from werkzeug.security import generate_password_hash, check_password_hash

# --- Config ---
APP_SECRET = 'change-me'
MQTT_BROKER = '127.0.0.1'
MQTT_PORT = 1883
DB_FILE = 'users.db'
SCORES_DIR = './json'
SCORES_FILE = os.path.join(SCORES_DIR,'scores.json')

# --- Flask app ---
app = Flask(__name__)
app.secret_key = APP_SECRET

# --- DB init ---
def init_db():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password TEXT)''')
    conn.commit()
    conn.close()
init_db()

# --- MQTT client ---
mqtt_client = mqtt.Client()

def on_connect(client, userdata, flags, rc):
    print("MQTT connected", rc)
    client.subscribe("simon/scores")
    client.subscribe("simon/pair/ack")

def on_message(client, userdata, msg):
    payload = msg.payload.decode()
    try:
        data = json.loads(payload)
    except:
        return
    if msg.topic == "simon/scores":
        os.makedirs(SCORES_DIR, exist_ok=True)
        try:
            with open(SCORES_FILE,'r') as f:
                arr = json.load(f)
        except FileNotFoundError:
            arr = []
        arr.append({
            "ssid": data.get("ssid"),
            "username": data.get("username"),
            "score": data.get("score"),
            "ts": int(time.time())
        })
        with open(SCORES_FILE,'w') as f:
            json.dump(arr,f,indent=2)
    elif msg.topic == "simon/pair/ack":
        print(f"{data.get('ssid')} paired with {data.get('username')} status={data.get('status')}")

mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_BROKER,MQTT_PORT,60)
threading.Thread(target=mqtt_client.loop_forever,daemon=True).start()

# --- Templates ---
BASE_TEMPLATE = '''<!doctype html>
<html><head><meta charset="utf-8"><title>{{title}}</title></head>
<body>
<h1>{{title}}</h1>
{{content|safe}}
</body></html>'''

# --- Routes ---
@app.route('/')
def index():
    return redirect(url_for('login'))

@app.route('/register',methods=['GET','POST'])
def register():
    if request.method=='POST':
        username=request.form['username']
        password=generate_password_hash(request.form['password'])
        conn=sqlite3.connect(DB_FILE)
        try:
            conn.execute("INSERT INTO users(username,password) VALUES(?,?)",(username,password))
            conn.commit()
        except sqlite3.IntegrityError:
            conn.close()
            return "Username exists"
        conn.close()
        return redirect(url_for('login'))
    content='''<form method="post">
        <input name="username" placeholder="username"><br>
        <input type="password" name="password" placeholder="password"><br>
        <button>Register</button>
    </form>'''
    return render_template_string(BASE_TEMPLATE,title="Register",content=content)

@app.route('/login',methods=['GET','POST'])
def login():
    if request.method=='POST':
        username=request.form['username']
        password=request.form['password']
        conn=sqlite3.connect(DB_FILE)
        c=conn.cursor()
        c.execute("SELECT password FROM users WHERE username=?",(username,))
        r=c.fetchone()
        conn.close()
        if r and check_password_hash(r[0],password):
            session['username']=username
            return redirect(url_for('pair'))
        return "Bad credentials"

    content='''<form method="post">
        <input name="username" placeholder="username"><br>
        <input type="password" name="password" placeholder="password"><br>
        <button>Login</button>
    </form>
    <p>Pas encore de compte ? <a href="/register">Créer un compte</a></p>
    <p>Voir les scores de test sans ESP32 : <a href="/test-dashboard">Test Dashboard</a></p>'''
    
    return render_template_string(BASE_TEMPLATE,title="Login",content=content)


@app.route('/pair',methods=['GET','POST'])
def pair():
    if 'username' not in session: return redirect(url_for('login'))
    if request.method=='POST':
        ssid=request.form['ssid']
        pwd=request.form['password']
        mqtt_client.publish("simon/pair",json.dumps({"ssid":ssid,"password":pwd,"username":session['username']}))
        return "Pair request sent to ESP32."
    content='''<form method="post">
        SSID ESP32: <input name="ssid"><br>
        Password: <input name="password" type="password"><br>
        <button>Pair ESP32</button>
    </form>'''
    return render_template_string(BASE_TEMPLATE,title="Pair ESP32",content=content)

@app.route('/dashboard')
def dashboard():
    if 'username' not in session: return redirect(url_for('login'))
    try:
        with open(SCORES_FILE,'r') as f:
            scores=json.load(f)
    except: scores=[]
    table="<table border=1><tr><th>SSID</th><th>User</th><th>Score</th><th>Time</th></tr>"
    for s in reversed(scores[-200:]):
        table+=f"<tr><td>{s.get('ssid')}</td><td>{s.get('username')}</td><td>{s.get('score')}</td><td>{time.ctime(s.get('ts'))}</td></tr>"
    table+="</table>"
    content=f"<p>Bonjour {session['username']} — <a href='/pair'>Pair ESP32</a> — <a href='/logout'>Logout</a></p>{table}"
    return render_template_string(BASE_TEMPLATE,title="Dashboard",content=content)

# --- Page de test supplémentaire ---
@app.route('/test-dashboard')
def test_dashboard():
    if 'username' not in session: return redirect(url_for('login'))
    try:
        with open(SCORES_FILE,'r') as f:
            scores=json.load(f)
    except: scores=[]
    table="<table border=1><tr><th>SSID</th><th>User</th><th>Score</th><th>Time</th></tr>"
    for s in reversed(scores[-50:]):
        table+=f"<tr><td>{s.get('ssid')}</td><td>{s.get('username')}</td><td>{s.get('score')}</td><td>{time.ctime(s.get('ts'))}</td></tr>"
    table+="</table>"
    content=f"<p>Test dashboard pour {session['username']} — <a href='/dashboard'>Retour Dashboard</a></p>{table}"
    return render_template_string(BASE_TEMPLATE,title="Test Dashboard",content=content)

@app.route('/logout')
def logout():
    session.clear()
    return redirect(url_for('login'))

if __name__=="__main__":
    os.makedirs(SCORES_DIR, exist_ok=True)
    app.run(host="0.0.0.0",port=5000,debug=True)
