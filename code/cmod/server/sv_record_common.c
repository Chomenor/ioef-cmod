/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2017 Noah Metzger (chomenor@gmail.com)

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifdef CMOD_RECORD
#include "sv_record_local.h"

/* ******************************************************************************** */
// Data Stream
/* ******************************************************************************** */

void record_stream_error(record_data_stream_t *stream, const char *message) {
	// General purpose error that can be called by any function doing encoding/decoding on the stream
	// Uses stream abort jump if set, otherwise calls Com_Error
	record_printf(RP_ALL, "%s\n", message);
	if(stream->abort_set) {
		stream->abort_set = qfalse;
		longjmp(stream->abort, 1); }
	Com_Error(ERR_FATAL, "%s", message); }

char *record_stream_write_allocate(int size, record_data_stream_t *stream) {
	char *data = stream->data + stream->position;
	if(stream->position + size > stream->size || stream->position + size < stream->position) {
		record_stream_error(stream, "record_stream_write_allocate: stream overflow"); }
	stream->position += size;
	return data; }

void record_stream_write(void *data, int size, record_data_stream_t *stream) {
	char *target = record_stream_write_allocate(size, stream);
	if(target) Com_Memcpy(target, data, size); }

void record_stream_write_value(int value, int size, record_data_stream_t *stream) {
	record_stream_write(&value, size, stream); }

char *record_stream_read_static(int size, record_data_stream_t *stream) {
	char *output = stream->data + stream->position;
	if(stream->position + size > stream->size || stream->position + size < stream->position) {
		record_stream_error(stream, "record_stream_read_static: stream overflow"); }
	stream->position += size;
	return output; }

void record_stream_read_buffer(void *output, int size, record_data_stream_t *stream) {
	void *data = record_stream_read_static(size, stream);
	if(data) Com_Memcpy(output, data, size); }

void dump_stream_to_file(record_data_stream_t *stream, fileHandle_t file) {
	FS_Write(stream->data, stream->position, file);
	stream->position = 0; }

/* ******************************************************************************** */
// Memory allocation
/* ******************************************************************************** */

int alloc_count = 0;

void *record_calloc(unsigned int size) {
	++alloc_count;
	return calloc(size, 1); }

void record_free(void *ptr) {
	--alloc_count;
	free(ptr); }

/* ******************************************************************************** */
// Bit operations
/* ******************************************************************************** */

void record_bit_set(int *target, int position) {
	target[position / 32] |= 1 << (position % 32); }

void record_bit_unset(int *target, int position) {
	target[position / 32] &= ~(1 << (position % 32)); }

int record_bit_get(int *source, int position) {
	return (source[position / 32] >> (position % 32)) & 1; }

/* ******************************************************************************** */
// Flag operations
/* ******************************************************************************** */

// These flags tend to be game/mod specific, so their access is aggregated here
// so game-specific changes can be made in one place if needed

qboolean usercmd_is_firing_weapon(const usercmd_t *cmd) {
	if(cmd->buttons & (1|32)) return qtrue;
	return qfalse; }

qboolean playerstate_is_spectator(const playerState_t *ps) {
	if(ps->pm_type == 2) return qtrue;	// 2=PM_SPECTATOR
	if(ps->pm_flags & 4096) return qtrue;	// 4096=PMF_FOLLOW
	if(ps->eFlags & 0x400) return qtrue;	// 0x400=EF_ELIMINATED
	return qfalse; }

void playerstate_set_follow_mode(playerState_t *ps) {
	ps->pm_flags |= 4096;	// 4096=PMF_FOLLOW
}

/* ******************************************************************************** */
// Message printing
/* ******************************************************************************** */

void QDECL record_printf(record_print_mode_t mode, const char *fmt, ...) {
	va_list argptr;
	char message[1024];

	if(mode == RP_DEBUG && !record_debug_prints->integer) return;

	va_start(argptr, fmt);
	Q_vsnprintf(message, sizeof(message), fmt, argptr);
	va_end(argptr);

	Com_Printf("%s", message); }

