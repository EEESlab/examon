/*
Copyright (c) 2010-2013 Roger Light <roger@atchoo.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of mosquitto nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include <config.h>

#ifdef WITH_PERSISTENCE

#ifndef WIN32
#include <arpa/inet.h>
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <mosquitto_broker.h>
#include <memory_mosq.h>
#include <persist.h>
#include <time_mosq.h>
#include "util_mosq.h"

static uint32_t db_version;


static int _db_restore_sub(struct mosquitto_db *db, const char *client_id, const char *sub, int qos);

static struct mosquitto *_db_find_or_add_context(struct mosquitto_db *db, const char *client_id, uint16_t last_mid)
{
	struct mosquitto *context;
	struct mosquitto **tmp_contexts;
	int i;

	context = NULL;
	for(i=0; i<db->context_count; i++){
		if(db->contexts[i] && !strcmp(db->contexts[i]->id, client_id)){
			context = db->contexts[i];
			break;
		}
	}
	if(!context){
		context = mqtt3_context_init(-1);
		if(!context) return NULL;

		context->clean_session = false;

		for(i=0; i<db->context_count; i++){
			if(!db->contexts[i]){
				db->contexts[i] = context;
				break;
			}
		}
		if(i==db->context_count){
			db->context_count++;
			tmp_contexts = _mosquitto_realloc(db->contexts, sizeof(struct mosquitto*)*db->context_count);
			if(tmp_contexts){
				db->contexts = tmp_contexts;
				db->contexts[db->context_count-1] = context;
			}else{
				mqtt3_context_cleanup(db, context, true);
				return NULL;
			}
		}
		context->id = _mosquitto_strdup(client_id);
		context->db_index = i;
	}
	if(last_mid){
		context->last_mid = last_mid;
	}
	return context;
}

static int mqtt3_db_client_messages_write(struct mosquitto_db *db, FILE *db_fptr, struct mosquitto *context)
{
	uint32_t length;
	dbid_t i64temp;
	uint16_t i16temp, slen;
	uint8_t i8temp;
	struct mosquitto_client_msg *cmsg;

	assert(db);
	assert(db_fptr);
	assert(context);

	cmsg = context->msgs;
	while(cmsg){
		slen = strlen(context->id);

		length = htonl(sizeof(dbid_t) + sizeof(uint16_t) + sizeof(uint8_t) +
				sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) +
				sizeof(uint8_t) + 2+slen);

		i16temp = htons(DB_CHUNK_CLIENT_MSG);
		write_e(db_fptr, &i16temp, sizeof(uint16_t));
		write_e(db_fptr, &length, sizeof(uint32_t));

		i16temp = htons(slen);
		write_e(db_fptr, &i16temp, sizeof(uint16_t));
		write_e(db_fptr, context->id, slen);

		i64temp = cmsg->store->db_id;
		write_e(db_fptr, &i64temp, sizeof(dbid_t));

		i16temp = htons(cmsg->mid);
		write_e(db_fptr, &i16temp, sizeof(uint16_t));

		i8temp = (uint8_t )cmsg->qos;
		write_e(db_fptr, &i8temp, sizeof(uint8_t));

		i8temp = (uint8_t )cmsg->retain;
		write_e(db_fptr, &i8temp, sizeof(uint8_t));

		i8temp = (uint8_t )cmsg->direction;
		write_e(db_fptr, &i8temp, sizeof(uint8_t));

		i8temp = (uint8_t )cmsg->state;
		write_e(db_fptr, &i8temp, sizeof(uint8_t));

		i8temp = (uint8_t )cmsg->dup;
		write_e(db_fptr, &i8temp, sizeof(uint8_t));

		cmsg = cmsg->next;
	}

	return MOSQ_ERR_SUCCESS;
error:
	_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	return 1;
}


static int mqtt3_db_message_store_write(struct mosquitto_db *db, FILE *db_fptr)
{
	uint32_t length;
	dbid_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp, slen;
	uint8_t i8temp;
	struct mosquitto_msg_store *stored;
	bool force_no_retain;

	assert(db);
	assert(db_fptr);

	stored = db->msg_store;
	while(stored){
		if(!strncmp(stored->msg.topic, "$SYS", 4)){
			/* Don't save $SYS messages as retained otherwise they can give
			 * misleading information when reloaded. They should still be saved
			 * because a disconnected durable client may have them in their
			 * queue. */
			force_no_retain = true;
		}else{
			force_no_retain = false;
		}
		length = htonl(sizeof(dbid_t) + 2+strlen(stored->source_id) +
				sizeof(uint16_t) + sizeof(uint16_t) +
				2+strlen(stored->msg.topic) + sizeof(uint32_t) +
				stored->msg.payloadlen + sizeof(uint8_t) + sizeof(uint8_t));

		i16temp = htons(DB_CHUNK_MSG_STORE);
		write_e(db_fptr, &i16temp, sizeof(uint16_t));
		write_e(db_fptr, &length, sizeof(uint32_t));

		i64temp = stored->db_id;
		write_e(db_fptr, &i64temp, sizeof(dbid_t));

		slen = strlen(stored->source_id);
		i16temp = htons(slen);
		write_e(db_fptr, &i16temp, sizeof(uint16_t));
		if(slen){
			write_e(db_fptr, stored->source_id, slen);
		}

		i16temp = htons(stored->source_mid);
		write_e(db_fptr, &i16temp, sizeof(uint16_t));

		i16temp = htons(stored->msg.mid);
		write_e(db_fptr, &i16temp, sizeof(uint16_t));

		slen = strlen(stored->msg.topic);
		i16temp = htons(slen);
		write_e(db_fptr, &i16temp, sizeof(uint16_t));
		write_e(db_fptr, stored->msg.topic, slen);

		i8temp = (uint8_t )stored->msg.qos;
		write_e(db_fptr, &i8temp, sizeof(uint8_t));

		if(force_no_retain == false){
			i8temp = (uint8_t )stored->msg.retain;
		}else{
			i8temp = 0;
		}
		write_e(db_fptr, &i8temp, sizeof(uint8_t));

		i32temp = htonl(stored->msg.payloadlen);
		write_e(db_fptr, &i32temp, sizeof(uint32_t));
		if(stored->msg.payloadlen){
			write_e(db_fptr, stored->msg.payload, (unsigned int)stored->msg.payloadlen);
		}

		stored = stored->next;
	}

	return MOSQ_ERR_SUCCESS;
