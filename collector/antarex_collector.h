/* ANTAREX - Collector API.
 *
 * © 2017 ETH Zurich, 
 *   [Integrated System Laboratory, D-ITET], 
 *   [Antonio Libri, a.libri@iis.ee.ethz.ch]
 * 
 * © 2017 University of Bologna, 
 *   [Department of Electrical, Electronic and Information Engineering, DEI],
 *   [Andrea Bartolini, a.bartolini@unibo.it]
 */
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include "mosquitto.h"

#define MQTT_KEEPALIVE 1000
#define MQTT_QOS 0 // Quality of Service

/**
 * Struct collector_val - Struct containing the state of the metric
 * @mqtt_topic:           MQTT topic to subscribe
 * @mosq:                 (INTERNAL USE) ptr to the mosquitto object
 * @flag_en:              (INTERNAL USE) MQTT Broker port
 * @sum_val:              (INTERNAL USE) Sum monitored samples
 * @count_mean:           (INTERNAL USE) Number of monitored samples
 * @mean_val:             Mean of the monitored samples
 * @start:                Timestamp start
 * @end:                  Timestamp end
 *
 * Returns:           0 on Success, 1 on Failure.
 **/
struct collector_val
{
   char *mqtt_topic;
   struct mosquitto *mosq;
   bool flag_en;
   double sum_val;
   double count_mean;
   double mean_val;
   struct timeval start;
   struct timeval end;
};

/**
 * collector_init() - Initialize ANTAREX collector
 * @val:              Struct containing the state of the metric
 * @mqtt_broker_ip:   MQTT Broker IP Address
 * @mqtt_port:        MQTT Broker port
 *
 * Returns:           0 on Success, 1 on Failure.
 **/
int collector_init(struct collector_val *val, char *mqtt_broker_ip, int mqtt_port);

/**
 * collector_start() - Start monitoring the metric
 * @val:               Struct containing the state of the metric
 *
 * Returns:            0 on Success, 1 on Failure.
 **/
int collector_start(struct collector_val *val);

/**
 * collector_get() - Get mean value, but continue the monitoring
 * @val:             Struct containing the state of the metric
 *
 * Returns:          0 on Success, 1 on Failure.
 **/
int collector_get(struct collector_val *val);

/**
 * collector_end() - End monitoring and get mean value
 * @val:             Struct containing the state of the metric
 *
 * Returns:          0 on Success, 1 on Failure.
 **/
int collector_end(struct collector_val *val);

/**
 * collector_clean() - Cleanup ANTAREX collector
 * @val:               Struct containing the state of the metric
 
 * Returns:            0 on Success, 1 on Failure.
 **/
int collector_clean(struct collector_val *val);
