🔥 Test rapide complet en 2 lignes
PC 1 (serveur Mosquitto) :
mosquitto_sub -h 192.168.X.X -t "test"

PC 2 :
mosquitto_pub -h 192.168.X.X -t "test" -m "YOOOO!"
