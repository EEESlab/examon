#!/usr/bin/python

"""
 pmu_pub_sp.py - MSR parser

	(c) 2017 ETH Zurich, [Integrated System Laboratory, D-ITET] 
	(c) 2017 University of Bologna, [Integrated System Laboratory, D-ITET]
	 
	Contributed by:
	Francesco Beneventi <francesco.beneventi@unibo.it>
	Andrea Bartolini	<barandre@iis.ee.ethz.ch>
"""

import sys
import argparse
import ConfigParser
import logging
from mqtt import Mqtt
from collections import Counter
from collections import OrderedDict
from datetime import datetime
from datetime import timedelta



class LimitedSizeTS(OrderedDict):
    """
        Fixed size dict with auto-reordering by key.
    """
    def __init__(self, *args, **kwds):
        self.size_limit = kwds.pop("size_limit", None)
        self.last_key = -1
        OrderedDict.__init__(self, *args, **kwds)
        self._check_size_limit()

    def __setitem__(self, key, value):
        if len(self.items()) > 1:
            self.last_key = self.last().keys()[0]
        OrderedDict.__setitem__(self, key, value)
        if key < self.last_key:
            self._sort()
        self._check_size_limit()

    def _check_size_limit(self):
        if self.size_limit is not None:
            while len(self) > self.size_limit:
                self.popitem(last=False)

    def _sort(self):
        for k,v in sorted(self.items(), key=lambda t: t[0]):
            if k in self:
                del self[k]
            OrderedDict.__setitem__(self, k, v)

    def last(self):
        return dict([self.items()[-1]])

    def first(self):
        return dict([self.items()[0]])




