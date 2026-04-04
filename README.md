Read me file

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
Once the server is running, visit the interactive docs at:
[http://127.0.0.1:8000/docs](http://127.0.0.1:8000/docs)