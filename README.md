# Hydroponics IoT

## Arduino Mega Sensor Hub
1. Reads all sensors 
2. Builds JSON 
3. Sends to ESP32 via Serial1

### Arduino Mega Wiring
- Serial1 (TX1=Pin18, RX1=Pin19) → ESP32 RX/TX
- Baud: 115200

## ESP32 WiFi Bridge
1. Listens on Serial2 (RX2=GPIO16) for newline-terminated JSON
2. Then POSTs it to the FastAPI server

### ESP32 Wiring
- Mega TX1 (pin 18) → voltage divider → ESP32 RX2 (GPIO16)
- Common GND between Mega and ESP32 is REQUIRED

# Hydroponics Backend

Python Backend via FastAPI and SQLite3

- Takes JSON packets via HTTP POST from ESP32 
- Validates Data via Pydantic
- Commits into SQLite database file (WAL mode enabled)
- Creates Endpoints for frontend data retrieval

## Running the Backend
1.  **Create Environment**: 
    `python -m venv .venv`
2.  **Activate Environment**: 
    `.\.venv\Scripts\activate`
3.  **Install Dependencies**: 
    `pip install -r requirements.txt`
4.  **Launch API**: 
    `uvicorn main:app --reload`

## Interactive Documentation
- Once the API is running, visit the interactive docs at:
    <http://127.0.0.1:8000/docs>

# Hydroponics Frontend

