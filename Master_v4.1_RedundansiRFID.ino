#include <Wire.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>

#define MY_ADDRESS     41
#define SLAVE_ADDRESS  42

#define MASTER_POLLING_INTERVAL       500  // in milliseconds

#define EEPROM_ADDR_RECORD_COUNTER    0
#define EEPROM_ADDR_IS_MASTER_EXIST   1
#define EEPROM_ADDR_MASTER_UID_START  2
#define EEPROM_ADDR_RECORD_UID_START  6

#define RST_PIN     9
#define SS_PIN      10
#define RELAY_1   A0
#define RELAY_2   A1

MFRC522 mfrc522(SS_PIN, RST_PIN);

byte readCard[4], masterCard[4], storedCard[4];
boolean programMode = false;
boolean isRfidError = false;

void setup() {
  Serial.begin(9600);
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  while (!Serial);

  SPI.begin();
  mfrc522.PCD_Init();
  isRfidError = mfrc522.PCD_DumpVersionToSerial();

  while(isRfidError);
  
  Serial.println();

  if (EEPROM.read(EEPROM_ADDR_IS_MASTER_EXIST) != 1) {
    Serial.println(F("No master card defined"));
    Serial.println(F("Scan a PICC to be defined as the master card"));

    while (!getID());
    for (int i = 0; i < 4; i++) {
      EEPROM.write(EEPROM_ADDR_MASTER_UID_START + i, readCard[i]);
    }
    EEPROM.write(EEPROM_ADDR_IS_MASTER_EXIST, 1);
    Serial.println(F("Master Card Defined"));
  }

  Serial.print(F("UID of Master Card = "));
  for ( int i = 0; i < 4; i++ ) {
    masterCard[i] = EEPROM.read(EEPROM_ADDR_MASTER_UID_START + i);
    Serial.print(masterCard[i], HEX);
  }
  Serial.println();
  printAllRecord();
  Serial.println();

  Serial.println(F("Scan PICC to read the UID\n"));

  Wire.begin(MY_ADDRESS);
  Wire.onReceive(receiveEvent);
}

void receiveEvent() {
  unsigned long taskStart;
  
  Serial.println(F("Resyncing record"));

  taskStart = micros();  
  int i = 0, addr = EEPROM_ADDR_RECORD_UID_START;

  clearAllRecord();
  
  while (Wire.available()) {
    if (i == 0) {
      EEPROM.write(EEPROM_ADDR_RECORD_COUNTER, Wire.read());
      i++;
    } else {
      byte b = Wire.read();
      EEPROM.write(addr, b);
      addr++;
    }
  }

  printAllRecord();
  Serial.print("Resync done in ");
  Serial.print(micros()-taskStart);
  Serial.println(" microsecon");
  Serial.println();
}

