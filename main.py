"""
Python Backend via FastAPI and SQLite3

- Takes JSON packets via HTTP POST from ESP32 
- Validates Data via Pydantic
- Commits into SQLite database file (WAL mode enabled)
- Creates Endpoints for frontend data retrieval

Running the Backend
    Create Environment: 
        python -m venv .venv
    Activate Environment:
        - Linux: source .venv/bin/activate
        - Windows: .\.venv\Scripts\activate
    Install Dependencies: 
        pip install -r requirements.txt
    Launch API: 
        uvicorn main:hydro_backend --reload
    Interactive Documentation: http://127.0.0.1:8000/docs
    
"""
import os
from typing import List
from fastapi import FastAPI, Depends, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import Boolean, create_engine, event, Column, Integer, String, Float
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, Session
from pydantic import BaseModel, Field


# Create FastAPI Instance
hydro_backend = FastAPI()

# Utilize CORS middleware to manage permissions between backend and frontend domains
hydro_backend.add_middleware(
    CORSMiddleware,
    allow_origins=["https://www.cs.oswego.edu",
                    "http://www.cs.oswego.edu",
                    "https://cs.oswego.edu",
                    "http://cs.oswego.edu"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Database Setup, specifies location of main.py filepath and removes main.py -> attaches hydroponics.db 
DATABASE_URL = f"sqlite:///{os.path.join(os.path.dirname(os.path.abspath(__file__)),'hydroponics.db')}"
engine = create_engine(
    DATABASE_URL, 
    connect_args={
        "check_same_thread": False, 
        "timeout": 15  # Waits 15 seconds before failing a locked write
    }
)
# Event to enable WAL mode (writing and reading can occur concurrently) and synchronous in NORMAL to improve performance (not syncing every write like FULL)
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


# Creates local session for database
def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


# Pydantic model for data creation
class DataCreate(BaseModel):
    timestamp: str 
    pump_on: bool
    lights_on: bool
    # Data Validation via Pydantic field using ranges provided by mega_sensors.ino
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
@hydro_backend.post("/telemetry/", response_model=DataResponse)
async def create_item(
                    item: DataCreate, 
                    db: Session = Depends(get_db)):
    db_item = Telemetry(**item.model_dump())
    db.add(db_item)
    db.commit()
    db.refresh(db_item)
    return db_item

# API Endpoint for data retrieval from database via ID
@hydro_backend.get("/telemetry/{item_id}", response_model=DataResponse)
async def read_item(item_id: int, 
                    db: Session = Depends(get_db)):
    db_item = db.query(Telemetry).filter(Telemetry.id == item_id).first()
    if db_item is None:
        raise HTTPException(status_code=404, detail="Data not found")
    return db_item

# API Endpoint for bulk data retrieval
@hydro_backend.get("/telemetry/", response_model=List[DataResponse])
async def bulk_read_item(
                        start_time: str | None = None, 
                        end_time: str | None = None, 
                        limit: int = 100, # CHANGE LATER IF NEEDED
                        db: Session = Depends(get_db)):
    query = db.query(Telemetry)
    # Use SQLAlchemy filters to sort through database for given time range
    if start_time: # Example timestamp: 2026-04-21 18:30:21
        query = query.filter(Telemetry.timestamp >= start_time)
    if end_time:
        query = query.filter(Telemetry.timestamp <= end_time)
    # Create a sorted list of queried items in time range
    db_items = query.order_by(Telemetry.id.desc()).limit(limit).all()
    if not db_items:
        raise HTTPException(status_code=404, detail="Data not found at given time range")
    return db_items