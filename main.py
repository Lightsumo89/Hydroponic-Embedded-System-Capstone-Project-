"""
Python Backend via FastAPI and SQLite3

- Takes JSON packets via HTTP POST from ESP32 
- Validates Data via Pydantic
- Commits into SQLite database file (WAL mode enabled)
- Creates Endpoints for frontend data retrieval
"""

from typing import List

from fastapi import FastAPI, Depends, HTTPException
from sqlalchemy import Boolean, create_engine, event, Column, Integer, String, Float
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, Session
from pydantic import BaseModel, Field
from fastapi.responses import HTMLResponse

# Create FastAPI App
app = FastAPI()

# DATABASE SETUP
DATABASE_URL = "sqlite:///./test.db"
engine = create_engine(
    DATABASE_URL, 
    connect_args={
        "check_same_thread": False, 
        "timeout": 15  # Waits 15 seconds before failing a locked write
    }
)
# Event to enable WAL mode (writing and reading can occur concurrently) and synchonous in NORMAL to improve performance (not syncing every write like FULL)
@event.listens_for(engine, "connect")
def set_sqlite_pragma(dbapi_connection, connection_record):
    cursor = dbapi_connection.cursor()
    cursor.execute("PRAGMA journal_mode=WAL;")
    cursor.execute("PRAGMA synchronous=NORMAL;")
    cursor.close()  
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)
Base = declarative_base()


# Model For Database
class Telemetry(Base):
    __tablename__ = "telemetry"
    id = Column(Integer, primary_key=True, index=True)
    timestamp = Column(String, index=True)
    temperature_water = Column(Float)
    temperature_air = Column(Float)
    humidity = Column(Float)
    ph = Column(Float)
    ec = Column(Float)
    turbidity = Column(Float)
    water_level = Column(Float)
    pump_on = Column(Boolean)
    lights_on = Column(Boolean)

# Create Database Tables
Base.metadata.create_all(bind=engine)


# Verify Database Connection
def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


# Pydantic Model for data creation
class DataCreate(BaseModel):
    timestamp: str 
    pump_on: bool
    lights_on: bool
    # Data Validation Via Pydantic Field Using Ranges From Arduino
    temperature_water: float = Field(ge=0.0, le=50) # x >= 0, x <= 50
    temperature_air: float = Field(ge=0.0, le=50) 
    humidity: float = Field(ge=0.0, le=100)
    ph: float = Field(ge=0.0, le=14)
    ec: float = Field(ge=0.0, le=10)
    turbidity: float = Field(ge=0.0, le=3000)
    water_level: float = Field(ge=0.0, le=100)

# Pydantic model for data retrieval from SQLite Database
class DataResponse(BaseModel):
    id: int
    timestamp: str
    pump_on: bool
    lights_on: bool
    temperature_water: float
    temperature_air: float
    humidity: float
    ph: float
    ec: float
    turbidity: float
    water_level: float


# API Endpoint for data creation and database commits
@app.post("/telemetry/", response_model=DataResponse)
async def create_item(item: DataCreate, db: Session = Depends(get_db)):
    db_item = Telemetry(**item.model_dump())
    db.add(db_item)
    db.commit()
    db.refresh(db_item)
    return db_item

# API Endpoint For data retrieval from database via ID
@app.get("/telemetry/{item_id}", response_model=DataResponse)
async def read_item(item_id: int, db: Session = Depends(get_db)):
    db_item = db.query(Telemetry).filter(Telemetry.id == item_id).first()
    if db_item is None:
        raise HTTPException(status_code=404, detail="Item not found")
    return db_item

# API Endpoint for bulk data retrieval
@app.get("/telemetry/", response_model=List[DataResponse])
async def bulk_read_item(limit: int = 100, db: Session = Depends(get_db)):
    items = db.query(Telemetry).order_by(Telemetry.id.desc()).limit(limit).all()
    return items


"""
Initialize and view FastAPI Docs:
- uvicorn main:app --reload
- http://localhost:8000/docs

Build venv virtual environment (vscode):
- python -m venv .venv
- .\.venv\Scripts\activate
- pip install fastapi uvicorn sqlalchemy pydantic

"""