error:
	_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	return 1;
}

static int mqtt3_db_client_write(struct mosquitto_db *db, FILE *db_fptr)
{
	int i;
	struct mosquitto *context;
	uint16_t i16temp, slen;
	uint32_t length;

	assert(db);
	assert(db_fptr);

	for(i=0; i<db->context_count; i++){
		context = db->contexts[i];
		if(context && context->clean_session == false){
			length = htonl(2+strlen(context->id) + sizeof(uint16_t) + sizeof(time_t));

			i16temp = htons(DB_CHUNK_CLIENT);
			write_e(db_fptr, &i16temp, sizeof(uint16_t));
			write_e(db_fptr, &length, sizeof(uint32_t));

			slen = strlen(context->id);
			i16temp = htons(slen);
			write_e(db_fptr, &i16temp, sizeof(uint16_t));
			write_e(db_fptr, context->id, slen);
			i16temp = htons(context->last_mid);
			write_e(db_fptr, &i16temp, sizeof(uint16_t));
			write_e(db_fptr, &(context->disconnect_t), sizeof(time_t));

			if(mqtt3_db_client_messages_write(db, db_fptr, context)) return 1;
		}
	}

	return MOSQ_ERR_SUCCESS;
error:
	_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	return 1;
}

static int _db_subs_retain_write(struct mosquitto_db *db, FILE *db_fptr, struct _mosquitto_subhier *node, const char *topic)
{
	struct _mosquitto_subhier *subhier;
	struct _mosquitto_subleaf *sub;
	char *thistopic;
	uint32_t length;
	uint16_t i16temp;
	dbid_t i64temp;
	size_t slen;

	slen = strlen(topic) + strlen(node->topic) + 2;
	thistopic = _mosquitto_malloc(sizeof(char)*slen);
	if(!thistopic) return MOSQ_ERR_NOMEM;
	if(strlen(topic)){
		snprintf(thistopic, slen, "%s/%s", topic, node->topic);
	}else{
		snprintf(thistopic, slen, "%s", node->topic);
	}

	sub = node->subs;
	while(sub){
		if(sub->context->clean_session == false){
			length = htonl(2+strlen(sub->context->id) + 2+strlen(thistopic) + sizeof(uint8_t));

			i16temp = htons(DB_CHUNK_SUB);
			write_e(db_fptr, &i16temp, sizeof(uint16_t));
			write_e(db_fptr, &length, sizeof(uint32_t));

			slen = strlen(sub->context->id);
			i16temp = htons(slen);
			write_e(db_fptr, &i16temp, sizeof(uint16_t));
			write_e(db_fptr, sub->context->id, slen);

			slen = strlen(thistopic);
			i16temp = htons(slen);
			write_e(db_fptr, &i16temp, sizeof(uint16_t));
			write_e(db_fptr, thistopic, slen);

			write_e(db_fptr, &sub->qos, sizeof(uint8_t));
		}
		sub = sub->next;
	}
	if(node->retained){
		if(strncmp(node->retained->msg.topic, "$SYS", 4)){
			/* Don't save $SYS messages. */
			length = htonl(sizeof(dbid_t));

			i16temp = htons(DB_CHUNK_RETAIN);
			write_e(db_fptr, &i16temp, sizeof(uint16_t));
			write_e(db_fptr, &length, sizeof(uint32_t));

			i64temp = node->retained->db_id;
			write_e(db_fptr, &i64temp, sizeof(dbid_t));
		}
	}

	subhier = node->children;
	while(subhier){
		_db_subs_retain_write(db, db_fptr, subhier, thistopic);
		subhier = subhier->next;
	}
	_mosquitto_free(thistopic);
	return MOSQ_ERR_SUCCESS;
error:
	_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	return 1;
}

