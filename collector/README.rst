ANTAREX Collector-example
=========================
This program is intented as usage-example of the ANTAREX collector API.


Build
=====
To build the collector-example application::

$> make


Usage
=====
To run the collector-example application, assuming "TESTNODE" is the hostname of the monitored node::

$> ./collector-example TESTNODE


This will start stressing the compute mode. 
Click Ctrl-C to get the mean power_pkg value till that moment.
Click again Ctrl-C to stop the stress-test and get the ultimate mean power_pkg value.
