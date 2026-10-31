"""AeroMet v0.7 demo integration server: REST + SQLite + official source adapters.

Warning: localhost/trusted-LAN hackathon prototype. No public multi-user deployment.
"""
from __future__ import annotations
import json
import os
import secrets
import uuid
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI, Header, HTTPException, Query, UploadFile, File
from fastapi.responses import FileResponse, HTMLResponse
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import ValidationError

from . import storage as db
from .mock_data import make_mock_data
from .schemas import DeviceIn, SessionIn, SessionPatch, MeasurementIn, Metric, State, LogIn, SeedIn
from .official import SOURCES,search_food,search_pubmed,search_trials

ROOT=Path(__file__).resolve().parent.parent
UPLOADS = ROOT/'data'/'uploads'
DEVICE_TOKEN=os.getenv('AEROMET_TOKEN','local-demo')


def utcnow():
    return datetime.now(timezone.utc).isoformat()


def check_key(token: str | None):
    if not token or not secrets.compare_digest(token,DEVICE_TOKEN):
        raise HTTPException(status_code=401, detail='Invalid X-AeroMet-Token')


def ensure_device(device_id: str):
    existing=db.query('SELECT device_id FROM devices WHERE device_id=?',(device_id,))
    if not existing:
        db.insert('devices',['device_id','display_name','model','firmware_version','hardware_status','created_at','last_seen_at'],
                  (device_id,'呼呼鲸开发板','NiceMCU-T5-2.8IPS','unverified','unverified',utcnow(),None))


def save_measurement(msg: MeasurementIn):
    ensure_device(msg.device_id)
    if msg.session_id and not db.query('SELECT session_id FROM sessions WHERE session_id=? AND device_id=?',(msg.session_id,msg.device_id)):
        raise HTTPException(status_code=422,detail='session_id not found for device')
    mid='m-'+uuid.uuid4().hex[:16]
    saved=msg.model_dump(mode='json')
    is_mock=any(m['source']=='mock' and m['value'] is not None for m in saved['metrics'].values())
    is_sensor=any(m['source']=='sensor' and m['value'] is not None for m in saved['metrics'].values())
    # The per-metric provenance remains authoritative, including values that are null.
    now=utcnow()
    db.insert('measurements',['id','device_id','session_id','captured_at','recorded_at','state','payload_json','contains_mock','contains_sensor'],
              (mid,msg.device_id,msg.session_id,saved['captured_at'],now,saved['state'],json.dumps(saved,ensure_ascii=False),int(is_mock),int(is_sensor)))
    db.execute('UPDATE devices SET last_seen_at=? WHERE device_id=?',(now,msg.device_id))
    return {'id':mid,'recorded_at':now,**saved,'contains_mock':is_mock,'contains_sensor':is_sensor}


def seed_scenario(req: SeedIn):
    device_id='nicemcu-demo-01'
    ensure_device(device_id)
    sid='s-'+uuid.uuid4().hex[:16]
    now=utcnow()
    db.insert('sessions',['session_id','device_id','user_id','label','mode','status','started_at','ended_at'],
              (sid,device_id,'demo-user',f'演示场景：{req.scenario}','demo','completed',now,now))
    points=make_mock_data(req.count,req.scenario,device_id)
    ids=[]
    for p in points:
        p.session_id=sid
        ids.append(save_measurement(p)['id'])
    return {'ok':True,'demo_only':True,'session_id':sid,'count':len(ids),'device_id':device_id,'first_measurement_id':ids[0]}

@asynccontextmanager
async def lifespan(_:FastAPI):
    db.initialize()
    if os.getenv('AEROMET_AUTO_SEED','1')=='1' and not db.query('SELECT id FROM measurements LIMIT 1'):
        seed_scenario(SeedIn(scenario='mixed',count=32))
    yield

app=FastAPI(title='AeroMet Open Integration API',version='0.7.0',
            description='Hackathon demo. All mock/sensor readings carry per-metric provenance. Not a clinical system.',
            lifespan=lifespan)
origins=os.getenv('AEROMET_CORS_ORIGINS','http://localhost:5173,http://127.0.0.1:5173,http://localhost:3000,http://127.0.0.1:3000').split(',')
app.add_middleware(CORSMiddleware,allow_origins=[s.strip() for s in origins if s.strip()],allow_credentials=False,allow_methods=['GET','POST','PATCH','OPTIONS'],allow_headers=['*'])


@app.get('/api/v1/health',tags=['System'])
@app.get('/health',tags=['Legacy'])
def health():
    return {'ok':True,'service':'aeromet-v0.7','db':'sqlite','mode':'hackathon-demo', 'time':utcnow()}

