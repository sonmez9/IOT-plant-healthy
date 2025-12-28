#include <Arduino.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <DHT.h>

// --- AYARLAR ---
const char* ssid = "iPhone";           // Hotspot Adın
const char* password = "sonmez7830";   // Şifren
String serverName = "172.20.10.2";     // IP Adresini Kontrol Et!
int serverPort = 5000;

// --- PIN TANIMLARI ---
#define PIN_DHT 14      // DHT11
#define PIN_LDR 12      // LDR (Işık)
#define PIN_SOIL 13     // Toprak Sensörü
#define PIN_BUTTON 15   // Buton (GPIO 15)
#define FLASH_PIN 4     

#define DHTTYPE DHT11
DHT dht(PIN_DHT, DHTTYPE);

// Kamera Pinleri (AI Thinker)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WiFiClient client;

void sendPhotoAndData() {
  camera_fb_t * fb = esp_camera_fb_get();
  if(!fb) {
    Serial.println("Kamera hatasi: Goruntu alinamadi");
    return;
  }

  Serial.println("Servera baglaniyor...");
  if (client.connect(serverName.c_str(), serverPort)) {
    Serial.println("Baglandi! Veriler paketleniyor...");

    // 1. SENSÖRLERİ OKU
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    int ldrState = digitalRead(PIN_LDR); 
    int soilState = digitalRead(PIN_SOIL);
    
    // --- SERİ PORT EKRANINA YAZDIR ---
    Serial.println("----------------------------------------");
    Serial.print("🌡️ Sicaklik: "); Serial.print(temp); Serial.println(" °C");
    Serial.print("💧 Nem: %"); Serial.println(hum);
    
    Serial.print("💡 Isik Durumu: ");
    if(ldrState == 1) Serial.println("AYDINLIK (Gunesli)");
    else Serial.println("KARANLIK (Gece/Golge)");

    Serial.print("🌱 Toprak Durumu: ");
    if(soilState == 0) Serial.println("ISLAK (Yeterli)"); 
    else Serial.println("KURU (Su Verilmeli!)");
    Serial.println("----------------------------------------");

    // Değerleri Metne Çevir
    String s_temp = isnan(temp) ? "0" : String(temp);
    String s_hum = isnan(hum) ? "0" : String(hum);
    String s_ldr = (ldrState == 1) ? "Aydinlik" : "Karanlik";
    String s_soil = (soilState == 0) ? "Islak" : "Kuru"; 

    // 2. HTTP POST HAZIRLIĞI
    String boundary = "------------------------Boundary1234567890";
    String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--" + boundary + "--\r\n";
    
    String extraData = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"temperature\"\r\n\r\n" + s_temp + "\r\n";
    extraData += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"humidity\"\r\n\r\n" + s_hum + "\r\n";
    extraData += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"light_status\"\r\n\r\n" + s_ldr + "\r\n";
    extraData += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"soil_moisture\"\r\n\r\n" + s_soil + "\r\n";

    uint32_t imageLen = fb->len;
    uint32_t extraLen = extraData.length();
    uint32_t totalLen = imageLen + head.length() + tail.length() + extraLen;

    client.println("POST /predict HTTP/1.1");
    client.println("Host: " + serverName);
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println();
    client.print(head);
    
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n=0; n<fbLen; n=n+1024) {
      if (n+1024 < fbLen) {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      } else if (fbLen%1024>0) {
        size_t remainder = fbLen%1024;
        client.write(fbBuf, remainder);
      }
    }
    
    client.print("\r\n");
    client.print(extraData);
    client.print(tail);

    Serial.println("Yanit bekleniyor...");
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break;
    }
    String responseBody = client.readString();
    Serial.println("SERVER YANITI: " + responseBody);

    client.stop();
  } else {
    Serial.println("HATA: Servera baglanilamadi. IP adresini kontrol et!");
  }
  
  esp_camera_fb_return(fb);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);
  
  // Pin Modları
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_SOIL, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP); // Düğme için dahili direnç
  pinMode(FLASH_PIN, OUTPUT);
  digitalWrite(FLASH_PIN, LOW);

  dht.begin();

  WiFi.begin(ssid, password);
  Serial.print("WiFi Baglaniyor");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Baglandi!");

  // Kamera Ayarları
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA; // RAM için VGA (640x480)
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Kamera hatasi: 0x%x", err);
    return;
  }
  
  // Görüntü İyileştirme
  sensor_t * s = esp_camera_sensor_get();
  s->set_brightness(s, 1);
  s->set_contrast(s, 1);
  s->set_saturation(s, 0);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);

  Serial.println("Isinma turlari (4 poz)...");
  for(int i=0; i<4; i++){
    camera_fb_t * fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    delay(200);
  }
  
  Serial.println("Sistem Hazir! Butona basmani bekliyorum...");
}

void loop() {
  // BUTON KONTROLÜ
  // INPUT_PULLUP olduğu için basılınca LOW (0) olur
  if (digitalRead(PIN_BUTTON) == LOW) {
    delay(50); // Debounce (Titreme önleyici)
    if (digitalRead(PIN_BUTTON) == LOW) {
      Serial.println(">> Butona Basildi! İslem basliyor...");
      
      sendPhotoAndData();
      
      Serial.println(">> İslem tamamlandi. Beklemede...");
      delay(2000); // 2 saniye bekle (Yanlışlıkla çift basmayı önler)
    }
  }
}