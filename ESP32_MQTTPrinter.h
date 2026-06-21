#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

// ============================================================
// BENUTZER-EINSTELLUNGEN
// ============================================================

// WLAN-Zugangsdaten
const char* WIFI_SSID     = "DEINE_WLAN_SSID";
const char* WIFI_PASSWORD = "DEIN_WLAN_PASSWORT";

// Netzwerkname des ESP32
// Bindestriche sind besser als Unterstriche.
const char* DEVICE_HOSTNAME = "ESP32-QR701-Printer";

// MQTT-Broker
// Bei dir wahrscheinlich: homeassistant.local
// Falls es Probleme gibt, hier die feste IP von Home Assistant eintragen.
const char* MQTT_HOST = "homeassistant.local";

// Standard-Port Mosquitto
const uint16_t MQTT_PORT = 1883;

// MQTT-Login
const char* MQTT_USER     = "mqtt_printer";
const char* MQTT_PASSWORD = "DEIN_MQTT_PASSWORT";

// MQTT Client-ID
const char* MQTT_CLIENT_ID = "ESP32-QR701-Printer";

// ============================================================
// MQTT TOPICS
// ============================================================

const char* TOPIC_PRINT_TEXT      = "qr701/print/text";
const char* TOPIC_PRINT_FORMATTED = "qr701/print/formatted";
const char* TOPIC_PRINT_TODO      = "qr701/print/todo";

const char* TOPIC_CMD_TEST = "qr701/cmd/test";
const char* TOPIC_CMD_FEED = "qr701/cmd/feed";
const char* TOPIC_CMD_INIT = "qr701/cmd/init";

const char* TOPIC_STATUS = "qr701/status";

// ============================================================
// DRUCKER / UART EINSTELLUNGEN
// ============================================================

// Deine funktionierende Verdrahtung
static const int PRINTER_RX = 16;
static const int PRINTER_TX = 17;

// QR701 laut Testdruck
static const uint32_t PRINTER_BAUD = 9600;

// 58-mm-Drucker, normaler Font: ca. 32 Zeichen pro Zeile
static const int PRINT_WIDTH = 32;

// Kleine originale Checkbox
const char* TODO_CHECKBOX = "[ ] ";

// UART2 vom ESP32
HardwareSerial Printer(2);

// ============================================================
// WLAN / MQTT OBJEKTE
// ============================================================

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ============================================================
// TEXT NORMALISIEREN
// ============================================================
// Der QR701 bleibt offenbar in PC936 / GB18030.
// Deshalb ersetzen wir problematische Zeichen durch ASCII.

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
// ESC/POS BASISFUNKTIONEN
// ============================================================

void printerInit() {
  Printer.write(0x1B); // ESC
  Printer.write('@');  // Init
  delay(100);
}

void printerAlign(uint8_t align) {
  // 0 links, 1 zentriert, 2 rechts
  Printer.write(0x1B);
  Printer.write('a');
  Printer.write(align);
}

void printerBold(bool enabled) {
  Printer.write(0x1B);
  Printer.write('E');
  Printer.write(enabled ? 1 : 0);
}

void printerTextSize(uint8_t size) {
  // 0x00 normal
  // 0x11 doppelte Breite + doppelte Höhe
  Printer.write(0x1D);
  Printer.write('!');
  Printer.write(size);
}

void printerFeed(uint8_t lines) {
  Printer.write(0x1B);
  Printer.write('d');
  Printer.write(lines);
}

void printerLine() {
  Printer.println("--------------------------------");
}

void printerPrintNormalized(String text) {
  text = normalizeTextForPrinter(text);
  Printer.print(text);
}

void printerPrintlnNormalized(String text) {
  text = normalizeTextForPrinter(text);
  Printer.println(text);
}

// ============================================================
// ZEIT / DATUM
// ============================================================

void setupTime() {
  Serial.println("Synchronisiere Uhrzeit per NTP...");

  // Deutschland: automatische Sommer-/Winterzeit
  configTzTime(
    "CET-1CEST,M3.5.0/2,M10.5.0/3",
    "pool.ntp.org",
    "time.nist.gov"
  );

  struct tm timeinfo;

  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&timeinfo)) {
      Serial.println("Uhrzeit synchronisiert.");
      Serial.printf(
        "Aktuelle Zeit: %02d.%02d.%04d %02d:%02d:%02d\n",
        timeinfo.tm_mday,
        timeinfo.tm_mon + 1,
        timeinfo.tm_year + 1900,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
      );
      return;
    }

    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Warnung: Uhrzeit konnte nicht synchronisiert werden.");
}

String getDateTimeString() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    return "Datum/Zeit unbekannt";
  }

  char buffer[32];

  snprintf(
    buffer,
    sizeof(buffer),
    "%02d.%02d.%04d  %02d:%02d",
    timeinfo.tm_mday,
    timeinfo.tm_mon + 1,
    timeinfo.tm_year + 1900,
    timeinfo.tm_hour,
    timeinfo.tm_min
  );

  return String(buffer);
}

