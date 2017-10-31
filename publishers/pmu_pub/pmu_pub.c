/*
 * 
 * pmu_pub.c : CPU data publisher over MQTT
 * 
 * (c) 2017 ETH Zurich, [Integrated System Laboratory, D-ITET] 
 * (c) 2017 University of Bologna, [Department of Electrical, Electronic and Information Engineering, DEI]
 *
 * Contributed by:
 * Francesco Beneventi <francesco.beneventi@unibo.it>
 * Andrea Bartolini	<barandre@iis.ee.ethz.ch>
 * 
 *  v0.2.3
 * 
 * Date:
 * 19/09/2014
 * 
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <inttypes.h>
#include <unistd.h>
#include <sched.h>
#include "mosquitto.h"
#include "iniparser.h"
#include "sensor_read_lib.h"
#include "perf_event_lib.h"
#include "pmu_pub.h"


struct mosquitto* mosq;
timer_t timer1;
int keepRunning;
char * sync_ck = "CK";
char const *version = "v0.2.3";


inline void pub_to_broker(struct sys_data * sysd, struct mosquitto * mosq);
void sig_handler(int sig);
int start_timer(struct sys_data * sysd);
inline void get_timestamp(char * buf);
void daemonize(char * pidfile);
int daemon_stop(char * pidfile);
int daemon_status(char * pidfile);
inline void my_sleep(float delay);
int enabled_host(char * host, char * host_whitelist_file, struct sys_data * sysd);
char **strsplit(const char* str, const char* delim, int* numtokens);
void usage();
int program_pmu(struct sys_data * sysd);
inline int vtune_is_running(void);



#ifdef USE_TIMER
//void samp_handler(int signum)

void samp_handler(int signo, siginfo_t *si, void *uc) {
    struct sys_data *sysd;
    sysd = (struct sys_data *) si->si_value.sival_ptr;
#else

void samp_handler(struct sys_data * sysd) {
#endif

#ifdef READ_LOOP_TIMING
    uint64_t before, after;
    before = read_tsc();
    get_timestamp(sysd->tmpstr);
    after = read_tsc();
    fprintf(stderr, "[DEBUG]: get_timestamp() CPU cycles: %lu \n", abs(before - after));
    before = read_tsc();
    mosquitto_publish(mosq, NULL, sysd->topic, strlen(sync_ck), sync_ck, 0, false);
    after = read_tsc();
    fprintf(stderr, "[DEBUG]: sync_ck() CPU cycles: %lu \n", abs(before - after));
    before = read_tsc();
    read_msr_data(sysd);
    after = read_tsc();
    fprintf(stderr, "[DEBUG]: read_msr_data() -ALL- CPU cycles: %lu \n", abs(before - after));
    before = read_tsc();
    pub_to_broker(sysd, mosq);
    after = read_tsc();
    fprintf(stderr, "[DEBUG]: pub_to_broker() -ALL- CPU cycles: %lu \n", abs(before - after));

#else
    get_timestamp(sysd->tmpstr);
    mosquitto_publish(mosq, NULL, sysd->topic, strlen(sync_ck), sync_ck, 0, false);
    read_msr_data(sysd);
    pub_to_broker(sysd, mosq);
#endif

}

/* on_connect_callback */
void on_connect_callback(struct mosquitto *mosq, void *obj, int result) {
    struct sys_data *sysd;


    assert(obj);
    sysd = (struct sys_data *) obj;

    fprintf(stderr, "[MQTT]: Subscribing to command topic...\n");
    if (!result) {
        mosquitto_subscribe(mosq, NULL, sysd->cmd_topic, 0); // QoS!
        fprintf(stderr, "[MQTT]: Ready!\n");
    } else {
        fprintf(stderr, "%s\n", mosquitto_connack_string(result));
    }
}

