# 🎮 Simon Communautaire

Projet réalisé par **Yarkin Oner** et **Matheo Maussant**

## 📖 Description

Simon Communautaire est une version connectée du célèbre jeu électronique Simon, implémentée sur ESP32 avec un système de scores en ligne partagés. Les joueurs peuvent s'affronter et comparer leurs performances via une interface web centralisée.

## 🏗️ Architecture du Projet

Le projet se compose de trois éléments principaux :

### 1. **ESP32 + Circuit Simon**
- Microcontrôleur ESP32 gérant la logique du jeu
- 4 LEDs de couleur (rouge, verte, bleue, jaune)
- 4 boutons poussoirs correspondants
- 1 buzzer pour les sons
- Communication MQTT pour l'envoi des scores

### 2. **Serveur MQTT (Broker)**
- Point central de communication
- Gère les messages entre l'ESP32 et le serveur web
- Topics utilisés :
  - `simon/scores` : réception des scores
  - `simon/pair` : appairage ESP32
  - `simon/pair/ack` : confirmation d'appairage
- MQTT utilise le port 1883 par défaut
- Messages JSON standardisés pour scores et appairage

### 3. **Serveur Web Flask**
- Interface utilisateur web
- Authentification des joueurs (inscription/connexion)
- Appairage avec l'ESP32
- Tableau de bord des scores
- Stockage des scores en JSON et SQLite

## 🔌 Schéma de Câblage

![Schéma de câblage](schema.png)

### Connexions principales :
- **LEDs** : GPIO13 (rouge), GPIO14 (verte), GPIO16 (bleue), GPIO17 (jaune)
- **Boutons** : GPIO18 (rouge), GPIO19 (vert), GPIO25 (bleu), GPIO26 (jaune)
- **Buzzer** : GPIO23
- **Alimentation** : +5V et GND

## 🚀 Installation et Lancement

### Prérequis
```bash
# Python 3.7+
# Mosquitto MQTT Broker
# ESP32 avec MicroPython/Arduino
# git
```
```bash
git clone https://github.com/Matheo-Msst/IOT_SIMON_GAME.git
```

### Installation du serveur
```bash
cd server
pip install -r requirements.txt
```

### Lancement du broker MQTT
```bash
mosquitto -v
```

### Lancement du serveur Flask
```bash
cd server
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
python3 main.py
```

L'application sera accessible sur `http://localhost:5000`

## 📊 Fonctionnalités

### Interface Web
- ✅ Inscription et connexion utilisateur
- ✅ Appairage avec un ESP32 via SSID et mot de passe
- ✅ Visualisation du tableau des scores
- ✅ Historique des 200 derniers scores

### ESP32
- ✅ Jeu Simon classique avec séquences aléatoires
- ✅ Envoi automatique des scores via MQTT
- ✅ Connexion WiFi et appairage sécurisé
- ✅ Retour sonore et visuel

## 📁 Structure du Projet

```
.
└── server/
    ├── main.py                 # Serveur Flask principal
    ├── users.db               # Base de données SQLite
    ├── json/
    │   └── scores.json        # Stockage des scores
    └── templates/
        ├── base.html          # Template de base
        ├── login.html         # Page de connexion
        ├── register.html      # Page d'inscription
        ├── pair.html          # Page d'appairage
        └── dashboard.html     # Tableau de bord
```

## 🎯 Flux de Données

```
[ESP32] --MQTT--> [Broker] --MQTT--> [Serveur Flask] --HTTP--> [Navigateur]
   ↓                                        ↓
Jeu Simon                              scores.json + users.db
```

1. Le joueur lance une partie sur l'ESP32
2. À la fin de la partie, l'ESP32 publie le score via MQTT
3. Le serveur Flask reçoit le score et le stocke
4. Les scores sont affichés en temps réel sur le dashboard web

## 🔐 Sécurité

- Mots de passe hashés avec Werkzeug
- Sessions Flask sécurisées
- Authentification requise pour accéder au dashboard
- Appairage ESP32 avec mot de passe

## 📝 Format des Scores

```json
{
  "ssid": "SIMON_123",
  "username": "alice",
  "score": 10,
  "ts": 1701427380
}
```

## 📡 MQTT

- Le serveur Flask se connecte au broker MQTT à `127.0.0.1:1883`
- Topics principaux :
  - `simon/scores` : réception des scores publiés par les ESP32
  - `simon/pair` : ESP32 envoie les informations pour appairage
  - `simon/pair/ack` : confirmation d'appairage envoyée par le serveur
- Messages format JSON avec champs `ssid`, `username`, `score`, `status`
- Utilisation de `paho-mqtt` côté serveur pour la communication asynchrone

## 👥 Contributeurs

- **Yarkin Oner**
- **Mathéo Maussant**

---

*Projet réalisé dans le cadre d'un cours d'électronique et systèmes embarqués*

