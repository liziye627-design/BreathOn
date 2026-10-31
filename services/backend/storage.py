"""Single-file SQLite storage for local hackathon demo; not medical production storage."""
import os
import sqlite3
import json
from pathlib import Path
from threading import RLock

DB_PATH = Path(os.getenv('AEROMET_DB', str(Path(__file__).resolve().parent.parent / 'data' / 'aeromet_demo.sqlite3')))
LOCK = RLock()

def conn():
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    c = sqlite3.connect(str(DB_PATH), timeout=15, check_same_thread=False)
    c.row_factory = sqlite3.Row
    c.execute('PRAGMA journal_mode=WAL')
    c.execute('PRAGMA foreign_keys=ON')
    return c

def initialize():
    with LOCK, conn() as c:
        c.executescript('''
        CREATE TABLE IF NOT EXISTS devices (
           device_id TEXT PRIMARY KEY, display_name TEXT NOT NULL, model TEXT NOT NULL,
           firmware_version TEXT NOT NULL, hardware_status TEXT NOT NULL,
           created_at TEXT NOT NULL, last_seen_at TEXT
        );
        CREATE TABLE IF NOT EXISTS sessions (
           session_id TEXT PRIMARY KEY, device_id TEXT NOT NULL, user_id TEXT NOT NULL,
           label TEXT NOT NULL, mode TEXT NOT NULL, status TEXT NOT NULL,
           started_at TEXT NOT NULL, ended_at TEXT
        );
        CREATE TABLE IF NOT EXISTS measurements (
           id TEXT PRIMARY KEY, device_id TEXT NOT NULL, session_id TEXT,
           captured_at TEXT NOT NULL, recorded_at TEXT NOT NULL, state TEXT NOT NULL,
           payload_json TEXT NOT NULL, contains_mock INTEGER NOT NULL,
           contains_sensor INTEGER NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_measurements_device_time ON measurements(device_id,captured_at);
        CREATE TABLE IF NOT EXISTS user_logs (
           id TEXT PRIMARY KEY, user_id TEXT NOT NULL, kind TEXT NOT NULL,
           occurred_at TEXT NOT NULL, payload_json TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS uploads (
           id TEXT PRIMARY KEY, file_name TEXT NOT NULL, media_type TEXT NOT NULL,
           size_bytes INTEGER NOT NULL, created_at TEXT NOT NULL
        );
        ''')

def insert(table, columns, values):
    with LOCK, conn() as c:
        placeholders = ','.join(['?']*len(columns))
        c.execute(f"INSERT INTO {table} ({','.join(columns)}) VALUES ({placeholders})",values)

def execute(sql, args=()):
    with LOCK, conn() as c:
        c.execute(sql, args)

def query(sql, args=()):
    with LOCK, conn() as c:
        return [dict(x) for x in c.execute(sql,args).fetchall()]


def as_measurement(row):
    if row is None:
        return None
    payload = json.loads(row['payload_json'])
    payload['id'] = row['id']
    payload['recorded_at'] = row['recorded_at']
    payload['contains_mock'] = bool(row['contains_mock'])
    payload['contains_sensor'] = bool(row['contains_sensor'])
    return payload