@app.get('/api/v1/devices',tags=['Devices'])
def devices():
    return {'items':db.query('SELECT * FROM devices ORDER BY created_at DESC')}

@app.post('/api/v1/devices',tags=['Devices'],status_code=201)
def upsert_device(payload:DeviceIn,x_aeromet_token:str|None=Header(default=None)):
    check_key(x_aeromet_token)
    now=utcnow()
    with db.LOCK, db.conn() as c:
        c.execute('INSERT INTO devices (device_id,display_name,model,firmware_version,hardware_status,created_at,last_seen_at) VALUES (?,?,?,?,?,?,NULL) ON CONFLICT(device_id) DO UPDATE SET display_name=excluded.display_name, model=excluded.model, firmware_version=excluded.firmware_version, hardware_status=excluded.hardware_status',
                (payload.device_id,payload.display_name,payload.model,payload.firmware_version,payload.hardware_status,now))
    return payload

@app.post('/api/v1/sessions',tags=['Sessions'],status_code=201)
def create_session(payload:SessionIn,x_aeromet_token:str|None=Header(default=None)):
    check_key(x_aeromet_token)
    ensure_device(payload.device_id)
    sid='s-'+uuid.uuid4().hex[:16]; now=utcnow()
    db.insert('sessions',['session_id','device_id','user_id','label','mode','status','started_at','ended_at'],
              (sid,payload.device_id,payload.user_id,payload.label,payload.mode,'in_progress',now,None))
    return {'session_id':sid,**payload.model_dump(),'started_at':now,'status':'in_progress'}

@app.get('/api/v1/sessions',tags=['Sessions'])
def sessions(device_id:str|None=None,limit:int=Query(20,ge=1,le=200)):
    if device_id:
        rows=db.query('SELECT * FROM sessions WHERE device_id=? ORDER BY started_at DESC LIMIT ?',(device_id,limit))
    else: rows=db.query('SELECT * FROM sessions ORDER BY started_at DESC LIMIT ?',(limit,))
    return {'items':rows}

@app.patch('/api/v1/sessions/{session_id}',tags=['Sessions'])
def update_session(session_id:str,payload:SessionPatch,x_aeromet_token:str|None=Header(default=None)):
    check_key(x_aeromet_token)
    if not db.query('SELECT session_id FROM sessions WHERE session_id=?',(session_id,)):
        raise HTTPException(status_code=404,detail='session not found')
    ended_at=utcnow() if payload.status!='in_progress' else None
    db.execute('UPDATE sessions SET status=?, ended_at=? WHERE session_id=?',(payload.status,ended_at,session_id))
    return {'session_id':session_id,'status':payload.status,'ended_at':ended_at}

@app.post('/api/v1/measurements',tags=['Measurements'],status_code=201)
def ingest(payload:MeasurementIn,x_aeromet_token:str|None=Header(default=None)):
    check_key(x_aeromet_token)
    return save_measurement(payload)

@app.get('/api/v1/measurements',tags=['Measurements'])
def measurements(device_id:str|None=None,session_id:str|None=None,only_source:str|None=Query(None,pattern='^(mock|sensor)$'),limit:int=Query(120,ge=1,le=500)):
    where=[]; args=[]
    if device_id:where.append('device_id=?');args.append(device_id)
    if session_id:where.append('session_id=?');args.append(session_id)
    if only_source:where.append('contains_'+only_source+'=1')
    sql='SELECT * FROM measurements'+(' WHERE '+' AND '.join(where) if where else '')+' ORDER BY captured_at DESC,recorded_at DESC LIMIT ?'
    args.append(limit)
    rows=db.query(sql,tuple(args))
    return {'items':[db.as_measurement(r) for r in reversed(rows)],'count':len(rows),'demo_warning':'Mock data is synthetic; not a patient measurement.'}

@app.get('/api/v1/measurements/latest',tags=['Measurements'])
def latest(device_id:str|None=None):
    if device_id:
        rows=db.query('SELECT * FROM measurements WHERE device_id=? ORDER BY captured_at DESC,recorded_at DESC LIMIT 1',(device_id,))
    else:rows=db.query('SELECT * FROM measurements ORDER BY captured_at DESC,recorded_at DESC LIMIT 1')
    return {'item': db.as_measurement(rows[0]) if rows else None}

