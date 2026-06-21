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
const char* MQTT_HOST = "homeassistant.local";

// Standard-Port vom Mosquitto Broker ist meistens 1883
const uint16_t MQTT_PORT = 1883;

// MQTT-Login
// Benutzer hast du bereits angelegt: mqtt_printer
const char* MQTT_USER     = "mqtt_printer";
const char* MQTT_PASSWORD = "DEIN_MQTT_PASSWORT";

// MQTT Client-ID.
// Sollte eindeutig im MQTT-Netz sein.
const char* MQTT_CLIENT_ID = "qr701_esp32_printer";

// ============================================================
// MQTT TOPICS
// ============================================================

// Über dieses Topic kann Home Assistant normalen Text drucken lassen
const char* TOPIC_PRINT_TEXT = "qr701/print/text";

// Testdruck auslösen
const char* TOPIC_CMD_TEST = "qr701/cmd/test";

// Papier vorschieben
const char* TOPIC_CMD_FEED = "qr701/cmd/feed";

// Drucker initialisieren
const char* TOPIC_CMD_INIT = "qr701/cmd/init";

// Statusmeldung des ESP32
const char* TOPIC_STATUS = "qr701/status";

// ============================================================
// DRUCKER / UART EINSTELLUNGEN
// ============================================================

// Deine funktionierende Verdrahtung:
// ESP32 GPIO16 = RX2
// ESP32 GPIO17 = TX2
static const int PRINTER_RX = 16;
static const int PRINTER_TX = 17;

// QR701 laut Testdruck: 9600 Baud, RS232
static const uint32_t PRINTER_BAUD = 9600;

// HardwareSerial UART2 vom ESP32 verwenden
HardwareSerial Printer(2);

// ============================================================
// WLAN / MQTT OBJEKTE
// ============================================================

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ============================================================
// HILFSFUNKTION: Text für den Drucker vereinfachen
// ============================================================
// Dein Drucker steht laut Testdruck auf PC936 / GB18030.
// Damit deutsche Umlaute nicht als Müll gedruckt werden,
// ersetzen wir sie erstmal bewusst durch ae, oe, ue, ss.
// Das ist robust und für ToDo-Listen gut lesbar.

String normalizeTextForPrinter(String text) {
  text.replace("ä", "ae");
  text.replace("ö", "oe");
  text.replace("ü", "ue");
  text.replace("Ä", "Ae");
  text.replace("Ö", "Oe");
  text.replace("Ü", "Ue");
  text.replace("ß", "ss");
  text.replace("€", "EUR");

  return text;
}

// ============================================================
// ESC/POS: Drucker initialisieren
// ============================================================
// ESC @ setzt viele Druckereinstellungen auf Standard zurück.

void printerInit() {
  Printer.write(0x1B); // ESC
  Printer.write('@');  // Initialisieren
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
// size = 0 normal
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
// Einen normalen Textblock drucken
// ============================================================

void printPlainText(String text) {
  text = normalizeTextForPrinter(text);

  printerInit();
  printerAlign(0);
  printerBold(false);
  printerTextSize(0x00);

  Printer.println(text);

  // Am Ende ein paar Zeilen vorschieben,
  // damit man den Ausdruck sauber abreißen kann.
  printerFeed(4);
}

// ============================================================
// Formatierter Testdruck
// ============================================================

void printTestPage() {
  printerInit();

  // Überschrift
  printerAlign(1);       // zentriert
  printerBold(true);     // fett
  printerTextSize(0x11); // groß
  Printer.println("QR701");
  Printer.println("MQTT Test");

  // Normaler Text
  printerTextSize(0x00);
  printerBold(false);
  Printer.println();

  printerAlign(0); // links
  Printer.println("------------------------");
  Printer.println("ESP32 + MAX3232");
  Printer.println("Home Assistant MQTT");
  Printer.println("Baudrate: 9600");
  Printer.println();

  printerBold(true);
  Printer.println("Befehle:");
  printerBold(false);

  Printer.println("qr701/print/text");
  Printer.println("qr701/cmd/test");
  Printer.println("qr701/cmd/feed");
  Printer.println("qr701/cmd/init");
  Printer.println();

  Printer.println("Umlaute:");
  Printer.println(normalizeTextForPrinter("ae oe ue ss"));
  Printer.println(normalizeTextForPrinter("ä ö ü ß"));

  printerFeed(4);
}

// ============================================================
// MQTT Status senden
// ============================================================
// retain=true sorgt dafür, dass Home Assistant den letzten Status
// auch nach Neustart noch sehen kann.

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

  // Topic auswerten
  String topicString = String(topic);

  if (topicString == TOPIC_PRINT_TEXT) {
    Serial.println("Drucke Text...");
    printPlainText(message);
    publishStatus("printed_text");
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

  // Warten bis WLAN verbunden ist
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
  // Solange versuchen, bis MQTT verbunden ist
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

      // Status online setzen
      publishStatus("online");

      // Topics abonnieren
      mqttClient.subscribe(TOPIC_PRINT_TEXT);
      mqttClient.subscribe(TOPIC_CMD_TEST);
      mqttClient.subscribe(TOPIC_CMD_FEED);
      mqttClient.subscribe(TOPIC_CMD_INIT);

      Serial.println("MQTT Topics abonniert:");
      Serial.println(TOPIC_PRINT_TEXT);
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
  // PubSubClient ist standardmäßig eher klein eingestellt.
  // 2048 reicht für einfache ToDo-Listen.
  mqttClient.setBufferSize(2048);

  // MQTT verbinden
  connectMQTT();

  Serial.println("QR701 MQTT Drucker bereit.");
}

// ============================================================
// LOOP
// Läuft dauerhaft.
// Hier wird die MQTT-Verbindung am Leben gehalten.
// ============================================================

void loop() {
  // Falls WLAN getrennt wurde, neu verbinden
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WLAN getrennt. Verbinde neu...");
    connectWiFi();
  }

  // Falls MQTT getrennt wurde, neu verbinden
  if (!mqttClient.connected()) {
    connectMQTT();
  }

  // MQTT-Nachrichten verarbeiten
  mqttClient.loop();
}
