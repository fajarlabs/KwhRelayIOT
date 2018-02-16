#include <stdint.h> 
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <SimpleTimer.h>
#include "DHT.h"
#include "EmonLib.h"                   // Include Emon Library

EnergyMonitor emon1;

SimpleTimer timer, readSite, readBox, timerPiR;

/**
 * ARDUINO LINUX (LUBUNTU)
 * chmod linux usb -> sudo chmod a+rw /dev/ttyUSB0 
 * 
 * Mengirim data ke url tersebut ke server
 * Hasil dari settingan char ID & PIN 
 * contoh update state saklar manual -> http://smartcontrol.diftatechindo.com/sensor-B-4-1
 * contoh update arus http://smartcontrol.diftatechindo.com/pemakaian-B-1-0.76-arus
 */
 
/* Setting WIFI */
WiFiClient client;
const char* ssid   = "Smart Control";
const char* pass   = "sherlock2018";
const char* server = "smartcontrol.diftatechindo.com";

/* General Settings ID alat */
char ID           = 'B';                                //default A ==>> A=ruangan 1, B=ruangan 2, C=ruangan 3
unsigned char PIN = 2;                                  //default 1 ==>> dibatasi 1-255

/* lama waktu pir */
long pirSleep     = 600000; /* miliseconds, defaultnya 600000 */

String subSite    = "/value-" + String(ID) + ".json";   //default data tipe parsingJSON
String datSite    = "value" + String(ID);               //replace with A,B,C whenever you changes the ID
boolean runningC  = false;                             //mode mouse, change true to use BOX's Program
boolean flagPIR   = true;
boolean flagHour  = false;

#define pinPIR   D6
#define pinSUHU  D5
#define pinRelay D2
#define DHTPIN   D6 
#define DHTTYPE  DHT11

float U, I, P, arus;

unsigned long previousMillis = 0;
const long interval = 1000;
int count;
boolean postToWeb = false;
int state = 0;

bool startShutdownTimer     = false;
unsigned long long onMillis = 0;

int pirVal   = 1;
int stateVal = 0;

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  
  Serial.begin(115200);
  pinMode(16, OUTPUT);

  emon1.current(A0, 60);  
  dht.begin();
  
  timer.setInterval(0,    counter);
  readSite.setInterval(0, readData);
  timerPiR.setInterval(pirSleep, shutdownTimer); /* 600000 is 10 minutes */
  
  pinMode(pinPIR,       INPUT);
  pinMode(pinRelay,     OUTPUT);

  Serial.print("Connecting");
  
  /* username dan password agar terhubung ke WIFI */
  WiFi.begin(ssid, pass); 
  /* jika belum terhubung ke WIFI loop dan keluarkan titik titik animasi loading */
  while (WiFi.status() != WL_CONNECTED) { 
    Serial.print(".");
    delay(500);
  }

  /* cetak IP address lokal dari WIFI */
  Serial.print(", IP address: ");
  Serial.println(WiFi.localIP());  //Print the local IP

  ESP.wdtDisable();
  ESP.wdtEnable(WDTO_8S);

  /* Nyalakan led builtin */
  digitalWrite(16, HIGH);

  /* tunggu delay selama 1 detik */
  delay(1000);
}

void loop() {
  
  /* prepare koneksi ke server dengan metode GET dan port 80 */
  ESP.wdtFeed();
  client.connect(server, 80);

  /** 
   * baca jam server
   * bisa jadi ini tidak sinkron dengan kondisi letak geografis
   * jam tiap-tiap lokasi dengan server 
   */
  checkHourServer();

  if(flagHour == true) {
    /* baca sensor pir */
    readPIR();
  }
    
  /* baca arus dari sensor arus */
  readarus();

  /* cek relay timer saklar */
  readSite.run();
  /* jalankan timer post data */
  timer.run();

  /* bersihkan serial */
  clearSerial();
}//end loop

/**
 * Untuk membersihkan buffer serial
 */
void clearSerial(){
  while(Serial.available()){
    Serial.read();
  }
  Serial.flush();
}

/**
 * Fungsi untuk membuat jeda -+ 1 menit untuk mengirim data
 */
void counter(){
  postData();

  /* untuk membuat flag jika 60 detik atau lebih dari 60 detik maka kirim data ke server */
  unsigned long currentMillis = millis();

  /* waktur sekarang - waktu sebelumnya >= 1 detik */
  if (currentMillis - previousMillis >= interval) {
    /* update waktu sebelumnya */
    previousMillis  = currentMillis;
    /* buat flag hitungan +1 */
    count++;
    /* jika hitung flag == 0 */
    if(count == 60){
      /* setujui untuk kirim data */
      postToWeb = true;
      /* reset ke 0 */
      count = 0;
    }

    /* jika lebih dari 60 */
    if(count > 60){
      /* reset flag 0 */
      count = 0;
      /* stop kirim data */
      postToWeb = false;
    }
    
  }
}

/**
  * baca sensor DHT11 dan mengembalikan data
  * dalam bentuk FLOAT
  */
float readDHT11() {
  float result = 0;
  //float h = dht.readHumidity();
  float t = dht.readTemperature();

  Serial.println("Suhu "+String(t));
  
  if (isnan(t)) {
    result = 0;
  } else {
    result = t;
  }

  return result;
}

