#include <Arduino.h>

// ============================================================
// QR701 / ESP32 UART-Einstellungen
// ============================================================

// Deine bisher funktionierenden Pins
static const int PRINTER_RX = 16; // ESP32 RX2
static const int PRINTER_TX = 17; // ESP32 TX2

// QR701 laut Testdruck
static const uint32_t PRINTER_BAUD = 9600;

// UART2 vom ESP32 verwenden
HardwareSerial Printer(2);

// ============================================================
// ESC/POS: Drucker initialisieren
// ============================================================
// ESC @ setzt viele Druckereinstellungen zurück.
// Danach senden wir FS . um den chinesischen/Kanji-Modus abzuschalten.
// Das ist bei deinem Test wichtig, weil der Drucker aktuell PC936/GB18030
// verwendet und sonst Umlaute-Bytes als asiatische Zeichen interpretiert.

void printerInit() {
  Printer.write(0x1B); // ESC
  Printer.write('@');  // Init
  delay(200);

  // Chinese/Kanji character mode OFF
  // ESC/POS: FS .
  Printer.write(0x1C); // FS
  Printer.write('.');  // Cancel Chinese/Kanji mode
  delay(200);
}

// ============================================================
// ESC/POS: Chinese/Kanji-Modus explizit ausschalten
// ============================================================

void printerChineseModeOff() {
  Printer.write(0x1C); // FS
  Printer.write('.');  // Chinese/Kanji mode off
  delay(200);
}

// ============================================================
// ESC/POS: Textausrichtung
// align = 0 links, 1 zentriert, 2 rechts
// ============================================================

void printerAlign(uint8_t align) {
  Printer.write(0x1B); // ESC
  Printer.write('a');
  Printer.write(align);
  delay(20);
}

// ============================================================
// ESC/POS: Fettdruck ein/aus
// ============================================================

void printerBold(bool enabled) {
  Printer.write(0x1B); // ESC
  Printer.write('E');
  Printer.write(enabled ? 1 : 0);
  delay(20);
}

// ============================================================
// ESC/POS: Schriftgröße
// size = 0x00 normal
// size = 0x11 doppelte Breite + doppelte Höhe
// ============================================================

void printerTextSize(uint8_t size) {
  Printer.write(0x1D); // GS
  Printer.write('!');
  Printer.write(size);
  delay(20);
}

// ============================================================
// ESC/POS: Papier vorschieben
// ============================================================

void printerFeed(uint8_t lines) {
  Printer.write(0x1B); // ESC
  Printer.write('d');
  Printer.write(lines);
  delay(100);
}

// ============================================================
// ESC/POS: Codepage wählen
// ============================================================
// ESC t n
//
// Die Nummern sind bei vielen ESC/POS-Druckern ähnlich,
// aber bei günstigen Modulen nicht immer identisch.
//
// Häufig:
//   0  = PC437
//   2  = CP850 / Westeuropa
//   16 = Windows-1252
//   19 = CP858 / Westeuropa mit Euro
// ============================================================

void printerCodePage(uint8_t codepage) {
  Printer.write(0x1B); // ESC
  Printer.write('t');
  Printer.write(codepage);
  delay(200);
}

// ============================================================
// ESC/POS: International Character Set wählen
// ============================================================
// ESC R n
//
// Häufig:
//   0 = USA
//   2 = Germany
//
// Für ä/ö/ü ist meistens ESC t wichtiger,
// aber ESC R Germany testen wir trotzdem.
// ============================================================

void printerInternational(uint8_t country) {
  Printer.write(0x1B); // ESC
  Printer.write('R');
  Printer.write(country);
  delay(200);
}

// ============================================================
// UTF-8 -> CP850 für deutsche Sonderzeichen
// ============================================================
// Arduino/PlatformIO speichert Quelltext normalerweise als UTF-8.
// Der Drucker erwartet nach Codepage-Umschaltung einzelne 8-Bit-Zeichen.
// Diese Funktion wandelt deutsche Umlaute in CP850-Bytes.
//
// CP850:
//   ä = 0x84
//   ö = 0x94
//   ü = 0x81
//   Ä = 0x8E
//   Ö = 0x99
//   Ü = 0x9A
//   ß = 0xE1
// ============================================================

