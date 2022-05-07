
import json
import time
import random

from examon.plugin.examonapp import ExamonApp
from examon.plugin.sensorreader import SensorReader


class Sensor:
    def __init__(self, sensor_name='random_sensor', range_min=0, range_max=100.0):
        self.sensor_name = sensor_name
        self.range_min = range_min
        self.range_max = range_max
    
    def get_sensor_data(self, num_sensors=10):
        payload = []
        
        for s in range(0, num_sensors):
            payload.append({
                'sensor_name': self.sensor_name,
                'id': str(s),
                'value': random.uniform(self.range_min, self.range_max)
           })
                    
        return payload
    
    def read_data(self):
        pass
    

def read_data(sr):
    
    # get timestamp and data 
    timestamp = int(time.time()*1000)
    raw_packet = sr.sensor.get_sensor_data()
    
    # build the examon metric
    examon_data = []
    for raw_data in raw_packet:
        metric = {}
        metric['name'] = raw_data['sensor_name']
        metric['value'] = raw_data['value']
        metric['timestamp'] = timestamp
        metric['tags'] = sr.get_tags()
        # dynamically add new custom tags
        metric['tags']['id'] = str(raw_data['id'])
        # build the final packet
        examon_data.append(metric)
        
    # worker id (string) useful for debug/log
    worker_id = sr.sensor.sensor_name
      
    return (worker_id, examon_data,)
        
                
def worker(conf, tags):
    """
        Worker process code
    """
    # sensor instance 
    sensor = Sensor()
    
    # SensorReader app
    sr = SensorReader(conf, sensor)
    
    # add read_data callback
    sr.read_data = read_data  
    
    # set the default tags
    sr.add_tags(tags)
    
    # run the worker loop
    sr.run()

   
if __name__ == '__main__':

    # start creating an Examon app
    app = ExamonApp()

    app.parse_opt()
    # for checking
    print("Config:")
    print(json.dumps(app.conf, indent=4))

    # set default metrics tags
    tags = app.examon_tags()
    tags['org']      = 'examon'
    tags['plugin']   = 'examon_pub'
    tags['chnl']     = 'data'
  
    # add a worker
    app.add_worker(worker, app.conf, tags)
    
    # run!
    app.run()    
