from flask import Flask, request, jsonify, render_template_string
from tensorflow.keras.models import load_model
from tensorflow.keras.applications.mobilenet_v2 import preprocess_input
from tensorflow.keras.preprocessing import image
import numpy as np
from PIL import Image
import io
import os
import datetime

app = Flask(__name__)

# --- AYARLAR ---
MODEL_PATH = 'sunum_modeli_v2.h5'
SAVE_DIR = 'gelen_fotograflar'

if not os.path.exists(SAVE_DIR):
    os.makedirs(SAVE_DIR)

CLASS_LABELS = {
    0: 'Erken Yaniklik (Early Blight)',
    1: 'Gec Yaniklik (Late Blight)',
    2: 'Bilinmeyen / Tanimlanamadi',
    3: 'Saglikli (Healthy)'
}

print("Model yükleniyor...")
try:
    model = load_model(MODEL_PATH)
    print("Model başarıyla yüklendi!")
except Exception as e:
    print(f"HATA: Model yüklenemedi. {e}")

def prepare_image(img_path):
    img = Image.open(img_path)
    if img.mode != 'RGB':
        img = img.convert('RGB')
    img = img.resize((224, 224))
    img_array = image.img_to_array(img)
    img_array = np.expand_dims(img_array, axis=0)
    img_array = preprocess_input(img_array)
    return img_array

# --- SADELEŞTİRİLMİŞ WEB ARAYÜZÜ ---
HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>IOT Domates Doktoru</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; padding: 20px; background-color: #f0f2f5; }
        .container { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); max-width: 600px; margin: auto; }
        h1 { color: #1a73e8; }
        .upload-section { border: 2px dashed #ccc; padding: 30px; border-radius: 10px; margin-top: 20px; }
        .btn { background-color: #1a73e8; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; font-size: 16px; margin-top: 10px; }
        .btn:hover { background-color: #1557b0; }
        .result-box { margin-top: 20px; padding: 20px; border-radius: 5px; font-weight: bold; background-color: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🍅 Akıllı Domates Hastalık Tespiti</h1>
        <p>Modeli test etmek için bilgisayarınızdan net bir fotoğraf yükleyin.</p>
        
        <div class="upload-section">
            <form action="/" method="post" enctype="multipart/form-data">
                <input type="file" name="file" accept="image/*" required>
                <br><br>
                <button type="submit" class="btn">Analiz Et</button>
            </form>
        </div>

        {% if prediction %}
        <div class="result-box">
            <h3>SONUÇ RAPORU</h3>
            <p>Tanı: {{ prediction }}</p>
            <p>Güven Skoru: %{{ confidence }}</p>
        </div>
        {% endif %}
        
        <p style="margin-top: 30px; font-size: 12px; color: #666;">
            * ESP32'den gelen veriler arka planda işlenmeye devam ediyor.
        </p>
    </div>
</body>
</html>
"""

@app.route('/', methods=['GET', 'POST'])
def home():
    prediction_text = None
    confidence_text = None

    if request.method == 'POST':
        if 'file' in request.files:
            file = request.files['file']
            if file.filename != '':
                # Dosyayı kaydet
                timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
                file_path = os.path.join(SAVE_DIR, f"web_{timestamp}.jpg")
                file.save(file_path)
                
                # Tahmin et
                processed_image = prepare_image(file_path)
                prediction = model.predict(processed_image)
                class_idx = np.argmax(prediction, axis=1)[0]
                confidence = float(np.max(prediction)) * 100
                
                prediction_text = CLASS_LABELS.get(class_idx, "Bilinmiyor")
                confidence_text = f"{confidence:.2f}"

    return render_template_string(HTML_TEMPLATE, prediction=prediction_text, confidence=confidence_text)

# --- ESP32 API (ESP32 BURAYA RESİM ATACAK) ---
@app.route('/predict', methods=['POST'])
def predict_api():
    if 'image' not in request.files:
        return jsonify({'error': 'Resim dosyasi yok'}), 400
    
    file = request.files['image']
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"esp32_{timestamp}.jpg"
    file_path = os.path.join(SAVE_DIR, filename)
    file.save(file_path)
    
    try:
        processed_image = prepare_image(file_path)
        prediction = model.predict(processed_image)
        class_idx = np.argmax(prediction, axis=1)[0]
        confidence = float(np.max(prediction))
        result_text = CLASS_LABELS.get(class_idx, "Bilinmiyor")
        
        # Ekstra sensör verilerini de alabiliriz (Loglamak istersen)
        temp = request.form.get('temperature', '0')
        
        print(f"ESP32 Gelen Veri -> Tahmin: {result_text} (Güven: %{confidence*100:.2f})")
        
        return jsonify({
            'status': 'success',
            'prediction': result_text,
            'confidence': f"{confidence:.2f}"
        })
    except Exception as e:
        print(f"Hata: {e}")
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    # Bilgisayarın IP'sini ESP32'ye girmeyi unutma
    app.run(host='0.0.0.0', port=5000, debug=True)