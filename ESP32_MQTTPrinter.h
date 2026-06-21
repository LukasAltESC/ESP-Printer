#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
// BENUTZER-EINSTELLUNGEN
// Diese Werte bitte an dein Netzwerk und Home Assistant anpassen
// ============================================================

// WLAN-Zugangsdaten
const char* WIFI_SSID     = "DEINE_WLAN_SSID";
const char* WIFI_PASSWORD = "DEIN_WLAN_PASSWORT";

// MQTT-Broker
// Bei dir wahrscheinlich: homeassistant.local
// Falls es Probleme gibt, hier die feste IP von Home Assistant eintragen.
const char* MQTT_HOST = "homeassistant.local";

// Standard-Port vom Mosquitto Broker ist meistens 1883
const uint16_t MQTT_PORT = 1883;

// MQTT-Login
const char* MQTT_USER     = "mqtt_printer";
const char* MQTT_PASSWORD = "DEIN_MQTT_PASSWORT";

// MQTT Client-ID.
// Muss im MQTT-Netz eindeutig sein.
const char* MQTT_CLIENT_ID = "qr701_esp32_printer";

// ============================================================
// MQTT TOPICS
// ============================================================

// Normalen Text drucken
const char* TOPIC_PRINT_TEXT = "qr701/print/text";

// Testdruck auslösen
const char* TOPIC_CMD_TEST = "qr701/cmd/test";

// Papier vorschieben
const char* TOPIC_CMD_FEED = "qr701/cmd/feed";

// Drucker initialisieren
const char* TOPIC_CMD_INIT = "qr701/cmd/init";

// Daily-ToDo formatiert drucken
// Payload ist normaler mehrzeiliger Text.
const char* TOPIC_PRINT_TODO = "qr701/print/todo";

// Statusmeldung des ESP32
const char* TOPIC_STATUS = "qr701/status";

// ============================================================
// DRUCKER / UART EINSTELLUNGEN
// ============================================================

// Deine funktionierende Verdrahtung:
static const int PRINTER_RX = 16; // ESP32 RX2
static const int PRINTER_TX = 17; // ESP32 TX2

// QR701 laut Testdruck: 9600 Baud
static const uint32_t PRINTER_BAUD = 9600;

// UART2 vom ESP32 verwenden
HardwareSerial Printer(2);

// ============================================================
// WLAN / MQTT OBJEKTE
// ============================================================

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ============================================================
// Text für den QR701 vereinfachen
// ============================================================
// Dein Drucker bleibt offenbar im PC936 / GB18030-Modus.
// Damit deutsche Zeichen nicht als asiatische Zeichen gedruckt werden,
// ersetzen wir Umlaute durch ASCII-Schreibweisen.
//
// Beispiel:
//   Öl prüfen  ->  Oel pruefen
//   Grüße      ->  Gruesse
// ============================================================

String normalizeTextForPrinter(String text) {
  text.replace("ä", "ae");
  text.replace("ö", "oe");
  text.replace("ü", "ue");

  text.replace("Ä", "Ae");
  text.replace("Ö", "Oe");
  text.replace("Ü", "Ue");

  text.replace("ß", "ss");
  text.replace("€", "EUR");

  // Typografische Zeichen aus Notion/Smartphones entschärfen
  text.replace("–", "-");
  text.replace("—", "-");
  text.replace("„", "\"");
  text.replace("“", "\"");
  text.replace("”", "\"");
  text.replace("’", "'");
  text.replace("‘", "'");
  text.replace("…", "...");

  return text;
}

// ============================================================
// ESC/POS: Drucker initialisieren
// ============================================================
// ESC @ setzt viele Druckereinstellungen zurück.
// Danach arbeiten wir bewusst mit einfachem ASCII-Text.
// ============================================================

void printerInit() {
  Printer.write(0x1B); // ESC
  Printer.write('@');  // Init
  delay(100);
}

// ============================================================
// ESC/POS: Ausrichtung setzen
// align = 0 links, 1 zentriert, 2 rechts
// ============================================================

void printerAlign(uint8_t align) {
  Printer.write(0x1B); // ESC
  Printer.write('a');
  Printer.write(align);
}

// ============================================================
// ESC/POS: Fettdruck ein/aus
// ============================================================

void printerBold(bool enabled) {
  Printer.write(0x1B); // ESC
  Printer.write('E');
  Printer.write(enabled ? 1 : 0);
}

// ============================================================
// ESC/POS: Schriftgröße setzen
// size = 0x00 normal
// size = 0x11 doppelte Breite und doppelte Höhe
// ============================================================