/* ******************************************************************************** */
// Record State Functions
/* ******************************************************************************** */

record_state_t *allocate_record_state(int max_clients) {
	int i;
	record_state_t *rs = record_calloc(sizeof(*rs));
	rs->clients = record_calloc(sizeof(*rs->clients) * max_clients);
	rs->max_clients = max_clients;

	// Initialize configstrings
	for(i=0; i<MAX_CONFIGSTRINGS; ++i) {
		rs->configstrings[i] = CopyString(""); }

	rs->current_servercmd = CopyString("");
	return rs; }

void free_record_state(record_state_t *rs) {
	int i;
	record_free(rs->clients);
	for(i=0; i<MAX_CONFIGSTRINGS; ++i) {
		if(rs->configstrings[i]) Z_Free(rs->configstrings[i]); }
	if(rs->current_servercmd) Z_Free(rs->current_servercmd);
	record_free(rs); }

/* ******************************************************************************** */
// Structure Encoding/Decoding Functions
/* ******************************************************************************** */

// ***** Strings *****

void record_encode_string(char *string, record_data_stream_t *stream) {
	int length = strlen(string);
	record_stream_write_value(length, 4, stream);
	record_stream_write(string, length+1, stream); }

char *record_decode_string(record_data_stream_t *stream, int *length_out) {
	int length = *(int *)record_stream_read_static(4, stream);
	char *string;
	if(length < 0) record_stream_error(stream, "record_decode_string: invalid length");
	string = record_stream_read_static(length+1, stream);
	if(string[length]) record_stream_error(stream, "record_decode_string: string not null terminated");
	return string; }

// ***** Generic Structure *****

static void record_encode_structure(qboolean byte_pass, unsigned int *state, unsigned int *source, int size, record_data_stream_t *stream) {
	// Basic structure encoding sends the index byte followed by data chunk
	// Field encoding sends the index byte with high bit set, followed by byte field indicating
	//    the following 8 indexes to send, followed by specified data chunks
	// In byte_pass mode only data chunks that can be encoded as 1 byte are encoded, otherwise
	//    chunks are 4 bytes
	int i, j;
	unsigned char *field = 0;
	int field_position = 0;

	for(i=0; i<size; ++i) {
		if(state[i] != source[i] && (!byte_pass || (state[i] & ~255) == (source[i] & ~255))) {
			if(field && i - field_position < 8) {
				*field |= (1 << (i - field_position)); }
			else {
				int field_hits = 0;
				for(j=i+1; j<i+9 && j<size; ++j) {
					if(state[j] != source[j] && (!byte_pass || (state[j] & ~255) == (source[j] & ~255))) ++field_hits; }
				if(field_hits > 1) {
					record_stream_write_value(i | 128, 1, stream);
					field = (unsigned char *)record_stream_write_allocate(1, stream);
					*field = 0;
					field_position = i + 1; }
				else {
					record_stream_write_value(i, 1, stream); } }
			record_stream_write_value(state[i] ^ source[i], byte_pass ? 1 : 4, stream);
			state[i] = source[i]; } }

	record_stream_write_value(255, 1, stream); }

static void record_decode_structure(qboolean byte_pass, unsigned int *state, unsigned int size, record_data_stream_t *stream) {
	while(1) {
		unsigned char cmd = *(unsigned char *)record_stream_read_static(1, stream);
		int index = cmd & 127;
		int field = 0;

		if(cmd == 255) break;
		if(cmd & 128) field = *(unsigned char *)record_stream_read_static(1, stream);
		field = (field << 1) | 1;

		while(field) {
			if(field & 1) {
				if(index >= size) record_stream_error(stream, "record_decode_structure: out of bounds");
				if(byte_pass) state[index] ^= *(unsigned char *)record_stream_read_static(1, stream);
				else state[index] ^= *(unsigned int *)record_stream_read_static(4, stream); }
			field >>= 1;
			++index; } } }