/**
 * fungsi untuk membaca sensor arus 
 */
void readarus(){ 
  
  double Irms = emon1.calcIrms(1480);  // Calculate Irms only
 
  Serial.print(Irms*230.0);            // Apparent power
  Serial.print(" ");
  Serial.println(Irms);                // Irms
  arus = Irms;
}

/**
 * Fungsi untuk membaca sensor PIR
 */
void readPIR(){
  pirVal = digitalRead(pinPIR);

  /* debug */
  Serial.println(String("PIR: ") + pirVal);
  Serial.println();

  /* debug */
  /* jika sudah direlease, hapus tag komentar */

  if(pirVal == 0) {
    timerPiR.run();
  } else {
    flagPIR = true;
  }
}

/**
 * Fungsi untuk mematikan saklar jika tidak ada objek
 * yang bergerak dan tidak ditankap oleh sensor PIR
 */
void shutdownTimer() {
    Serial.print("Shutdown (s): ");
    Serial.println(millis() / 1000);
    digitalWrite(pinRelay, LOW);  //relay active low
    flagPIR = false;
}

/**
 * Fungsi untuk mengirimkan data status pemakain
 * dalam bentuk data arus dan diolah di sisi server
 */
void postData() {
  String sub = "/pemakaian-";
  
  if(postToWeb == true){
    if(WiFi.status()== WL_CONNECTED){   //Check WiFi connection status
      String reqHeaderarus = "GET " + sub + ID + "-" + PIN + "-"+ arus + "-arus" + " HTTP/1.1\r\n" + "Host: " + server + "\r\n" + "Connection: keep-alive\r\n\r\n";
      Serial.println(reqHeaderarus);
      client.println(reqHeaderarus);
      client.println("Host: " + String(server));
      client.println("Connection: close");
      client.println();
      client.stop();
      
      delay(100);
      client.connect(server, 80);
      String reqHeaderSuhu = "GET " + sub + ID + "-" + PIN + "-"+ readDHT11() + "-Celcius" + " HTTP/1.1\r\n" + "Host: " + server + "\r\n" + "Connection: keep-alive\r\n\r\n";
      Serial.println(reqHeaderSuhu);
      client.println(reqHeaderSuhu);
      client.println("Host: " + String(server));
      client.println("Connection: close");
      client.println();
      client.stop();
      Serial.println("Data Sent...");
      postToWeb = false;
    }
    
  }
}

/**
 * Fungsi untuk memeriksa jam dari server
 * untuk acuan mengaktifkan sensor PIR dari
 * jam 7 pagi sampai dengan jam 5 sore WIB
 */
void checkHourServer() {
    if(WiFi.status()== WL_CONNECTED){   //Check WiFi connection status
      HTTPClient http;
      http.begin("http://smartcontrol.diftatechindo.com/gethours");
      int httpCode = http.GET();
 
      if (httpCode > 0) { 
        int rangeInHours = http.getString().toInt();   
        /**
         * batas waktu hidup sensor pir 
         * dari jam 7 pagi sampai jam 5 sore
         */
        if((rangeInHours > 6) && (rangeInHours < 18)) {
          flagHour = true;
        } else {
          flagHour = false;
        }
       
      }
 
      http.end(); 
    }
}

/**
 * Fungsi untuk membaca data dari status saklar
 * yang berada di aplikasi website
 */
void readData(){

  /* jika kondisi WIFI sudah terhubung */
  if(WiFi.status()== WL_CONNECTED){   //Check WiFi connection status
    HTTPClient  http;
    String      payload;

    /* baca status relay */
    String reqRelay = "http://smartcontrol.diftatechindo.com" + subSite;
    Serial.println(reqRelay);
    http.begin(reqRelay);
    http.addHeader("Content-Type", "text/plain");
    
    int httpCode  = http.POST("NODE!");   //Send the request
    payload       = http.getString(); 

    StaticJsonBuffer<300> jsonBuff;
    JsonObject& jsonData = jsonBuff.parseObject(payload);

    if(jsonData.success()) {
      String jsonVal = jsonData[datSite][PIN-1];
      Serial.println(payload);
  
      // jika status PIR hidup maka aktifkan
      if(flagPIR == true) {
        if(jsonData.success() && jsonVal.toInt() == 0){
          stateVal = 0;
          digitalWrite(16, HIGH);
          digitalWrite(pinRelay, LOW);
        }
        if(jsonData.success() && jsonVal.toInt() == 1){
          stateVal = 1;
          digitalWrite(16, LOW);
          digitalWrite(pinRelay, HIGH);
        }
      }
    }

    http.end();
  }
}

/**
 * fungsi untuk merubah posisi switch yang ada diserver
 * dengan parameter 0 untuk mematikan dan 1 untuk menghidupkan
 */
void saklar(int state) {
  String sub = "/sensor-";
  if(WiFi.status()== WL_CONNECTED){   //Check WiFi connection status
    String reqSuhu = "GET " + sub + ID + "-" + PIN + "-"+ state + " HTTP/1.1\r\n" + "Host: " + server + "\r\n" + "Connection: keep-alive\r\n\r\n";
    Serial.println(reqSuhu);
    client.println(reqSuhu);
    client.println("Host: " + String(server));
    client.println("Connection: close");
    client.println();
    client.stop();
  }
}

