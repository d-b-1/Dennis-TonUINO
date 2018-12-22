#include <DFMiniMp3.h>
#include <EEPROM.h>
#include <JC_Button.h>
#include <MFRC522.h>
#include <SPI.h>
#include <SoftwareSerial.h>

#define shutdownPin 7 // Power Switch
#define busyPin 4

// DFPlayer Mini
SoftwareSerial mySoftwareSerial(2, 3); // RX, TX
uint16_t numTracksInFolder;
uint16_t currentTrack;
uint16_t firstTrack;
uint16_t queue[255];
uint8_t volume;

//Admin-Settings
const long standbyTimer = 10; //Wert in Minuten
const uint8_t minVolume = 3;
const uint8_t maxVolume = 24;
const uint8_t initVolume = 17;

// this object stores nfc tag data
struct nfcTagObject {
  uint32_t cookie;
  uint8_t version;
  uint8_t folder;
  uint8_t mode;
  uint8_t special;
};

nfcTagObject myCard;
unsigned long sleepAtMillis = 0;

static void nextTrack(uint16_t track);
int voiceMenu(int numberOfOptions, int startMessage, int messageOffset,
              bool preview = false, int previewFromFolder = 0);

bool knownCard = false;

// implement a notification class,
// its member methods will get called
//
class Mp3Notify {
public:
  static void OnError(uint16_t errorCode) {
    // see DfMp3_Error for code meaning
    Serial.println();
    Serial.print("Com Error ");
    Serial.println(errorCode);
  }
  static void OnPlayFinished(uint16_t track) {
  //  Serial.print(F("Track beendet "));
  //  Serial.println(track);
  //  delay(100);
    nextTrack(track);
  }
  static void OnCardOnline(uint16_t code) {
    Serial.println(F("SD Karte online "));
  }
  static void OnCardInserted(uint16_t code) {
    Serial.println(F("SD Karte bereit "));
  }
  static void OnCardRemoved(uint16_t code) {
    Serial.println(F("SD Karte entfernt "));
  }
};

static DFMiniMp3<SoftwareSerial, Mp3Notify> mp3(mySoftwareSerial);

void shuffleQueue() {
  // Queue für die Zufallswiedergabe erstellen
  for (uint16_t x = 0; x < numTracksInFolder - firstTrack + 1; x++)
    queue[x] = x + firstTrack;
  // Rest mit 0 auffüllen
  for (uint16_t x = numTracksInFolder - firstTrack + 1; x < 255; x++)
    queue[x] = 0;
  // Queue mischen
  for (uint16_t i = 0; i < numTracksInFolder - firstTrack + 1; i++)
  {
    uint16_t j = random (0, numTracksInFolder - firstTrack + 1);
    uint16_t t = queue[i];
    queue[i] = queue[j];
    queue[j] = t;
  }
  Serial.println(F("Queue :"));
  for (uint16_t x = 0; x < numTracksInFolder - firstTrack + 1 ; x++)
    Serial.println(queue[x]);
}

bool isPlaying() {
  return !digitalRead(busyPin);
}

void setstandbyTimer() {
  if (standbyTimer != 0) {
    sleepAtMillis = millis() + (standbyTimer * 60000);
    Serial.println(F("Set Standby-Timer "));
  }
}

void disablestandbyTimer() {
  sleepAtMillis = 0;
  Serial.println(F("Disable Standby-Timer "));
}

void checkStandbyAtMillis() {
  if (sleepAtMillis != 0 && millis() > sleepAtMillis) {
    // enter sleep state
    Serial.println(F("Force shutdown…"));
    digitalWrite(shutdownPin, HIGH);
  }
}