// ***** Playerstates *****

void record_encode_playerstate(playerState_t *state, playerState_t *source, record_data_stream_t *stream) {
	record_encode_structure(qtrue, (unsigned int *)state, (unsigned int *)source, sizeof(*state)/4, stream);
	record_encode_structure(qfalse, (unsigned int *)state, (unsigned int *)source, sizeof(*state)/4, stream); }

void record_decode_playerstate(playerState_t *state, record_data_stream_t *stream) {
	record_decode_structure(qtrue, (unsigned int *)state, sizeof(*state)/4, stream);
	record_decode_structure(qfalse, (unsigned int *)state, sizeof(*state)/4, stream); }

// ***** Entitystates *****

void record_encode_entitystate(entityState_t *state, entityState_t *source, record_data_stream_t *stream) {
	record_encode_structure(qtrue, (unsigned int *)state, (unsigned int *)source, sizeof(*state)/4, stream);
	record_encode_structure(qfalse, (unsigned int *)state, (unsigned int *)source, sizeof(*state)/4, stream); }

void record_decode_entitystate(entityState_t *state, record_data_stream_t *stream) {
	record_decode_structure(qtrue, (unsigned int *)state, sizeof(*state)/4, stream);
	record_decode_structure(qfalse, (unsigned int *)state, sizeof(*state)/4, stream); }

// ***** Entitysets *****

void record_encode_entityset(record_entityset_t *state, record_entityset_t *source, record_data_stream_t *stream) {
	// Sets state equal to source, and writes delta change to stream
	int i;
	for(i=0; i<MAX_GENTITIES; ++i) {
		if(!record_bit_get(state->active_flags, i) && !record_bit_get(source->active_flags, i)) continue;
		else if(record_bit_get(state->active_flags, i) && !record_bit_get(source->active_flags, i)) {
			//Com_Printf("encode remove %i\n", i);
			record_stream_write_value(i | (1 << 12), 2, stream);
			record_bit_unset(state->active_flags, i); }
		else if(!record_bit_get(state->active_flags, i) || memcmp(&state->entities[i], &source->entities[i], sizeof(state->entities[i]))) {
			//Com_Printf("encode modify %i\n", i);
			record_stream_write_value(i | (2 << 12), 2, stream);
			record_encode_entitystate(&state->entities[i], &source->entities[i], stream);
			record_bit_set(state->active_flags, i); } }

	// Finished
	record_stream_write_value(-1, 2, stream); }

void record_decode_entityset(record_entityset_t *state, record_data_stream_t *stream) {
	// Modifies state to reflect delta changes in stream
	while(1) {
		short data = *(short *)record_stream_read_static(2, stream);
		short newnum = data & ((1 << 12) - 1);
		short command = data >> 12;

		// Finished
		if(data == -1) break;

		if(newnum < 0 || newnum >= MAX_GENTITIES) {
			record_stream_error(stream, "record_decode_entityset: bad entity number"); }

		if(command == 1) {
			//Com_Printf("decode remove %i\n", newnum);
			record_bit_unset(state->active_flags, newnum); }
		else if(command == 2) {
			//Com_Printf("decode modify %i\n", newnum);
			record_decode_entitystate(&state->entities[newnum], stream);
			record_bit_set(state->active_flags, newnum); }
		else {
			record_stream_error(stream, "record_decode_entityset: bad command"); } } }

// ***** Visibility States *****

void record_encode_visibility_state(record_visibility_state_t *state, record_visibility_state_t *source,
		record_data_stream_t *stream) {
	record_encode_structure(qfalse, (unsigned int *)state, (unsigned int *)source, sizeof(*state)/4, stream); }

void record_decode_visibility_state(record_visibility_state_t *state, record_data_stream_t *stream) {
	record_decode_structure(qfalse, (unsigned int *)state, sizeof(*state)/4, stream); }

// ***** Usercmd States *****