/* on message callback */
void on_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message) {

    char * data = NULL;
    struct sys_data *sysd;
    char brokerHost[256];
    int brokerPort;
    int i;
    char buffer[BUFSIZ];
    float dt;
    char delimit[] = " \t\r\n\v\f,"; //POSIX whitespace characters
    char * tmpstr = NULL;

    assert(obj);
    sysd = (struct sys_data *) obj;

    fprintf(stderr, "[MQTT]: cmd received\n");
    if (message->payloadlen) {
        /* print data */
        data = (char *) (message->payload); // get payload

        // parse commands
        if (!strncmp(data, "-s", 2)) {
            sscanf(data, "%*s%f", &sysd->dT);
            fprintf(stderr, "New dT: %f\n", sysd->dT);
#ifdef USE_TIMER
            timer_delete(timer1);
            start_timer(sysd);
#endif  
        }

        if (!strncmp(data, "-b", 2)) {
            sscanf(data, "%*s%s%d", brokerHost, &brokerPort);
            fprintf(stderr, "New brokerHost: %s\n", brokerHost);
            fprintf(stderr, "New brokerPort: %d\n", brokerPort);

            if (mosquitto_disconnect(mosq) != MOSQ_ERR_SUCCESS) {
                fprintf(stderr, "\n [MQTT]: Error while disconnecting!\n");
            }
            sleep(1);
            if (mosquitto_connect(mosq, brokerHost, brokerPort, 1000) != MOSQ_ERR_SUCCESS) {
                fprintf(stderr, "\n [MQTT]: Could not connect to broker\n");
                mosquitto_connect(mosq, sysd->brokerHost, sysd->brokerPort, 1000);
            }
            mosquitto_loop_start(mosq);
        }

        if (!strncmp(data, "-t", 2)) {
            sscanf(data, "%*s%s", buffer);
            sysd->topic = strdup(buffer);
            fprintf(stderr, "New topic: %s\n", sysd->topic);
        }

        if (!strncmp(data, "-i", 2)) {
            sscanf(data, "%*s%s", buffer);
            sysd->cmd_topic = strdup(buffer);
            fprintf(stderr, "New cmd topic: %s\n", sysd->cmd_topic);
        }

        if (!strncmp(data, "-c", 2)) {
            sscanf(data, "%*s%d", &sysd->extra_counters);
            fprintf(stderr, "New extra_couters value: %d\n", sysd->extra_counters);
        }

        if (!strncmp(data, "-P", 2)) {
            int temp = 0;
            sscanf(data, "%*s%d", &temp);

            if (temp != sysd->use_perf) {
                sysd->use_perf = temp;
                fprintf(stderr, "New use_perf value: %d\n", sysd->use_perf);
                perf_disable_per_core(sysd->fdd, sysd);
                free(sysd->fdd);
                program_pmu(sysd);
            }
        }

        if (!strncmp(data, "-e", 2)) {

            perf_disable_per_core(sysd->fdd, sysd);
            free(sysd->fdd);

            sysd->my_events = strsplit(data + 2, delimit, &sysd->perf_num_events);

            program_pmu(sysd);
        }
    }
}

