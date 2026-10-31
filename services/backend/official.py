"""Official-source adapters. Never substitute mock data on network failures."""
import json
import os
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from fastapi import HTTPException

SOURCES = [
 {"id":"usda_fdc","name":"USDA FoodData Central","category":"nutrition","url":"https://fdc.nal.usda.gov/api-guide/","endpoint":"https://api.nal.usda.gov/fdc/v1/foods/search","usage":"官方营养成分参考，需数据源 ID 和食物份量；不是自动热量识别","status":"requires_api_key"},
 {"id":"ncbi_pubmed","name":"NCBI PubMed E-utilities","category":"literature","url":"https://www.ncbi.nlm.nih.gov/home/develop/api/","endpoint":"https://eutils.ncbi.nlm.nih.gov/entrez/eutils/","usage":"检索公开医学文献，回传 PMID、标题和来源；文献不等于个体诊断","status":"available_with_network"},
 {"id":"clinicaltrials","name":"ClinicalTrials.gov API v2","category":"clinical_trials","url":"https://clinicaltrials.gov/data-api","endpoint":"https://clinicaltrials.gov/api/v2/studies","usage":"查看注册研究状态和来源，不据此判断个体药物效果","status":"available_with_network"},
 {"id":"openfda","name":"openFDA Drug Label","category":"drug_reference","url":"https://open.fda.gov/apis/drug/label/","endpoint":"https://api.fda.gov/drug/label.json","usage":"仅供 FDA 标签引用，美国标签不替代中国获批说明书","status":"source_catalog_only"},
 {"id":"nhanes","name":"CDC NHANES","category":"population_statistics","url":"https://wwwn.cdc.gov/nchs/nhanes/continuousnhanes/","endpoint":None,"usage":"群体流行病学参考，不生成任何用户个人传感器值","status":"source_catalog_only"},
]

def fetch_json(url, payload=None):
    if os.environ.get('AEROMET_ENABLE_OFFICIAL_NETWORK','0')!='1':
        raise HTTPException(status_code=503,detail='官方数据代理未启用；设置 AEROMET_ENABLE_OFFICIAL_NETWORK=1 后在联网环境测试')
    try:
        headers={'User-Agent':'AeroMet-Hackathon-Prototype/0.7 (educational research)','Accept':'application/json'}
        if payload is not None:
            body=json.dumps(payload).encode('utf-8')
            headers['Content-Type']='application/json'
        else: body=None
        req=urllib.request.Request(url,data=body,headers=headers,method='POST' if body else 'GET')
        with urllib.request.urlopen(req,timeout=8) as r:
            if int(r.headers.get('Content-Length','0') or 0)>2_000_000:
                raise ValueError('Official response too large')
            return json.loads(r.read(2_000_001))
    except (urllib.error.URLError,TimeoutError,ValueError,json.JSONDecodeError) as e:
        raise HTTPException(status_code=502,detail=f'无法获取官方数据：{type(e).__name__}')


def search_food(q,limit=5):
    key=os.getenv('FDC_API_KEY','').strip()
    if not key:
        raise HTTPException(status_code=503,detail='未设置 FDC_API_KEY；不能返回伪造的官方营养数据')
    url='https://api.nal.usda.gov/fdc/v1/foods/search?'+urllib.parse.urlencode({'api_key':key})
    data=fetch_json(url,{'query':q,'pageSize':min(limit,10)})
    items=[{'fdc_id':it.get('fdcId'),'description':it.get('description'), 'data_type':it.get('dataType'), 'brand_owner':it.get('brandOwner'), 'url':'https://fdc.nal.usda.gov/food-details/'+str(it.get('fdcId'))+'/nutrients'} for it in data.get('foods',[])[:limit]]
    return {'data_status':'official_live','provider':'USDA FoodData Central','retrieved_at':datetime.now(timezone.utc).isoformat(),'items':items}


def search_pubmed(q,limit=5):
    base='https://eutils.ncbi.nlm.nih.gov/entrez/eutils/'
    # ESearch then ESummary are both NCBI-managed endpoints.
    first=fetch_json(base+'esearch.fcgi?'+urllib.parse.urlencode({'db':'pubmed','term':q,'retmax':min(limit,10),'retmode':'json'}))
    ids=first.get('esearchresult',{}).get('idlist',[])[:limit]
    if not ids:
        return {'data_status':'official_live','provider':'NCBI PubMed','items':[],'retrieved_at':datetime.now(timezone.utc).isoformat()}
    records=fetch_json(base+'esummary.fcgi?'+urllib.parse.urlencode({'db':'pubmed','id':','.join(ids),'retmode':'json'})).get('result',{})
    return {'data_status':'official_live','provider':'NCBI PubMed','retrieved_at':datetime.now(timezone.utc).isoformat(), 'items':[{'pmid':i,'title':records.get(i,{}).get('title'),'pubdate':records.get(i,{}).get('pubdate'),'url':'https://pubmed.ncbi.nlm.nih.gov/'+i+'/'} for i in ids]}


def search_trials(q,limit=5):
    url='https://clinicaltrials.gov/api/v2/studies?'+urllib.parse.urlencode({'query.term':q,'pageSize':min(limit,10),'format':'json'})
    data=fetch_json(url)
    items=[]
    for s in data.get('studies',[])[:limit]:
        p=s.get('protocolSection',{})
        i=p.get('identificationModule',{})
        status=p.get('statusModule',{})
        ident=i.get('nctId')
        items.append({'nct_id':ident,'title':i.get('briefTitle'), 'status':status.get('overallStatus'),'url':('https://clinicaltrials.gov/study/'+ident) if ident else None})
    return {'data_status':'official_live','provider':'ClinicalTrials.gov','retrieved_at':datetime.now(timezone.utc).isoformat(),'items':items}
