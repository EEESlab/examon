/* 
 * File:   pmu_pub.h
 * 
 * (c) 2017 ETH Zurich, [Integrated System Laboratory, D-ITET] 
 * (c) 2017 University of Bologna, [Department of Electrical, Electronic and Information Engineering, DEI]
 *
 * Contributed by:
 * Francesco Beneventi <francesco.beneventi@unibo.it>
 * Andrea Bartolini	<barandre@iis.ee.ethz.ch>
 *
 * Created on 14 dicembre 2016, 15.27
 */

#ifndef PMU_PUB_H
#define	PMU_PUB_H



/* DEBUG FLAGS */
//#define DEBUG
//#define READ_LOOP_TIMING



/* DAEMON OPTION */
#define USE_TIMER
#define RUN             0
#define START           1
#define STOP            2
#define STATUS          3
#define RESTART         4        
   

#ifdef DEBUG
#define DEBUGMSG(...) fprintf(__VA_ARGS__)
#else
#define DEBUGMSG /* nop */
#endif
 
    
#define PUB_METRIC(type, name, function, id, format) \
    sprintf(tmp_, "%s/%s/%d/%s", sysd->topic, type, id, name); \
    sprintf(data, format, function, sysd->tmpstr); \
    if(mosquitto_publish(mosq, NULL, tmp_, strlen(data), data, sysd->qos, false) != MOSQ_ERR_SUCCESS) { \
        fprintf(fp, "[MQTT]: Warning: cannot send message.\n");  \
    } \
    


    

#endif	/* PMU_PUB_H */