void record_encode_usercmd(record_usercmd_t *state, record_usercmd_t *source, record_data_stream_t *stream) {
	record_encode_structure(qfalse, (unsigned int *)state, (unsigned int *)source, sizeof(*state)/4, stream); }

void record_decode_usercmd(record_usercmd_t *state, record_data_stream_t *stream) {
	record_decode_structure(qfalse, (unsigned int *)state, sizeof(*state)/4, stream); }

/* ******************************************************************************** */
// Usercmd Conversion
/* ******************************************************************************** */

// Usercmds are stored in a custom 'record usercmd' structure which is easier to encode
// These functions are used to convert between the record and normal usercmd formats

void record_convert_usercmd_to_record_usercmd(usercmd_t *source, record_usercmd_t *target) {
	target->serverTime = source->serverTime;
	target->angles[0] = source->angles[0];
	target->angles[1] = source->angles[1];
	target->angles[2] = source->angles[2];
	target->buttons = source->buttons;
	target->forwardmove = source->forwardmove;
	target->rightmove = source->rightmove;
	target->upmove = source->upmove;
	target->weapon = source->weapon;
	memset(target->padding, 0, sizeof(target->padding)); }

void record_convert_record_usercmd_to_usercmd(record_usercmd_t *source, usercmd_t *target) {
	memset(target, 0, sizeof(*target));
	target->serverTime = source->serverTime;
	target->angles[0] = source->angles[0];
	target->angles[1] = source->angles[1];
	target->angles[2] = source->angles[2];
	target->buttons = source->buttons;
	target->forwardmove = source->forwardmove;
	target->rightmove = source->rightmove;
	target->upmove = source->upmove;
	target->weapon = source->weapon; }

/* ******************************************************************************** */
// Entity Set Building
/* ******************************************************************************** */

void get_current_entities(record_entityset_t *target) {
	int i;
	if(sv.num_entities > MAX_GENTITIES) {
		record_printf(RP_ALL, "get_current_entities: sv.num_entities > MAX_GENTITIES\n");
		return; }

	memset(target->active_flags, 0, sizeof(target->active_flags));

	for(i=0; i<sv.num_entities; ++i) {
		sharedEntity_t *ent = SV_GentityNum(i);
		if(!ent->r.linked) continue;
		if(ent->s.number != i) {
			record_printf(RP_DEBUG, "get_current_entities: bad ent->s.number\n");
			continue; }
		target->entities[i] = ent->s;
		record_bit_set(target->active_flags, i); } }

void get_current_baselines(record_entityset_t *target) {
	int i;
	memset(target->active_flags, 0, sizeof(target->active_flags));

	for(i=0; i<MAX_GENTITIES; ++i) {
		if(!sv.svEntities[i].baseline.number) continue;
		if(sv.svEntities[i].baseline.number != i) {
			record_printf(RP_DEBUG, "get_current_baselines: bad baseline number\n");
			continue; }
		target->entities[i] = sv.svEntities[i].baseline;
		record_bit_set(target->active_flags, i); } }

/* ******************************************************************************** */
// Visibility Building
/* ******************************************************************************** */

