/* Collector API.
 *
 * © 2017 ETH Zurich, 
 *   [Integrated System Laboratory, D-ITET], 
 *   [Antonio Libri, a.libri@iis.ee.ethz.ch]
 * 
 * © 2017 University of Bologna, 
 *   [Department of Electrical, Electronic and Information Engineering, DEI],
 *   [Andrea Bartolini, a.bartolini@unibo.it]
 */
#include "collector.h"

#define MONITORING_BENEVENTI

static void connect_callback(struct mosquitto*, void*, int result);
static void message_callback(struct mosquitto *, void*, const struct mosquitto_message*);

int collector_init(struct collector_val *val, char *mqtt_broker_ip, int mqtt_port)
{
   mosquitto_lib_init();
   val->mosq = mosquitto_new(NULL, true, val);
   if(!val->mosq){
      fprintf(stderr, "[Collector]: Init error.\n");
      return 1;
   }

   mosquitto_connect_callback_set(val->mosq, connect_callback);
   mosquitto_message_callback_set(val->mosq, message_callback);

   if(mosquitto_connect(val->mosq, mqtt_broker_ip, mqtt_port, MQTT_KEEPALIVE)){
      fprintf(stderr, "[Collector]: Unable to connect to the broker.\n");
      return 1;
   }

   if (mosquitto_loop_start(val->mosq)){
      fprintf(stderr,"[Collector]: Unable to create Thread.");
      return 1;
   }
   return 0;
}

int collector_start(struct collector_val *val)
{
   if(val->flag_en){
      fprintf(stderr, "[Collector]: Start() already running\n");
      return 1;
   }

   val->sum_val = 0;
   val->count_mean = 0;
   gettimeofday(&(val->start), NULL);
   val->flag_en = 1;
   return 0;
}

int collector_get(struct collector_val *val)
{
   if(!val->flag_en){
      fprintf(stderr, "[Collector]: Start() not running\n");
      return 1;
   }

   gettimeofday(&(val->end), NULL);
   val->mean_val = val->sum_val/val->count_mean;
   return 0;
}

int collector_end(struct collector_val *val)
{
   if(!val->flag_en){
      fprintf(stderr, "[Collector]: Start-cmd not running\n");
      return 1;
   }

   val->flag_en = 0;
   gettimeofday(&(val->end), NULL);
   val->mean_val = val->sum_val/val->count_mean;
   return 0;
}

int collector_clean(struct collector_val *val)
{
   if(!val->mosq){
      return 1;
   }

   if(mosquitto_loop_stop(val->mosq, true)){
      fprintf(stderr,"[Collector]: Error closing Thread.");
      return 1;
   }

   mosquitto_destroy(val->mosq);
   mosquitto_lib_cleanup();
   return 0;
}

static void connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
   if(!result){
      /* 
       * Subscribe to broker information topics on successful connect.
       */
      struct collector_val *tmp_val = userdata;
      mosquitto_subscribe(mosq, NULL, (void*)(tmp_val->mqtt_topic), MQTT_QOS);
   }else{
      fprintf(stderr, "[Collector]: Connect failed\n");
   }
}

static void message_callback(struct mosquitto *mosq, void *userdata, 
                                       const struct mosquitto_message *message)
{
   struct collector_val *tmp_val = userdata;
   if(tmp_val->flag_en){
      if(message->payloadlen){
         #ifdef MONITORING_BENEVENTI
         /*
          * Parse the payload string to catch the measured value
          */
         char *subString = strtok(message->payload,";"); 
         tmp_val->sum_val += atof(subString);
         #else
         tmp_val->sum_val += atof(message->payload);
         #endif
         tmp_val->count_mean++;
      }
   }
}

