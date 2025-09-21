#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <PZEM004Tv30.h>
#include <SoftwareSerial.h>
#include <DHT.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

// MQTT Info
const char *mqtt_broker = "broker.emqx.io";
const char *mqtt_topic = "device/test";
const char *mqtt_username = "cumed";
const char *mqtt_password = "public";
const int mqtt_port = 1883;

// HTTP Server PHP
const char* serverName = "http://monitoringac.my.id/data.php";

const char* custom_css = R"rawliteral(
    body{background: linear-gradient(135deg, #2c3e50, #3498db); color:white; font-family:'Segoe UI',sans-serif;}
    .c, .b, input, button, select{border-radius:8px !important; border:none !important;}
    .c{background-color:rgba(255,255,255,0.1) !important; backdrop-filter:blur(10px) !important; padding:30px !important; box-shadow:0 8px 24px rgba(0,0,0,0.3) !important;}
    h1{color:#ecf0f1 !important;}
    input, select{background-color:rgba(255,255,255,0.8) !important; color:#2c3e50 !important; padding:12px 14px !important;}
    button{background-color:#1abc9c !important; transition:background 0.3s ease; padding:12px !important; font-weight:bold !important;}
    button:hover{background-color:#16a085 !important;}
    a{color:#1abc9c !important;}
    .m a[href='/info'], .m a[href='/update'] {display:none !important;}

)rawliteral";

// PZEM TX → D1, RX → D2
SoftwareSerial pzemSerial(D1, D2);
PZEM004Tv30 pzem(pzemSerial);

// DHT22 setup
#define DHTPIN D4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// MQTT Client
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

void connectToMQTTBroker();
void mqttCallback(char *topic, byte *payload, unsigned int length);

// Data pembacaan
float voltage = NAN, current = NAN, power = NAN;
float temperature = NAN, humidity = NAN;

void setup() {
  Serial.begin(115200);
  dht.begin();

  WiFiManager wifiManager;
  

  String headElement = "<style>" + String(custom_css) + "</style>";
  wifiManager.setCustomHeadElement(headElement.c_str());

  if (!wifiManager.autoConnect("Smart Energry Config")) {
    Serial.println("Gagal konek WiFi, restart...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("Terhubung ke WiFi!");

  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  connectToMQTTBroker();

  delay(2000); 
}


void loop() {
  if (!mqtt_client.connected()) {
    connectToMQTTBroker();
  }
  mqtt_client.loop();

  if (WiFi.status() == WL_CONNECTED) {
    readSensors();

    if (!isnan(voltage) && !isnan(current) && !isnan(power) &&
        !isnan(temperature) && !isnan(humidity)) {

      Serial.println("=== Data Sensor ===");
      Serial.print("Tegangan (V): "); Serial.println(voltage);
      Serial.print("Arus (A): "); Serial.println(current);
      Serial.print("Daya (W): "); Serial.println(power);
      Serial.print("Suhu (°C): "); Serial.println(temperature);
      Serial.print("Kelembaban (%): "); Serial.println(humidity);
      Serial.println("===================");

      // Kirim MQTT
      char payloadStr[250];
      snprintf(payloadStr, sizeof(payloadStr),
        "{\"unique_id\":\"ESP_ABC124\",\"temperature\":%.2f,\"humidity\":%.2f,\"voltage\":%.2f,\"current\":%.2f,\"watt\":%.2f}",
        temperature, humidity, voltage, current, power);
      mqtt_client.publish(mqtt_topic, payloadStr);

      // Kirim HTTP hanya jika daya > 0
      if (power > 0.0) {
        WiFiClient client;
        HTTPClient http;
        http.begin(client, serverName);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        String postData = "unique_id=ESP_ABC124";
        postData += "&temperature=" + String(temperature);
        postData += "&humidity=" + String(humidity);
        postData += "&voltage=" + String(voltage);
        postData += "&current=" + String(current);
        postData += "&watt=" + String(power);

        int httpResponseCode = http.POST(postData);
        Serial.print("HTTP Response Code: ");
        Serial.println(httpResponseCode);

        if (httpResponseCode > 0) {
          String response = http.getString();
          Serial.println("Respon server:");
          Serial.println(response);
        }

        http.end();
      } else {
        Serial.println("Daya = 0, tidak mengirim ke server PHP.");
      }
    } else {
      Serial.println("Data sensor tidak valid.");
    }
  } else {
    Serial.println("WiFi tidak terhubung");
  }

  delay(30000);
  yield();
}

void readSensors() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  voltage = pzem.voltage();
  current = pzem.current();
  power = pzem.power();

  if (isnan(voltage) || isnan(current) || isnan(power)) {
    Serial.println("Gagal membaca data dari PZEM. Re-inisialisasi...");
    pzemSerial.end();
    delay(500);
    pzemSerial.begin(9600);
    PZEM004Tv30 newPzem(pzemSerial);
    pzem = newPzem;
    delay(1000);
  }
}

void connectToMQTTBroker() {
  while (!mqtt_client.connected()) {
    String client_id = "esp8266-client-" + String(WiFi.macAddress());
    Serial.printf("Connecting to MQTT Broker as %s...\n", client_id.c_str());
    if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("MQTT terhubung!");
      mqtt_client.subscribe(mqtt_topic);
    } else {
      Serial.print("Gagal terhubung, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" coba lagi 5 detik");
      delay(5000);
    }
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Pesan dari topic: ");
  Serial.println(topic);
  Serial.print("Isi: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  Serial.println("-----------------------");
}