static int mqtt3_db_subs_retain_write(struct mosquitto_db *db, FILE *db_fptr)
{
	struct _mosquitto_subhier *subhier;

	subhier = db->subs.children;
	while(subhier){
		_db_subs_retain_write(db, db_fptr, subhier, "");
		subhier = subhier->next;
	}
	
	return MOSQ_ERR_SUCCESS;
}

int mqtt3_db_backup(struct mosquitto_db *db, bool cleanup, bool shutdown)
{
	int rc = 0;
	FILE *db_fptr = NULL;
	uint32_t db_version_w = htonl(MOSQ_DB_VERSION);
	uint32_t crc = htonl(0);
	dbid_t i64temp;
	uint32_t i32temp;
	uint16_t i16temp;
	uint8_t i8temp;
	char err[256];
	char *outfile = NULL;
	int len;

	if(!db || !db->config || !db->config->persistence_filepath) return MOSQ_ERR_INVAL;
	_mosquitto_log_printf(NULL, MOSQ_LOG_INFO, "Saving in-memory database to %s.", db->config->persistence_filepath);
	if(cleanup){
		mqtt3_db_store_clean(db);
	}

	len = strlen(db->config->persistence_filepath)+5;
	outfile = _mosquitto_calloc(len+1, 1);
	if(!outfile){
		_mosquitto_log_printf(NULL, MOSQ_LOG_INFO, "Error saving in-memory database, out of memory.");
		return MOSQ_ERR_NOMEM;
	}
	snprintf(outfile, len, "%s.new", db->config->persistence_filepath);
	db_fptr = _mosquitto_fopen(outfile, "wb");
	if(db_fptr == NULL){
		_mosquitto_log_printf(NULL, MOSQ_LOG_INFO, "Error saving in-memory database, unable to open %s for writing.", outfile);
		goto error;
	}

	/* Header */
	write_e(db_fptr, magic, 15);
	write_e(db_fptr, &crc, sizeof(uint32_t));
	write_e(db_fptr, &db_version_w, sizeof(uint32_t));

	/* DB config */
	i16temp = htons(DB_CHUNK_CFG);
	write_e(db_fptr, &i16temp, sizeof(uint16_t));
	/* chunk length */
	i32temp = htonl(sizeof(dbid_t) + sizeof(uint8_t) + sizeof(uint8_t));
	write_e(db_fptr, &i32temp, sizeof(uint32_t));
	/* db written at broker shutdown or not */
	i8temp = shutdown;
	write_e(db_fptr, &i8temp, sizeof(uint8_t));
	i8temp = sizeof(dbid_t);
	write_e(db_fptr, &i8temp, sizeof(uint8_t));
	/* last db mid */
	i64temp = db->last_db_id;
	write_e(db_fptr, &i64temp, sizeof(dbid_t));

	if(mqtt3_db_message_store_write(db, db_fptr)){
		goto error;
	}

	mqtt3_db_client_write(db, db_fptr);
	mqtt3_db_subs_retain_write(db, db_fptr);

	fclose(db_fptr);

#ifdef WIN32
	if(remove(db->config->persistence_filepath) != 0){
		goto error;
	}
#endif
	if(rename(outfile, db->config->persistence_filepath) != 0){
		goto error;
	}
	_mosquitto_free(outfile);
	outfile = NULL;
	return rc;
error:
	if(outfile) _mosquitto_free(outfile);
	strerror_r(errno, err, 256);
	_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", err);
	if(db_fptr) fclose(db_fptr);
	return 1;
}