// Leider kann das Modul keine Queue abspielen.
static uint16_t _lastTrackFinished;
static void nextTrack(uint16_t track) {
  if (track == _lastTrackFinished) {
    return;
   }
   _lastTrackFinished = track;
   
   if (knownCard == false)
    // Wenn eine neue Karte angelernt wird soll das Ende eines Tracks nicht
    // verarbeitet werden
    return;

  if (myCard.mode == 1) {
    Serial.println(F("Hörspielmodus ist aktiv -> keinen neuen Track spielen"));
    setstandbyTimer();
//    mp3.sleep(); // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
  }
  if (myCard.mode == 2) {
    if (currentTrack != numTracksInFolder) {
      currentTrack = currentTrack + 1;
      mp3.playFolderTrack(myCard.folder, currentTrack);
      Serial.print(F("Albummodus ist aktiv -> nächster Track: "));
      Serial.print(currentTrack);
    } else 
      //      mp3.sleep();   // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
      if (isPlaying() == false)
      setstandbyTimer();
    { }
  }
  if (myCard.mode == 3) {
    if (currentTrack != numTracksInFolder - firstTrack + 1) {
      Serial.print(F("Party -> weiter in der Queue "));
      currentTrack++;
    } else {
      Serial.println(F("Ende der Queue -> beginne von vorne"));
      currentTrack = 1;
      //// Wenn am Ende der Queue neu gemischt werden soll bitte die Zeilen wieder aktivieren
      //     Serial.println(F("Ende der Queue -> mische neu"));
      //     shuffleQueue();

      //Wiedergabe stoppen mit return; ??
    }
    Serial.println(queue[currentTrack - 1]);
    mp3.playFolderTrack(myCard.folder, queue[currentTrack - 1]);
  }
  
  if (myCard.mode == 4) {
    Serial.println(F("Einzel Modus aktiv -> Strom sparen"));
    //    mp3.sleep();      // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
    setstandbyTimer();
  }
  
  if (myCard.mode == 5) {
    if (currentTrack != numTracksInFolder) {
      currentTrack = currentTrack + 1;
      Serial.print(F("Hörbuch Modus ist aktiv -> nächster Track und "
                     "Fortschritt speichern"));
      Serial.println(currentTrack);
      mp3.playFolderTrack(myCard.folder, currentTrack);
      // Fortschritt im EEPROM abspeichern
      EEPROM.write(myCard.folder, currentTrack);
    } else {
//      mp3.sleep();  // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
      // Fortschritt zurück setzen
      EEPROM.write(myCard.folder, 1);
      setstandbyTimer();
    }
  }
}

static void previousTrack() {
  Serial.println(F("=== previousTrack()")); 
  if (myCard.mode == 1) {
    Serial.println(F("Hörspielmodus ist aktiv -> Track von vorne spielen "));
    mp3.playFolderTrack(myCard.folder, currentTrack);
  }
  
  if (myCard.mode == 2) {
    Serial.println(F("Albummodus ist aktiv -> vorheriger Track "));
    if (currentTrack != firstTrack) {
      currentTrack = currentTrack - 1;
    }
    mp3.playFolderTrack(myCard.folder, currentTrack);
  }
  
  if (myCard.mode == 3 || myCard.mode == 9) {
    if (currentTrack != 1) {
      Serial.print(F("Party Modus ist aktiv -> zurück in der Qeueue "));
      currentTrack--;
    }
    else
    {
      Serial.print(F("Anfang der Queue -> springe ans Ende "));
      currentTrack = numTracksInFolder;
    }
    Serial.println(queue[currentTrack - 1]);
    mp3.playFolderTrack(myCard.folder, queue[currentTrack - 1]);
  }
  
  if (myCard.mode == 4) {
    Serial.println(F("Einzel Modus aktiv -> Track von vorne spielen "));
    mp3.playFolderTrack(myCard.folder, currentTrack);
  }
  
  if (myCard.mode == 5) {
    Serial.println(F("Hörbuch Modus ist aktiv -> vorheriger Track und "
                     "Fortschritt speichern"));
    if (currentTrack != 1) {
      currentTrack = currentTrack - 1;
    }
    mp3.playFolderTrack(myCard.folder, currentTrack);
    // Fortschritt im EEPROM abspeichern
    EEPROM.write(myCard.folder, currentTrack);
  }
}

// MFRC522
#define RST_PIN 9                 // Configurable, see typical pin layout above
#define SS_PIN 10                 // Configurable, see typical pin layout above
MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522
MFRC522::MIFARE_Key key;
bool successRead;
byte sector = 1;
byte blockAddr = 4;
byte trailerBlock = 7;
MFRC522::StatusCode status;

#define buttonPause A0
#define buttonUp A1
#define buttonDown A2

#define LONG_PRESS 1000

//Button pauseButton(buttonPause);
Button pauseButton(buttonPause,25,false,false); //mit Pull-Down-Widerstand
Button upButton(buttonUp);
Button downButton(buttonDown);
bool ignorePauseButton = false;
bool ignoreUpButton = false;
bool ignoreDownButton = false;

void waitForTrackToFinish() {
  long currentTime = millis();
#define TIMEOUT 1000
  do {
  } while (!isPlaying() && millis() < currentTime + TIMEOUT);
  delay(1000); //in neuester Version
  do {
  delay(50); //in neuester Version
  } while (isPlaying());
}

