// Sertakan library yang dibutuhkan
#include <WiFi.h>              // Library untuk koneksi WiFi di ESP32
#include <Firebase_ESP_Client.h> // Library Firebase oleh Mobizt
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"


// ----- Library untuk Sensor ADXL345 -----
#include <Adafruit_ADXL345_U.h> // Menggunakan versi Unified Sensor
#include <Adafruit_Sensor.h>
#include <Wire.h>               // Diperlukan untuk komunikasi I2C
// ---------------------------------------

// ----- GANTI DENGAN KREDENSIAL WIFI KAMU -----
#define WIFI_SSID "LAPTOP-IDEAPAD GAMMING 3"
#define WIFI_PASSWORD "manasayatau"
// ---------------------------------------------

// ----- GANTI DENGAN KREDENSIAL FIREBASE KAMU -----
#define API_KEY "AIzaSyAolV_Pu7p_-v8H__XZpL8cePWt2BWyKXc" // API Key dari Project settings > General
#define DATABASE_URL "https://integration-iot-default-rtdb.asia-southeast1.firebasedatabase.app/" // Contoh: https://nama-proyekmu.firebaseio.com/ atau https://nama-proyekmu.asia-southeast1.firebasedatabase.app/
// -------------------------------------------------

// ----- KREDENSIAL USER FIREBASE -----
#define USER_EMAIL "admin123@gmail.com"
#define USER_PASSWORD "admin123"

// ----- PATH FIREBASE YANG AKAN DIHAPUS SAAT STARTUP DAN DIGUNAKAN DI LOOP -----
const String PARENT_PATH = "data_iot_esp32";

// Objek Firebase utama
FirebaseData fbdo;
FirebaseData fbdoDelete;

// Objek untuk konfigurasi Firebase
FirebaseConfig config;

// Objek untuk autentikasi Firebase (jika diperlukan, untuk contoh ini tidak pakai user/password)
FirebaseAuth auth;

// Objek sensor ADXL345
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// Variabel untuk timing pengiriman data
unsigned long sendDataPrevMillis = 0;
unsigned int count = 0; // Penghitung data yang dikirim
bool firebaseIsReady = false;

// ----- KONFIGURASI THRESHOLD AKSELEROMETER -----
// Definisi nilai threshold dalam G. Nilai ini akan dibandingkan dengan magnitudo vektor total.
// Nilai ini bisa jadi perlu sedikit lebih tinggi dari sebelumnya, karena sekarang menghitung total guncangan.
// Mari kita mulai dengan 1.8G dan sesuaikan jika perlu.
const float ACCEL_THRESHOLD_G = 1.8; // Contoh: 1.8G
// Konversi threshold dari G ke m/s^2 untuk perbandingan
float ACCEL_THRESHOLD_MPS2 = ACCEL_THRESHOLD_G * 9.81; 

