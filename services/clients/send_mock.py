"""Test data producer to integrate with both App and Web. No device needed."""
import json, os, urllib.request
BASE=os.getenv('AEROMET_BASE_URL','http://127.0.0.1:8000')
TOKEN=os.getenv('AEROMET_TOKEN','local-demo')
body={'scenario':'mixed','count':32}
req=urllib.request.Request(BASE+'/api/v1/demo/seed',data=json.dumps(body).encode(),
  method='POST',headers={'Content-Type':'application/json','X-AeroMet-Token':TOKEN})
with urllib.request.urlopen(req,timeout=8) as r:
    print('Mock records inserted:',r.read().decode())