String getGermanDateTimeLine() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    return "Datum/Zeit unbekannt";
  }

  const char* weekdays[] = {
    "Sonntag",
    "Montag",
    "Dienstag",
    "Mittwoch",
    "Donnerstag",
    "Freitag",
    "Samstag"
  };

  char buffer[64];

  snprintf(
    buffer,
    sizeof(buffer),
    "%s, den %02d.%02d.%02d %02d:%02d Uhr",
    weekdays[timeinfo.tm_wday],
    timeinfo.tm_mday,
    timeinfo.tm_mon + 1,
    (timeinfo.tm_year + 1900) % 100,
    timeinfo.tm_hour,
    timeinfo.tm_min
  );

  return String(buffer);
}

// ============================================================
// NORMALER TEXTDRUCK
// ============================================================

void printPlainText(String text) {
  printerInit();

  printerAlign(0);
  printerBold(false);
  printerTextSize(0x00);

  text = normalizeTextForPrinter(text);
  Printer.println(text);

  printerFeed(4);
}

// ============================================================
// FORMATIERTER TEXTDRUCK
// ============================================================
// Unterstützte Steuerbefehle im Payload:
//
// #LEFT
// #CENTER
// #RIGHT
// #BOLD_ON
// #BOLD_OFF
// #SIZE_NORMAL
// #SIZE_BIG
// #LINE
// #DATETIME
// #FEED 4
//
// Alles andere wird normal gedruckt.

void executeFormattedCommand(String line) {
  line.trim();

  if (line == "#LEFT") {
    printerAlign(0);
  }
  else if (line == "#CENTER") {
    printerAlign(1);
  }
  else if (line == "#RIGHT") {
    printerAlign(2);
  }
  else if (line == "#BOLD_ON") {
    printerBold(true);
  }
  else if (line == "#BOLD_OFF") {
    printerBold(false);
  }
  else if (line == "#SIZE_NORMAL") {
    printerTextSize(0x00);
  }
  else if (line == "#SIZE_BIG") {
    printerTextSize(0x11);
  }
  else if (line == "#LINE") {
    printerLine();
  }
  else if (line == "#DATETIME") {
    Printer.println(getGermanDateTimeLine());
  }
  else if (line.startsWith("#FEED")) {
    int spacePos = line.indexOf(' ');
    int lines = 4;

    if (spacePos > 0) {
      lines = line.substring(spacePos + 1).toInt();
    }

    if (lines <= 0) {
      lines = 4;
    }

    if (lines > 20) {
      lines = 20;
    }

    printerFeed((uint8_t)lines);
  }
}

void printFormattedText(String payload) {
  payload = normalizeTextForPrinter(payload);

  printerInit();

  printerAlign(0);
  printerBold(false);
  printerTextSize(0x00);

  int start = 0;

  while (start < payload.length()) {
    int end = payload.indexOf('\n', start);

    if (end == -1) {
      end = payload.length();
    }

    String line = payload.substring(start, end);
    line.trim();

    if (line.startsWith("#")) {
      executeFormattedCommand(line);
    } else {
      Printer.println(line);
    }

    start = end + 1;
  }

  printerTextSize(0x00);
  printerBold(false);
  printerAlign(0);

  printerFeed(4);
}

// ============================================================
// TODO-DRUCK
// ============================================================
// Payload-Beispiel:
//
// #TITLE Daily ToDo
// #PERSON Lukas Müller
// Aufgabe 1
// [x] Aufgabe 2
// Aufgabe 3
//
// Format:
// Daily ToDo groß, zentriert, fett
// Person klein, zentriert, fett
// Datum/Uhrzeit klein, zentriert
// Aufgaben klein, links, nicht fett