static int _db_client_msg_restore(struct mosquitto_db *db, const char *client_id, uint16_t mid, uint8_t qos, uint8_t retain, uint8_t direction, uint8_t state, uint8_t dup, uint64_t store_id)
{
	struct mosquitto_client_msg *cmsg;
	struct mosquitto_msg_store *store;
	struct mosquitto *context;

	cmsg = _mosquitto_calloc(1, sizeof(struct mosquitto_client_msg));
	if(!cmsg){
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}

	cmsg->store = NULL;
	cmsg->mid = mid;
	cmsg->qos = qos;
	cmsg->retain = retain;
	cmsg->direction = direction;
	cmsg->state = state;
	cmsg->dup = dup;

	store = db->msg_store;
	while(store){
		if(store->db_id == store_id){
			cmsg->store = store;
			cmsg->store->ref_count++;
			break;
		}
		store = store->next;
	}
	if(!cmsg->store){
		_mosquitto_free(cmsg);
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error restoring persistent database, message store corrupt.");
		return 1;
	}
	context = _db_find_or_add_context(db, client_id, 0);
	if(!context){
		_mosquitto_free(cmsg);
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error restoring persistent database, message store corrupt.");
		return 1;
	}
	if(context->msgs){
		context->last_msg->next = cmsg;
	}else{
		context->msgs = cmsg;
	}
	cmsg->next = NULL;
	context->last_msg = cmsg;

	return MOSQ_ERR_SUCCESS;
}

