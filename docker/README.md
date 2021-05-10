# ExaMon Docker Setup
This setup will install all server-side components of the ExaMon framework:

 - MQTT broker and Db connector
 - Grafana
 - KairosDB
 - Cassandra

## Prerequisites
Since Cassandra is the component that requires the majority of resources, you can find more details about the suggested hardware configuration of the system that will host the services here:

[Hardware Configuration](https://cassandra.apache.org/doc/latest/operating/hardware.html#:~:text=While%20Cassandra%20can%20be%20made,at%20least%2032GB%20of%20RAM)

To install all the services needed by ExaMon we will use Docker and Docker Compose:

[Install Docker and Docker Compose](https://docs.docker.com/engine/installation/).


## Setup

### Clone the Git repository

First you will need to clone the Git repository:

```bash
git clone https://github.com/EEESlab/examon
cd examon/docker/
```

### Create Docker Services

Once you have the above setup, you need to create the Docker services:

```bash
docker-compose up -d
```

This will build the Docker images and fetch some prebuilt images and then start the services. You can refer to the `docker-compose.yml` file to see the full configuration. 

### Configure Grafana

Log in to the Grafana server using your browser and the default credentials:

http://localhost:3000

Follow the normal procedure for adding a new data source:

[Add a Datasource](https://grafana.com/docs/grafana/latest/datasources/add-a-data-source/)

Fill out the form with the following settings:

 - Type: `KairosDB`  
 - Name: `kairosdb` 
 - Url: http://kairosdb:8083 
 - Access: `Proxy`

## Usage Examples

### Collecting data using the "pmu_pub" plugin
Once all Docker services are running (can be started either by `docker-compose up -d` or `docker-compose start`), the MQTT broker is available at `TEST_SERVER` port `1883` where `TEST_SERVER` is the address of the server where the services run.

To test the installation we can use the `pmu_pub` plugin available in the `publishers/pmu_pub` folder of  this project.

After having installed and configured it on one or more test nodes we can start the data collection running for example:

```bash
[root@testnode00]$ ./pmu_pub -b TEST_SERVER -p 1883 -t org/myorg -s 1 run
```
If everything went well, the data are available both through the Grafana interface and using the `examon-client`. 


