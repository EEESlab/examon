
pmu_pub_sp
==========

Stream processor calculating the derived per-core and per-cpu 
metrics starting from data published by the pmu_pub daemon.


Derived metrics
===============

In the following are listed some of the metrics that can be 
derived from the measured counters.

Energy
------

Energy consumed by the unit x (CPU socket or DRAM) in the time 
interval (t1-t0):

     DE_x = Erg_U·(Erg_x(t1) - Erg_x(t0))


Power
-----

Average Power consumed by the unit x (CPU socket or DRAM) in the 
time interval (t1-t0):

             DErg_x  
     P_x  =  -------
             t1 - t0


Clocks Per Instruction (CPI)
----------------------------


             Clk_curr(t1)  -  Clk_curr(t0)
     CPI  =  -----------------------------
                Instr(t1)  -  Instr(t0)  


Instructions Per Second (IPS)
-----------------------------


             Instr(t1)  -  Instr(t0)
     IPS  =  -----------------------
                     t1 - t0 

Load
----


              Clk_ref(t1)  -  Clk_ref(t0)         
     Load  =  --------------------------- · 100
                  Tsc(t1)  -  Tsc(t0)  



Frequency
---------


              Clk_curr(t1)  -  Clk_curr(t0)
     Freq  =  ----------------------------- · Freq_ref
               Clk_ref(t1)  -  Clk_ref(t0) 



Usage
=====

usage: pmu_pub_sp.py [-h] [-b B] [-p P] [-t T [T ...]] [-o O] [-f F] [-d]
                     {run,start,stop,restart}

positional arguments:
  {run,start,stop,restart}
                        Run mode

optional arguments:
  -h, --help            show this help message and exit
  -b B                  IP address of the MQTT broker
  -p P                  Port of the MQTT broker
  -t T [T ...]          Input topics (list)
  -o O                  Output topic
  -f F                  Reference CPU frequency (Freq_ref)
  -d                    Boolean value to daemonize or not the sampling process



The default parameters can be specified in the "pmu_pub_sp.conf" file.

Example
=======

To run the stream processor in daemon mode for the host "TESTNODE":
(Replace "TESTNODE" with the hostname of the node where the pmu_pub service is running")

$> python ./pmu_pub_sp.py start \
	-b 127.0.0.1 \
	-p 1883 \
	-t org/antarex/cluster/testcluster/node/TESTNODE/plugin/pmu_pub/chnl/data \
 	-o org/antarex/cluster/testcluster/node/TESTNODE/plugin/pmu_pub/chnl/data \
	-d



To stop the stream processor:

$> python ./pmu_pub_sp.py stop