void printTodoList(String payload) {
  payload = normalizeTextForPrinter(payload);

  String title = "Daily ToDo";
  String person = "Lukas Mueller";
  String lines = "";

  int start = 0;

  while (start < payload.length()) {
    int end = payload.indexOf('\n', start);

    if (end == -1) {
      end = payload.length();
    }

    String line = payload.substring(start, end);
    line.trim();

    if (line.startsWith("#TITLE ")) {
      title = line.substring(7);
      title.trim();
    }
    else if (line.startsWith("#PERSON ")) {
      person = line.substring(8);
      person.trim();
    }
    else if (line.length() > 0) {
      lines += line;
      lines += "\n";
    }

    start = end + 1;
  }

  printerInit();

  // ------------------------------------------------------------
  // Kopf: Daily ToDo groß, zentriert, fett
  // ------------------------------------------------------------
  printerAlign(1);
  printerBold(true);
  printerTextSize(0x11);
  Printer.println(title);

  // ------------------------------------------------------------
  // Name: klein, zentriert, fett
  // ------------------------------------------------------------
  printerTextSize(0x00);
  printerBold(true);
  printerAlign(1);
  Printer.println(person);

  // ------------------------------------------------------------
  // Datum/Uhrzeit: klein, zentriert, nicht fett
  // ------------------------------------------------------------
  printerTextSize(0x00);
  printerBold(false);
  printerAlign(1);
  Printer.println(getGermanDateTimeLine());
  Printer.println();

  // ------------------------------------------------------------
  // ToDo-Liste: klein, links, nicht fett
  // ------------------------------------------------------------
  printerTextSize(0x00);
  printerBold(false);
  printerAlign(0);
  printerLine();

  start = 0;

  while (start < lines.length()) {
    int end = lines.indexOf('\n', start);

    if (end == -1) {
      end = lines.length();
    }

    String line = lines.substring(start, end);
    line.trim();

    if (line.length() > 0) {
      if (
        line.startsWith("[ ]") ||
        line.startsWith("[x]") ||
        line.startsWith("[X]")
      ) {
        Printer.println(line);
      } else {
        Printer.print(TODO_CHECKBOX);
        Printer.println(line);
      }
    }

    start = end + 1;
  }

  printerLine();
  Printer.println();

  // Fußzeile
  printerTextSize(0x00);
  printerBold(false);
  printerAlign(1);
  Printer.println("Gedruckt via Home Assistant");

  // Am Ende sicher wieder auf Standard
  printerTextSize(0x00);
  printerBold(false);
  printerAlign(0);

  printerFeed(4);
}

// ============================================================
// TESTDRUCK
// ============================================================

void printTestPage() {
  printerInit();

  printerAlign(1);
  printerBold(true);
  printerTextSize(0x11);
  Printer.println("QR701");
  Printer.println("MQTT Test");

  printerTextSize(0x00);
  printerBold(false);
  Printer.println();

  Printer.println(getGermanDateTimeLine());
  Printer.println();

  printerAlign(0);
  printerLine();
  Printer.println("ESP32 + MAX3232");
  Printer.println("Home Assistant MQTT");
  Printer.println("Baudrate: 9600");
  Printer.print("Hostname: ");
  Printer.println(DEVICE_HOSTNAME);
  printerLine();
  Printer.println();

  printerBold(true);
  Printer.println("Topics:");
  printerBold(false);

  Printer.println("qr701/print/text");
  Printer.println("qr701/print/formatted");
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

  printerBold(true);
  Printer.println("Checkbox:");
  printerBold(false);
  Printer.print(TODO_CHECKBOX);
  printerPrintlnNormalized("Ölstand prüfen");

  printerFeed(4);
}

// ============================================================
// MQTT STATUS
// ============================================================

void publishStatus(const char* statusText) {
  mqttClient.publish(TOPIC_STATUS, statusText, true);
}

// ============================================================
// MQTT CALLBACK
// ============================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
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
  else if (topicString == TOPIC_PRINT_FORMATTED) {
    Serial.println("Drucke formatierten Text...");
    printFormattedText(message);
    publishStatus("printed_formatted");
  }
  else if (topicString == TOPIC_PRINT_TODO) {
    Serial.println("Drucke ToDo-Liste...");
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
// WLAN VERBINDEN
// ============================================================

void connectWiFi() {
  Serial.println();
  Serial.print("Setze Hostname: ");
  Serial.println(DEVICE_HOSTNAME);

  WiFi.mode(WIFI_STA);

  // Hostname muss vor WiFi.begin() gesetzt werden.
  WiFi.setHostname(DEVICE_HOSTNAME);

  Serial.print("Verbinde mit WLAN: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WLAN verbunden.");
  Serial.print("Hostname: ");
  Serial.println(WiFi.getHostname());
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());
}

// ============================================================
// MQTT VERBINDEN
// ============================================================

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.println();
    Serial.print("Verbinde mit MQTT Broker: ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.println(MQTT_PORT);

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
      mqttClient.subscribe(TOPIC_PRINT_FORMATTED);
      mqttClient.subscribe(TOPIC_PRINT_TODO);

      mqttClient.subscribe(TOPIC_CMD_TEST);
      mqttClient.subscribe(TOPIC_CMD_FEED);
      mqttClient.subscribe(TOPIC_CMD_INIT);

      Serial.println("MQTT Topics abonniert:");
      Serial.println(TOPIC_PRINT_TEXT);
      Serial.println(TOPIC_PRINT_FORMATTED);
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
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Starte QR701 MQTT Drucker...");

  Printer.begin(PRINTER_BAUD, SERIAL_8N1, PRINTER_RX, PRINTER_TX);

  printerInit();

  connectWiFi();
  setupTime();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // Für normale Listen ausreichend.
  // Bei sehr langen Listen später erhöhen.
  mqttClient.setBufferSize(2048);

  connectMQTT();

  Serial.println("QR701 MQTT Drucker bereit.");
}

// ============================================================
// LOOP
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
