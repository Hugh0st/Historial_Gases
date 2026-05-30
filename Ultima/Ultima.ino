// ============================================================
// Fase 4: ESP32 → Firebase Realtime Database
// Lee el MQ-135 y sube cada lectura a Firebase (actual + histórico)
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ---------- CONFIGURA ESTAS LÍNEAS ----------
const char* WIFI_SSID     = "Wifi_Isla"; // INFINITUM9DB3
const char* WIFI_PASSWORD = "fQfK65uH2#%";   // vacío si la red es abierta - u72fUvpDUk

// Tu URL de Firebase SIN la barra final
const char* FIREBASE_HOST = "https://monitor-gas-mq135-d8c3d-default-rtdb.firebaseio.com";
// --------------------------------------------

const int MQ135_AO_PIN              = 34;
const int MUESTRAS_PROMEDIO         = 10;
const unsigned long INTERVALO_MS    = 5000;     // envía cada 5 s
const unsigned long CALENTAMIENTO_MS = 60000;   // 1 min de warmup
const int LED_PIN                   = 2;        // LED azul integrado

unsigned long bootMillis  = 0;
unsigned long exitos      = 0;
unsigned long fallos      = 0;

// ---------- WiFi ----------
void conectarWiFi() {
  Serial.print("Conectando a \""); Serial.print(WIFI_SSID); Serial.print("\" ");
  WiFi.mode(WIFI_STA);
  if (strlen(WIFI_PASSWORD) > 0) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  else                           WiFi.begin(WIFI_SSID);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) {
    Serial.print("."); delay(500);
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("OK | IP: "); Serial.print(WiFi.localIP());
    Serial.print(" | RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  } else {
    Serial.println("No se pudo conectar.");
  }
}

// ---------- Sensor ----------
int leerSensorPromediado() {
  long suma = 0;
  for (int i = 0; i < MUESTRAS_PROMEDIO; i++) {
    suma += analogRead(MQ135_AO_PIN);
    delay(5);
  }
  return suma / MUESTRAS_PROMEDIO;
}

// ---------- Firebase ----------
// metodo = "PUT" (sobreescribe) o "POST" (agrega con ID auto)
bool enviarAFirebase(const String& path, const String& json, const String& metodo) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure cliente;
  cliente.setInsecure();   // No validamos certificado (suficiente para este proyecto)

  HTTPClient http;
  String url = String(FIREBASE_HOST) + path + ".json";
  http.begin(cliente, url);
  http.addHeader("Content-Type", "application/json");

  int codigo;
  if (metodo == "PUT")       codigo = http.PUT(json);
  else if (metodo == "POST") codigo = http.POST(json);
  else { http.end(); return false; }

  bool ok = (codigo == 200);
  if (!ok) {
    Serial.print("   ! HTTP "); Serial.print(codigo);
    if (codigo > 0) {
      String resp = http.getString();
      Serial.print(" | "); Serial.println(resp.substring(0, 120));
    } else {
      Serial.println(" (error de transporte)");
    }
  }
  http.end();
  return ok;
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(500);

  Serial.println();
  Serial.println("==========================================");
  Serial.println("  ESP32 -> Firebase | MQ-135 (Fase 4)");
  Serial.println("==========================================");

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  conectarWiFi();
  bootMillis = millis();

  Serial.println();
  Serial.print("Calentando sensor "); Serial.print(CALENTAMIENTO_MS/1000);
  Serial.println(" s antes de empezar a publicar...");
}

// ---------- Loop ----------
void loop() {
  unsigned long ahora    = millis();
  unsigned long uptime_s = (ahora - bootMillis) / 1000;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi caído, reconectando...");
    conectarWiFi();
    delay(2000);
    return;
  }

  int adc = leerSensorPromediado();
  float vPin = (adc / 4095.0) * 3.3;
  float vAO  = vPin * 1.5;
  int   rssi = WiFi.RSSI();
  

  // Periodo de warmup: solo log, no enviamos aún
  if (ahora - bootMillis < CALENTAMIENTO_MS) {
    Serial.print("[warmup "); Serial.print((CALENTAMIENTO_MS - (ahora - bootMillis))/1000);
    Serial.print("s] ADC="); Serial.print(adc);
    Serial.print(" V_AO="); Serial.println(vAO, 2);
    delay(INTERVALO_MS);
    return;
  }

  // Construir JSON
  String json = "{";
  json += "\"adc\":"      + String(adc)        + ",";
  json += "\"voltaje\":"  + String(vAO, 3)     + ",";
  json += "\"rssi\":"     + String(rssi)       + ",";
  json += "\"uptime_s\":" + String(uptime_s)   + ",";
  json += "\"ts\":{\".sv\":\"timestamp\"}";   // <-- timestamp del servidor
  json += "}";

  Serial.print("["); Serial.print(uptime_s); Serial.print("s] ADC=");
  Serial.print(adc); Serial.print(" V_AO="); Serial.print(vAO, 2);
  Serial.print(" RSSI="); Serial.print(rssi); Serial.print(" dBm  -> ");

  digitalWrite(LED_PIN, HIGH);
  bool ok1 = enviarAFirebase("/sensores/mq135/actual",    json, "PUT");
  bool ok2 = enviarAFirebase("/sensores/mq135/historico", json, "POST");
  digitalWrite(LED_PIN, LOW);

  if (ok1 && ok2) {
    exitos++;
    Serial.print("OK ("); Serial.print(exitos); Serial.println(")");
  } else {
    fallos++;
    Serial.print("FALLO ("); Serial.print(fallos); Serial.println(")");
  }

  delay(INTERVALO_MS);
}