void pub_to_broker(struct sys_data * sysd, struct mosquitto * mosq) {

    FILE* fp;
    char data[255];
    char tmp_[255];
    char tmp_s[255];
    int cpuid;
    int coreid;
    int i;

    fp = fopen(sysd->logfile, "a");

    for (cpuid = 0; cpuid < sysd->NCPU; cpuid++) {
        PUB_METRIC("cpu", "tsc", sysd->cpu_data[cpuid].tsc, cpuid, "%lu;%s");
        PUB_METRIC("cpu", "temp_pkg", sysd->cpu_data[cpuid].tempPkg, cpuid, "%u;%s");
        if (sysd->DRAM_SUPP == 1) {
            PUB_METRIC("cpu", "erg_dram", sysd->cpu_data[cpuid].powDramC, cpuid, "%u;%s");
        }
        if (sysd->PP1_SUPP == 1) {
            PUB_METRIC("cpu", "erg_cores", sysd->cpu_data[cpuid].powPP1, cpuid, "%u;%s");
        }

        PUB_METRIC("cpu", "erg_pkg", sysd->cpu_data[cpuid].powPkg, cpuid, "%u;%s");
        PUB_METRIC("cpu", "erg_units", sysd->cpu_data[cpuid].ergU, cpuid, "%u;%s");
        PUB_METRIC("cpu", "freq_ref", sysd->nom_freq, cpuid, "%f;%s");
        if (sysd->extra_counters == 1) {
            PUB_METRIC("cpu", "C2", sysd->cpu_data[cpuid].C2, cpuid, "%lu;%s");
            PUB_METRIC("cpu", "C3", sysd->cpu_data[cpuid].C3, cpuid, "%lu;%s");
            PUB_METRIC("cpu", "C6", sysd->cpu_data[cpuid].C6, cpuid, "%lu;%s");
            if (sysd->CPU_MODEL == HASWELL_EP) {
                PUB_METRIC("cpu", "uclk", sysd->cpu_data[cpuid].uclk, cpuid, "%lu;%s");
            }
            //if (sysd->use_perf){
            if (1) { // Currently always read and send uncore events 
                for (i = 0; i < sysd->perf_num_events; i++) {
                    if (sysd->is_uncore_event[i]) {
                        PUB_METRIC("cpu", sysd->my_events[i], sysd->core_data[cpuid * (sysd->NCORE / sysd->NCPU)].perf_event[i].value, cpuid, "%lu;%s");
                    }
                }
            }
        }
    }

    for (coreid = 0; coreid < sysd->NCORE; coreid++) {
        PUB_METRIC("core", "tsc", sysd->core_data[coreid].tsc, coreid, "%lu;%s");
        PUB_METRIC("core", "temp", sysd->core_data[coreid].temp, coreid, "%d;%s");
        PUB_METRIC("core", "instr", sysd->core_data[coreid].instr, coreid, "%lu;%s");
        PUB_METRIC("core", "clk_curr", sysd->core_data[coreid].clk_curr, coreid, "%lu;%s");
        PUB_METRIC("core", "clk_ref", sysd->core_data[coreid].clk_ref, coreid, "%lu;%s");
        if (sysd->extra_counters == 1) {
            PUB_METRIC("core", "C3", sysd->core_data[coreid].C3, coreid, "%lu;%s");
            PUB_METRIC("core", "C6", sysd->core_data[coreid].C6, coreid, "%lu;%s");
            PUB_METRIC("core", "aperf", sysd->core_data[coreid].aperf, coreid, "%lu;%s");
            PUB_METRIC("core", "mperf", sysd->core_data[coreid].mperf, coreid, "%lu;%s");
            if (!sysd->use_perf) {
                for (i = 0; i < sysd->perf_num_events; i++) {
                    if (!sysd->is_uncore_event[i]) {
                        PUB_METRIC("core", sysd->my_events[i], sysd->core_data[coreid].pmc[sysd->core_pmu_events[coreid].event_pmu_idx[i]], coreid, "%lu;%s");
                    }
                }
            } else {
                for (i = 0; i < sysd->perf_num_events; i++) {
                    if (!sysd->is_uncore_event[i]) {
                        PUB_METRIC("core", sysd->my_events[i], sysd->core_data[coreid].perf_event[i].value, coreid, "%lu;%s");
                    }
                }
            }
        }
    }

    fclose(fp);
}

void sig_handler(int sig) {

#ifdef USE_TIMER
    timer_delete(timer1);
#endif
    keepRunning = 0;
    printf(" Clean exit!\n");
}

int start_timer(struct sys_data * sysd) {

    struct itimerspec new_value, old_value;
    struct sigaction action;
    struct sigevent sevent;
    sigset_t set;
    int signum;
    float dT = 0;

    memset(&action, 0, sizeof (struct sigaction));
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = samp_handler;
    if (sigaction(SIGRTMAX, &action, NULL) == -1)
        perror("sigaction");


    memset(&sevent, 0, sizeof (sevent));
    sevent.sigev_notify = SIGEV_SIGNAL;
    sevent.sigev_signo = SIGRTMAX;
    sevent.sigev_value.sival_ptr = sysd;

    dT = sysd->dT;

    if (timer_create(CLOCK_MONOTONIC, &sevent, &timer1) == 0) {

        new_value.it_interval.tv_sec = (int) dT;
        new_value.it_interval.tv_nsec = (dT - (int) dT)*1000000000;
        new_value.it_value.tv_sec = (int) dT;
        new_value.it_value.tv_nsec = (dT - (int) dT)*1000000000;

        my_sleep(dT); //align

        if (timer_settime(timer1, 0, &new_value, &old_value) != 0) {
            perror("timer_settime");
            return 1;
        }

    } else {
        perror("timer_create");
        return 1;
    }
    return 0;

}

void get_timestamp(char * buf) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    sprintf(buf, "%.3f", tv.tv_sec + (tv.tv_usec / 1000000.0));
}

void daemonize(char * pidfile) {

    pid_t process_id = 0;
    pid_t sid = 0;
    FILE *fp = NULL;

    process_id = fork();
    if (process_id < 0) {
        printf("fork failed!\n");
        exit(1);
    }
    if (process_id > 0) {
        printf("process_id of child process %d \n", process_id);
        fp = fopen(pidfile, "w");
        fprintf(fp, "%d\n", process_id);
        fclose(fp);
        exit(0);
    }
    umask(0);
    sid = setsid();
    if (sid < 0) {
        exit(1);
    }
    //chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

}