void printerPrintCp850(const String& text) {
  for (size_t i = 0; i < text.length(); i++) {
    uint8_t c = (uint8_t)text[i];

    // Deutsche UTF-8-Zeichen beginnen hier meist mit 0xC3.
    if (c == 0xC3 && i + 1 < text.length()) {
      uint8_t next = (uint8_t)text[i + 1];

      switch (next) {
        case 0xA4: Printer.write((uint8_t)0x84); break; // ä
        case 0xB6: Printer.write((uint8_t)0x94); break; // ö
        case 0xBC: Printer.write((uint8_t)0x81); break; // ü
        case 0x84: Printer.write((uint8_t)0x8E); break; // Ä
        case 0x96: Printer.write((uint8_t)0x99); break; // Ö
        case 0x9C: Printer.write((uint8_t)0x9A); break; // Ü
        case 0x9F: Printer.write((uint8_t)0xE1); break; // ß
        default:
          Printer.write('?');
          break;
      }

      // Zweites UTF-8-Byte überspringen
      i++;
    }
    else {
      // Normale ASCII-Zeichen direkt senden
      Printer.write(c);
    }
  }
}

void printerPrintlnCp850(const String& text) {
  printerPrintCp850(text);
  Printer.println();
}

// ============================================================
// UTF-8 -> CP1252 für deutsche Sonderzeichen
// ============================================================
// Falls dein Drucker mit Windows-1252 besser funktioniert,
// testen wir diese Variante zusätzlich.
//
// CP1252 / ISO-8859-1 ähnlich:
//   ä = 0xE4
//   ö = 0xF6
//   ü = 0xFC
//   Ä = 0xC4
//   Ö = 0xD6
//   Ü = 0xDC
//   ß = 0xDF
// ============================================================

void printerPrintCp1252(const String& text) {
  for (size_t i = 0; i < text.length(); i++) {
    uint8_t c = (uint8_t)text[i];

    if (c == 0xC3 && i + 1 < text.length()) {
      uint8_t next = (uint8_t)text[i + 1];

      switch (next) {
        case 0xA4: Printer.write((uint8_t)0xE4); break; // ä
        case 0xB6: Printer.write((uint8_t)0xF6); break; // ö
        case 0xBC: Printer.write((uint8_t)0xFC); break; // ü
        case 0x84: Printer.write((uint8_t)0xC4); break; // Ä
        case 0x96: Printer.write((uint8_t)0xD6); break; // Ö
        case 0x9C: Printer.write((uint8_t)0xDC); break; // Ü
        case 0x9F: Printer.write((uint8_t)0xDF); break; // ß
        default:
          Printer.write('?');
          break;
      }

      i++;
    }
    else {
      Printer.write(c);
    }
  }
}

void printerPrintlnCp1252(const String& text) {
  printerPrintCp1252(text);
  Printer.println();
}

// ============================================================
// Rohbytes-Test CP850
// ============================================================
// Testet unabhängig vom Quelltext-Encoding, ob CP850-Bytes auf
// der gewählten Drucker-Codepage richtig aussehen.
// ============================================================

void printRawCp850Umlauts() {
  const uint8_t umlauts[] = {
    0x84, ' ', // ä
    0x94, ' ', // ö
    0x81, ' ', // ü
    0x8E, ' ', // Ä
    0x99, ' ', // Ö
    0x9A, ' ', // Ü
    0xE1,      // ß
    '\n'
  };

  Printer.write(umlauts, sizeof(umlauts));
}

// ============================================================
// Rohbytes-Test CP1252
// ============================================================

void printRawCp1252Umlauts() {
  const uint8_t umlauts[] = {
    0xE4, ' ', // ä
    0xF6, ' ', // ö
    0xFC, ' ', // ü
    0xC4, ' ', // Ä
    0xD6, ' ', // Ö
    0xDC, ' ', // Ü
    0xDF,      // ß
    '\n'
  };

  Printer.write(umlauts, sizeof(umlauts));
}

// ============================================================
// Eine Codepage-Testsektion drucken
// ============================================================