void printerTextSize(uint8_t size) {
  Printer.write(0x1D); // GS
  Printer.write('!');
  Printer.write(size);
}

// ============================================================
// ESC/POS: Papier vorschieben
// ============================================================

void printerFeed(uint8_t lines) {
  Printer.write(0x1B); // ESC
  Printer.write('d');
  Printer.write(lines);
}

// ============================================================
// Hilfsfunktion: Trennlinie drucken
// ============================================================
// Bei 58-mm-Druckern sind je nach Font etwa 32 Zeichen pro Zeile gut.
// ============================================================

void printerLine() {
  Printer.println("--------------------------------");
}

// ============================================================
// Hilfsfunktion: Normalisierten Text drucken
// ============================================================

void printerPrintNormalized(String text) {
  text = normalizeTextForPrinter(text);
  Printer.print(text);
}

void printerPrintlnNormalized(String text) {
  text = normalizeTextForPrinter(text);
  Printer.println(text);
}

// ============================================================
// Einen normalen Textblock drucken
// ============================================================

void printPlainText(String text) {
  printerInit();

  printerAlign(0);
  printerBold(false);
  printerTextSize(0x00);

  text = normalizeTextForPrinter(text);

  Printer.println(text);

  // Am Ende ein paar Zeilen vorschieben,
  // damit man den Ausdruck sauber abreißen kann.
  printerFeed(4);
}

// ============================================================
// Daily-ToDo formatiert drucken
// ============================================================
// Payload-Beispiel per MQTT:
//
// Einkaufen
// Ölstand prüfen
// E-Mails beantworten
//
// Der ESP32 macht daraus:
//
// DAILY TODO
// 21.06.2026 wird später von Home Assistant im Text mitgegeben
// --------------------------------
// [ ] Einkaufen
// [ ] Oelstand pruefen
// [ ] E-Mails beantworten
// ============================================================

void printTodoList(String payload) {
  payload = normalizeTextForPrinter(payload);

  printerInit();

  // Kopf
  printerAlign(1);
  printerBold(true);
  printerTextSize(0x11);
  Printer.println("DAILY");
  Printer.println("TODO");

  printerTextSize(0x00);
  printerBold(false);
  Printer.println();

  printerAlign(0);
  printerLine();

  // Jede Zeile aus dem Payload als ToDo-Zeile drucken.
  // Leere Zeilen werden ignoriert.
  int start = 0;

  while (start < payload.length()) {
    int end = payload.indexOf('\n', start);

    if (end == -1) {
      end = payload.length();
    }

    String line = payload.substring(start, end);
    line.trim();

    if (line.length() > 0) {
      // Falls Home Assistant schon Checkboxen mitsendet, nicht doppeln.
      if (line.startsWith("[ ]") || line.startsWith("[x]") || line.startsWith("[X]")) {
        Printer.println(line);
      } else {
        Printer.print("[ ] ");
        Printer.println(line);
      }
    }

    start = end + 1;
  }

  printerLine();
  Printer.println();

  printerAlign(1);
  Printer.println("Gedruckt via Home Assistant");

  printerFeed(4);
}

// ============================================================
// Formatierter Testdruck
// ============================================================

void printTestPage() {
  printerInit();

  // Überschrift
  printerAlign(1);
  printerBold(true);
  printerTextSize(0x11);
  Printer.println("QR701");
  Printer.println("MQTT Test");

  printerTextSize(0x00);
  printerBold(false);
  Printer.println();

  printerAlign(0);
  printerLine();
  Printer.println("ESP32 + MAX3232");
  Printer.println("Home Assistant MQTT");
  Printer.println("Baudrate: 9600");
  printerLine();
  Printer.println();

  printerBold(true);
  Printer.println("Topics:");
  printerBold(false);

  Printer.println("qr701/print/text");
  Printer.println("qr701/print/todo");
  Printer.println("qr701/cmd/test");
  Printer.println("qr701/cmd/feed");
  Printer.println("qr701/cmd/init");
  Printer.println();

  printerBold(true);
  Printer.println("Umlaut-Ersatz:");
  printerBold(false);

  printerPrintlnNormalized("ä -> ae, ö -> oe, ü -> ue");
  printerPrintlnNormalized("Ä -> Ae, Ö -> Oe, Ü -> Ue");
  printerPrintlnNormalized("ß -> ss, € -> EUR");
  Printer.println();

  printerPrintlnNormalized("Beispiel: Öl prüfen");
  printerPrintlnNormalized("Beispiel: Grüße vom Prüfstand");

  printerFeed(4);
}

