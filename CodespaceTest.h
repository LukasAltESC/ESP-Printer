#include <Arduino.h>

// ============================================================
// QR701 / ESP32 UART-Einstellungen
// ============================================================

// Deine bisher funktionierenden Pins:
static const int PRINTER_RX = 16; // ESP32 RX2
static const int PRINTER_TX = 17; // ESP32 TX2

// QR701 laut Testdruck:
static const uint32_t PRINTER_BAUD = 9600;

// UART2 vom ESP32 verwenden
HardwareSerial Printer(2);

// ============================================================
// ESC/POS: Drucker initialisieren
// ============================================================
// ESC @ setzt viele Druckereinstellungen zurück.

void printerInit() {
  Printer.write(0x1B); // ESC
  Printer.write('@');  // Init
  delay(100);
}

// ============================================================
// ESC/POS: Textausrichtung
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
// ESC/POS: Schriftgröße
// size = 0x00 normal
// size = 0x11 doppelte Breite + doppelte Höhe
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
// ESC/POS: Codepage wählen
// ============================================================
// ESC t n
//
// Die Nummern sind bei vielen ESC/POS-Druckern ähnlich,
// aber bei günstigen Modulen nicht immer identisch.
// Wir testen deshalb mehrere Werte.
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
  delay(50);
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
// aber ESC R Germany schadet für den Test nicht.
// ============================================================

void printerInternational(uint8_t country) {
  Printer.write(0x1B); // ESC
  Printer.write('R');
  Printer.write(country);
  delay(50);
}

// ============================================================
// UTF-8 -> CP850 für deutsche Sonderzeichen
// ============================================================
// Arduino/PlatformIO speichert Quelltext meist als UTF-8.
// Der Drucker erwartet aber nach Codepage-Umschaltung einzelne
// 8-Bit-Zeichen. Diese Funktion wandelt die wichtigsten deutschen
// Zeichen manuell in CP850-Bytes um.
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

    // Deutsche Umlaute in UTF-8 beginnen typischerweise mit 0xC3.
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
        default:   Printer.write('?'); break;
      }

      // Zweites Byte des UTF-8-Zeichens überspringen
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
// Rohbytes-Test
// ============================================================
// Damit testen wir unabhängig vom UTF-8-Quelltext, ob die CP850-Bytes
// auf einer bestimmten Drucker-Codepage korrekt erscheinen.
// ============================================================

void printRawCp850Umlauts() {
  // ä ö ü Ä Ö Ü ß als CP850-Bytes
  const uint8_t umlauts[] = {
    0x84, ' ',
    0x94, ' ',
    0x81, ' ',
    0x8E, ' ',
    0x99, ' ',
    0x9A, ' ',
    0xE1,
    '\n'
  };

  Printer.write(umlauts, sizeof(umlauts));
}

// ============================================================
// Eine einzelne Codepage-Testsektion drucken
// ============================================================

void printOneCodepageTest(uint8_t codepage, const char* name) {
  printerCodePage(codepage);

  Printer.println("------------------------");

  Printer.print("ESC t ");
  Printer.print(codepage);
  Printer.print("  ");
  Printer.println(name);

  Printer.println("ASCII:");
  Printer.println("ae oe ue ss EUR");

  Printer.println("CP850 raw bytes:");
  printRawCp850Umlauts();

  Printer.println("UTF8 -> CP850:");
  printerPrintlnCp850("ä ö ü Ä Ö Ü ß");

  Printer.println("Satz:");
  printerPrintlnCp850("Falsches Öl übermäßig süß");
  printerPrintlnCp850("Grüße aus dem Prüfstand");
  Printer.println();
}

// ============================================================
// Komplette deutsche Testseite
// ============================================================

void printGermanCodepageTest() {
  printerInit();

  // Deutschland als internationales Zeichenset setzen
  printerInternational(2);

  // Überschrift
  printerAlign(1);
  printerBold(true);
  printerTextSize(0x11);
  Printer.println("QR701");
  Printer.println("Deutsch Test");

  printerTextSize(0x00);
  printerBold(false);
  Printer.println();

  printerAlign(0);
  Printer.println("Suche die Zeile, in der");
  Printer.println("Umlaute korrekt aussehen.");
  Printer.println();

  // Mehrere mögliche Codepages testen
  printOneCodepageTest(0,  "PC437?");
  printOneCodepageTest(1,  "Katakana?");
  printOneCodepageTest(2,  "CP850?");
  printOneCodepageTest(3,  "PC860?");
  printOneCodepageTest(4,  "PC863?");
  printOneCodepageTest(5,  "PC865?");
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
  Serial.println("Starte QR701 Deutsch-Codepage-Test...");

  // Drucker-UART starten
  Printer.begin(PRINTER_BAUD, SERIAL_8N1, PRINTER_RX, PRINTER_TX);

  delay(1000);

  printGermanCodepageTest();

  Serial.println("Testdruck gesendet.");
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  // Nichts tun.
  // Der Testdruck wird nur einmal beim Start gedruckt.
}
