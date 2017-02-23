Antarex HPC Monitoring
======================

A highly scalable framework for the performance and energy monitoring of HPC servers


Features
========

The main executable "pmu_pub" measures: 

- per-core performance counters:
    - Instructions retired 
    - Un-halted core clock cycles at the current frequency 
    - Un-halted core clock cycles at the reference frequency 
    - Core temperature
    - Time stamp counter     
- per-CPU/Socket energy/power consumption
- DRAM energy/power consumption

The measured data are sent over the network using the MQTT protocol (TCP/IP).


Dependencies
============

It requires two libraries to work:

- Iniparser: used to handle the .conf files
- Mosquitto: used for the MQTT protocol 

(These libraries are provided in the ./lib folder)

To properly build the Mosquitto library you also need

- libssl 
- libcrypto

Available in the following distro packages:

- "libssl-dev" in Ubuntu/Debian 
- "openssl-devel" in Centos


Installation
============

Build
-------

To build all the libraries and the main executable "pmu_pub", go to the main directory and::

 >$ make


Install
----------

WARNING: To install the plugin binary only (and excluding the libraries) DO NOT 
execute make install in the main directory but move in the plugin directory first::

 >$ cd ./publishers/pmu_pub
 
Create and edit the configuration files (see the Configuration section for details)::
 
 >$ cp example_pmu_pub.conf pmu_pub.conf

 >$ cp example_host_whitelist host_whitelist

 >$ make install

The default install folder is ./bin. To specify a different install location::

 >$ make PREFIX=<install-dir> install

The install step will copy the executable, the "pmu_pub.conf" file and the "host_whitelist" file to the <install-dir>.

Configuration
-------------

The main executable needs at least of the "pmu_pub.conf" file to work. If available, it uses also the "host_whitelist" file to filter the hosts where to run. 
The executable will search for the "pmu_pub.conf" file and the "host_whitelist" file in the current working folder first and then, if not found, in the "/etc/" folder.

The "pmu_pub.conf" file
^^^^^^^^^^^^^^^^^^^^^^^

The "pmu_pub.conf" file in the ./publishers/pmu_pub directory contains the default parameters needed by the "pmu_pub" executable.

MQTT parameters:

- brokerHost: IP address of the MQTT broker
- brokerPort: Port number of the MQTT broker (1883)
- topic: Base topic where to publish data (usually it is built as: org/<organization name>/cluster/<cluster name>)

Sampling process parameters:

- dT: data sampling interval in seconds 
- daemonize: Boolean value to daemonize or not the sampling process
- pidfiledir: path to the folder where the pidfile will be stored 
- logfiledir: path to the folder where the logfile will be stored

The "pmu_pub.conf" file must be in the working directory of the executable.

However, most of the parameters can be overridden, when executed, by command line::

 >$ sudo ./pmu_pub -h


 usage: pmu_pub [-h] [-b B] [-p P] [-t T] [-q Q] [-s S] [-x X] [-l L] [-e E] 
                     [-c C] [-P P] [-v]
                     {run,start,stop,restart}

 positional arguments:
  {run,start,stop,restart}
                        Run mode

 optional arguments:
  -h                    Show this help message and exit
  -b B                  IP address of the MQTT broker
  -p P                  Port of the MQTT broker
  -s S                  Sampling interval (seconds)
  -t T                  Output topic
  -q Q                  Message QoS level (0,1,2)
  -x X                  Pid filename dir
  -l L                  Log filename dir
  -c C                  Enable or disable extra counters (Bool)
  -e E                  Perf events list (comma separated)
  -P P                  Enable or disable perf subsystem (Bool)
  -v                    Print version number


The "host_whitelist" file
^^^^^^^^^^^^^^^^^^^^^^^^^

This file contains the list of the hosts in the cluster enabled to execute the plugin.
The hostnames enabled are listed one per row.
Optionally can be included the broker IP address where the hosts that follows are going to be connected.
This is useful for example in the balancing of the load/bandwidth in the front-end nodes.

The format of the file can be::

 [BROKER:] <IP address> <port number>
 host0
 host1
 host2

To disable an host or a group of hosts use "#" as a general comment marker.



Example of the host_whitelist file::


 [BROKER:] 192.168.0.1 1883
 node100
 node101

 [BROKER:] 192.168.0.1 1884
 #node102
 node103

In this example, there are 4 total hosts and 2 brokers.
node100 and node101 will connect to the broker at 192.168.0.1:1883.
node102 and node103 will connect to the broker at 192.168.0.1:1884.
Host "node102" is disabled so the plugin will not run.


Usage
=====

The following instructions indicate how to build a single node measuring setup composed by:

- A broker used as endpoint where to send and ask for the CPU data.
- A publisher agent that collects and publishes CPU data to the broker.
- A subscriber agent that receives the CPU data.

1) Run the broker process as daemon::

    >$ ./lib/mosquitto-1.3.5/src/mosquitto -d 

2) Edit the "pmu_pub.conf" file and set at least the following parameters::

   a) brokerHost: IP address of the node where the broker is running. If it is running on the same machine set equal to 127.0.0.1
   b) topic: set it to:  org/antarex/cluster/testcluster

3) Make sure that the msr driver is loaded::

   >$ sudo modprobe msr
  
4) Run the pmu_pub process (publisher) as supeurser, cd ./publishers/pmu_pub/ and::
   
   >$ sudo ./pmu_pub

   At this point the CPU data should be available to the broker at the topic indicated in the .conf file

5) Subscribing to the topic it is possible to redirect the data stream to the shell or to a file.
   An MQTT subscriber client is available in the ./lib/mosquitto-1.3.5/clients folder. Assuming the broker is running at IP address 127.0.0.1, the following command will print on the standard output the data published by the sampling process "pmu_pub"::

   >$ ./mosquitto_sub -h 127.0.0.1 -t "org/antarex/cluster/testcluster/#" -v

   or::

   >$ ./mosquitto_sub -h 127.0.0.1 -t "org/antarex/cluster/testcluster/#" -v >> cpudata.log

   for saving to a file.

6) To calculate additional metrics see the pmu_pub_sp doc in the ./parser/pmu_pub_sp folder.

   Example (assuming that "TESTNODE" is the hostname where the pmu_pub service is running::

   >$ python ./pmu_pub_sp.py -b 127.0.0.1 -p 1883 -t org/antarex/cluster/testcluster/node/TESTNODE/plugin/pmu_pub/chnl/data -o org/antarex/cluster/testcluster/node/TESTNODE/plugin/pmu_pub/chnl/data 

   the additional metrics will be available at::

   >$ ./mosquitto_sub -h 127.0.0.1 -t "org/antarex/cluster/testcluster/node/TESTNODE/plugin/pmu_pub/chnl/data/#" -v

7) To kill the sampling process, in the ./publishers/pmu_pub folder execute::

   >$ sudo ./pmu_pub stop

   While, to kill the pmu_pub_sp process, in the ./parser/pmu_pub_sp folder execute::

   >$ python ./pmu_pub_sp.py stop




