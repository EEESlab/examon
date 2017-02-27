Collector-example
=================
This program is intented as usage-example of the following collector APIs:

collector_init(collector, broker_ip, broker_port);
collector_clean(collector);
collector_start(collector);
collector_get(collector);
collector_end(collector);

Such APIs are based on the struct collector (defined in the ./collector/collector.c file) to carry out the monitoring. The collector_init() initializes the collector and subscribes to the topic (e.g. power_package), creating a thread to handle the received messages, while the collector_clean() is used to clean up the collector and close the callback thread. The remaining APIs are the core of the metrics aggregator: collector_start() starts the monitoring of the metric, taking note of the starting time of the monitoring; collector_get() stores in the struct collector both the monitoring time till that instant and the mean value, and continues the monitoring; finally, collector_end() has the same functionality of the collector_get(), but stops the monitoring. 

It is noteworthy that all the monitoring APIs handle the collector in a completely transparent way from the user point-of-view. The only required steps are::

1. Include in your project the collector APIs 
      i.e.	#include "./collector/collector.h"

2. initialize a struct collector and select the related topic for each metric that needs to be monitored
      e.g.	struct collector_val pow_pkg = { 0 }; 
		      pow_pkg.mqtt_topic = "topic_pow_pkg";

3. pass a *pointer of the the struct collector to the monitoring APIs
      e.g.	collector_init(&pow_pkg, … );
		      collector_start(&pow_pkg);

Note that for each metric that has to be monitored, the collector_init() and _clean() functions have to be called accordingly.

4. Read the timestamps and the mean value from the struct collector, after either collector_get() or collector_end() is executed.



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
