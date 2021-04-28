# VERSION   1.2.1

FROM adoptopenjdk:8-jre-hotspot-bionic

#RUN apt-get -y update

RUN set -eux; \
	apt-get update; \
	apt-get install -y --no-install-recommends \
		wget \
                ca-certificates \
	; \
	rm -rf /var/lib/apt/lists/*

# KairosDB

ENV KAIROSDB_VERSION 1.2.1

RUN wget https://github.com/kairosdb/kairosdb/releases/download/v${KAIROSDB_VERSION}/kairosdb_${KAIROSDB_VERSION}-1_all.deb
RUN dpkg -i kairosdb_${KAIROSDB_VERSION}-1_all.deb
RUN rm kairosdb_${KAIROSDB_VERSION}-1_all.deb

ADD kairosdb.properties /opt/kairosdb/conf/kairosdb.properties
ADD kairosdb-env.sh /opt/kairosdb/bin/kairosdb-env.sh

EXPOSE 4242 8083 2003

# Set Kairos config vars
#ENV KAIROS_JETTY_PORT 8083
#ENV CASSANDRA_HOST_LIST 192.168.0.22:9160

ADD config-kairos.sh /usr/bin/config-kairos.sh

# Run kairosdb in foreground on boot
CMD ["/usr/bin/config-kairos.sh"]
