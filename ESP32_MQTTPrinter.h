#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
// BENUTZER-EINSTELLUNGEN
// ============================================================

// WLAN-Zugangsdaten
const char* WIFI_SSID     = "DEINE_WLAN_SSID";
const char* WIFI_PASSWORD = "DEIN_WLAN_PASSWORT";

// Netzwerkname des ESP32
const char* DEVICE_HOSTNAME = "ESP32-QR701-Printer";

// MQTT-Broker
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

// ToDo-Zeilenabstand.
// Standard ist ungefähr 30. 60 ist etwa doppelt so luftig.
static const uint8_t TODO_LINE_SPACING = 60;

// Der Text soll bei Zeilenumbruch nicht unter dem Kästchen starten.
// Das echte Kästchen ist ungefähr 2 Zeichen breit plus Leerzeichen.
static const int TODO_TEXT_START_COL = 3;

// Fallback-Checkbox, falls echtes Kästchen später nicht passt
const char* TODO_CHECKBOX_FALLBACK = "[ ] ";

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

  // CR entfernen, LF behalten
  text.replace("\r", "");

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

void printerLineSpacing(uint8_t spacing) {
  // ESC 3 n = Zeilenabstand setzen
  Printer.write(0x1B);
  Printer.write('3');
  Printer.write(spacing);
}

void printerLineSpacingDefault() {
  // ESC 2 = Standard-Zeilenabstand
  Printer.write(0x1B);
  Printer.write('2');
}

void printerResetTextStyle() {
  printerTextSize(0x00);
  printerBold(false);
  printerAlign(0);
}

void printerLine() {
  Printer.println("--------------------------------");
}

void printerPrintSpaces(uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    Printer.print(" ");
  }
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
// NORMALER TEXTDRUCK
// ============================================================

void printPlainText(String text) {
  printerInit();
  printerResetTextStyle();
  printerLineSpacingDefault();

  text = normalizeTextForPrinter(text);
  Printer.println(text);

  printerResetTextStyle();
  printerLineSpacingDefault();
  printerFeed(4);
}

// ============================================================
// FORMATIERTER TEXTDRUCK
// ============================================================
// Unterstützte Steuerbefehle:
//
// #LEFT
// #CENTER
// #RIGHT
// #BOLD_ON
// #BOLD_OFF
// #SIZE_NORMAL
// #SIZE_BIG
// #LINE
// #FEED 4
// #LINE_SPACING 60
// #LINE_SPACING_DEFAULT

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
  else if (line.startsWith("#LINE_SPACING")) {
    int spacePos = line.indexOf(' ');
    int spacing = 30;

    if (spacePos > 0) {
      spacing = line.substring(spacePos + 1).toInt();
    }

    if (spacing < 16) {
      spacing = 16;
    }

    if (spacing > 100) {
      spacing = 100;
    }

    printerLineSpacing((uint8_t)spacing);
  }
  else if (line == "#LINE_SPACING_DEFAULT") {
    printerLineSpacingDefault();
  }
}

void printFormattedText(String payload) {
  payload = normalizeTextForPrinter(payload);

  printerInit();
  printerResetTextStyle();
  printerLineSpacingDefault();

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

  printerResetTextStyle();
  printerLineSpacingDefault();
  printerFeed(4);
}

// ============================================================
// TODO-CHECKBOX UND ZEILENUMBRUCH
// ============================================================

void printerTodoCheckbox(bool done) {
  if (done) {
    // Erledigte Aufgaben erstmal sicher als [x].
    // Ein echtes abgehaktes Kästchen können wir später grafisch machen.
    Printer.print("[x] ");
  } else {
    // Versuch: echtes leeres Kästchen "□" über GB2312/GBK/PC936.
    // Falls dein QR701 hier etwas Falsches druckt, stellen wir auf Fallback um.
    Printer.write((uint8_t)0xA1);
    Printer.write((uint8_t)0xF5);
    Printer.print(" ");
  }
}