int daemon_stop(char * pidfile) {

    FILE* fp;
    char cmd[100];
    char pid[100];
    pid_t pid_;
    int ret = 0;


    printf("open file %s!\n", pidfile);
    fp = fopen(pidfile, "r");
    if (fp != NULL) {
        fscanf(fp, "%s", pid);
        printf("Process pid = %s!\n", pid);
        pid_ = atoi(pid);
        ret = 1;
        fclose(fp);
    }
    if (daemon_status(pidfile)) {
        ret = 1;
    } else {
        printf("Daemon is not running!\n");
        ret = 0;
    }
    if (ret == 1) {
        printf("killing pid: %d!\n", pid_);
        kill(pid_, SIGINT);
        sleep(1);
    }

    return ret;
}

int daemon_status(char * pidfile) {

    FILE* fp;
    FILE* fd;
    struct stat sts;
    char cmd[100];
    char pid[100];
    char name[100];
    int ret = 0;

    fp = fopen(pidfile, "r");
    if (fp != NULL) {
        fscanf(fp, "%s", pid);
        sprintf(cmd, "/proc/%s/comm", pid);
        fd = fopen(cmd, "r");
        if (fd != NULL) {
            fscanf(fd, "%s", name);
            if (strncmp(name, "pmu_pub", 7) != 0) {
                printf("Process does not exist!\n");
                ret = 0;
            } else {
                printf("Daemon is running!\n");
                ret = 1;
            }
            fclose(fd);
        } else {
            printf("Process does not exist!\n");
            ret = 0;
        }
        fclose(fp);
    } else {
        printf("Daemon is not running!\n");
        ret = 0;
    }
    return ret;
}

int enabled_host(char * host, char * host_whitelist_file, struct sys_data * sysd) {

    FILE *fd;
    char buffer[BUFSIZ], *result;
    char item[BUFSIZ];
    int ret = -1;
    char brokerHost[256];
    char tmpstr[256];
    int brokerPort;

    fd = fopen(host_whitelist_file, "r");
    if (fd == NULL) {
        strcpy(tmpstr, "/etc/");
        strcat(tmpstr, host_whitelist_file);
        fd = fopen(tmpstr, "r");
        if (fd == NULL) {
            printf("No '%s' file found: Enable on ALL hosts\n", host_whitelist_file);
            return 0;
            //exit(1);
        }
    }

    while (1) {
        result = fgets(buffer, BUFSIZ, fd);
        if (result == NULL) break;

        //search for Group Broker 'BROKER ip:port' format
        if (!strncmp(result, "[BROKER:]", 9)) {
            sscanf(result, "%*s%s%d", brokerHost, &brokerPort);
            printf("Update brokerhost settings to: %s:%d\n", brokerHost, brokerPort);
            sysd->brokerHost = strdup(brokerHost);
            sysd->brokerPort = brokerPort;
        }

        sscanf(result, "%s", item);
        if (strcmp(item, host) == 0) {
            ret = 0;
            break;
        }
    }

    fclose(fd);

    return ret;
}

void usage() {

    printf("pmu_pub: PMU sensors plugin\n\n");
    printf("usage: pmu_pub [-h] [-b B] [-p P] [-t T] [-q Q] [-s S] [-x X]\n");
    printf("                     [-l L] [-e E] [-c C] [-P P] [-v] \n");
    printf("                     {run,start,stop,restart}\n");
    printf("\n");
    printf("positional arguments:\n");
    printf("  {run,start,stop,restart}\n");
    printf("                        Run mode\n");
    printf("\n");
    printf("optional arguments:\n");
    printf("  -h                    Show this help message and exit\n");
    printf("  -b B                  IP address of the MQTT broker\n");
    printf("  -p P                  Port of the MQTT broker\n");
    printf("  -s S                  Sampling interval (seconds)\n");
    printf("  -t T                  Output topic\n");
    printf("  -q Q                  Message QoS level (0,1,2)\n");
    printf("  -x X                  Pid filename dir\n");
    printf("  -l L                  Log filename dir\n");
    printf("  -c C                  Enable or disable extra counters (Bool)\n");
    printf("  -e E                  Perf events list (comma separated)\n");
    printf("  -P P                  Enable or disable perf subsystem (Bool)\n");
    printf("  -v                    Print version number\n");

    exit(0);

}