void setup() {

  Serial.begin(115200); // Es gibt ein paar Debug Ausgaben über die serielle
                        // Schnittstelle
  randomSeed(analogRead(A7)); // Zufallsgenerator initialisieren

  Serial.println(F("TonUINO Version 2.1"));
  Serial.println(F("(c) Thorsten Voß"));
  Serial.println(F("    Dennis Bender"));
  
  // Busy Pin
  pinMode(busyPin, INPUT);

  // Power Switch
  pinMode(shutdownPin, OUTPUT);
  digitalWrite(shutdownPin, LOW);

  // activate standby timer
  setstandbyTimer();

  // DFPlayer Mini initialisieren
  mp3.begin();
  volume = initVolume;
  mp3.setVolume(volume);

  // NFC Leser initialisieren
  SPI.begin();        // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522
  mfrc522
      .PCD_DumpVersionToSerial(); // Show details of PCD - MFRC522 Card Reader
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  pinMode(buttonPause, INPUT); // mit Pull-Down-Widerstand
  pinMode(buttonUp, INPUT_PULLUP);
  pinMode(buttonDown, INPUT_PULLUP);

  // RESET --- ALLE DREI KNÖPFE BEIM STARTEN GEDRÜCKT HALTEN -> alle EINSTELLUNGEN werden gelöscht
  if (digitalRead(buttonPause) == LOW && digitalRead(buttonUp) == LOW &&
      digitalRead(buttonDown) == LOW) {
    Serial.println(F("Reset -> EEPROM wird gelöscht"));
    for (int i = 0; i < EEPROM.length(); i++) {
      EEPROM.update(i, 0);
    }
  }
  // Begruessung abspielen
  mp3.playMp3FolderTrack(500);
}

void readButtons() {
  pauseButton.read();
  upButton.read();
  downButton.read();
}

