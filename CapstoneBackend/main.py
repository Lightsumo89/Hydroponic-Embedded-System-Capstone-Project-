from fastapi import FastAPI, Depends, HTTPException
from sqlalchemy import create_engine, event, Column, Integer, String, Float
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, Session
from pydantic import BaseModel, Field

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
    time = Column(Float, index=True)
    temp = Column(Float)
    ph = Column(Float)
    ntu = Column(Float)

    # metric_type = Column(String) # Temp, PH, Turpidity, etc.
    # metric_value = Column(Float) # Degrees, PH Level, NTU

# Create Database Tables
Base.metadata.create_all(bind=engine)


# Verify Database Connection
def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


# Pydantic Model For Data Creation
class DataCreate(BaseModel):
    time: float
    temp = float
    ph = float
    ntu = float


# Pydantic Model For Data Retrieval
class DataResponse(BaseModel):
    time: float
    temp = float
    ph = float
    ntu = float


# API Endpoint For Created Data Insertion Into Database
@app.post("/telemetry/", response_model=DataResponse)
async def create_item(item: DataCreate, db: Session = Depends(get_db)):
    db_item = Telemetry(**item.model_dump())
    db.add(db_item)
    db.commit()
    db.refresh(db_item)
    return db_item


# API Endpoint For Item Retrieval From Database Via ID
@app.get("/telemetry/{item_id}", response_model=DataResponse)
async def read_item(item_id: int, db: Session = Depends(get_db)):
    db_item = db.query(Telemetry).filter(Telemetry.id == item_id).first()
    if db_item is None:
        raise HTTPException(status_code=404, detail="Item not found")
    return db_item

# | COMMANDS & LINKS |
# uvicorn main:app --reload
# http://localhost:8000/
# http://localhost:8000/docs