inline void my_sleep(float delay) {
    struct timespec sleep_intrval;
    struct timeval tp;
    double now;

    gettimeofday(&tp, NULL);
    now = (double) tp.tv_sec + tp.tv_usec * 1e-6;
    delay -= fmod(now, delay);

    sleep_intrval.tv_nsec = (delay - (int) delay)*1e9;
    sleep_intrval.tv_sec = (int) delay;
    //printf("%lld.%.9ld\n", (long long)sleep_intrval.tv_sec, sleep_intrval.tv_nsec);     
    nanosleep(&sleep_intrval, NULL);

}

int init_pmu_pub(struct sys_data * sysd) {

    memset(sysd, 0, sizeof (*sysd));

    sysd->NCPU = 0; // NCPU;
    sysd->NCORE = 0; // NCORE;
    sysd->CPU_MODEL = -1; // CPU_MODEL;

    /*       
        sysd->HT_EN =  0;                                      // HT_EN;
        sysd->nom_freq = 0.0;                                  // nom_freq;
        sysd->DRAM_SUPP = 0;                                   // DRAM_SUPP;
        sysd->PP1_SUPP = 0;                                    // PP1_SUPP;
    
     */
    memset(sysd->dieTemp, 100, sizeof (*sysd->dieTemp)); // dieTemp
    memset(sysd->dieTempEn, 0, sizeof (*sysd->dieTempEn)); // dieTempEn

    sysd->cpu_data = NULL; // cpu_data 
    sysd->core_data = NULL; // core_data 
    strcpy(sysd->logfile, ""); // logfile
    strcpy(sysd->tmpstr, ""); // tmpstr
    sysd->hostid = NULL; // hostid;
    sysd->topic = NULL; // topic;
    sysd->cmd_topic = NULL; // cmd_topic;
    sysd->brokerHost = NULL; // brokerHost;

    sysd->brokerPort = 1883; // brokerPort;
    sysd->qos = 0; // qos;
    sysd->dT = 2.0; // dT;
    sysd->extra_counters = 1; // extra_counters;

    sysd->num_core_events = 0;


    return 0;

}

int cleanup_pmu_pub(struct sys_data * sysd) {

    free(sysd->cpu_data);
    free(sysd->core_data);

    return 0;
}

char **strsplit(const char* str, const char* delim, int* numtokens) {

    char *s = strdup(str);
    int tokens_alloc = 1;
    int tokens_used = 0;
    char **tokens = calloc(tokens_alloc, sizeof (char*));
    char *token, *strtok_ctx;


    for (token = strtok_r(s, delim, &strtok_ctx);
            token != NULL;
            token = strtok_r(NULL, delim, &strtok_ctx)) {
        // check if we need to allocate more space for tokens
        if (tokens_used == tokens_alloc) {
            tokens_alloc *= 2;
            tokens = realloc(tokens, tokens_alloc * sizeof (char*));
        }
        tokens[tokens_used++] = strdup(token);
    }
    // cleanup
    if (tokens_used == 0) {
        free(tokens);
        tokens = NULL;
    } else {
        tokens = realloc(tokens, tokens_used * sizeof (char*));
    }
    *numtokens = tokens_used;
    free(s);
    return tokens;
}

int program_pmu(struct sys_data * sysd) {
    int i = 0;

    /* Perf events */
    printf("PMU num events requested:\t %d\n", sysd->perf_num_events);

    if (sysd->perf_num_events) {
        // PMU events programming
        printf("\n\nPMU events programming:\n");

        //allocate per core event data
        for (i = 0; i < sysd->NCORE; i++) {
            sysd->core_data[i].perf_event = malloc(sysd->perf_num_events * sizeof (perf_read_format));
        }

        //allocate uncore event flag array (1=uncore event) 
        sysd->is_uncore_event = calloc(sysd->perf_num_events, sizeof (int));

        //allocate perf driver file descriptors 
        sysd->fdd = malloc(sysd->NCORE * sizeof (int *));
        for (i = 0; i < sysd->NCORE; i++) {
            sysd->fdd[i] = malloc(sysd->perf_num_events * sizeof (int));
        }

        // program perf
#ifdef DEBUG
        before = read_tsc();
        perf_program_os_events(sysd->perf_num_events, sysd->my_events, sysd->fdd, sysd);
        after = read_tsc();
        fprintf(stderr, "[DEBUG]: perf_program_os_events() overhead CPU cycles: %d \n", abs(before - after));
#else
        perf_program_os_events(sysd->perf_num_events, sysd->my_events, sysd->fdd, sysd);
#endif
    }

    return 0;
}

