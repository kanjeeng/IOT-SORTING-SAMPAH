#include <ESP8266WiFi.h> // Library untuk menghubungkan ESP8266 ke jaringan Wi-Fi
#include <PubSubClient.h> // Library untuk komunikasi MQTT
#include <Servo.h> // Library untuk mengontrol servo motor

// Wi-Fi Configuration
const char *ssid = "BIZNET"; // Nama SSID jaringan Wi-Fi
const char *password = "biznet321"; // Password jaringan Wi-Fi

// ThingsBoard Configuration
const char *mqttServer = "demo.thingsboard.io"; // Server ThingsBoard
const int mqttPort = 1883; // Port MQTT untuk komunikasi
const char *tbToken = "wahanoh21gu56eepdk9m"; // Token autentikasi ThingsBoard

WiFiClient wifiClient; // Objek untuk mengelola koneksi Wi-Fi
PubSubClient mqttClient(wifiClient); // Objek untuk mengelola komunikasi MQTT menggunakan koneksi Wi-Fi

// Pin Configuration
#define SOIL_SENSOR_PIN A0 // Pin untuk sensor kelembaban tanah
#define TRIGGER_PIN D1 // Pin Trigger untuk sensor ultrasonik
#define ECHO_PIN D2 // Pin Echo untuk sensor ultrasonik
#define SERVO_PIN D5 // Pin untuk mengontrol microservo

// Threshold Values
const int soilThreshold = 1000; // Ambang batas kelembaban tanah untuk menentukan jenis sampah
const int minDistance = 10; // Jarak minimal untuk mendeteksi sampah

// Default Maximum Counts
int maxWetCount = 20; // Jumlah maksimum untuk sampah basah
int maxDryCount = 20; // Jumlah maksimum untuk sampah kering

// Counters
int wetCount = 0; // Counter untuk sampah basah
int dryCount = 0; // Counter untuk sampah kering

// Flags for Full Status
bool wetFull = false; // Status apakah tempat sampah basah penuh
bool dryFull = false; // Status apakah tempat sampah kering penuh

// Servo Positions
const int servoWetPosition = 180; // Posisi servo untuk sampah basah
const int servoDryPosition = 0; // Posisi servo untuk sampah kering
const int servoNeutralPosition = 120; // Posisi netral servo

Servo myServo; // Objek untuk mengontrol servo motor

// Location Information
float latitude = -6.969282; // Lokasi latitude tempat sampah
float longitude = 107.6255821; // Lokasi longitude tempat sampah
const char *binName = "Trash Bin 1"; // Nama tempat sampah

// Timing for stabilization
unsigned long lastDetected = 0; // Waktu terakhir mendeteksi sampah
const unsigned long detectionInterval = 1000; // Interval stabil dalam milidetik

void sendTelemetry(const char *key, int value); // Deklarasi fungsi untuk mengirim data telemetri

// Function to Read Distance from Ultrasonic Sensor
long readUltrasonicDistance() {
  digitalWrite(TRIGGER_PIN, LOW); // Pastikan Trigger dalam keadaan LOW
  delayMicroseconds(2); // Tunggu 2 mikrodetik
  digitalWrite(TRIGGER_PIN, HIGH); // Aktifkan Trigger
  delayMicroseconds(10); // Tunggu 10 mikrodetik
  digitalWrite(TRIGGER_PIN, LOW); // Matikan Trigger
  long duration = pulseIn(ECHO_PIN, HIGH); // Hitung waktu pantulan dari sensor ultrasonik
  return duration * 0.034 / 2; // Konversi waktu menjadi jarak dalam cm
}

int readAverageSoilMoisture() {
  int sum = 0; // Variabel untuk menyimpan jumlah pembacaan
  const int readings = 5; // Jumlah pembacaan untuk rata-rata
  for (int i = 0; i < readings; i++) { // Loop untuk membaca nilai beberapa kali
    sum += analogRead(SOIL_SENSOR_PIN); // Baca nilai dari sensor kelembaban tanah
    delay(50); // Tunggu 50 milidetik antar pembacaan
  }
  return sum / readings; // Kembalikan rata-rata nilai pembacaan
}

void connectToWiFi() {
  Serial.print("Connecting to Wi-Fi..."); // Tampilkan pesan status
  WiFi.begin(ssid, password); // Mulai koneksi ke Wi-Fi
  while (WiFi.status() != WL_CONNECTED) { // Tunggu hingga perangkat terhubung
    delay(1000); // Tunggu 1 detik
    Serial.print("."); // Tampilkan titik untuk indikasi
  }
  Serial.println("\nConnected to Wi-Fi."); // Tampilkan pesan berhasil
}

void connectToThingsBoard() {
  while (!mqttClient.connected()) { // Tunggu hingga koneksi MQTT terhubung
    Serial.println("Connecting to ThingsBoard..."); // Tampilkan pesan status
    if (mqttClient.connect("ESP8266", tbToken, NULL)) { // Hubungkan ke server ThingsBoard dengan token
      Serial.println("Connected to ThingsBoard."); // Tampilkan pesan berhasil
      sendTelemetry("wet_count", wetCount); // Kirim data awal wet_count
      sendTelemetry("dry_count", dryCount); // Kirim data awal dry_count
      sendTelemetry("wet_status", 0); // Kirim status awal tempat sampah basah
      sendTelemetry("dry_status", 0); // Kirim status awal tempat sampah kering
    } else {
      Serial.println("Failed to connect. Retrying in 5 seconds..."); // Tampilkan pesan gagal
      delay(5000); // Tunggu 5 detik sebelum mencoba lagi
    }
  }
}

