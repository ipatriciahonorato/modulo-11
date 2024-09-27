# Documentação do Projeto: Detecção de Faces com ESP32-CAM em Tempo Real

Este projeto implementa um sistema de detecção de faces utilizando um ESP32-CAM, um microcontrolador com capacidade de processamento de imagens, e um computador host que processa as imagens capturadas. O sistema é dividido em threads para cumprir os requisitos de um sistema operacional de tempo real.

## Objetivos

- **Thread de Aquisição de Imagens**: Capturar imagens utilizando a câmera do ESP32-CAM, controlando um semáforo binário para indicar que uma nova imagem está disponível.
- **Thread de Envio de Imagens**: Enviar as imagens capturadas para o computador host. Esta thread sincroniza com a thread de aquisição para garantir que apenas imagens novas sejam enviadas.
- **Thread de Recebimento de Detecção**: No ESP32, uma thread monitora a entrada de dados para receber as coordenadas das faces detectadas enviadas pelo computador host.

## Estrutura do Projeto

O projeto é composto por dois componentes principais:

1. **Código do ESP32-CAM (Arduino IDE)**
2. **Código Python no Computador Host**

### 1. Código do ESP32-CAM

#### Principais Componentes

- **Thread de Aquisição de Imagens (`capturePhotoTask`)**
  - Utiliza um semáforo binário (`photoSemaphore`) para controlar a captura de novas imagens.
  - Captura uma imagem quando o semáforo é liberado e salva no sistema de arquivos SPIFFS.
  - Sinaliza que a imagem está pronta definindo a variável `photo_ready` como `true`.

- **Thread de Envio de Imagens (`sendPhotoTask`)**
  - Aguarda o semáforo (`sendSemaphore`) ser liberado após a captura de uma nova imagem.
  - Envia a imagem para o computador host via HTTP POST.
  - Após o envio, redefine `photo_ready` para `false`.

- **Thread de Recebimento de Detecção (`receiveDetectionTask`)**
  - Lida com requisições HTTP POST recebidas na rota `/face-detection`.
  - Processa os dados de detecção de faces recebidos do computador host.

#### Configuração da Câmera

- Inicialização dos parâmetros da câmera, incluindo resolução e qualidade da imagem.
- Configuração dos pinos da câmera OV2640.

#### Servidor Web

- Rota `/capture`: Libera o semáforo para capturar uma nova imagem.
- Rota `/saved-photo`: Serve a imagem salva no SPIFFS.
- Rota `/face-detection`: Recebe os dados de detecção de faces enviados pelo computador host.

#### Comunicação com o Computador Host

- Envia imagens via HTTP POST para o endpoint `/receive-image` no computador host.
- Recebe dados de detecção de faces via HTTP POST no endpoint `/face-detection`.

### 2. Código Python no Computador Host

#### Principais Componentes

- **Servidor Flask**
  - Endpoint `/receive-image`: Recebe as imagens enviadas pelo ESP32-CAM.
  - Endpoint `/show-image`: Exibe a imagem processada com as detecções de faces.

- **Thread de Processamento de Imagens (`image_processing_thread`)**
  - Aguarda novas imagens recebidas e processa para detecção de faces.
  - Desenha retângulos ao redor das faces detectadas.
  - Salva a imagem processada para ser exibida via servidor Flask.
  - Envia as coordenadas das faces detectadas de volta para o ESP32-CAM via HTTP POST.

#### Detecção de Faces

- Utiliza o classificador Haar Cascade do OpenCV para detectar faces nas imagens.
- Processa as imagens em escala de cinza para melhorar a eficiência.

#### Comunicação com o ESP32-CAM

- Envia solicitações para o ESP32-CAM capturar novas imagens.
- Recebe imagens do ESP32-CAM no endpoint `/receive-image`.
- Envia dados de detecção de faces para o ESP32-CAM no endpoint `/face-detection`.

## Fluxo de Operação

1. **Captura de Imagem**
   - O computador host solicita ao ESP32-CAM que capture uma nova imagem através da rota `/capture`.
   - A thread `capturePhotoTask` no ESP32-CAM captura a imagem e salva no SPIFFS.

2. **Envio de Imagem**
   - A thread `sendPhotoTask` no ESP32-CAM envia a imagem capturada para o computador host via HTTP POST para o endpoint `/receive-image`.

3. **Processamento de Imagem**
   - O servidor Flask no computador host recebe a imagem e sinaliza para a thread de processamento.
   - A thread `image_processing_thread` processa a imagem, detecta faces e desenha retângulos.

4. **Envio de Dados de Detecção**
   - O computador host envia as coordenadas das faces detectadas para o ESP32-CAM via HTTP POST para o endpoint `/face-detection`.

5. **Recebimento de Dados de Detecção**
   - O ESP32-CAM recebe os dados na thread `receiveDetectionTask` e pode processá-los conforme necessário.

## Requisitos Atendidos

- **Thread de Aquisição de Imagens**
  - Implementada pela `capturePhotoTask` no ESP32-CAM.
  - Controlada por um semáforo binário para sincronização.

- **Thread de Envio de Imagens**
  - Implementada pela `sendPhotoTask` no ESP32-CAM.
  - Sincronizada com a thread de aquisição através de semáforos.

- **Thread de Recebimento de Detecção**
  - Implementada pela `receiveDetectionTask` no ESP32-CAM.
  - Monitora o endpoint `/face-detection` para receber dados do computador host.

## Considerações de Implementação

- **Sincronização**
  - Semáforos binários são utilizados para sincronizar as threads no ESP32-CAM.
  - Garante que as imagens são capturadas e enviadas em sequência correta.

- **Comunicação**
  - Utiliza protocolos HTTP para comunicação entre o ESP32-CAM e o computador host.
  - Endpoints específicos são configurados para diferentes tipos de dados.

- **Processamento de Imagens**
  - A detecção de faces é realizada no computador host para aproveitar maior poder de processamento.
  - As imagens são processadas e retornadas ao ESP32-CAM com informações relevantes.

- **Interface Web**
  - O servidor Flask disponibiliza uma página web para visualizar a imagem processada.
  - A página é atualizada automaticamente para mostrar as novas detecções.

## Configuração do Ambiente (.env) no Windows

Execute o comando para criar um ambiente virtual:

1. **Criação do Ambiente Virtual**

```
python -m venv venv
```

2. **Ativação do Ambiente Virtual**

No terminal, execute o comando para ativar o ambiente virtual:
```
venv\Scripts\activate
```

3. **Instalação das Dependências**

Com o ambiente virtual ativado, execute o comando para instalar as dependências do projeto:

```
pip install -r requirements.txt
```

4. **Criação do Arquivo .env (Opcional)**

Crie um arquivo .env na raiz do projeto e adicione variáveis de ambiente personalizadas, se necessário.
Exemplo de um arquivo .env:

```
FLASK_ENV=development
ESP32_IP=192.168.23.3
```

## Como Executar o Projeto

1. **Configurar o ESP32-CAM**
   - Carregar o código no ESP32-CAM através do Arduino IDE.
   - Configurar as credenciais da rede Wi-Fi.
   - Ajustar os endereços IP conforme necessário.

2. **Executar o Código Python**
   - Instalar as dependências necessárias (`opencv-python`, `flask`, etc.).
   - Executar o script Python no computador host.
   - Certificar-se de que o computador está na mesma rede que o ESP32-CAM.

3. **Acessar a Interface Web**
   - Navegar até `http://localhost:5000/show-image` para visualizar a imagem processada.