class MqttSp(Mqtt):
    """
        MQTT Stream Processor
    """
    def __init__(self, pidfile, brokerip, brokerport, intopic, outtopic):
        self.QUEUE_SIZE = 2
        self.outtopic = outtopic
        self.queues = {}
        self.queues['core'] = {}
        self.queues['cpu'] = {}
        self.CORE_PKT_ITMS = ['tsc','temp','instr','clk_curr','clk_ref','C3','C6']
        self.CPU_PKT_ITMS = ['freq_ref','tsc','erg_units','erg_dram','erg_pkg','C2','C3','C6']
        self.freq_ref = -1
        Mqtt.__init__(self, pidfile, brokerip, brokerport, intopic)


    def diff(self, data, regsz):
        """
            Calculate Delta
        """
        d = data.values()

        if d[1] >= d[0]:
            res = d[1] - d[0]
        else:
            res = long(1<<regsz) -1
            res = res + d[1] - d[0]
        return res

    # ENTRY POINT
    def process(self, client, msg):
        """
            Stream processing user implementation
        """
        tpc = (msg.topic).split('/')
        data = (msg.payload).split(';')  # data: {value;timestamp}
        #print msg.topic
        #print msg.payload

        if 'core' in tpc:
            self.update_queue(tpc, data, 'core')
        if 'cpu' in tpc:
            self.update_queue(tpc, data, 'cpu')


    def update_queue(self, tpc, data, queue):
        """
            Update queues with new values
        """
        # get unit num. Assume: ../<unit>/<unitnum>/<counter>
        c = str(tpc[tpc.index(queue) + 1])
        # get counter name
        reg = str(tpc[tpc.index(queue) + 2])
        # create queues (dict), one per unit and each contains all unit counters
        if c not in self.queues[queue]:
            self.queues[queue][c] = dict()
            self.queues[queue][c]['pktsize'] = list();
        if reg not in self.queues[queue][c]:
            self.queues[queue][c][reg] = LimitedSizeTS(size_limit=self.QUEUE_SIZE)
            #self.queues[queue][c][reg][0] = 0.0
        self.queues[queue][c][reg][float(data[1])] = float(data[0])
        self.queues[queue][c]['pktsize'].append(reg);
        # check if packet is complete and calculate per core and cpu vars
        if queue == 'core':
            #print self.queues[queue][c]['pktsize']
            if Counter(self.queues[queue][c]['pktsize']) == Counter(self.CORE_PKT_ITMS):
                #if 0 in self.queues[queue][c][reg]:
                if len(self.queues[queue][c][reg]) < self.QUEUE_SIZE:
                    self.queues[queue][c]['pktsize'] = []
                    return
                else:
                    self.pub_core_values(queue,c)
            elif len(self.queues[queue][c]['pktsize']) == len(self.CORE_PKT_ITMS):
                missing = list(set(self.queues[queue][c]['pktsize'][:]).symmetric_difference(set(self.CORE_PKT_ITMS)))
                logging.warning('wrong packet size in CORE_PKT_ITMS. Missing items: %s' % ', '.join(missing))
                self.queues[queue][c]['pktsize'][:-len(missing)] = []
                return
        if queue == 'cpu':
            #print self.queues[queue][c]['pktsize']
            if Counter(self.queues[queue][c]['pktsize']) == Counter(self.CPU_PKT_ITMS):
                #if 0 in self.queues[queue][c][reg]:
                if len(self.queues[queue][c][reg]) < self.QUEUE_SIZE:
                    self.queues[queue][c]['pktsize'] = []
                    return
                else:
                    self.pub_cpu_values(queue,c)
            elif len(self.queues[queue][c]['pktsize']) == len(self.CPU_PKT_ITMS):
                missing = list(set(self.queues[queue][c]['pktsize'][:]).symmetric_difference(set(self.CPU_PKT_ITMS)))
                logging.warning('wrong packet size in CPU_PKT_ITMS. Missing items: %s' % ', '.join(missing))
                self.queues[queue][c]['pktsize'][:-len(missing)] = []
                return


    def pub_core_values(self, queue, c):
        """
            Calculate and publish core data
        """
        self.queues[queue][c]['pktsize'] = []
        #print self.freq_ref
        if self.freq_ref < 0:
            return
        tick        = self.diff(self.queues[queue][c]['tsc'],64)
        dT          = float(tick)/self.freq_ref;
        Temp        = int(self.queues[queue][c]['temp'].last().values()[0])
        instr       = self.diff(self.queues[queue][c]['instr'],48)
        clk_curr    = self.diff(self.queues[queue][c]['clk_curr'],48)
        clk_ref     = self.diff(self.queues[queue][c]['clk_ref'],48)
        Time        = self.queues[queue][c]['tsc'].last().keys()[0]

        cpi         = float(clk_curr) / instr
        ips         = float(instr) / dT
        idle        = 100 * float(tick - clk_ref) / tick
        freq_real   = float(clk_curr) / clk_ref
        C3res       = float(self.diff(self.queues[queue][c]['C3'],64) / tick)*100
        C6res       = float(self.diff(self.queues[queue][c]['C6'],64) / tick)*100

        data_ = {   'cpi':str(cpi),
                    'load_core':str(100 - idle),
                    'freq_ref':str(self.freq_ref / 1000000),
                    'freq':str(freq_real * self.freq_ref / 1000000),
                    'dT_core':str(dT * 1000),
                    'ips':str(ips),
                    'C3res':C3res,
                    'C6res':C6res}

        timestamp = Time
        # publish per-sensor values in csv format value;timestamp
        for k,v in data_.iteritems():
            mqtt_tpc = self.outtopic  + "/core/" + c + "/" + str(k)
            mqtt_pay = str(v) + ";" + ("%.3f" % timestamp)
            #print mqtt_tpc + ' ' + mqtt_pay
            self.client.publish(mqtt_tpc, payload=str(mqtt_pay),qos=0,retain=False)


    def pub_cpu_values(self, queue, c):
        """
            Calculate and publish cpu data
        """
        self.queues[queue][c]['pktsize'] = []
        self.freq_ref   = self.queues[queue][c]['freq_ref'].last().values()[0]
        ergU        = pow(0.5,(int(self.queues[queue][c]['erg_units'].last().values()[0])>>8)&0x1F)
        dram_ergU   = pow(0.5,16.0)
        tick        = self.diff(self.queues[queue][c]['tsc'],64)
        dT          = float(tick)/self.freq_ref;
        enDRAM      = self.diff(self.queues[queue][c]['erg_dram'],32)*dram_ergU
        enPkg       = self.diff(self.queues[queue][c]['erg_pkg'],32)*ergU
        #Temp        = int(self.queues[queue][c]['temp'].last().values()[0])
        Time        = self.queues[queue][c]['tsc'].last().keys()[0]
        C2res       = float(self.diff(self.queues[queue][c]['C2'],64) / tick)*100
        C3res       = float(self.diff(self.queues[queue][c]['C3'],64) / tick)*100
        C6res       = float(self.diff(self.queues[queue][c]['C6'],64) / tick)*100

        data_ = {   'pow_dram':str(enDRAM / dT),
                    'pow_pkg':str(enPkg / dT),
                    'dT_cpu':str(dT * 1000),
                    'C2res':C2res,
                    'C3res':C3res,
                    'C6res':C6res}

        timestamp = Time
        # publish per-sensor values in csv format value;timestamp
        for k,v in data_.iteritems():
            mqtt_tpc = self.outtopic  + "/cpu/" + c + "/" + str(k)
            mqtt_pay = str(v) + ";" + ("%.3f" % timestamp)
            #print mqtt_tpc + ' ' + mqtt_pay
            self.client.publish(mqtt_tpc, payload=str(mqtt_pay),qos=0,retain=False)



