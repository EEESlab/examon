/* Collector API Usage Example.
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
#include <signal.h>

/*
 * Collector macro.
 *
 * Note. The topics below listen to the pow_pkg and pow_dram metrics 
 *       of all CPU sockets in the compute node.
 * 
 */
#define TOPIC_POWER_PKG "org/myorg/cluster/testcluster/node/%s/plugin/pmu_pub/chnl/data/cpu/+/pow_pkg"
#define TOPIC_POWER_DRAM "org/myorg/cluster/testcluster/node/%s/plugin/pmu_pub/chnl/data/cpu/+/pow_dram"
#define MQTT_BROKER_IP "127.0.0.1"
#define MQTT_PORT 1883 // Default MQTT port

/*
 * Select number of CPU sockets in the compute node.
 */
#define NCPU 2

static volatile int keepRunning = 1;
void intHandler(int dummy) { keepRunning = 0;}

int main(int argc, char *argv[])
{
   volatile int count=0;
   char *brokerIp = MQTT_BROKER_IP;
   int port = MQTT_PORT, i=0;
   struct collector_val pow_pkg = { NULL, NULL, false, 0, 0, 0, {0}, {0} };
   struct collector_val pow_dram = { NULL, NULL, false, 0, 0, 0, {0}, {0} };

   /*
    * Select MQTT TOPIC
    */
   pow_pkg.mqtt_topic = (char *)malloc(256*sizeof(char));
   pow_dram.mqtt_topic = (char *)malloc(256*sizeof(char));
   sprintf(pow_pkg.mqtt_topic, TOPIC_POWER_PKG, argv[1]);
   sprintf(pow_dram.mqtt_topic, TOPIC_POWER_DRAM, argv[1]);

   /*
    * Init collectors and subscribe to the topic.
    */
   if(collector_init(&pow_pkg, brokerIp, port)){
      fprintf(stderr, "[Collector]: Init PowerPkg error.\n");
      return 1;
   }
   if(collector_init(&pow_dram, brokerIp, port)){
      fprintf(stderr, "[Collector]: Init PowerDram error.\n");
      return 1;
   }

   /*
    * Start monitoring the metrics.
    */
   if(collector_start(&pow_pkg)){
      fprintf(stderr, "[Collector]: Start PowerPkg error.\n");
      return 1;
   }
   if(collector_start(&pow_dram)){
      fprintf(stderr, "[Collector]: Start PowerDram error.\n");
      return 1;
   }

   signal(SIGINT, intHandler);

   /*
    * Stress all cores.
    */
   #pragma omp parallel
   while(keepRunning)
       count++;

   /*
    * Get mean value and continue the monitoring.
    */
   if(collector_get(&pow_pkg)){
      fprintf(stderr, "[Collector]: Get PowerPkg error.\n");
      return 1;
   }
   if(collector_get(&pow_dram)){
      fprintf(stderr, "[Collector]: Get PowerDram error.\n");
      return 1;
   }
   printf("\nTstart=%ld.%06ld[s], Tend=%ld.%06ld[s], MeanPowerPkg=%f[W]"
          "\nTstart=%ld.%06ld[s], Tend=%ld.%06ld[s], MeanPowerDram=%f[W]\n",
          pow_pkg.start.tv_sec, pow_pkg.start.tv_usec, 
          pow_pkg.end.tv_sec, pow_pkg.end.tv_usec, pow_pkg.mean_val*NCPU,
          pow_dram.start.tv_sec, pow_dram.start.tv_usec,
          pow_dram.end.tv_sec, pow_dram.end.tv_usec, pow_dram.mean_val*NCPU);
   keepRunning=1;

   /*
    * Stress all cores.
    */
   #pragma omp parallel
   while(keepRunning)
       count++;

   /*
    * End monitoring and get mean value.
    */
   if(collector_end(&pow_pkg)){
      fprintf(stderr, "[Collector]: End PowerPkg error.\n");
      return 1;
   }
   if(collector_end(&pow_dram)){
      fprintf(stderr, "[Collector]: End PowerDram error.\n");
      return 1;
   }
   printf("\nTstart=%ld.%06ld[s], Tend=%ld.%06ld[s], MeanPowerPkg=%f[W]"
          "\nTstart=%ld.%06ld[s], Tend=%ld.%06ld[s], MeanPowerDram=%f[W]\n",
          pow_pkg.start.tv_sec, pow_pkg.start.tv_usec, 
          pow_pkg.end.tv_sec, pow_pkg.end.tv_usec, pow_pkg.mean_val*NCPU,
          pow_dram.start.tv_sec, pow_dram.start.tv_usec,
          pow_dram.end.tv_sec, pow_dram.end.tv_usec, pow_dram.mean_val*NCPU);

   /*
    * Cleanup collectors.
    */   
   if(collector_clean(&pow_pkg)){
      fprintf(stderr, "[Collector]: Clean PowerPkg error.\n");
      return 1;
   }
   if(collector_clean(&pow_dram)){
      fprintf(stderr, "[Collector]: Clean PowerDram error.\n");
      return 1;
   }

   return 0;
}