// ============================================================
// MQTT Status senden
// ============================================================
// retain=true sorgt dafür, dass Home Assistant den letzten Status
// auch nach Neustart noch sehen kann.
// ============================================================

void publishStatus(const char* statusText) {
  mqttClient.publish(TOPIC_STATUS, statusText, true);
}

// ============================================================
// MQTT Callback
// Diese Funktion wird automatisch aufgerufen,
// wenn eine abonnierte MQTT-Nachricht ankommt.
// ============================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Payload in einen String umwandeln
  String message;
  message.reserve(length + 1);

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.println();
  Serial.print("MQTT Nachricht empfangen auf Topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(message);

  String topicString = String(topic);

  if (topicString == TOPIC_PRINT_TEXT) {
    Serial.println("Drucke normalen Text...");
    printPlainText(message);
    publishStatus("printed_text");
  }
  else if (topicString == TOPIC_PRINT_TODO) {
    Serial.println("Drucke Daily-ToDo...");
    printTodoList(message);
    publishStatus("printed_todo");
  }
  else if (topicString == TOPIC_CMD_TEST) {
    Serial.println("Drucke Testseite...");
    printTestPage();
    publishStatus("printed_test");
  }
  else if (topicString == TOPIC_CMD_FEED) {
    Serial.println("Schiebe Papier vor...");

    // Optional kann man als Payload die Anzahl Zeilen senden.
    // Beispiel: Payload "5" schiebt 5 Zeilen vor.
    int lines = message.toInt();

    if (lines <= 0) {
      lines = 4;
    }

    if (lines > 20) {
      lines = 20;
    }

    printerFeed((uint8_t)lines);
    publishStatus("feed_done");
  }
  else if (topicString == TOPIC_CMD_INIT) {
    Serial.println("Initialisiere Drucker...");
    printerInit();
    publishStatus("printer_initialized");
  }
}

// ============================================================
// WLAN verbinden
// ============================================================

void connectWiFi() {
  Serial.println();
  Serial.print("Verbinde mit WLAN: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WLAN verbunden.");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());
}

// ============================================================
// MQTT verbinden
// ============================================================

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.println();
    Serial.print("Verbinde mit MQTT Broker: ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    // Last Will:
    // Falls der ESP32 hart ausfällt, setzt der Broker den Status auf offline.
    bool connected = mqttClient.connect(
      MQTT_CLIENT_ID,
      MQTT_USER,
      MQTT_PASSWORD,
      TOPIC_STATUS,
      0,
      true,
      "offline"
    );

    if (connected) {
      Serial.println("MQTT verbunden.");

      publishStatus("online");

      mqttClient.subscribe(TOPIC_PRINT_TEXT);
      mqttClient.subscribe(TOPIC_PRINT_TODO);
      mqttClient.subscribe(TOPIC_CMD_TEST);
      mqttClient.subscribe(TOPIC_CMD_FEED);
      mqttClient.subscribe(TOPIC_CMD_INIT);

      Serial.println("MQTT Topics abonniert:");
      Serial.println(TOPIC_PRINT_TEXT);
      Serial.println(TOPIC_PRINT_TODO);
      Serial.println(TOPIC_CMD_TEST);
      Serial.println(TOPIC_CMD_FEED);
      Serial.println(TOPIC_CMD_INIT);
    }
    else {
      Serial.print("MQTT Verbindung fehlgeschlagen, rc=");
      Serial.println(mqttClient.state());
      Serial.println("Neuer Versuch in 5 Sekunden...");
      delay(5000);
    }
  }
}

// ============================================================
// SETUP
// Wird einmal beim Start ausgeführt
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Starte QR701 MQTT Drucker...");

  // Drucker-UART starten
  Printer.begin(PRINTER_BAUD, SERIAL_8N1, PRINTER_RX, PRINTER_TX);

  // Drucker einmal initialisieren
  printerInit();

  // WLAN verbinden
  connectWiFi();

  // MQTT vorbereiten
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // Größere MQTT-Nachrichten erlauben.
  // 2048 reicht für normale ToDo-Listen.
  mqttClient.setBufferSize(2048);

  // MQTT verbinden
  connectMQTT();

  Serial.println("QR701 MQTT Drucker bereit.");
}

// ============================================================
// LOOP
// Läuft dauerhaft.
// Hier wird WLAN/MQTT am Leben gehalten.
// ============================================================

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WLAN getrennt. Verbinde neu...");
    connectWiFi();
  }

  if (!mqttClient.connected()) {
    connectMQTT();
  }

  mqttClient.loop();
}