if __name__ == '__main__':

    # Load Config.
    config = ConfigParser.RawConfigParser()
    config.read('pmu_pub_sp.conf')


    # MQTT
    MQTT_BROKER = config.get('MQTT', 'MQTT_BROKER')
    MQTT_PORT = config.get('MQTT', 'MQTT_PORT')
    MQTT_IN_TOPIC = config.get('MQTT','MQTT_IN_TOPIC').split(',')
    MQTT_OUT_TOPIC = config.get('MQTT','MQTT_OUT_TOPIC')
    #MQTT_SUB_NAME = config.get('MQTT','MQTT_SUB_NAME')

    # Daemon
    PID_FILENAME = config.get('Daemon','PID_FILENAME')
    LOGFILE = config.get('Daemon','LOG_FILENAME')
    DAEMON = config.getboolean('Daemon','Daemonize')

    #target
    #freq = config.getfloat('TARGET','freq')

    # cmd line parser
    parser = argparse.ArgumentParser()
    parser.add_argument("runmode", choices=["run","start","stop","restart"], help="Run mode")
    parser.add_argument("-b", help="IP address of the MQTT broker")
    parser.add_argument("-p", help="Port of the MQTT broker")
    parser.add_argument("-t", nargs='+', help="Input topics (list)")
    parser.add_argument("-o", help="Output topic")
    parser.add_argument("-f", help="Base CPU frequency")
    parser.add_argument("-x", help="pid filename")
    parser.add_argument("-l", help="log filename")
    parser.add_argument("-d", action="store_true", help="Boolean value to daemonize or not the sampling process")
    args = parser.parse_args()

    if args.b:
        MQTT_BROKER = args.b
    if args.p:
        MQTT_PORT = args.p
    if args.t:
        MQTT_IN_TOPIC = args.t
    if args.o:
        MQTT_OUT_TOPIC = args.o
    if args.f:
        freq = float(args.f)
    if args.l:
        LOGFILE = args.l
    if args.x:
        PID_FILENAME = args.x
    if args.d:
        DAEMON = args.d


    # create logger
    logging.basicConfig(filename=LOGFILE,
                            filemode='a',
                            format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                            datefmt='%m/%d/%Y %I:%M:%S %p',
                            level=logging.WARNING)



    metrics = ['/cpu/+/tsc',
               '/cpu/+/temp',
               '/cpu/+/erg_dram',
               '/cpu/+/erg_pkg',
               '/cpu/+/erg_units',
               '/core/+/tsc',
               '/core/+/temp',
               '/core/+/instr',
               '/core/+/clk_curr',
               '/core/+/clk_ref',
               '/core/+/C3',
               '/core/+/C6',
               '/cpu/+/C2',
               '/cpu/+/C3',
               '/cpu/+/C6',
               '/cpu/+/freq_ref']

    # Run
    print MQTT_IN_TOPIC
    intpc = []
#    for tpc in MQTT_IN_TOPIC:
#        intpc.append((tpc.strip(), 0))
    for tpc in metrics:
        intpc.append((MQTT_IN_TOPIC[0].strip() + tpc.strip(), 0))
        
        

    daemon = MqttSp(PID_FILENAME, MQTT_BROKER, MQTT_PORT, intpc, MQTT_OUT_TOPIC)

    if 'start' == args.runmode:
        if DAEMON:
            print "start in daemon mode"
            daemon.start()
        else:
            daemon.run()
    elif 'run'== args.runmode:
        daemon.run()
    elif 'stop' == args.runmode:
        daemon.stop()
    elif 'restart' == args.runmode:
        daemon.restart()
    else:
        print "Unknown command"
        sys.exit(2)
    sys.exit(0)