void sendTelemetry(const char *key, int value) {
  char payload[64]; // Buffer untuk payload JSON
  snprintf(payload, sizeof(payload), "{\"%s\": %d}", key, value); // Buat payload JSON
  mqttClient.publish("v1/devices/me/telemetry", payload); // Kirim data ke ThingsBoard
}

void sendCombinedTelemetry(float moisturePercentage, const char *status, int wetCount, int dryCount, float latitude, float longitude, const char *binName) {
  char payload[256]; // Buffer untuk payload JSON
  snprintf(payload, sizeof(payload),
           "{\"moisture_percentage\": %.2f, \"status\": \"%s\", \"wet_count\": %d, \"dry_count\": %d, \"latitude\": %.7f, \"longitude\": %.7f, \"bin_name\": \"%s\"}",
           moisturePercentage, status, wetCount, dryCount, latitude, longitude, binName); // Buat payload JSON
  mqttClient.publish("v1/devices/me/telemetry", payload); // Kirim data ke ThingsBoard
}

void scanSoilAndMoveServo() {
  if (wetFull && dryFull) { // Periksa apakah kedua tempat sampah penuh
    Serial.println("Both bins are full. Waiting for reset..."); // Tampilkan pesan status
    return; // Hentikan proses jika penuh
  }

  long distance = readUltrasonicDistance(); // Baca jarak dari sensor ultrasonik
  Serial.print("Distance: "); // Tampilkan jarak
  Serial.println(distance);

  if (distance >= 10 && distance <= 15) { // Periksa apakah jarak berada dalam jangkauan
    int soilMoistureValue = readAverageSoilMoisture(); // Baca kelembaban tanah
    float moisturePercentage = (float)(1023 - soilMoistureValue) / 1023.0 * 100.0; // Hitung persentase kelembaban

    unsigned long currentMillis = millis(); // Dapatkan waktu saat ini
    if (!wetFull && soilMoistureValue < soilThreshold) { // Periksa jika tempat sampah basah tidak penuh
      if (currentMillis - lastDetected >= detectionInterval) { // Stabilkan pendeteksian
        lastDetected = currentMillis; // Perbarui waktu terakhir
        wetCount++; // Tambahkan wetCount
        if (wetCount >= maxWetCount) { // Periksa apakah tempat sampah basah penuh
          wetFull = true; // Tandai sebagai penuh
          sendTelemetry("wet_status", 1); // Kirim status penuh ke ThingsBoard
          Serial.println("Wet bin is full."); // Tampilkan pesan status
        }
        myServo.write(servoWetPosition); // Gerakkan servo ke posisi basah
        sendCombinedTelemetry(moisturePercentage + 50, "Wet Waste", wetCount, dryCount, latitude, longitude, binName); // Kirim data ke ThingsBoard
      }
    } else if (!dryFull && soilMoistureValue >= soilThreshold) { // Periksa jika tempat sampah kering tidak penuh
      if (currentMillis - lastDetected >= detectionInterval) { // Stabilkan pendeteksian
        lastDetected = currentMillis; // Perbarui waktu terakhir
        dryCount++; // Tambahkan dryCount
        if (dryCount >= maxDryCount) { // Periksa apakah tempat sampah kering penuh
          dryFull = true; // Tandai sebagai penuh
          sendTelemetry("dry_status", 1); // Kirim status penuh ke ThingsBoard
          Serial.println("Dry bin is full."); // Tampilkan pesan status
        }
        myServo.write(servoDryPosition); // Gerakkan servo ke posisi kering
        sendCombinedTelemetry(moisturePercentage + 50, "Dry Waste", wetCount, dryCount, latitude, longitude, binName); // Kirim data ke ThingsBoard
      }
    }

    delay(3000); // Tambahkan jeda sebelum kembali ke posisi netral
    myServo.write(servoNeutralPosition); // Kembalikan servo ke posisi netral
  }
}

void setup() {
  Serial.begin(115200); // Inisialisasi komunikasi serial dengan baud rate 115200
  pinMode(TRIGGER_PIN, OUTPUT); // Atur pin Trigger sebagai output
  pinMode(ECHO_PIN, INPUT); // Atur pin Echo sebagai input
  pinMode(SOIL_SENSOR_PIN, INPUT); // Atur pin sensor kelembaban tanah sebagai input

  myServo.attach(SERVO_PIN); // Hubungkan servo ke pin yang ditentukan
  myServo.write(servoNeutralPosition); // Atur servo ke posisi netral

  connectToWiFi(); // Hubungkan ke jaringan Wi-Fi
  mqttClient.setServer(mqttServer, mqttPort); // Atur server MQTT ThingsBoard

  connectToThingsBoard(); // Hubungkan ke ThingsBoard
}

void loop() {
  connectToThingsBoard(); // Pastikan koneksi ke ThingsBoard tetap aktif
  mqttClient.loop(); // Pastikan loop MQTT tetap berjalan

  scanSoilAndMoveServo(); // Pindai sensor dan gerakkan servo sesuai kondisi
  delay(1000); // Tunggu 1 detik sebelum membaca ulang
}