void printOneCodepageTest(uint8_t codepage, const char* name) {
  // Vor jeder Sektion sicherheitshalber Chinese Mode aus
  printerChineseModeOff();

  // Codepage setzen
  printerCodePage(codepage);

  Printer.println("------------------------");

  Printer.print("ESC t ");
  Printer.print(codepage);
  Printer.print("  ");
  Printer.println(name);

  Printer.println("ASCII:");
  Printer.println("ae oe ue ss EUR");
  Printer.println();

  Printer.println("RAW CP850:");
  printRawCp850Umlauts();

  Printer.println("UTF8->CP850:");
  printerPrintlnCp850("ä ö ü Ä Ö Ü ß");
  printerPrintlnCp850("Falsches Öl übermäßig süß");
  printerPrintlnCp850("Grüße aus dem Prüfstand");
  Printer.println();

  Printer.println("RAW CP1252:");
  printRawCp1252Umlauts();

  Printer.println("UTF8->CP1252:");
  printerPrintlnCp1252("ä ö ü Ä Ö Ü ß");
  printerPrintlnCp1252("Falsches Öl übermäßig süß");
  printerPrintlnCp1252("Grüße aus dem Prüfstand");
  Printer.println();
}

// ============================================================
// Einzeltest: Nur CP850 nach Chinese Mode Off
// ============================================================
// Dieser Block ist wichtig, weil CP850 eigentlich der Kandidat
// für deutsche Umlaute auf einfachen ESC/POS-Druckern ist.
// ============================================================

void printCp850DirectTest() {
  printerInit();
  printerInternational(2); // Germany
  printerChineseModeOff();
  printerCodePage(2);      // CP850, falls Drucker Standard-ESC/POS folgt

  printerAlign(1);
  printerBold(true);
  printerTextSize(0x11);
  Printer.println("CP850");
  Printer.println("Direkttest");

  printerTextSize(0x00);
  printerBold(false);
  Printer.println();

  printerAlign(0);
  Printer.println("Soll lesbar sein:");
  printerPrintlnCp850("ä ö ü Ä Ö Ü ß");
  printerPrintlnCp850("Falsches Öl übermäßig süß");
  printerPrintlnCp850("Grüße aus dem Prüfstand");
  Printer.println();

  Printer.println("Falls asiatisch:");
  Printer.println("Chinese Mode bleibt aktiv");
  Printer.println("oder ESC t 2 ist falsch.");
  Printer.println();

  printerFeed(3);
}

// ============================================================
// Kompletttest mehrerer Codepages
// ============================================================

void printGermanCodepageFullTest() {
  printerInit();

  // Germany als internationales Zeichenset
  printerInternational(2);

  printerAlign(1);
  printerBold(true);
  printerTextSize(0x11);
  Printer.println("QR701");
  Printer.println("Deutsch Test");

  printerTextSize(0x00);
  printerBold(false);
  Printer.println();

  printerAlign(0);
  Printer.println("Test mit Chinese Mode OFF");
  Printer.println("Suche lesbare Umlaute:");
  Printer.println("ae/oe/ue sind nur Ersatz.");
  Printer.println();

  // Mögliche Codepage-Nummern
  printOneCodepageTest(0,  "PC437?");
  printOneCodepageTest(1,  "Katakana?");
  printOneCodepageTest(2,  "CP850?");
  printOneCodepageTest(3,  "PC860?");
  printOneCodepageTest(4,  "PC863?");
  printOneCodepageTest(5,  "PC865?");
  printOneCodepageTest(6,  "WPC1251?");
  printOneCodepageTest(7,  "PC866?");
  printOneCodepageTest(8,  "MIK?");
  printOneCodepageTest(16, "WPC1252?");
  printOneCodepageTest(17, "PC866?");
  printOneCodepageTest(18, "PC852?");
  printOneCodepageTest(19, "CP858?");
  printOneCodepageTest(30, "Unknown 30?");
  printOneCodepageTest(31, "Unknown 31?");

  Printer.println("------------------------");
  Printer.println("Ende Deutsch Test");

  printerFeed(5);
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("Starte QR701 Deutsch-Test ohne MQTT...");

  // UART zum Drucker starten
  Printer.begin(PRINTER_BAUD, SERIAL_8N1, PRINTER_RX, PRINTER_TX);

  delay(1000);

  // Erst kurzer CP850-Direkttest
  Serial.println("Drucke CP850-Direkttest...");
  printCp850DirectTest();

  delay(2000);

  // Danach kompletter Codepage-Test
  Serial.println("Drucke kompletten Codepage-Test...");
  printGermanCodepageFullTest();

  Serial.println("Testdrucke gesendet.");
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  // Nichts tun.
  // Der Testdruck läuft nur einmal beim Start.
}
