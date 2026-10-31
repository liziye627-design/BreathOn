/* AeroMet v0.7 API adapter. Share between Web and TypeScript-based App.
   In production, replace local-demo token with authenticated per-user gateway. */
export type DataSource = 'mock' | 'sensor';
export type Quality = 'ok' | 'suspect' | 'invalid' | 'unknown';
export type MetricKey = 'acetone_ppm'|'co2_pct'|'o2_pct'|'flow_lpm'|'temperature_c'|'humidity_pct'|'pressure_kpa';
export type SampleState = 'idle'|'warmup'|'baseline'|'ready'|'blowing'|'measuring'|'purging'|'completed'|'invalid'|'error';
export type Metric = {value:number|null;unit:string;source:DataSource;sensor_model?:string|null;calibration:'uncalibrated'|'verified'|'not_applicable';quality:Quality};
export type Measurement = {schema_version:'1.0';id:string;device_id:string;session_id:string|null;captured_at:string;recorded_at:string;state:SampleState;metrics:Partial<Record<MetricKey,Metric>>;quality_flags:string[];transport:'mock_script'|'wifi'|'ble_gateway'|'manual_test';contains_mock:boolean;contains_sensor:boolean};
export type MeasurementList = {items:Measurement[];count:number;demo_warning:string};
export class AeroMetApi {
  constructor(public baseUrl:string=''){}
  private async req<T>(path:string,options?:RequestInit):Promise<T>{
    const response=await fetch(this.baseUrl+path,{...options,headers:{'Accept':'application/json',...(options?.headers||{})}});
    if(!response.ok)throw new Error(`AeroMet API ${response.status}: ${await response.text()}`);
    return await response.json() as T;
  }
  listMeasurements(deviceId?:string,limit=100){const q=new URLSearchParams({limit:String(limit)});if(deviceId)q.set('device_id',deviceId);return this.req<MeasurementList>('/api/v1/measurements?'+q)}
  latest(deviceId?:string){return this.req<{item:Measurement|null}>('/api/v1/measurements/latest'+(deviceId?'?device_id='+encodeURIComponent(deviceId):''))}
  devices(){return this.req<{items:unknown[]}>('/api/v1/devices')}
  referenceSources(){return this.req<{items:unknown[]}>('/api/v1/reference/sources')}
  addManualLog(payload:unknown,token:string){return this.req('/api/v1/user-logs',{method:'POST',headers:{'Content-Type':'application/json','X-AeroMet-Token':token},body:JSON.stringify(payload)})}
  async uploadMealPhoto(blob:Blob,token:string){const form=new FormData();form.append('file',blob,'meal.jpg');return this.req<{image_id:string;url:string}>('/api/v1/uploads/image',{method:'POST',headers:{'X-AeroMet-Token':token},body:form})}
}
// Interface usage: show a MOCK badge per metric; source=mock must never be treated as a true reading.