void printWrappedTodoItem(String line) {
  bool done = false;

  line.trim();

  if (line.startsWith("[x]") || line.startsWith("[X]")) {
    done = true;
    line = line.substring(3);
    line.trim();
  }
  else if (line.startsWith("[ ]")) {
    done = false;
    line = line.substring(3);
    line.trim();
  }

  if (line.length() == 0) {
    printerTodoCheckbox(done);
    Printer.println();
    return;
  }

  const int textWidth = PRINT_WIDTH - TODO_TEXT_START_COL;
  bool firstLine = true;

  while (line.length() > 0) {
    String part;

    if (line.length() <= textWidth) {
      part = line;
      line = "";
    } else {
      int breakPos = -1;

      // Möglichst am Leerzeichen umbrechen
      for (int i = textWidth; i >= 6; i--) {
        if (line.charAt(i) == ' ') {
          breakPos = i;
          break;
        }
      }

      // Falls kein Leerzeichen gefunden wurde, hart umbrechen
      if (breakPos == -1) {
        breakPos = textWidth;
      }

      part = line.substring(0, breakPos);
      line = line.substring(breakPos);
      line.trim();
    }

    if (firstLine) {
      printerTodoCheckbox(done);
      Printer.println(part);
      firstLine = false;
    } else {
      printerPrintSpaces(TODO_TEXT_START_COL);
      Printer.println(part);
    }
  }
}

// ============================================================
// TODO-DRUCK
// ============================================================
// Payload-Beispiel:
//
// #TITLE Daily ToDo
// #PERSON Lukas Müller
// #DATE_LINE Montag, den 22.06.26
// Aufgabe 1
// [x] Aufgabe 2
// Aufgabe 3

void printTodoList(String payload) {
  payload = normalizeTextForPrinter(payload);

  String title = "Daily ToDo";
  String person = "Lukas Mueller";
  String dateLine = "";
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
    else if (line.startsWith("#DATE_LINE ")) {
      dateLine = line.substring(11);
      dateLine.trim();
    }
    else if (line.length() > 0) {
      lines += line;
      lines += "\n";
    }

    start = end + 1;
  }

  printerInit();
  printerLineSpacingDefault();

  // ------------------------------------------------------------
  // Kopf: Daily ToDo groß, zentriert, fett
  // ------------------------------------------------------------
  printerAlign(1);
  printerBold(true);
  printerTextSize(0x11);
  Printer.println(title);

  // Nach Überschrift sicher zurück auf normal
  printerTextSize(0x00);
  printerBold(false);
  Printer.println();

  // ------------------------------------------------------------
  // Name: klein, zentriert, fett
  // ------------------------------------------------------------
  printerAlign(1);
  printerTextSize(0x00);
  printerBold(true);
  Printer.println(person);

  // ------------------------------------------------------------
  // Listen-Datum: klein, zentriert, nicht fett
  // ------------------------------------------------------------
  printerTextSize(0x00);
  printerBold(false);
  printerAlign(1);

  if (dateLine.length() > 0) {
    Printer.println(dateLine);
  }

  Printer.println();

  // ------------------------------------------------------------
  // ToDo-Liste: normal, links, großer Zeilenabstand
  // ------------------------------------------------------------
  printerResetTextStyle();
  printerLineSpacing(TODO_LINE_SPACING);

  start = 0;

  while (start < lines.length()) {
    int end = lines.indexOf('\n', start);

    if (end == -1) {
      end = lines.length();
    }

    String line = lines.substring(start, end);
    line.trim();

    if (line.length() > 0) {
      printerResetTextStyle();
      printerLineSpacing(TODO_LINE_SPACING);
      printWrappedTodoItem(line);
    }

    start = end + 1;
  }

  // Am Ende wieder Standard setzen
  printerLineSpacingDefault();
  printerResetTextStyle();

  // Keine untere Linie, kein Footer.
  // Nur Papier vorschieben.
  printerFeed(4);
}

// ============================================================
// TESTDRUCK
// ============================================================

void printTestPage() {
  printerInit();
  printerLineSpacingDefault();

  printerAlign(1);
  printerBold(true);
  printerTextSize(0x11);
  Printer.println("QR701");
  Printer.println("MQTT Test");

  printerTextSize(0x00);
  printerBold(false);
  Printer.println();

  printerResetTextStyle();
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
  Printer.println("ToDo-Test:");
  printerBold(false);

  printerLineSpacing(TODO_LINE_SPACING);
  printWrappedTodoItem("Ölstand prüfen");
  printWrappedTodoItem("Das ist eine sehr lange Aufgabe die automatisch umbrechen soll");
  printWrappedTodoItem("[x] Diese Aufgabe ist erledigt");

  printerLineSpacingDefault();
  printerResetTextStyle();
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
  Serial.print("Payload:");
  Serial.println();
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
    printerResetTextStyle();
    printerLineSpacingDefault();
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
  printerResetTextStyle();
  printerLineSpacingDefault();

  connectWiFi();

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
