#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>


////Konfigurasi Jaringan WiFi
//const char* ssid = "Galaxy A03s2255";
//const char* password = "11221122";

const char* ssid = "Hotspotnya dwi";
const char* password = "87654321";

// Konfigurasi Google Sheets
const char* scriptURL = "https://script.google.com/macros/s/AKfycbxGdRYL4Z-aD30Qxt-kmtn9IdIsfJG5Ge-2V2YE-YhvCHw9wM9zJ-U6L5ewjOMk5OtP/exec";

// Konfigurasi ThingsBoard
const char* thingsboardServer = "demo.thingsboard.io";
const char* accessToken = "3be1j1yjR4WBka29eAsH";

// Konfigurasi PZEM-004T
const byte pzem_rx_pin = 16;  // RX2
const byte pzem_tx_pin = 17;  // TX2
//SoftwareSerial pzemSerial(pzem_rx_pin, pzem_tx_pin); // Remove SoftwareSerial
PZEM004Tv30 pzem(Serial2, pzem_rx_pin, pzem_tx_pin); // Use Serial2, and the correct constructor

// Konfigurasi LCD I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);

WiFiClient espClient;
PubSubClient client(espClient);

double total_energi = 0.0;
double energiInt = 0.0;
double energi_interval = 0.0;


// Rata-rata
double total_daya_30_menit = 0.0;
int jumlah_sampel_daya = 0;
double total_tegangan_30_menit = 0.0;
int jumlah_sampel_teg = 0;
double total_arus_30_menit = 0.0;
int jumlah_sampel_arus = 0;

// Timer
unsigned long lastEnergy = 0;
unsigned long lastInterval = 0;
unsigned long lastTime = 0;
unsigned long lastSheet = 0;
const long updateInterval = 10000;
//const long sheetInt = 1800000;
const long sheetInt = 300000;


void connectToWiFi() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi Connected!");
}

void connectToThingsBoard() {
  Serial.print("Menghubungkan ke ThingsBoard...");
  while (!client.connect("ESP32", accessToken, NULL)) {
    delay(500);
    Serial.print("Gagal, rc=");
    Serial.print(client.state());
    Serial.println(" coba lagi dalam 5 detik");
  }
  Serial.println("Terhubung ke ThingsBoard");
}

void setup() {
  connectToWiFi();
  client.setServer(thingsboardServer, 1883);
  connectToThingsBoard();
  Serial2.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.print("Monitoring Listrik");
  delay(2000);
  lcd.clear();

  unsigned long now = millis();
  lastEnergy = now;
  lastInterval = now;
  lastTime = now;
  lastSheet = now;
}

void loop() {
  if (!client.connected()) {
    connectToThingsBoard();
  }
  client.loop();

  unsigned long now = millis();

  float tegangan = pzem.voltage();
  float arus = pzem.current();
  float daya = pzem.power();
  float faktor_daya = pzem.pf();

  if(now - lastEnergy >= 1000){
    energiInt += (daya / 1000.0) * (1.0 / 3600.0);
    total_daya_30_menit += daya;
    jumlah_sampel_daya++;

    total_tegangan_30_menit += tegangan;
    jumlah_sampel_teg++;

    total_arus_30_menit += arus;
    jumlah_sampel_arus++;
    
    lastEnergy = now;
  }

  if(now - lastInterval >= sheetInt){
    total_energi += energiInt;
    energi_interval = energiInt;
    energiInt = 0;
    lastInterval = now;
  }

  if (now - lastTime >= updateInterval) {
    lastTime = now;

    String payload = "{";
    payload += "\"tegangan\":" + String(tegangan) + ",";
    payload += "\"arus\":" + String(arus) + ",";
    payload += "\"daya\":" + String(daya) + ",";
    payload += "\"total_energi\":" + String(total_energi, 4) + ",";
    payload += "\"energi_15_menit\":" + String(energi_interval, 4) + ",";
    payload += "\"faktor_daya\":" + String(faktor_daya);
    payload += "}";

    char attributes[256];
    payload.toCharArray(attributes, sizeof(attributes));
    client.publish("v1/devices/me/telemetry", attributes);
    Serial.println("Data terkirim ke ThingsBoard: " + payload);

    lcd.setCursor(0, 0);
    lcd.print("V:");
    lcd.print(String(tegangan, 1));   // 1 angka desimal
    lcd.print(" A:");
    lcd.print(String(arus, 2));       // 2 angka desimal
    lcd.print("  "); // untuk menimpa karakter sisa jika nilai lama lebih panjang

    lcd.setCursor(0, 1);
    lcd.print("W:");
    lcd.print(String(daya, 0));       // tanpa desimal
    lcd.print(" E:");
    lcd.print(String(total_energi, 4)); // 2 angka desimal
    lcd.print("  "); // untuk menimpa karakter sisa jika nilai lama lebih panjang

  if (now - lastSheet >= sheetInt){
    lastSheet = now;

    double rata_rata_daya = (jumlah_sampel_daya > 0) ? (total_daya_30_menit / jumlah_sampel_daya) : 0;
    double rata_rata_tegangan = (jumlah_sampel_teg > 0) ? (total_tegangan_30_menit / jumlah_sampel_teg) : 0;
    double rata_rata_arus = (jumlah_sampel_arus > 0) ? (total_arus_30_menit / jumlah_sampel_arus) : 0;

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(scriptURL);
      http.addHeader("Content-Type", "application/json");

      String jsonData = "{";
      jsonData += "\"tegangan\":" + String(tegangan) + ",";
      jsonData += "\"arus\":" + String(arus) + ",";
      jsonData += "\"daya\":" + String(daya) + ",";
      jsonData += "\"rata_rata_tegangan\":" + String(rata_rata_tegangan, 2) + ",";
      jsonData += "\"rata_rata_arus\":" + String(rata_rata_arus, 2) + ",";
      jsonData += "\"rata_rata_daya\":" + String(rata_rata_daya, 2) + ",";
      jsonData += "\"energi_15_menit\":" + String(energi_interval, 4) + ",";
      jsonData += "\"total_energi\":" + String(total_energi, 4) + ",";
      jsonData += "\"faktor_daya\":" + String(faktor_daya);
      jsonData += "}";

      int response = http.POST(jsonData);
      Serial.print("Google Sheets Response code: ");
      Serial.println(response);
      http.end();
    }

    total_daya_30_menit = 0.0;
    jumlah_sampel_daya = 0;
    total_tegangan_30_menit = 0.0;
    jumlah_sampel_teg = 0;
    total_arus_30_menit = 0.0;
    jumlah_sampel_arus = 0;
    energi_interval = 0.0;
  }

  delay(100);
}
}