@app.get('/api/v1/dashboard/summary',tags=['Dashboard'])
def dashboard_summary():
    rows=db.query('SELECT COUNT(*) AS c,SUM(contains_mock) AS mocks,SUM(contains_sensor) AS sensors FROM measurements')
    latest_data=latest()['item']
    return {'measurements':rows[0]['c'], 'mock_measurements':rows[0]['mocks'] or 0,'sensor_measurements':rows[0]['sensors'] or 0,
            'latest':latest_data,'devices':len(db.query('SELECT device_id FROM devices')),
            'warnings':['Demo data is synthetic unless each metric source=sensor.','Acetone concentration and O₂/CO₂ concentrations do not determine individual BMR or RER.']}

@app.post('/api/v1/demo/seed',tags=['Demo'])
def seed(req:SeedIn,x_aeromet_token:str|None=Header(default=None)):
    check_key(x_aeromet_token)
    return seed_scenario(req)

@app.post('/api/v1/user-logs',tags=['Manual Logs'],status_code=201)
def add_log(payload:LogIn,x_aeromet_token:str|None=Header(default=None)):
    check_key(x_aeromet_token)
    log_id='l-'+uuid.uuid4().hex[:16]
    content=payload.model_dump(mode='json')
    db.insert('user_logs',['id','user_id','kind','occurred_at','payload_json'],
              (log_id,payload.user_id,payload.kind,content['occurred_at'],json.dumps(content,ensure_ascii=False)))
    return {'id':log_id,**content}

@app.get('/api/v1/user-logs',tags=['Manual Logs'])
def user_logs(user_id:str='demo-user',limit:int=Query(40,ge=1,le=100)):
    rows=db.query('SELECT * FROM user_logs WHERE user_id=? ORDER BY occurred_at DESC LIMIT ?',(user_id,limit))
    return {'items':[{'id':r['id'],**json.loads(r['payload_json'])} for r in rows]}

@app.post('/api/v1/uploads/image',tags=['Images'],status_code=201)
async def upload_image(file:UploadFile=File(...),x_aeromet_token:str|None=Header(default=None)):
    check_key(x_aeromet_token)
    allowed={'image/jpeg':'.jpg','image/png':'.png','image/webp':'.webp'}
    if file.content_type not in allowed:
        raise HTTPException(status_code=415,detail='Only JPEG, PNG, WebP')
    body=await file.read(5_000_001)
    if len(body)>5_000_000:
        raise HTTPException(status_code=413,detail='5MB image limit')
    good=(file.content_type=='image/jpeg' and body.startswith(b'\xff\xd8\xff')) or (file.content_type=='image/png' and body.startswith(b'\x89PNG\r\n\x1a\n')) or (file.content_type=='image/webp' and body[:4]==b'RIFF' and body[8:12]==b'WEBP')
    if not good:
        raise HTTPException(status_code=415,detail='Image signature does not match content type')
    fid='img-'+uuid.uuid4().hex[:16]
    UPLOADS.mkdir(parents=True,exist_ok=True)
    dest=UPLOADS/(fid+allowed[file.content_type])
    dest.write_bytes(body)
    db.insert('uploads',['id','file_name','media_type','size_bytes','created_at'],(fid,dest.name,file.content_type,len(body),utcnow()))
    return {'image_id':fid,'url':f'/api/v1/uploads/image/{fid}', 'size_bytes':len(body),'source':'user_upload','ai_processed':False}

@app.get('/api/v1/uploads/image/{image_id}',tags=['Images'])
def get_image(image_id:str):
    # Local demo only: this route has no user authentication and must not be public.
    rows=db.query('SELECT * FROM uploads WHERE id=?',(image_id,))
    if not rows:raise HTTPException(status_code=404,detail='Not found')
    item=rows[0]
    return FileResponse(UPLOADS/item['file_name'],media_type=item['media_type'],headers={'Cache-Control':'no-store'})

@app.get('/api/v1/reference/sources',tags=['Official Reference'])
def official_sources():
    return {'items':SOURCES,'rule':'Official references must NEVER be inserted into measurements as sensor readings.'}

@app.get('/api/v1/reference/foods',tags=['Official Reference'])
def official_foods(q:str=Query(min_length=2,max_length=120),limit:int=Query(5,ge=1,le=10)):
    return search_food(q,limit)

@app.get('/api/v1/reference/pubmed',tags=['Official Reference'])
def official_pubmed(q:str=Query(min_length=2,max_length=120),limit:int=Query(5,ge=1,le=10)):
    return search_pubmed(q,limit)

@app.get('/api/v1/reference/trials',tags=['Official Reference'])
def official_trials(q:str=Query(min_length=2,max_length=120),limit:int=Query(5,ge=1,le=10)):
    return search_trials(q,limit)

