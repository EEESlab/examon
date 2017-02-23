Pmu_pub plugin
==============

pmu_pub provides an interface to the PMU (Performance Monitoring Unit) and RAPL (Running Average Power Limit) infrastructures of the Intel Xeon processor series.

This plugin periodically samples the PMU and RAPL units and sends the data over the network using the MQTT protocol.

The metrics are exported per-core and per-socket.

Per-core metrics
----------------

+-------------+--------------------------------------------------------+
| Metric name | Brief Description                                      |
+=============+========================================================+
| Instr       | Instruction retired                                    |
+-------------+--------------------------------------------------------+
| Clk_curr    | Un-halted core clock cycles at the current frequency   |
+-------------+--------------------------------------------------------+
| Clk_ref     | Un-halted core clock cycles at the reference frequency |
+-------------+--------------------------------------------------------+
| Temp        | Core temperature (°C)                                  |
+-------------+--------------------------------------------------------+
| Tsc         | Time stamp counter                                     |
+-------------+--------------------------------------------------------+

Per-Socket metrics
------------------

+-------------+------------------------------------+
| Metric name | Brief Description                  |
+=============+====================================+
| Erg_units   | RAPL Energy Units                  |
+-------------+------------------------------------+
| Erg_pkg     | Per-Socket CPU energy status       |
+-------------+------------------------------------+
| Erg_dram    | Per-Socket DRAM energy status      |
+-------------+------------------------------------+
| Temp        | Per-Socket CPU Package temperature |
+-------------+------------------------------------+
| Tsc         | Time stamp counter                 |
+-------------+------------------------------------+



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

Intel performance monitoring events:

- events: a list of events for the core PMU and Uncore performance monitoring units of the Intel processors.

This key accepts literals events names (space separated list) as defined in the "EventName" column of the events tables provided by the CPU vendor.
The core and uncore events list supported in the current version of pmu_pub can be found here: https://download.01.org/perfmon/HSX/.

To specify an uncore event it is neccessary to indicate also the hardware unit where it belongs, following this format:

	``<UncoreUnit>::<EventName>``

A list of the uncore units supported in the current version of pmu_pub is the following:

Uncore Units for the Intel Xeon-E5 v3 (Haswell) architecture
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

*Caching Agent "Cbo":*

The LLC coherence engine (CBo) manages the interface between the core and the last
level cache (LLC).

     ``hswep_unc_cbo<0-11>, "Intel Haswell-EP C-Box 0-11 uncore"``

*Home Agent "ha":*

It is the coherency agent responsible for guarding the memory
controller

     ``hswep_unc_ha<0-1>, "Intel Haswell-EP HA 0-1 uncore"``

*Memory controller "imc":*

The memory controller is the interface between the home Home Agent (HA) and DRAM,
translating read and write commands into specific memory commands and schedules
them with respect to memory timing.

     ``hswep_unc_imc<0-5>, "Intel Haswell-EP IMC0-IMC1 uncore"``


*Power Control Unit "PCU":*

Power control unit acting as a core/uncore power and thermal manager. It runs its firmware on an internal
micro-controller and coordinates the sockets power states.

     ``hswep_unc_pcu, "Intel Haswell-EP PCU uncore"``

*Quick Path "qpi":*

The Intel QPI Link Layer is responsible for packetizing requests from the caching agent
on the way out to the system interface.

     ``hswep_unc_qpi<0-1>, "Intel Haswell-EP QPI0-QPI1 uncore"``

*Ubox "ubo":*

The UBox acts as the central unit for a variety of functions:

- The master for reading and writing physically distributed registers across Intel Xeon Processor E5 and E7 v3 family using the Message Channel.
- The UBox is the intermediary for interrupt traffic, receiving interrupts from the system and dispatching interrupts to the appropriate core.
- The UBox serves as the system lock master used when quiescing the platform (e.g., Intel QPI bus lock).

     ``hswep_unc_ubo, "Intel Haswell-EP U-Box uncore"``

*Ring to PCI interface "r2pcie":*

R2PCIe represents the interface between the Ring and IIO traffic to/from PCIe.

     ``hswep_unc_r2pcie, "Intel Haswell-EP R2PCIe uncore"``

*Ring to QPi interface "r3qpi":*

R3QPI is the interface between the Intel QPI Link Layer, which packetizes requests, and
the Ring.

     ``hswep_unc_r3qpi<0-1>, "Intel Haswell-EP R3QPI0-R3QPI1 uncore"``


*Interface between internal rings "sbo":*

The SBox manages the interface between the two Rings.

     ``hswep_unc_sbo<0-3>, "Intel Haswell-EP S-BOX0-S-BOX3 uncore"``


The "pmu_pub.conf" file must be in the working directory of the executable.

Command line parameters
^^^^^^^^^^^^^^^^^^^^^^^

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

1. Edit the "pmu_pub.conf" file and set at least the following parameters:
    - brokerHost. IP address of the node where the broker is running.
    - topic. As example set it to: org/cineca/cluster/galileo for the Galileo nodes.

2. Make sure that the msr driver is loaded:
   ::

     >$ sudo modprobe msr

3. Run the pmu_pub process (publisher) as superuser, cd ./publishers/pmu_pub/ and:
   ::

     >$ sudo ./pmu_pub {run,start,stop,restart}

Systemd support
----------------

An example of the .service file needed by systemd is provided (pmu_pub.service)





