import cv2
import numpy as np
import requests
from io import BytesIO
from PIL import Image
import threading
import time
import json
from flask import Flask, render_template_string, request, jsonify

# Define o IP do seu ESP32-CAM
esp32_cam_url = "http://10.128.0.36/saved-photo"
esp32_capture_url = "http://10.128.0.36/capture"

# Carrega o classificador de face pré-treinado (Haar Cascade)
face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')

# Semáforo e lock
image_ready = threading.Semaphore(0)  # Semáforo binário para notificar quando uma nova imagem for adquirida
image_lock = threading.Lock()  # Lock para sincronizar o acesso à imagem

# Buffer compartilhado de imagem
image_buffer = None

# Inicializa o servidor Flask
app = Flask(__name__)

@app.route('/')
def show_image():
    return render_template_string('''
    <html>
    <head>
        <title>Imagem Processada</title>
        <meta http-equiv="refresh" content="1"> <!-- Atualiza a página a cada 1 segundo -->
    </head>
    <body>
        <h1>Imagem Processada com Detecção de Faces</h1>
        <img src="/static/processed_image.jpg" alt="Imagem Processada" width="640" height="480">
    </body>
    </html>
    ''')

# Endpoint para receber a imagem do ESP32-CAM
@app.route('/receive-image', methods=['POST'])
def receive_image():
    global image_buffer
    img_data = request.get_data()
    if not img_data:
        return "Nenhuma imagem recebida", 400
    try:
        img = Image.open(BytesIO(img_data))
    except Exception as e:
        print(f"Erro ao abrir a imagem: {e}")
        return "Dados de imagem inválidos", 400

    # Converte a imagem para o formato OpenCV
    image = np.array(img)
    image = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)

    # Adquire o lock para atualizar o buffer de imagem com segurança
    with image_lock:
        image_buffer = image

    # Sinaliza que uma nova imagem está pronta para ser processada
    image_ready.release()

    return "Imagem recebida com sucesso", 200

# Inicia o servidor Flask em uma thread separada
def start_flask_app():
    app.run(host='0.0.0.0', port=5000)

# Thread de aquisição de imagens (solicita ao ESP32-CAM que capture uma imagem)
def image_acquisition_thread():
    while True:
        try:
            # Solicita ao ESP32-CAM para capturar uma nova foto
            response = requests.get(esp32_capture_url)
            if response.status_code == 200:
                print("Solicitação de captura enviada ao ESP32-CAM.")
            else:
                print(f"Erro ao solicitar captura: {response.status_code}")
        except Exception as e:
            print(f"Erro ao acessar o ESP32-CAM: {e}")

        time.sleep(5)  # Solicita uma nova imagem a cada 5 segundos

# Thread de processamento de imagens (processa a imagem recebida)
def image_processing_thread():
    global image_buffer
    while True:
        # Espera pelo semáforo, que indica que uma nova imagem está pronta
        if image_ready.acquire(timeout=1):  # Usa timeout para verificar o evento de encerramento
            # Acessa o buffer de imagem compartilhado com segurança
            with image_lock:
                if image_buffer is not None:
                    print("Processando imagem para detecção de faces...")

                    # Converte para escala de cinza para detecção de faces
                    gray_image = cv2.cvtColor(image_buffer, cv2.COLOR_BGR2GRAY)

                    # Detecta faces
                    faces = face_cascade.detectMultiScale(
                        gray_image,
                        scaleFactor=1.1,
                        minNeighbors=5,
                        minSize=(30, 30)
                    )

                    # Se faces forem detectadas, envia as coordenadas para o ESP32
                    if len(faces) > 0:
                        print(f"Detectada(s) {len(faces)} face(s). Enviando coordenadas para o ESP32...")
                        face_data = []
                        for (x, y, w, h) in faces:
                            face_info = {'x': int(x), 'y': int(y), 'w': int(w), 'h': int(h)}
                            face_data.append(face_info)

                            # Desenha um retângulo ao redor da face detectada
                            cv2.rectangle(image_buffer, (x, y), (x + w, y + h), (255, 0, 0), 2)

                        if len(face_data) > 0:
                            print("Enviando os seguintes dados para o ESP32:", face_data)

                            try:
                                headers = {'Content-Type': 'application/json'}
                                json_data = json.dumps(face_data)

                                # Substitua pelo endereço IP do seu ESP32
                                esp32_url = 'http://10.128.0.36/face-detection'

                                response = requests.post(esp32_url, data=json_data, headers=headers)

                                if response.status_code == 200:
                                    print("Dados enviados com sucesso!")
                                else:
                                    print(f"Falha ao enviar dados para o ESP32. Código de status: {response.status_code}")
                                    print(f"Resposta do servidor: {response.text}")
                            except Exception as e:
                                print(f"Erro ao enviar dados para o ESP32: {e}")
                        else:
                            print("Nenhuma face detectada, dados não enviados.")
                    else:
                        print("Nenhuma face detectada.")

                    # Salva a imagem processada
                    cv2.imwrite('static/processed_image.jpg', image_buffer)
                    
        else:
            # Timeout ocorreu, verifica se o evento de encerramento foi definido
            continue

# Inicia as threads
flask_thread = threading.Thread(target=start_flask_app)
acquisition_thread = threading.Thread(target=image_acquisition_thread)
processing_thread = threading.Thread(target=image_processing_thread)

flask_thread.start()
acquisition_thread.start()
processing_thread.start()

# Aguarda as threads
flask_thread.join()
acquisition_thread.join()
processing_thread.join()