# Legacy bridge: v0.1 Web prototype expected flat source/state and /api/state.
@app.post('/api/measurements',tags=['Legacy'],status_code=201,deprecated=True)
def legacy_ingest(body:dict,x_aeromet_token:str|None=Header(default=None)):
    check_key(x_aeromet_token)
    source=body.get('source')
    if source not in ('mock','sensor'):
        raise HTTPException(status_code=422,detail='legacy source must be mock or sensor')
    units={'temperature_c':'°C','humidity_pct':'%RH','co2_pct':'%','o2_pct':'%','acetone_ppm':'ppm','flow_lpm':'L/min','pressure_kpa':'kPa'}
    mets={}
    for name,unit in units.items():
        val=body.get(name)
        if val is not None:
            mets[name]=Metric(value=val,unit=unit,source=source,
                              sensor_model=body.get('sensor_model','legacy_unverified_sensor') if source=='sensor' else None,
                              calibration='uncalibrated',quality='unknown')
    mapping={'prepare':'ready','done':'completed'}
    state=mapping.get(body.get('state','measuring'),body.get('state','measuring'))
    try:
        payload=MeasurementIn(device_id=body.get('device_id','huhu-v06'),state=state,metrics=mets,
                              quality_flags=['legacy_payload','no_calibration_claim']+(['synthetic_data'] if source=='mock' else []),
                              transport='manual_test')
    except (ValidationError,ValueError) as e:
        raise HTTPException(status_code=422,detail=str(e))
    sample=save_measurement(payload)
    return {'ok':True,'sample':sample}

@app.get('/api/state',tags=['Legacy'],deprecated=True)
def legacy_state():
    rows=db.query('SELECT * FROM measurements ORDER BY captured_at DESC LIMIT 180')
    flat=[]
    for r in reversed(rows):
        m=db.as_measurement(r)
        origin='mock' if m['contains_mock'] else 'sensor' if m['contains_sensor'] else 'unknown'
        row={'timestamp':datetime.fromisoformat(m['captured_at']).timestamp(),'device_id':m['device_id'],'source':origin,'state':m['state']}
        for name,metric in m['metrics'].items():row[name]=metric['value']
        flat.append(row)
    return {'samples':flat,'count':len(flat),'server_time':datetime.now(timezone.utc).timestamp()}

@app.get('/',tags=['Demo UI'],response_class=HTMLResponse,include_in_schema=False)
def home():
    return (ROOT/'web-preview'/'index.html').read_text(encoding='utf-8')

@app.get('/legacy',tags=['Demo UI'],response_class=HTMLResponse,include_in_schema=False)
def legacy_web():
    return (ROOT/'web-preview'/'legacy_dashboard.html').read_text(encoding='utf-8')

@app.get('/api/v1/agent/preview',tags=['AI Integration'])
def agent_preview(session_id:str|None=None):
    """Safe deterministic copy for demos; not an LLM or a personal medical assessment."""
    if session_id:
        rows=db.query('SELECT * FROM measurements WHERE session_id=? ORDER BY captured_at DESC LIMIT 1',(session_id,))
    else: rows=db.query('SELECT * FROM measurements ORDER BY captured_at DESC LIMIT 1')
    if not rows:return {'status':'no_data','is_llm':False,'message':'暂无可用数据'}
    m=db.as_measurement(rows[0])
    if m['contains_mock']:
        headline='这是一组模拟检测数据'
        text='本次数据用于验证采样状态、后端存储和多端同步；不反映任何人的真实代谢状态。你可以通过手机继续记录饮食、运动和体重。'
    elif m['contains_sensor']:
        headline='已接收设备传感器数据'
        text='已记录传感器的数值、时间及校准状态。未经校准和气路验证，当前数据只能用于工程调试，不能推算脂肪消耗、基础代谢率或药物效果。'
    else:
        headline='采样过程已被记录'
        text='当前没有可解释的气体浓度值，请先检查数据来源与采样条件。'
    return {'status':'demo_template','is_llm':False,'measurement_id':m['id'],'headline':headline,
            'text':text,'scope':'non_clinical', 'warnings':['不得根据模拟或未验证传感器数据生成诊断或修改处方'],
            'evidence_links':[
                {'name':'NCBI PubMed 官方文献检索','url':'https://www.ncbi.nlm.nih.gov/home/develop/api/'},
                {'name':'USDA FoodData Central 官方营养库','url':'https://fdc.nal.usda.gov/api-guide/'}]}
