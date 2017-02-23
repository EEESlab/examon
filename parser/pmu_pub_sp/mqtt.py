# -*- coding: utf-8 -*-
"""
 Mqtt.py - MQTT protocol handler


	(c) 2017 ETH Zurich, [Integrated System Laboratory, D-ITET] 
	(c) 2017 University of Bologna, [Integrated System Laboratory, D-ITET]
	 
	Contributed by:
	Francesco Beneventi <francesco.beneventi@unibo.it>
	Andrea Bartolini	<barandre@iis.ee.ethz.ch>

"""


#import paho.mqtt.client as mosquitto
import sys
sys.path.append('../../lib/mosquitto-1.3.5/lib/python')
import mosquitto
from daemon import Daemon


class Mqtt(Daemon):
    """
        MQTT layer
    """
    def __init__(self, pidfile, brokerip, brokerport, intopic):
        self.brokerip = brokerip
        self.brokerport = brokerport
        self.intopic = intopic
        Daemon.__init__(self, pidfile)
        
    def process(self, client, msg):
        """
            Stream processing method. Override
        """
        pass

    # The callback for when the client receives a CONNACK response from the server.
    #def on_connect(self, client, userdata, flags, rc):    # paho
    def on_connect(self, client, userdata, rc):
        """
            On connect callback
        """
        print("Connected with result code "+str(rc))
        # Subscribing in on_connect() means that if we lose the connection and
        # reconnect then subscriptions will be renewed.
        print self.intopic
        self.client.subscribe(self.intopic)

    # The callback for when a PUBLISH message is received from the server.
    def on_message(self, client, userdata, msg):
        """
            On message callback
        """
        self.process(client,msg)
        
    def run(self):
        """
            Daemon main code loop
        """
        self.client = mosquitto.Mosquitto()
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        
        self.client.connect(self.brokerip, port=self.brokerport)
        print "connecting MQTT..."	
        try:
            self.client.loop_forever()
        except KeyboardInterrupt:
            print " exit.."