void loop() {
  do {
    mp3.loop();
    //if(!isPlaying()) {mp3.loop() ;} //Bugfix von Christopher

    //Standby-Check
    checkStandbyAtMillis();
    
    // Buttons werden nun über JS_Button gehandelt, dadurch kann jede Taste
    // doppelt belegt werden
     readButtons();

    if (pauseButton.wasReleased()) {
      if (ignorePauseButton == false)
        if (isPlaying()) {
          mp3.pause();
          setstandbyTimer();
        }
        else if (knownCard) {
          disablestandbyTimer();
          mp3.start();
        }
      ignorePauseButton = false;
    } else if (pauseButton.pressedFor(LONG_PRESS) &&
               ignorePauseButton == false) {
      if (isPlaying() && myCard.mode != 3)
        mp3.playAdvertisement(currentTrack);
      else if (isPlaying() && myCard.mode == 3)
        mp3.playAdvertisement(queue[currentTrack - 1]);
      else {
        
        // Bis bald abspielen
        mp3.playMp3FolderTrack(501);
        delay(3000);
        
        // TonUINO auschalten
        Serial.println(F("Force shutdown…"));
        digitalWrite(shutdownPin, HIGH);
      
      }
      ignorePauseButton = true;
    }

    if (upButton.pressedFor(LONG_PRESS)) {
      if (volume < maxVolume) {
        Serial.println(F("Volume Up"));
        mp3.increaseVolume();
	      volume = mp3.getVolume();
        Serial.println(volume);
      }
      ignoreUpButton = true;
    } else if (upButton.wasReleased()) {
      if (!ignoreUpButton)
        nextTrack(random(65536));
      else
        ignoreUpButton = false;
    }

    if (downButton.pressedFor(LONG_PRESS)) {
      if (volume > minVolume) {
        Serial.println(F("Volume Down"));
        mp3.decreaseVolume();
	      volume = mp3.getVolume();
        Serial.println(volume);
      }
      ignoreDownButton = true;
    } else if (downButton.wasReleased()) {
      if (!ignoreDownButton)
        previousTrack();
      else
        ignoreDownButton = false;
    }

    // Neue Löschfunktion mit Vor- und Zurücktasten
    //if (downButton.pressedFor(LONG_PRESS) || upButton.pressedFor(LONG_PRESS) && upButton.isPressed() && downButton.isPressed() && isPlaying() == false) {
    if (downButton.pressedFor(LONG_PRESS) && upButton.pressedFor(LONG_PRESS) && isPlaying() == false) {
      //do {
      //  readButtons();
      //} while (upButton.isPressed() || downButton.isPressed());
      //readButtons(); überflüssig??

      knownCard = false;
      ignoreDownButton = true;
      ignoreUpButton = true;
      mp3.playMp3FolderTrack(800);
      Serial.println(F("Karte resetten..."));
      resetCard();
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
   }
    
    // Ende der Buttons
  } while (!mfrc522.PICC_IsNewCardPresent());

  // RFID Karte wurde aufgelegt

  if (!mfrc522.PICC_ReadCardSerial())
    return;

  if (readCard(&myCard) == true) {
    // make random a little bit more "random"
    randomSeed(millis());
    
    if (myCard.cookie == 322417479 && myCard.folder != 0 && myCard.mode != 0) {

      knownCard = true;
      _lastTrackFinished = 0;
      numTracksInFolder = mp3.getFolderTrackCount(myCard.folder);
      firstTrack = 1;

      // Standby-Timer deaktivieren
      disablestandbyTimer();
      
      Serial.print(numTracksInFolder);
      Serial.print(F(" Dateien in Ordner "));
      Serial.println(myCard.folder);

      // Hörspielmodus: eine zufällige Datei aus dem Ordner
      if (myCard.mode == 1) {
        Serial.println(F("Hörspielmodus -> zufälligen Track wiedergeben"));
        currentTrack = random(1, numTracksInFolder + 1);
        Serial.println(currentTrack);
        mp3.playFolderTrack(myCard.folder, currentTrack);
      }
      
      // Album Modus: kompletten Ordner spielen
      if (myCard.mode == 2) {
        Serial.println(F("Album Modus -> kompletten Ordner wiedergeben"));
        currentTrack = 1;
        mp3.playFolderTrack(myCard.folder, currentTrack);
      }
      
      // Party Modus: Ordner in zufälliger Reihenfolge
      if (myCard.mode == 3) {
        Serial.println(
            F("Party Modus -> Ordner in zufälliger Reihenfolge wiedergeben"));
        shuffleQueue();
        currentTrack = 1;
        mp3.playFolderTrack(myCard.folder,  queue[currentTrack - 1]);
      }
      
      // Einzel Modus: eine Datei aus dem Ordner abspielen
      if (myCard.mode == 4) {
        Serial.println(
            F("Einzel Modus -> eine Datei aus dem Odrdner abspielen"));
        currentTrack = myCard.special;
        mp3.playFolderTrack(myCard.folder, currentTrack);
      }
      // Hörbuch Modus: kompletten Ordner spielen und Fortschritt merken
      if (myCard.mode == 5) {
        Serial.println(F("Hörbuch Modus -> kompletten Ordner spielen und "
                         "Fortschritt merken"));
        currentTrack = EEPROM.read(myCard.folder);
        mp3.playFolderTrack(myCard.folder, currentTrack);
      }
    }

    // Neue Karte konfigurieren
    else {
      knownCard = false;

      // Standby-Timer deaktivieren
      disablestandbyTimer();
      
      setupCard();
    }
  }
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

int voiceMenu(int numberOfOptions, int startMessage, int messageOffset,
              bool preview = false, int previewFromFolder = 0) {
  int returnValue = 0;
  if (startMessage != 0)
    mp3.playMp3FolderTrack(startMessage);
  do {
    readButtons();
    mp3.loop();
    if (pauseButton.wasPressed()) {
      if (returnValue != 0)
        return returnValue;
      delay(1000);
    }

 if (upButton.pressedFor(LONG_PRESS)) {
      returnValue = min(returnValue + 10, numberOfOptions);
      mp3.playMp3FolderTrack(messageOffset + returnValue);
      delay(1000);
      if (preview) {
        do {
          delay(10);
        } while (isPlaying());
        if (previewFromFolder == 0)
          mp3.playFolderTrack(returnValue, 1);
        else
          mp3.playFolderTrack(previewFromFolder, returnValue);
      }
      ignoreUpButton = true;
    } else if (upButton.wasReleased()) {
      if (!ignoreUpButton) {
        returnValue = min(returnValue + 1, numberOfOptions);
        mp3.playMp3FolderTrack(messageOffset + returnValue);
        delay(1000);
        if (preview) {
          do {
            delay(10);
          } while (isPlaying());
          if (previewFromFolder == 0)
            mp3.playFolderTrack(returnValue, 1);
          else
            mp3.playFolderTrack(previewFromFolder, returnValue);
        }
      } else
        ignoreUpButton = false;
    }
    
    if (downButton.pressedFor(LONG_PRESS)) {
      returnValue = max(returnValue - 10, 1);
      mp3.playMp3FolderTrack(messageOffset + returnValue);
      delay(1000);
      if (preview) {
        do {
          delay(10);
        } while (isPlaying());
        if (previewFromFolder == 0)
          mp3.playFolderTrack(returnValue, 1);
        else
          mp3.playFolderTrack(previewFromFolder, returnValue);
      }
      ignoreDownButton = true;
    } else if (downButton.wasReleased()) {
      if (!ignoreDownButton) {
        returnValue = max(returnValue - 1, 1);
        mp3.playMp3FolderTrack(messageOffset + returnValue);
        delay(1000);
        if (preview) {
          do {
            delay(10);
          } while (isPlaying());
          if (previewFromFolder == 0)
            mp3.playFolderTrack(returnValue, 1);
          else
            mp3.playFolderTrack(previewFromFolder, returnValue);
        }
      } else
        ignoreDownButton = false;
    }
  } while (true);
}

void resetCard() {
  do {
        readButtons();
  } while (upButton.isPressed() || downButton.isPressed());
  do {
    readButtons();

    if (upButton.wasReleased() || downButton.wasReleased()) {
      Serial.print(F("Abgebrochen!"));
      mp3.playMp3FolderTrack(802);
      return;
    }
  } while (!mfrc522.PICC_IsNewCardPresent());

  if (!mfrc522.PICC_ReadCardSerial())
    return;

  Serial.print(F("Karte wird neu Konfiguriert!"));
  setupCard();
}

void setupCard() {
  mp3.pause();
  Serial.print(F("Neue Karte konfigurieren"));

  // Ordner abfragen
  myCard.folder = voiceMenu(99, 300, 0, true);

  // Wiedergabemodus abfragen
  myCard.mode = voiceMenu(6, 310, 310);

  // Hörbuchmodus -> Fortschritt im EEPROM auf 1 setzen
  EEPROM.write(myCard.folder,1);

  // Einzelmodus -> Datei abfragen
  if (myCard.mode == 4)
    myCard.special = voiceMenu(mp3.getFolderTrackCount(myCard.folder), 320, 0,
                               true, myCard.folder);

  // Admin Funktionen
  if (myCard.mode == 6)
    myCard.special = voiceMenu(3, 320, 320);

  // Karte ist konfiguriert -> speichern
  mp3.pause();
    do {
  } while (isPlaying());
  writeCard(myCard);
}

bool readCard(nfcTagObject *nfcTag) {
  bool returnValue = true;
  // Show some details of the PICC (that is: the tag/card)
  Serial.print(F("Card UID:"));
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.println();
  Serial.print(F("PICC type: "));
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.println(mfrc522.PICC_GetTypeName(piccType));

  byte buffer[18];
  byte size = sizeof(buffer);

  // Authenticate using key A
  Serial.println(F("Authenticating using key A..."));
  status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    returnValue = false;
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  // Show the whole sector as it currently is
  Serial.println(F("Current data in sector:"));
  mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
  Serial.println();

  // Read data from the block
  Serial.print(F("Reading data from block "));
  Serial.print(blockAddr);
  Serial.println(F(" ..."));
  status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(blockAddr, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    returnValue = false;
    Serial.print(F("MIFARE_Read() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
  }
  Serial.print(F("Data in block "));
  Serial.print(blockAddr);
  Serial.println(F(":"));
  dump_byte_array(buffer, 16);
  Serial.println();
  Serial.println();

  uint32_t tempCookie;
  tempCookie = (uint32_t)buffer[0] << 24;
  tempCookie += (uint32_t)buffer[1] << 16;
  tempCookie += (uint32_t)buffer[2] << 8;
  tempCookie += (uint32_t)buffer[3];

  nfcTag->cookie = tempCookie;
  nfcTag->version = buffer[4];
  nfcTag->folder = buffer[5];
  nfcTag->mode = buffer[6];
  nfcTag->special = buffer[7];

  return returnValue;
}

void writeCard(nfcTagObject nfcTag) {
  MFRC522::PICC_Type mifareType;
  byte buffer[16] = {0x13, 0x37, 0xb3, 0x47, // 0x1337 0xb347 magic cookie to
                                             // identify our nfc tags
                     0x01,                   // version 1
                     nfcTag.folder,          // the folder picked by the user
                     nfcTag.mode,    // the playback mode picked by the user
                     nfcTag.special, // track or function for admin cards
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  byte size = sizeof(buffer);

  mifareType = mfrc522.PICC_GetType(mfrc522.uid.sak);

  // Authenticate using key B
  Serial.println(F("Authenticating again using key B..."));
  status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_B, trailerBlock, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    mp3.playMp3FolderTrack(401);
    return;
  }

  // Write data to the block
  Serial.print(F("Writing data into block "));
  Serial.print(blockAddr);
  Serial.println(F(" ..."));
  dump_byte_array(buffer, 16);
  Serial.println();
  status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(blockAddr, buffer, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("MIFARE_Write() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
      mp3.playMp3FolderTrack(401);
  }
  else
    mp3.playMp3FolderTrack(400);
  Serial.println();
  delay(100);
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}
