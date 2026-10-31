import os
import tempfile
from pathlib import Path
os.environ['AEROMET_DB'] = str(Path(tempfile.mkdtemp())/'test.sqlite3')
os.environ['AEROMET_AUTO_SEED'] = '0'
os.environ['AEROMET_ENABLE_OFFICIAL_NETWORK']='0'
from fastapi.testclient import TestClient
from backend.main import app

HEAD={'X-AeroMet-Token':'local-demo'}


def test_api_and_mock_seed():
    with TestClient(app) as c:
        h=c.get('/api/v1/health')
        assert h.status_code==200 and h.json()['ok']
        assert c.get('/api/v1/measurements').json()['count']==0
        assert c.post('/api/v1/demo/seed',json={'scenario':'mixed','count':12}).status_code==401
        resp=c.post('/api/v1/demo/seed',headers=HEAD,json={'scenario':'mixed','count':12})
        assert resp.status_code==200, resp.text
        assert resp.json()['count']==12
        res=c.get('/api/v1/measurements?limit=20').json()
        assert res['count']==12
        assert all(v['contains_mock'] and not v['contains_sensor'] for v in res['items'])
        assert all(m['source']=='mock' for v in res['items'] for m in v['metrics'].values())
        assert c.get('/api/state').status_code==200
        assert c.get('/legacy').status_code==200
        assert 'UNIFIED DATA PLATFORM' in c.get('/').text
        assert c.get('/openapi.json').status_code==200


def test_mixed_record_and_bad_unit():
    with TestClient(app) as c:
        base={'device_id':'nicemcu-demo-01','state':'measuring',
              'metrics':{'humidity_pct':{'value':75,'unit':'%RH','source':'sensor','sensor_model':'SHT41-DEMO','calibration':'uncalibrated','quality':'unknown'},
                         'acetone_ppm':{'value':1.2,'unit':'ppm','source':'mock','calibration':'uncalibrated','quality':'unknown'}},
              'transport':'wifi'}
        r=c.post('/api/v1/measurements',json=base,headers=HEAD)
        assert r.status_code==201, r.text
        assert r.json()['contains_mock'] and r.json()['contains_sensor']
        assert c.get('/api/v1/measurements?only_source=sensor').json()['count']>=1
        base['metrics']['acetone_ppm']['unit']='%'
        assert c.post('/api/v1/measurements',json=base,headers=HEAD).status_code==422
        base['metrics']['acetone_ppm']['unit']='ppm'
        base['metrics']['humidity_pct']['sensor_model']=None
        assert c.post('/api/v1/measurements',json=base,headers=HEAD).status_code==422
        base['metrics']['humidity_pct']['sensor_model']='SHT41-DEMO'
        base['metrics']['acetone_ppm']['calibration']='verified'
        assert c.post('/api/v1/measurements',json=base,headers=HEAD).status_code==422


def test_session_and_logs_and_reference():
    with TestClient(app) as c:
        r=c.post('/api/v1/sessions',headers=HEAD,json={'device_id':'nicemcu-demo-01','mode':'demo'})
        assert r.status_code==201,r.text
        sid=r.json()['session_id']
        assert c.patch('/api/v1/sessions/'+sid,json={'status':'completed'},headers=HEAD).status_code==200
        assert c.post('/api/v1/user-logs',json={'kind':'meal','source':'mock','text':'TEST'},headers=HEAD).status_code==201
        assert c.get('/api/v1/user-logs').json()['items']
        s=c.get('/api/v1/reference/sources')
        assert s.status_code==200 and len(s.json()['items'])>=3
        assert c.get('/api/v1/reference/pubmed?q=acetone').status_code==503
        assert c.get('/api/v1/reference/foods?q=rice').status_code==503


def test_legacy_bridge():
    with TestClient(app) as c:
        r=c.post('/api/measurements',headers=HEAD,json={'device_id':'huhu-v06','source':'mock','state':'done','humidity_pct':75})
        assert r.status_code==201, r.text
        assert r.json()['sample']['state']=='completed'
        assert c.get('/api/state').json()['samples']


def test_safe_agent_preview():
    with TestClient(app) as c:
        c.post('/api/v1/demo/seed',json={'scenario':'baseline','count':4},headers=HEAD)
        r=c.get('/api/v1/agent/preview')
        assert r.status_code==200
        body=r.json()
        assert body['is_llm'] is False
        assert '模拟' in body['headline']
        assert '不反映' in body['text']
