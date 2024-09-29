#include "WiFi.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems
#include "driver/rtc_io.h"
#include <ESPAsyncWebServer.h>
#include <StringArray.h>
#include <SPIFFS.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

// Credenciais da rede Wi-Fi
const char* ssid = "Inteli.Iot";
const char* password = "@Intelix10T#";

// Cria o objeto AsyncWebServer na porta 80
AsyncWebServer server(80);

bool photo_ready = false;      // Indica que a foto está pronta para ser enviada
SemaphoreHandle_t photoSemaphore;  // Semáforo binário para controle de captura
SemaphoreHandle_t sendSemaphore;   // Semáforo binário para controle de envio

// Nome do arquivo da foto para salvar no SPIFFS
#define FILE_PHOTO "/photo.jpg"

// Pinos do módulo de câmera OV2640 (CAMERA_MODEL_AI_THINKER)
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

// HTML da página inicial
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { text-align:center; }
    .vert { margin-bottom: 10%; }
    .hori{ margin-bottom: 0%; }
  </style>
</head>
<body>
  <div id="container">
    <h2>ESP32-CAM Última Foto</h2>
    <p>Pode levar mais de 5 segundos para capturar uma foto.</p>
    <p>
      <button onclick="rotatePhoto();">GIRAR</button>
      <button onclick="capturePhoto()">CAPTURAR FOTO</button>
      <button onclick="location.reload();">ATUALIZAR PÁGINA</button>
    </p>
  </div>
  <div><img src="saved-photo" id="photo" width="70%"></div>
</body>
<script>
  var deg = 0;
  function capturePhoto() {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', "/capture", true);
    xhr.send();
  }
  function rotatePhoto() {
    var img = document.getElementById("photo");
    deg += 90;
    if(isOdd(deg/90)){ document.getElementById("container").className = "vert"; }
    else{ document.getElementById("container").className = "hori"; }
    img.style.transform = "rotate(" + deg + "deg)";
  }
  function isOdd(n) { return Math.abs(n % 2) == 1; }
</script>
</html>)rawliteral";

// Variáveis para armazenar os dados recebidos
String receivedData = "";

// Função para verificar se a foto foi capturada com sucesso
bool checkPhoto( fs::FS &fs ) {
  File f_pic = fs.open( FILE_PHOTO );
  unsigned int pic_sz = f_pic.size();
  return ( pic_sz > 100 );
}

