"""Deterministic, conspicuously synthetic demo sensor sequences (no patient data)."""
from __future__ import annotations
from datetime import datetime, timedelta, timezone
from random import Random
from .schemas import MeasurementIn, Metric


def make_mock_data(count=28, scenario='mixed', device_id='nicemcu-demo-01'):
    rnd = Random(20261030 + len(scenario))
    base = datetime.now(timezone.utc) - timedelta(seconds=2*count)
    points = []
    for i in range(count):
        phase = (i/(count-1)) if count > 1 else 0
        active = 0.28 <= phase <= 0.69
        state = ('warmup' if phase < .13 else 'baseline' if phase < .28
                 else 'blowing' if phase < .50 else 'measuring' if phase < .79
                 else 'purging' if phase < .93 else 'completed')
        acetone = (0.8 if phase < .28 else (1.2 if scenario != 'trend' else 1.5)) + rnd.uniform(-.10,.10)
        co2 = (0.05 if not active else 3.8 + rnd.uniform(-.4,.4))
        o2 = (20.8 if not active else 16.8 + rnd.uniform(-.5,.5))
        flow = (0 if not active else 7.2 + rnd.uniform(-1.0,1.0))
        temp = 24.0 + (5 if active else 0) + rnd.uniform(-.3,.3)
        humidity = 49 + (36 if active else 0) + rnd.uniform(-2,2)
        vals = {
            'temperature_c': round(temp,2),'humidity_pct': round(humidity,2),
            'co2_pct':round(co2,2),'o2_pct':round(o2,2),
            'acetone_ppm':round(acetone,2),'flow_lpm':round(flow,2)
        }
        if scenario=='baseline':
            vals['acetone_ppm'] = None
            vals['o2_pct'] = None
        metrics={k:Metric(value=v,unit={'temperature_c':'°C','humidity_pct':'%RH','co2_pct':'%','o2_pct':'%','acetone_ppm':'ppm','flow_lpm':'L/min'}[k],source='mock',calibration='uncalibrated',quality='unknown') for k,v in vals.items()}
        points.append(MeasurementIn(device_id=device_id, state=state, captured_at=base + timedelta(seconds=2*i),metrics=metrics,quality_flags=['synthetic_data','not_for_clinical_use'],transport='mock_script'))
    return points