static int _db_client_chunk_restore(struct mosquitto_db *db, FILE *db_fptr)
{
	uint16_t i16temp, slen, last_mid;
	char *client_id = NULL;
	int rc = 0;
	struct mosquitto *context;
	time_t disconnect_t;
	struct _clientid_index_hash *new_cih;

	read_e(db_fptr, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(!slen){
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Corrupt persistent database.");
		fclose(db_fptr);
		return 1;
	}
	client_id = _mosquitto_calloc(slen+1, sizeof(char));
	if(!client_id){
		fclose(db_fptr);
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}
	read_e(db_fptr, client_id, slen);

	read_e(db_fptr, &i16temp, sizeof(uint16_t));
	last_mid = ntohs(i16temp);

	if(db_version == 2){
		disconnect_t = mosquitto_time();
	}else{
		read_e(db_fptr, &disconnect_t, sizeof(time_t));
	}

	context = _db_find_or_add_context(db, client_id, last_mid);
	if(context){
		context->disconnect_t = disconnect_t;
	}else{
		rc = 1;
	}

	_mosquitto_free(client_id);

	if(!rc){
		new_cih = _mosquitto_malloc(sizeof(struct _clientid_index_hash));
		if(!new_cih){
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		new_cih->id = context->id;
		new_cih->db_context_index = context->db_index;
		HASH_ADD_KEYPTR(hh, db->clientid_index_hash, context->id, strlen(context->id), new_cih);
	}
	return rc;
error:
	_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", strerror(errno));
	fclose(db_fptr);
	if(client_id) _mosquitto_free(client_id);
	return 1;
}

static int _db_client_msg_chunk_restore(struct mosquitto_db *db, FILE *db_fptr)
{
	dbid_t i64temp, store_id;
	uint16_t i16temp, slen, mid;
	uint8_t qos, retain, direction, state, dup;
	char *client_id = NULL;
	int rc;
	char err[256];

	read_e(db_fptr, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(!slen){
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Corrupt persistent database.");
		fclose(db_fptr);
		return 1;
	}
	client_id = _mosquitto_calloc(slen+1, sizeof(char));
	if(!client_id){
		fclose(db_fptr);
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}
	read_e(db_fptr, client_id, slen);

	read_e(db_fptr, &i64temp, sizeof(dbid_t));
	store_id = i64temp;

	read_e(db_fptr, &i16temp, sizeof(uint16_t));
	mid = ntohs(i16temp);

	read_e(db_fptr, &qos, sizeof(uint8_t));
	read_e(db_fptr, &retain, sizeof(uint8_t));
	read_e(db_fptr, &direction, sizeof(uint8_t));
	read_e(db_fptr, &state, sizeof(uint8_t));
	read_e(db_fptr, &dup, sizeof(uint8_t));

	rc = _db_client_msg_restore(db, client_id, mid, qos, retain, direction, state, dup, store_id);
	_mosquitto_free(client_id);

	return rc;
error:
	strerror_r(errno, err, 256);
	_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", err);
	fclose(db_fptr);
	if(client_id) _mosquitto_free(client_id);
	return 1;
}

static int _db_msg_store_chunk_restore(struct mosquitto_db *db, FILE *db_fptr)
{
	dbid_t i64temp, store_id;
	uint32_t i32temp, payloadlen;
	uint16_t i16temp, slen, source_mid;
	uint8_t qos, retain, *payload = NULL;
	char *source_id = NULL;
	char *topic = NULL;
	int rc = 0;
	struct mosquitto_msg_store *stored = NULL;
	char err[256];

	read_e(db_fptr, &i64temp, sizeof(dbid_t));
	store_id = i64temp;

	read_e(db_fptr, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(slen){
		source_id = _mosquitto_calloc(slen+1, sizeof(char));
		if(!source_id){
			fclose(db_fptr);
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		read_e(db_fptr, source_id, slen);
	}
	read_e(db_fptr, &i16temp, sizeof(uint16_t));
	source_mid = ntohs(i16temp);

	/* This is the mid - don't need it */
	read_e(db_fptr, &i16temp, sizeof(uint16_t));

	read_e(db_fptr, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	if(slen){
		topic = _mosquitto_calloc(slen+1, sizeof(char));
		if(!topic){
			fclose(db_fptr);
			if(source_id) _mosquitto_free(source_id);
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		read_e(db_fptr, topic, slen);
	}else{
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Invalid msg_store chunk when restoring persistent database.");
		fclose(db_fptr);
		if(source_id) _mosquitto_free(source_id);
		return 1;
	}
	read_e(db_fptr, &qos, sizeof(uint8_t));
	read_e(db_fptr, &retain, sizeof(uint8_t));
	
	read_e(db_fptr, &i32temp, sizeof(uint32_t));
	payloadlen = ntohl(i32temp);

	if(payloadlen){
		payload = _mosquitto_malloc(payloadlen);
		if(!payload){
			fclose(db_fptr);
			if(source_id) _mosquitto_free(source_id);
			_mosquitto_free(topic);
			_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
			return MOSQ_ERR_NOMEM;
		}
		read_e(db_fptr, payload, payloadlen);
	}

	rc = mqtt3_db_message_store(db, source_id, source_mid, topic, qos, payloadlen, payload, retain, &stored, store_id);
	if(source_id) _mosquitto_free(source_id);
	_mosquitto_free(topic);
	_mosquitto_free(payload);

	return rc;
error:
	strerror_r(errno, err, 256);
	_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", err);
	fclose(db_fptr);
	if(source_id) _mosquitto_free(source_id);
	if(topic) _mosquitto_free(topic);
	if(payload) _mosquitto_free(payload);
	return 1;
}

static int _db_retain_chunk_restore(struct mosquitto_db *db, FILE *db_fptr)
{
	dbid_t i64temp, store_id;
	struct mosquitto_msg_store *store;
	char err[256];

	if(fread(&i64temp, sizeof(dbid_t), 1, db_fptr) != 1){
		strerror_r(errno, err, 256);
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", err);
		fclose(db_fptr);
		return 1;
	}
	store_id = i64temp;
	store = db->msg_store;
	while(store){
		if(store->db_id == store_id){
			mqtt3_db_messages_queue(db, NULL, store->msg.topic, store->msg.qos, store->msg.retain, store);
			break;
		}
		store = store->next;
	}
	return MOSQ_ERR_SUCCESS;
}

static int _db_sub_chunk_restore(struct mosquitto_db *db, FILE *db_fptr)
{
	uint16_t i16temp, slen;
	uint8_t qos;
	char *client_id;
	char *topic;
	int rc = 0;
	char err[256];

	read_e(db_fptr, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	client_id = _mosquitto_calloc(slen+1, sizeof(char));
	if(!client_id){
		fclose(db_fptr);
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		return MOSQ_ERR_NOMEM;
	}
	read_e(db_fptr, client_id, slen);
	read_e(db_fptr, &i16temp, sizeof(uint16_t));
	slen = ntohs(i16temp);
	topic = _mosquitto_calloc(slen+1, sizeof(char));
	if(!topic){
		fclose(db_fptr);
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Out of memory.");
		_mosquitto_free(client_id);
		return MOSQ_ERR_NOMEM;
	}
	read_e(db_fptr, topic, slen);
	read_e(db_fptr, &qos, sizeof(uint8_t));
	if(_db_restore_sub(db, client_id, topic, qos)){
		rc = 1;
	}
	_mosquitto_free(client_id);
	_mosquitto_free(topic);

	return rc;
error:
	strerror_r(errno, err, 256);
	_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", err);
	fclose(db_fptr);
	return 1;
}

int mqtt3_db_restore(struct mosquitto_db *db)
{
	FILE *fptr;
	char header[15];
	int rc = 0;
	uint32_t crc;
	dbid_t i64temp;
	uint32_t i32temp, length;
	uint16_t i16temp, chunk;
	uint8_t i8temp;
	ssize_t rlen;
	char err[256];

	assert(db);
	assert(db->config);
	assert(db->config->persistence_filepath);

	fptr = _mosquitto_fopen(db->config->persistence_filepath, "rb");
	if(fptr == NULL) return MOSQ_ERR_SUCCESS;
	read_e(fptr, &header, 15);
	if(!memcmp(header, magic, 15)){
		// Restore DB as normal
		read_e(fptr, &crc, sizeof(uint32_t));
		read_e(fptr, &i32temp, sizeof(uint32_t));
		db_version = ntohl(i32temp);
		/* IMPORTANT - this is where compatibility checks are made.
		 * Is your DB change still compatible with previous versions?
		 */
		if(db_version > MOSQ_DB_VERSION && db_version != 0){
			if(db_version == 2){
				/* Addition of disconnect_t to client chunk in v3. */
			}else{
				fclose(fptr);
				_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Unsupported persistent database format version %d (need version %d).", db_version, MOSQ_DB_VERSION);
				return 1;
			}
		}

		while(rlen = fread(&i16temp, sizeof(uint16_t), 1, fptr), rlen == 1){
			chunk = ntohs(i16temp);
			read_e(fptr, &i32temp, sizeof(uint32_t));
			length = ntohl(i32temp);
			switch(chunk){
				case DB_CHUNK_CFG:
					read_e(fptr, &i8temp, sizeof(uint8_t)); // shutdown
					read_e(fptr, &i8temp, sizeof(uint8_t)); // sizeof(dbid_t)
					if(i8temp != sizeof(dbid_t)){
						_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Incompatible database configuration (dbid size is %d bytes, expected %lu)",
								i8temp, (unsigned long)sizeof(dbid_t));
						fclose(fptr);
						return 1;
					}
					read_e(fptr, &i64temp, sizeof(dbid_t));
					db->last_db_id = i64temp;
					break;

				case DB_CHUNK_MSG_STORE:
					if(_db_msg_store_chunk_restore(db, fptr)) return 1;
					break;

				case DB_CHUNK_CLIENT_MSG:
					if(_db_client_msg_chunk_restore(db, fptr)) return 1;
					break;

				case DB_CHUNK_RETAIN:
					if(_db_retain_chunk_restore(db, fptr)) return 1;
					break;

				case DB_CHUNK_SUB:
					if(_db_sub_chunk_restore(db, fptr)) return 1;
					break;

				case DB_CHUNK_CLIENT:
					if(_db_client_chunk_restore(db, fptr)) return 1;
					break;

				default:
					_mosquitto_log_printf(NULL, MOSQ_LOG_WARNING, "Warning: Unsupported chunk \"%d\" in persistent database file. Ignoring.", chunk);
					fseek(fptr, length, SEEK_CUR);
					break;
			}
		}
		if(rlen < 0) goto error;
	}else{
		_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: Unable to restore persistent database. Unrecognised file format.");
		rc = 1;
	}

	fclose(fptr);

	return rc;
error:
	strerror_r(errno, err, 256);
	_mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error: %s.", err);
	if(fptr) fclose(fptr);
	return 1;
}

static int _db_restore_sub(struct mosquitto_db *db, const char *client_id, const char *sub, int qos)
{
	struct mosquitto *context;

	assert(db);
	assert(client_id);
	assert(sub);

	context = _db_find_or_add_context(db, client_id, 0);
	if(!context) return 1;
	return mqtt3_sub_add(db, context, sub, qos, &db->subs);
}

#endif