static void record_set_visible_entities(int clientNum, vec3_t origin, qboolean portal, record_visibility_state_t *target) {
	// Based on sv_snapshot.c->SV_AddEntitiesVisibleFromPoint
	int		e, i;
	sharedEntity_t *ent;
	svEntity_t	*svEnt;
	int		l;
	int		clientarea, clientcluster;
	int		leafnum;
	byte	*clientpvs;
	byte	*bitvector;

	if ( !sv.state ) {
		record_printf(RP_ALL, "record_set_visible_entities: sv.state error\n");
		return;
	}

	leafnum = CM_PointLeafnum (origin);
	clientarea = CM_LeafArea (leafnum);
	clientcluster = CM_LeafCluster (leafnum);

	// calculate the visible areas
	target->area_visibility_size = CM_WriteAreaBits( (byte *)target->area_visibility, clientarea );

	clientpvs = CM_ClusterPVS (clientcluster);

	for ( e = 0 ; e < sv.num_entities ; e++ ) {
		ent = SV_GentityNum(e);

		// never send entities that aren't linked in
		if ( !ent->r.linked ) {
			continue;
		}

		/*
		if (ent->s.number != e) {
			Com_DPrintf ("FIXING ENT->S.NUMBER!!!\n");
			ent->s.number = e;
		}
		*/

		// entities can be flagged to explicitly not be sent to the client
		if ( ent->r.svFlags & SVF_NOCLIENT ) {
			continue;
		}

		// entities can be flagged to be sent to only one client
		if ( ent->r.svFlags & SVF_SINGLECLIENT ) {
			if ( ent->r.singleClient != clientNum ) {
				continue;
			}
		}
		// entities can be flagged to be sent to everyone but one client
		if ( ent->r.svFlags & SVF_NOTSINGLECLIENT ) {
			if ( ent->r.singleClient == clientNum ) {
				continue;
			}
		}
		// entities can be flagged to be sent to a given mask of clients
		if ( ent->r.svFlags & SVF_CLIENTMASK ) {
			if (clientNum >= 32) {
				record_printf(RP_DEBUG, "record_set_visible_entities: clientNum >= 32\n");
				continue; }
			if (~ent->r.singleClient & (1 << clientNum))
				continue;
		}

		svEnt = SV_SvEntityForGentity( ent );

		// don't double add an entity through portals
		if ( record_bit_get(target->ent_visibility, e) ) {
			continue;
		}

		// broadcast entities are always sent
		if ( ent->r.svFlags & SVF_BROADCAST ) {
			record_bit_set(target->ent_visibility, e);
			continue;
		}

		// ignore if not touching a PV leaf
		// check area
		if ( !CM_AreasConnected( clientarea, svEnt->areanum ) ) {
			// doors can legally straddle two areas, so
			// we may need to check another one
			if ( !CM_AreasConnected( clientarea, svEnt->areanum2 ) ) {
				continue;		// blocked by a door
			}
		}

		bitvector = clientpvs;

		// check individual leafs
		if ( !svEnt->numClusters ) {
			continue;
		}
		l = 0;
		for ( i=0 ; i < svEnt->numClusters ; i++ ) {
			l = svEnt->clusternums[i];
			if ( bitvector[l >> 3] & (1 << (l&7) ) ) {
				break;
			}
		}

		// if we haven't found it to be visible,
		// check overflow clusters that coudln't be stored
		if ( i == svEnt->numClusters ) {
			if ( svEnt->lastCluster ) {
				for ( ; l <= svEnt->lastCluster ; l++ ) {
					if ( bitvector[l >> 3] & (1 << (l&7) ) ) {
						break;
					}
				}
				if ( l == svEnt->lastCluster ) {
					continue;	// not visible
				}
			} else {
				continue;
			}
		}

		// add it
		record_bit_set(target->ent_visibility, e);

		// if it's a portal entity, add everything visible from its camera position
		if ( ent->r.svFlags & SVF_PORTAL ) {
#ifndef ELITEFORCE
			if ( ent->s.generic1 ) {
				vec3_t dir;
				VectorSubtract(ent->s.origin, origin, dir);
				if ( VectorLengthSquared(dir) > (float) ent->s.generic1 * ent->s.generic1 ) {
					continue;
				}
			}
#endif
			record_set_visible_entities( clientNum, ent->s.origin2, qtrue, target );
		}

	}
}

void record_get_current_visibility(int clientNum, record_visibility_state_t *target) {
	// Based on sv_snapshot.c->SV_BuildClientSnapshot
	playerState_t *ps = SV_GameClientNum(clientNum);
	vec3_t org;

	memset(target, 0, sizeof(*target));

	// find the client's viewpoint
	VectorCopy( ps->origin, org );
	org[2] += ps->viewheight;

	// Account for behavior of SV_BuildClientSnapshot under "never send client's own entity..." comment
	record_bit_set(target->ent_visibility, ps->clientNum);

	record_set_visible_entities(ps->clientNum, org, qfalse, target);

	record_bit_unset(target->ent_visibility, ps->clientNum); }