inline int vtune_is_running(void) {

    const char* env = NULL;

    env = getenv("VTUNE_HOME");
    DEBUGMSG(stderr, "vtune_is_running(): env=%s\n", env);
    if (env != NULL) {
        DEBUGMSG(stderr, "vtune_is_running(): env=%s\n", env);
        return 1;
    }
    return 0;
}

void main(int argc, char* argv[]) {

    int mosqMajor, mosqMinor, mosqRevision;
    FILE *fp = stderr;
    float dT = 5;
    int daemon = -1;
    char hostname[256];
    char pidfile[256];
    char logfile[256];
    char pidfiledir[256];
    char logfiledir[256];
    char buffer[1024];
    char* conffile = "pmu_pub.conf";
    char* host_whitelist_file = "host_whitelist";
    char* data_topic_string = "plugin/pmu_pub/chnl/data";
    char* cmd_topic_string = "plugin/pmu_pub/chnl/cmd";
    char tmpstr[256];
    int i;
    dictionary *ini;
    char conf_events[1024];
    char delimit[] = " \t\r\n\v\f,"; //POSIX whitespace characters
    char * token;
    struct sys_data sysd_;
#ifdef DEBUG
    uint64_t before, after;
#endif

    init_pmu_pub(&sysd_);

    if (argc == 1)
        fprintf(fp, "Using configuration in file: %s\n", conffile);
    ini = iniparser_load(conffile);
    if (ini == NULL) { // search in /etc/
        strcpy(tmpstr, "/etc/");
        strcat(tmpstr, conffile);
        ini = iniparser_load(tmpstr);
        if (ini == NULL) {
            fprintf(fp, "Cannot parse file: %s\n", conffile);
            usage();
        }
    }

    fprintf(fp, "%s Version: %s\n", argv[0], version);
    fprintf(fp, "\nConf file parameters:\n\n");
    iniparser_dump(ini, stderr);

    sysd_.brokerHost = iniparser_getstring(ini, "MQTT:brokerHost", NULL);
    sysd_.brokerPort = iniparser_getint(ini, "MQTT:brokerPort", 1883);
    sysd_.topic = iniparser_getstring(ini, "MQTT:topic", NULL);
    sysd_.cmd_topic = iniparser_getstring(ini, "MQTT:cmd_topic", NULL);
    sysd_.qos = iniparser_getint(ini, "MQTT:qos", 0);
    sysd_.dT = iniparser_getdouble(ini, "Daemon:dT", 1);
    daemon = iniparser_getboolean(ini, "Daemon:daemonize", 0);
    strcpy(pidfiledir, iniparser_getstring(ini, "Daemon:pidfilename", "./"));
    strcpy(logfiledir, iniparser_getstring(ini, "Daemon:logfilename", "./"));
    sysd_.hostid = iniparser_getstring(ini, "Daemon:hostid", "node");
    sysd_.extra_counters = iniparser_getboolean(ini, "Daemon:extracounters", 1);
    strcpy(conf_events, iniparser_getstring(ini, "PMU:events", ""));


    if (argc > 1) {
        fprintf(fp, "\nCommand line parameters (override):\n\n");
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-p") == 0) // broker port
            {
                sysd_.brokerPort = atoi(argv[i + 1]);
                fprintf(fp, "New broker port: %d\n", sysd_.brokerPort);
            } else if (strcmp(argv[i], "-t") == 0) // topic name
            {
                sysd_.topic = strdup(argv[i + 1]);
                fprintf(fp, "New topic name: %s\n", sysd_.topic);
            } else if (strcmp(argv[i], "-i") == 0) // cmd topic name
            {
                sysd_.cmd_topic = strdup(argv[i + 1]);
                fprintf(fp, "New cmd topic name: %s\n", sysd_.cmd_topic);
            } else if (strcmp(argv[i], "-b") == 0) // broker ip address
            {
                sysd_.brokerHost = strdup(argv[i + 1]);
                fprintf(fp, "New brokerhost: %s\n", sysd_.brokerHost);
            } else if (strcmp(argv[i], "-q") == 0) // QOS
            {
                sysd_.qos = atoi(argv[i + 1]);
                fprintf(fp, "New QoS: %d\n", sysd_.qos);
            } else if (strcmp(argv[i], "-c") == 0) // extra_counters
            {
                sysd_.extra_counters = atoi(argv[i + 1]);
                fprintf(fp, "New extra_counters: %d\n", sysd_.extra_counters);
            } else if (strcmp(argv[i], "-s") == 0) // sampling interval
            {
                sysd_.dT = atof(argv[i + 1]);
                fprintf(fp, "New Daemon dT: %f\n", sysd_.dT);
            } else if (strcmp(argv[i], "-n") == 0) // unique hostid
            {
                sysd_.hostid = strdup(argv[i + 1]);
                fprintf(fp, "New hostid: %s\n", sysd_.hostid);
            } else if (strcmp(argv[i], "-x") == 0) // pidfiledir
            {
                strcpy(pidfiledir, argv[i + 1]);
                fprintf(fp, "New pidfiledir: %s\n", pidfiledir);
            } else if (strcmp(argv[i], "-l") == 0) // logfiledir
            {
                strcpy(logfiledir, argv[i + 1]);
                fprintf(fp, "New logfile: %s\n", logfiledir);
            } else if (strcmp(argv[i], "-h") == 0) // help
            {
                usage();
            } else if (strcmp(argv[i], "-e") == 0) // PMU events
            {
                strcpy(conf_events, argv[i + 1]);
                fprintf(fp, "New PMU events values: %s\n", conf_events);
            } else if (strcmp(argv[i], "-P") == 0) // PMU events
            {
                sysd_.use_perf = atoi(argv[i + 1]);
                fprintf(fp, "New use_perf value: %d\n", sysd_.use_perf);
            } else if (strcmp(argv[i], "-v") == 0) // daemonize
            {
                fprintf(fp, "Version: %s\n", version);
                exit(0);
            } else if (strcmp(argv[i], "start") == 0) // daemonize
            {
                daemon = START;
            } else if (strcmp(argv[i], "run") == 0) // normal execution (no daemon)
            {
                daemon = RUN;
            } else if (strcmp(argv[i], "stop") == 0) // daemon stop
            {
                daemon = STOP;
            } else if (strcmp(argv[i], "status") == 0) // daemon status
            {
                daemon = STATUS;
            } else if (strcmp(argv[i], "restart") == 0) // daemon restart
            {
                daemon = RESTART;
            }
        }
    }

    if (gethostname(hostname, 255) != 0) {
        fprintf(fp, "[MQTT]: Cannot get hostname.\n");
        exit(EXIT_FAILURE);
    }
    hostname[255] = '\0';
    printf("Hostname: %s\n", hostname);

    sprintf(pidfile, "%s%s_%s", pidfiledir, hostname, "pmu_pub.pid");
    sprintf(sysd_.logfile, "%s%s_%s", logfiledir, hostname, "pmu_pub.log");


    sprintf(buffer, "%s/%s/%s/%s", sysd_.topic, "node", hostname, cmd_topic_string);
    sysd_.cmd_topic = strdup(buffer);
    fprintf(fp, "Cmd topic name: %s\n", sysd_.cmd_topic);
    sprintf(buffer, "%s/%s/%s/%s", sysd_.topic, "node", hostname, data_topic_string);
    sysd_.topic = strdup(buffer);
    fprintf(fp, "Data topic name: %s\n", sysd_.topic);


    if (enabled_host(hostname, host_whitelist_file, &sysd_) != 0) {
        daemon_stop(pidfile); // stop if running!
        fprintf(fp, "[MQTT]: Host not enabled. Exiting...\n");
        exit(0);
    }

    switch (daemon) {
        case START:
            if (daemon_status(pidfile)) {
                fprintf(fp, "Exiting...\n");
                exit(0);
            }
            fprintf(fp, "Start now...\n");
            fprintf(fp, "Daemon mode...\n");
            fprintf(fp, "Open log file: %s\n", sysd_.logfile);
            fp = fopen(sysd_.logfile, "w");
            daemonize(pidfile);
            break;
        case STOP:
            daemon_stop(pidfile);
            exit(0);
            break;
        case RUN:
            if (daemon_status(pidfile)) {
                fprintf(fp, "Exiting...\n");
                exit(0);
            }
            fprintf(fp, "Start now...\n");
            break;
        case STATUS:
            daemon_status(pidfile);
            exit(0);
            break;
        case RESTART:
            if (daemon_status(pidfile) == 0) {
                fprintf(fp, "Exiting...\n");
                exit(0);
            }
            daemon_stop(pidfile);
            fprintf(fp, "Restart now...\n");
            fprintf(fp, "Daemon mode...\n");
            fprintf(fp, "Open log file: %s\n", sysd_.logfile);
            fp = fopen(sysd_.logfile, "w");
            daemonize(pidfile);
            break;
        default:
            fprintf(fp, "Exiting...\n");
            exit(0);
            break;
    }


    if (detect_cpu_model(&sysd_) < 0) {
        fprintf(fp, "[MQTT]: Error in detecting CPU model.\n");
        exit(EXIT_FAILURE);
    }

    if (detect_topology(&sysd_) != 0) {
        fprintf(fp, "[MQTT]: Cannot get host topology.\n");
        exit(EXIT_FAILURE);
    }

    if (detect_nominal_frequency(&sysd_) < 0) {
        fprintf(fp, "[MQTT]: Error in detecting Nominal Frequecy.\n");
        exit(EXIT_FAILURE);
    }

    // Allocate per cpu and per core data
    sysd_.cpu_data = (per_cpu_data *) malloc(sizeof (per_cpu_data) * sysd_.NCPU);
    sysd_.core_data = (per_core_data *) malloc(sizeof (per_core_data) * sysd_.NCORE);

    // config PMU
    /* Perf events */
    printf("\n\nRead PMU events from conf:\n");

    sysd_.my_events = strsplit(conf_events, delimit, &sysd_.perf_num_events);

    program_pmu(&sysd_);

    // config MSR
    program_msr(&sysd_);