void setup() {
  Serial.begin(115200); // Mulai komunikasi serial untuk debugging
  Serial.println();
  Serial.println("Memulai Setup...");

  // 1. Inisialisasi Sensor ADXL345
  Serial.println("Menginisialisasi ADXL345...");
  if (!accel.begin()) { // Coba inisialisasi sensor
    Serial.println("Gagal mendeteksi ADXL345. Periksa koneksi!");
    while (1) { // Hentikan eksekusi jika sensor tidak ditemukan
      delay(10);
    }
  }
  Serial.println("ADXL345 Terdeteksi.");
  
  // Atur range pengukuran akselerometer
  accel.setRange(ADXL345_RANGE_4_G); 
  Serial.printf("Range sensor diatur ke +/- %dG\n", (int)pow(2.0, (float)accel.getRange())); 
  Serial.println("");

  // 2. Koneksi ke WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Terhubung dengan IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // 3. Konfigurasi Firebase
  Serial.println("Mengonfigurasi Firebase...");
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  Serial.println("Mengatur Firebase.reconnectWiFi(true)...");

  // 4. HAPUS DATA LAMA DI FIREBASE PADA PARENT_PATH
  unsigned long startWaitReady = millis();
  Serial.print("Menunggu Firebase siap...");
  while(!Firebase.ready() && (millis() - startWaitReady < 15000)) { // Tunggu maksimal 15 detik
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if(Firebase.ready()){
    firebaseIsReady = true;
    Serial.println("Firebase siap dan terautentikasi.");
    
    Serial.printf("Mencoba menghapus data lama di path Firebase: %s\n", PARENT_PATH.c_str());
    if (Firebase.RTDB.deleteNode(&fbdoDelete, PARENT_PATH)) {
      Serial.printf("Data lama di path '%s' BERHASIL dihapus.\n", PARENT_PATH.c_str());
    } else {
      Serial.printf("GAGAL menghapus data lama di path '%s'. Alasan: %s\n", fbdoDelete.errorReason().c_str());
    }
  } else {
    firebaseIsReady = false;
    Serial.println("Firebase GAGAL siap setelah menunggu. Periksa kredensial.");
    Serial.println("Program akan tetap berjalan, tapi tidak akan mengirim data.");
  }

  Serial.println("--------------------------------");
  Serial.println("Setup selesai. Masuk ke loop()...");
  Serial.println("--------------------------------");
}

void loop() {
  // Pastikan Firebase siap dan waktu pengiriman sudah tiba
  if (firebaseIsReady && (millis() - sendDataPrevMillis > 1000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();

    // Membaca data dari sensor ADXL345
    sensors_event_t event; 
    accel.getEvent(&event); // Mengambil data akselerometer

    // Data akselerometer (dalam m/s^2)
    float accelX = event.acceleration.x;
    float accelY = event.acceleration.y;
    float accelZ = event.acceleration.z;

    // =========================================================================
    // ----- LOGIKA THRESHOLD BARU (Metode Vektor Resultan) -----
    // =========================================================================
    // 1. Hitung magnitudo (panjang) dari vektor akselerasi total
    float vectorMagnitude = sqrt( (accelX * accelX) + (accelY * accelY) + (accelZ * accelZ) );

    // 2. Tentukan status threshold berdasarkan perbandingan magnitudo vektor dengan threshold
    bool isThresholdExceeded = false;
    if (vectorMagnitude > ACCEL_THRESHOLD_MPS2) {
      isThresholdExceeded = true;
      Serial.printf("THRESHOLD TERLAMPUI! Magnitudo: %.2f (Threshold: %.2f m/s^2)\n", 
                    vectorMagnitude, ACCEL_THRESHOLD_MPS2);
    } else {
      Serial.printf("THRESHOLD TIDAK TERLAMPUI. Magnitudo: %.2f (Threshold: %.2f m/s^2)\n", 
                    vectorMagnitude, ACCEL_THRESHOLD_MPS2);
    }
    // =========================================================================
    
    count++;
    String childPath = String(count);

    FirebaseJson jsonData;
    // Mengirim data akselerometer individual (tetap dikirim untuk analisis)
    jsonData.set("accelerometer/x", String(accelX)); 
    jsonData.set("accelerometer/y", String(accxelY));
    jsonData.set("accelerometer/z", String(accelZ));
    
    // >>>>> INI BAGIAN PENTING <<<<<
    // Mengirim status boolean `isThresholdExceeded` yang sekarang nilainya
    // ditentukan oleh logika Vektor Resultan.
    jsonData.set("threshold_exceeded", isThresholdExceeded); 
    
    // Mengirim informasi tambahan yang berguna
    jsonData.set("vector_magnitude_mps2", String(vectorMagnitude)); // Opsional: kirim nilai magnitudo total
    jsonData.set("threshold_value_mps2", String(ACCEL_THRESHOLD_MPS2));

    jsonData.set("device_id", "ESP32_Kamar_01_ADXL345");
    jsonData.set("timestamp/.sv", "timestamp");

    Serial.printf("Mencoba mengirim data ke Firebase: %s/%s\n", PARENT_PATH.c_str(), childPath.c_str());
    
    if (Firebase.RTDB.setJSON(&fbdo, PARENT_PATH + "/" + childPath, &jsonData)) {
      Serial.println("BERHASIL DIKIRIM.");
    } else {
      Serial.println("GAGAL DIKIRIM. ALASAN: " + fbdo.errorReason());
    }
    Serial.println("--------------------------------");

  } else if (!firebaseIsReady) {
    if (millis() - sendDataPrevMillis > 10000) { 
      sendDataPrevMillis = millis();
      Serial.println("Firebase tidak siap. Tidak dapat mengirim data. Cek setup.");
    }
  }
  delay(100);
}