void clearAllRecord() {
  for (int i = EEPROM_ADDR_RECORD_UID_START; i <= getLastAddr(); i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.write(EEPROM_ADDR_RECORD_COUNTER, 0);
}

void loop() {
  static unsigned long last = millis();
  if (millis() - last > MASTER_POLLING_INTERVAL) {
    last = millis();
    sendCmd(2, masterCard);
  }

  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  Serial.print(F("UID = "));
  for (int i = 0; i < 4; i++) {
    readCard[i] = mfrc522.uid.uidByte[i];
    Serial.print(readCard[i], HEX);
  }
  Serial.println();

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  if (programMode) {
    if (isMaster(readCard)) {
      Serial.println(F("This is Master Card"));
      Serial.println(F("Exiting Program Mode\n"));
      programMode = false;
      return;
    } else {
      if (findID(readCard)) {
        Serial.println(F("I know this PICC, removing..."));
        deleteID(readCard);
      } else {
        Serial.println(F("I don't know this PICC, adding..."));
        writeID(readCard);
      }
    }
  } else {
    if (isMaster(readCard)) {
      Serial.println(F("Entering Program Mode"));
      programMode = true;

      int count = EEPROM.read(EEPROM_ADDR_RECORD_COUNTER);
      Serial.print(F("There are "));
      Serial.print(count);
      Serial.println(F(" record(s) on EEPROM"));
      
      printAllRecord();

      Serial.println(F("\nScan PICC to ADD or REMOVE\n"));
    } else {
      if (findID(readCard)) {
        Serial.println(F("Acces Granted\n"));
        digitalWrite (RELAY_1, LOW);
        digitalWrite (RELAY_2, LOW);
      } else {
        Serial.println(F("Acces Denied\n"));
        digitalWrite (RELAY_1, HIGH);
        digitalWrite (RELAY_2, HIGH);
      }
    }
  }
}

boolean getID() {
  if (!mfrc522.PICC_IsNewCardPresent()) return false;
  if (!mfrc522.PICC_ReadCardSerial()) return false;

  Serial.print(F("UID = "));
  for (int i = 0; i < 4; i++) {
    readCard[i] = mfrc522.uid.uidByte[i];
    Serial.print(readCard[i], HEX);
  }
  Serial.println();

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  return true;
}

boolean isMaster (byte test[]) {
  return compareID(test, masterCard);
}

boolean compareID (byte a[], byte b[]) {
  boolean match = true;
  
  for (int i = 0; i < 4; i++ ) {
    if (a[i] != b[i]) {
      match = false;
      break;
    }
  }

  return match;
}

boolean findID (byte id[]) {
  int count = EEPROM.read(EEPROM_ADDR_RECORD_COUNTER);
  
  for (int i = 1; i <= count; i++) {
    readID(i);
    if (compareID(id, storedCard)) {
      return true;
      break;
    }
  }

  return false;
}

void readID(int n) {
  int start = (n * 4 ) + 2;
  for (int i = 0; i < 4; i++) {
    storedCard[i] = EEPROM.read(start + i);
  }
}

void writeID(byte id[]) {
  int n = EEPROM.read(EEPROM_ADDR_RECORD_COUNTER);

  int start = (n * 4) + 6;
  
  n++;
  EEPROM.write(EEPROM_ADDR_RECORD_COUNTER, n);

  for (int i = 0; i < 4; i++) {
    EEPROM.write(start + i, id[i]);
  }

  Serial.println(F("Succesfully added new record"));
  printAllRecord();
  Serial.println();

  sendCmd(0, id);
}

void deleteID(byte id[]) {
  int n = EEPROM.read(EEPROM_ADDR_RECORD_COUNTER);

  int slot = findSlotID(id);
  int start = (slot * 4) + 2;
  int looping = (n - slot) * 4;

  n--;
  EEPROM.write(EEPROM_ADDR_RECORD_COUNTER, n);

  int i;
  for (i = 0; i < looping; i++) {
    EEPROM.write(start + i, EEPROM.read(start + 4 + i));
  }
  for (int j = 0; j < 4; j++) {
    EEPROM.write(start + i + j, 0);
  }

  Serial.println(F("Succesfully removed record"));
  printAllRecord();
  Serial.println();

  sendCmd(1, id);
}

void sendCmd(byte cmd, byte id[]) {
  Wire.beginTransmission(SLAVE_ADDRESS);
  Wire.write(cmd);
  for (int i = 0; i < 4; i++) {
    Wire.write(id[i]);
  }
  Wire.endTransmission();
}

int findSlotID(byte id[]) {
  int n = EEPROM.read(EEPROM_ADDR_RECORD_COUNTER);

  for (int i = 1; i <= n; i++) {
    readID(i);
    if (compareID(id, storedCard)) {
      return i;
      break;
    }
  }
}

void printAllRecord() {
  int n = EEPROM.read(EEPROM_ADDR_RECORD_COUNTER);

  Serial.print(F("Current record(s) = "));

  if (n > 0) {
    for (int i = 1; i <= n; i++) {
      readID(i);
      for (int j = 0; j < 4; j++) {
        Serial.print(storedCard[j], HEX);
      }
      if ((i + 1) <= n) Serial.print(F(", "));
      else Serial.print(F("."));
    }
    Serial.println();
  } else {
    Serial.println(F("N/A."));
  }
}

int getLastAddr() {
  int n = EEPROM.read(EEPROM_ADDR_RECORD_COUNTER);
  int last;

  if (n > 0) {
    for (int i = 1; i <= n; i++) {
      int start = (n * 4 ) + 2;
      for (int j = 0; j < 4; j++) {
        last = start + j;
      }
    }

    return last;
  } else {
    return EEPROM_ADDR_RECORD_UID_START - 1;
  }
}