// Função para capturar a foto e salvar no SPIFFS
void capturePhotoSaveSpiffs( void ) {
  camera_fb_t * fb = NULL;
  bool ok = 0;

  do {
    // Tira uma foto com a câmera
    Serial.println("Capturando uma foto...");

    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Falha na captura da câmera");
      return;
    }

    // Nome do arquivo da foto
    Serial.printf("Nome do arquivo da foto: %s\n", FILE_PHOTO);
    File file = SPIFFS.open(FILE_PHOTO, FILE_WRITE);

    // Insere os dados no arquivo da foto
    if (!file) {
      Serial.println("Falha ao abrir o arquivo no modo de escrita");
    }
    else {
      file.write(fb->buf, fb->len); // payload (imagem), tamanho do payload
      Serial.print("A foto foi salva em ");
      Serial.print(FILE_PHOTO);
      Serial.print(" - Tamanho: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    // Fecha o arquivo
    file.close();
    esp_camera_fb_return(fb);

    // Verifica se o arquivo foi salvo corretamente no SPIFFS
    ok = checkPhoto(SPIFFS);
  } while ( !ok );
}

// Thread de aquisição de imagens
void capturePhotoTask(void * parameter) {
  while (true) {
    if (xSemaphoreTake(photoSemaphore, portMAX_DELAY) == pdTRUE) {
      capturePhotoSaveSpiffs();
      photo_ready = true;  // Indica que a foto está pronta
      xSemaphoreGive(sendSemaphore);  // Libera o semáforo para envio
    }
  }
}

// Thread de envio de imagens
void sendPhotoTask(void * parameter) {
  while (true) {
    if (xSemaphoreTake(sendSemaphore, portMAX_DELAY) == pdTRUE) {
      if (photo_ready) {
        // Envia a foto para o computador via HTTP POST
        Serial.println("Enviando a foto para o computador...");
        File file = SPIFFS.open(FILE_PHOTO, FILE_READ);
        if (file) {
          size_t fileSize = file.size();
          uint8_t *buffer = (uint8_t *)malloc(fileSize);
          if (buffer) {
            file.read(buffer, fileSize);
            if (WiFi.status() == WL_CONNECTED) {
              HTTPClient http;
              http.begin("http://10.128.0.8:5000/receive-image"); //Ip computador, IPV4
              http.addHeader("Content-Type", "image/jpeg");

              int httpResponseCode = http.POST(buffer, fileSize);

              if (httpResponseCode > 0) {
                String response = http.getString();
                Serial.println(httpResponseCode);
                Serial.println(response);
              }
              else {
                Serial.print("Erro ao enviar a imagem. Código de erro: ");
                Serial.println(httpResponseCode);
              }
              http.end();
              free(buffer);
            }
            else {
              Serial.println("Não conectado ao WiFi");
            }
            file.close();
          }
        }
        else {
          Serial.println("Falha ao abrir o arquivo da foto para leitura.");
        }
      }
    }
  }
}

// Thread de recebimento de detecção
void receiveDetectionTask(void * parameter) {
  // Como estamos recebendo os dados via HTTP POST, não precisamos de uma thread separada
  // Se quisermos processar os dados de forma assíncrona, podemos implementar aqui
  while (true) {
    // Implementação opcional
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup() {
  // Porta serial para depuração
  Serial.begin(115200);

  // Inicializa os semáforos
  photoSemaphore = xSemaphoreCreateBinary();
  sendSemaphore = xSemaphoreCreateBinary();

  // Cria as tasks
  xTaskCreate(capturePhotoTask, "CapturePhotoTask", 4096, NULL, 1, NULL);
  xTaskCreate(sendPhotoTask, "SendPhotoTask", 4096, NULL, 1, NULL);
  xTaskCreate(receiveDetectionTask, "ReceiveDetectionTask", 2048, NULL, 1, NULL);

  // Conecta ao Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando ao WiFi...");
  }
  if (!SPIFFS.begin(true)) {
    Serial.println("Ocorreu um erro ao montar o SPIFFS");
    ESP.restart();
  }
  else {
    delay(500);
    Serial.println("SPIFFS montado com sucesso");
  }

  // Imprime o endereço IP
  Serial.print("Endereço IP: http://");
  Serial.println(WiFi.localIP());

  // Desliga o 'brownout detector'
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Configuração da câmera
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
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  // Inicializa a câmera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Falha na inicialização da câmera com erro 0x%x", err);
    ESP.restart();
  }

  // Rota para a página inicial
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest * request) {
    xSemaphoreGive(photoSemaphore);  // Libera o semáforo para capturar uma nova foto
    request->send_P(200, "text/plain", "Capturando Foto");
  });

  server.on("/saved-photo", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, FILE_PHOTO, "image/jpg", false);
  });

  // Endpoint para receber os dados de detecção
  server.on("/face-detection", HTTP_POST, [](AsyncWebServerRequest *request){
    // Manipulação da resposta
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Esta função é chamada quando os dados são recebidos
    String jsonData = String((char*)data);
    // Parseia o JSON
    const size_t capacity = JSON_ARRAY_SIZE(10) + 10 * JSON_OBJECT_SIZE(4);
    DynamicJsonDocument doc(capacity);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error) {
      Serial.print("Erro ao parsear JSON: ");
      Serial.println(error.c_str());
      request->send(400, "application/json", "{\"status\": \"error\", \"message\": \"Formato JSON inválido\"}");
      return;
    }

    Serial.println("Dados de detecção de face recebidos:");
    serializeJsonPretty(doc, Serial);
    request->send(200, "application/json", "{\"status\": \"success\"}");
    // Aqui você pode processar os dados recebidos conforme necessário
  });

  // Inicia o servidor
  server.begin();
}

void loop() {
  // O loop principal não precisa fazer nada, pois as tasks estão lidando com tudo
}