void record_verify_visibility_check(int clientNum, int numSnapshotEntities, int *snapshotEntities, int areabytes, byte *areabits) {
	if(record_verify_data->integer) {
		int i;
		record_visibility_state_t record_visibility;
		record_visibility_state_t snapshot_visibility;

		record_get_current_visibility(clientNum, &record_visibility);

		memset(&snapshot_visibility, 0, sizeof(snapshot_visibility));
		for(i=0; i<numSnapshotEntities; ++i) {
			if(snapshotEntities[i] < 0 || snapshotEntities[i] >= MAX_GENTITIES) {
				record_printf(RP_ALL, "record_verify_visibility_check: invalid entity number");
				return; }
			record_bit_set(snapshot_visibility.ent_visibility, snapshotEntities[i]); }

		if(memcmp(record_visibility.ent_visibility, snapshot_visibility.ent_visibility, sizeof(record_visibility.ent_visibility))) {
			record_printf(RP_ALL, "record_verify_visibility_check: ent_visibility discrepancy for client %i\n", clientNum); }

		if(memcmp(record_visibility.area_visibility, areabits, sizeof(record_visibility.area_visibility))) {
			record_printf(RP_ALL, "record_verify_visibility_check: area_visibility discrepancy for client %i\n", clientNum); }

		if(record_visibility.area_visibility_size != areabytes) {
			record_printf(RP_ALL, "record_verify_visibility_check: area_visibility_size discrepancy for client %i\n", clientNum); } } }

void record_tweak_inactive_visibility(record_entityset_t *entityset, record_visibility_state_t *old_visibility,
			record_visibility_state_t *source, record_visibility_state_t *target) {
	// Sets bits for inactive entities that are set in the previous visibility, to reduce data usage
	int i;
	*target = *source;	// Deal with non-entity stuff
	for(i=0; i<32; ++i) {
		// We should be able to assume no inactive entities are set as visible in the source
		if((source->ent_visibility[i] & entityset->active_flags[i]) != source->ent_visibility[i]) {
			record_printf(RP_ALL, "record_tweak_inactive_visibility: inactive entity was visible in source\n"); }

		// Toggle visibility of inactive entities that are visible in the old visibility
		target->ent_visibility[i] = source->ent_visibility[i] | (old_visibility->ent_visibility[i] & ~entityset->active_flags[i]); } }

/* ******************************************************************************** */
// Message Building
/* ******************************************************************************** */

static int record_calculate_baseline_cutoff(record_entityset_t *baselines, msg_t msg) {
	// Baseline cutoff calculation similar to sv_calculate_max_baselines
	// Returns -1 for no cutoff, otherwise returns first baseline index to drop
	int i;
	byte buffer[MAX_MSGLEN];
	entityState_t nullstate;

	msg.data = buffer;
	Com_Memset(&nullstate, 0, sizeof(nullstate));
	for(i=0; i<MAX_GENTITIES; ++i) {
		if(!record_bit_get(baselines->active_flags, i)) continue;
		MSG_WriteByte(&msg, svc_baseline);
		MSG_WriteDeltaEntity(&msg, &nullstate, &baselines->entities[i], qtrue);
		if(msg.cursize + 24 >= msg.maxsize) return i; }

	return -1; }