#ifdef DEBUG
    uint64_t acc = 0;
    for (i = 0; i < 100; i++) {
        before = read_tsc();
        after = read_tsc();
        acc += abs(before - after);
    }
    fprintf(stderr, "[DEBUG]: read_tsc() overhead CPU cycles: %f \n", (float) acc / 100.0);
#endif  


    // MQTT
    mosquitto_lib_version(&mosqMajor, &mosqMinor, &mosqRevision);
    fprintf(fp, "[MQTT]: Initializing Mosquitto Library Version %d.%d.%d\n", mosqMajor, mosqMinor, mosqRevision);
    mosquitto_lib_init();

    //mosq = mosquitto_new(hostname, false, NULL);
    mosq = mosquitto_new(NULL, true, &sysd_);
    if (!mosq) {
        perror(NULL);
        exit(EXIT_FAILURE);
    }

    mosquitto_connect_callback_set(mosq, on_connect_callback);
    mosquitto_message_callback_set(mosq, on_message_callback);


    fprintf(fp, "[MQTT]: Connecting to broker %s on port %d\n", sysd_.brokerHost, sysd_.brokerPort);
    while (mosquitto_connect(mosq, sysd_.brokerHost, sysd_.brokerPort, 1000) != MOSQ_ERR_SUCCESS) {
        fprintf(fp, "\n [MQTT]: Could not connect to broker\n");
        fprintf(fp, "\n [MQTT]: Retry in 60 seconds...\n");
        sleep(60);
        //exit(EXIT_FAILURE);
    }
    if (fp != stderr)
        fclose(fp);


    mosquitto_loop_start(mosq);

    signal(SIGINT, sig_handler); // Ctrl-C (2)
    signal(SIGTERM, sig_handler); // (15)
    keepRunning = 1;


    /* Main loop */
#ifdef USE_TIMER
    start_timer(&sysd_);
    while (keepRunning) {

        pause();

    }
#else 
    while (keepRunning) {

        my_sleep(sysd_.dT);

        samp_handler(&sysd_);

    }
#endif
    
    
    fp = fopen(sysd_.logfile, "a");
    fprintf(fp, "\n [MQTT]: exiting loop... \n");
    fprintf(fp, "\n [MQTT]: Disconnecting from broker... \n");
    if (mosquitto_disconnect(mosq) != MOSQ_ERR_SUCCESS) {
        fprintf(fp, "\n [MQTT]: Error while disconnecting!\n");
        exit(EXIT_FAILURE);
    }
    fclose(fp);
    mosquitto_destroy(mosq);
    iniparser_freedict(ini);
    cleanup_pmu_pub(&sysd_);

    perf_disable_per_core(sysd_.fdd, &sysd_);
    free(sysd_.fdd);

    reset_PMU(&sysd_);
    clean_PMU(&sysd_);

    exit(0);

}
