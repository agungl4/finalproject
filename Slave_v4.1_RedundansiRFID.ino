#include <Wire.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>

#define MY_ADDRESS         42
#define MASTER_ADDRESS     41

#define MASTER_TIMEOUT                1500  // in milliseconds

#define EEPROM_ADDR_RECORD_COUNTER    0
#define EEPROM_ADDR_IS_MASTER_EXIST   1
#define EEPROM_ADDR_MASTER_UID_START  2

#define RST_PIN     9
#define SS_PIN      10
#define RELAY_PIN      A0

MFRC522 mfrc522(SS_PIN, RST_PIN);

byte readCard[4], masterCard[4], storedCard[4];
boolean programMode = false, oldMasterStatus = false, newMasterStatus = false;
unsigned long lastDing;

void setup() {
  Serial.begin(9600);
  pinMode(RELAY_PIN, OUTPUT);
  while (!Serial);

  SPI.begin();
  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();
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

void receiveEvent (int howMany) {
  unsigned long taskStart;
  byte incoming[5];
  int i = 0;

  taskStart = micros();

  while (Wire.available()) {
    incoming[i] = Wire.read();
    i++;
  }

  byte uid[4];
  for (int i = 1; i < 5; i++) {
    uid[i-1] = incoming[i];
  }

  byte cmd[] = {0, 1, 2};
  Serial.print(F("Command received: "));
  if (incoming[0] == cmd[0]) {
    Serial.print(F("PUT "));
    for (int i = 0; i < 4; i++) Serial.print(uid[i], HEX);
    Serial.println();
    if (!findID(uid)) writeID(uid);
  } else if (incoming[0] == cmd[1]) {
    Serial.print(F("DELETE "));
    for (int i = 0; i < 4; i++) Serial.print(uid[i], HEX);
    Serial.println();
    deleteID(uid);
  } else if (incoming[0] == cmd[2]) {
    Serial.print(F("POLLING "));
    for (int i = 0; i < 4; i++) Serial.print(uid[i], HEX);
    Serial.println();
    lastDing = millis();
  }

  if (incoming[0] != cmd[2]) {
    Serial.print("Task done in ");
    Serial.print(micros()-taskStart);
    Serial.println(" microsecon");
  }
}

void loop() {
  if (millis() - lastDing > MASTER_TIMEOUT) newMasterStatus = LOW;
  else newMasterStatus = HIGH;

  if (newMasterStatus != oldMasterStatus) {
    Serial.print(F("Master Status: "));
    if (newMasterStatus) {
      Serial.println(F("ONLINE"));
      digitalWrite (RELAY_PIN, LOW);
      sendAllRecord();
    }
    else Serial.println(F("OFFLINE"));
      digitalWrite (RELAY_PIN, HIGH);

    oldMasterStatus = newMasterStatus;
  }

  if(!newMasterStatus) {
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
          //SolenoidState = !SolenoidState; 
          digitalWrite (RELAY_PIN, LOW);
        } else {
          Serial.println(F("Acces Denied\n"));
          digitalWrite (RELAY_PIN, HIGH);
        }
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

void sendAllRecord() {
  int n = EEPROM.read(EEPROM_ADDR_RECORD_COUNTER);
  
  Wire.beginTransmission(MASTER_ADDRESS);
  Wire.write((byte) n);
  for (int i = 1; i <= n; i++) {
    readID(i);
    for (int j = 0; j < 4; j++) {
      Wire.write(storedCard[j]);
    }
  }
  Wire.endTransmission();
}