void record_write_gamestate_message(record_entityset_t *baselines, char **configstrings, int clientNum,
		int serverCommandSequence, msg_t *msg, int *baseline_cutoff_out) {
	int i;
	entityState_t nullstate;

	MSG_WriteByte(msg, svc_gamestate);
	MSG_WriteLong(msg, serverCommandSequence);

	// Write configstrings
	for(i=0; i<MAX_CONFIGSTRINGS; ++i) {
		if(!*configstrings[i]) continue;
		MSG_WriteByte(msg, svc_configstring);
		MSG_WriteShort(msg, i);
		MSG_WriteBigString(msg, configstrings[i]); }

	*baseline_cutoff_out = record_calculate_baseline_cutoff(baselines, *msg);

	// Write the baselines
	Com_Memset(&nullstate, 0, sizeof(nullstate));
	for(i=0; i<MAX_GENTITIES; ++i) {
		if(!record_bit_get(baselines->active_flags, i)) continue;
		if(*baseline_cutoff_out >= 0 && i >= *baseline_cutoff_out) continue;
		MSG_WriteByte(msg, svc_baseline);
		MSG_WriteDeltaEntity(msg, &nullstate, &baselines->entities[i], qtrue); }

	if(msg->compat) MSG_WriteByte(msg, 0);
	else MSG_WriteByte(msg, svc_EOF);

	if(!msg->compat) {
		// write the client num
		MSG_WriteLong(msg, clientNum);

		// write the checksum feed
		MSG_WriteLong(msg, 0); } }

void record_write_snapshot_message(record_entityset_t *entities, record_visibility_state_t *visibility, playerState_t *ps,
		record_entityset_t *delta_entities, record_visibility_state_t *delta_visibility, playerState_t *delta_ps,
		record_entityset_t *baselines, int baseline_cutoff, int lastClientCommand, int deltaFrame, int snapFlags,
		int sv_time, msg_t *msg) {
	// Based on sv_snapshot.c->SV_SendClientSnapshot
	// For non-delta snapshot, set delta_entities, delta_visibility, delta_ps, and deltaFrame to null
	int i;

	MSG_WriteByte(msg, svc_snapshot);

#ifdef ELITEFORCE
	if(msg->compat) MSG_WriteLong(msg, lastClientCommand);
#endif

	MSG_WriteLong(msg, sv_time);

	// what we are delta'ing from
	MSG_WriteByte(msg, deltaFrame);

	// Write snapflags
	MSG_WriteByte(msg, snapFlags);

	// Write area visibility
	{	int inverted_area_visibility[8];
		for(i=0; i<8; ++i) inverted_area_visibility[i] = ~visibility->area_visibility[i];
		MSG_WriteByte(msg, visibility->area_visibility_size);
		MSG_WriteData(msg, inverted_area_visibility, visibility->area_visibility_size); }

	// Write playerstate
	MSG_WriteDeltaPlayerstate(msg, delta_ps, ps);

	// Write entities
	for(i=0; i<MAX_GENTITIES; ++i) {
		if(record_bit_get(entities->active_flags, i) && record_bit_get(visibility->ent_visibility, i)) {
			// Active and visible entity
			if(deltaFrame && record_bit_get(delta_entities->active_flags, i) && record_bit_get(delta_visibility->ent_visibility, i)) {
				// Keep entity (delta from previous entity)
				if(baseline_cutoff >= 0 && i >= baseline_cutoff) {
					entityState_t nullstate;
					Com_Memset(&nullstate, 0, sizeof(nullstate));
					MSG_WriteDeltaEntity(msg, &nullstate, &entities->entities[i], qfalse); }
				else {
					MSG_WriteDeltaEntity(msg, &delta_entities->entities[i], &entities->entities[i], qfalse); } }
			else {
				// New entity (delta from baseline)
				MSG_WriteDeltaEntity(msg, &baselines->entities[i], &entities->entities[i], qtrue); } }
		else if(deltaFrame && record_bit_get(delta_entities->active_flags, i) && record_bit_get(delta_visibility->ent_visibility, i)) {
			// Remove entity
			MSG_WriteBits(msg, i, GENTITYNUM_BITS);
			MSG_WriteBits(msg, 1, 1); } }

	// End of entities
	MSG_WriteBits(msg, (MAX_GENTITIES-1), GENTITYNUM_BITS); }

#endif
