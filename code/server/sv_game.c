/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

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
// sv_game.c -- interface to the game dll

#include "server.h"

#include "../botlib/botlib.h"
#ifdef CMOD_VM_BOUNDS_CHECKS
#include "../botlib/be_aas.h"
#include "../qcommon/vm_local.h"
#endif

botlib_export_t	*botlib_export;

#ifdef CMOD_MISC_SECURITY
#include "../botlib/be_interface.h"
#ifndef CMOD_VM_BOUNDS_CHECKS
#include "../qcommon/vm_local.h"
#endif

typedef struct {
	const char *name;
	int defaultValue;
	int minValue;
	int maxValue;
} botlibVarRange_t;

static const botlibVarRange_t botlibVarRanges[] = {
	{ "maxclients", 128, 0, MAX_CLIENTS },
	{ "maxentities", 1024, 0, MAX_GENTITIES },
	{ "max_routingcache", 4096, 0, 65536 },
	{ "max_aaslinks", 6144, 2, 65536 },
	{ "max_messages", 1024, 2, 65536 },
	{ "max_iteminfo", 256, 0, 4096 },
	{ "max_levelitems", 1024, 1, 4096 },
	{ "max_weaponinfo", 32, 0, 4096 },
	{ "max_projectileinfo", 32, 0, 4096 }
};

static const botlibVarRange_t *SV_BotLibVarRange( const char *var_name ) {
	int i;

	if ( !var_name ) {
		return NULL;
	}

	for ( i = 0; i < (int)( sizeof( botlibVarRanges ) / sizeof( botlibVarRanges[0] ) ); i++ ) {
		if ( !Q_stricmp( var_name, botlibVarRanges[i].name ) ) {
			return &botlibVarRanges[i];
		}
	}

	return NULL;
}

static int SV_BotLibVarIntegerValue( const char *string, const botlibVarRange_t *range ) {
	float value = 0;

	if ( string && Q_isanumber( string ) ) {
		value = atof( string );
		if ( Q_isnan( value ) ) {
			value = 0;
		}
	}

	if ( value < range->minValue ) {
		return range->defaultValue;
	}
	if ( value > range->maxValue ) {
		return range->maxValue;
	}

	return (int)value;
}

static int SV_BotLibVarSet( const char *var_name, const char *value ) {
	char clampedValue[16];
	int integerValue;
	const botlibVarRange_t *range = SV_BotLibVarRange( var_name );

	if ( range ) {
		integerValue = SV_BotLibVarIntegerValue( value, range );
		if ( !Q_stricmp( var_name, "maxclients" ) && integerValue < sv_maxclients->integer ) {
			integerValue = sv_maxclients->integer;
		}
		Com_sprintf( clampedValue, sizeof( clampedValue ), "%d", integerValue );
		value = clampedValue;
	}

	return botlib_export->BotLibVarSet( var_name, value );
}

static qboolean SV_BotLibValidClient( int client ) {
	return client >= 0 && client < sv_maxclients->integer;
}

static qboolean SV_BotLibEAClientSyscall( intptr_t callnum ) {
	return ( callnum >= BOTLIB_EA_SAY && callnum <= BOTLIB_EA_RESET_INPUT )
#ifdef ELITEFORCE
		|| callnum == BOTLIB_EA_ALT_ATTACK
#endif
		;
}
#endif

// these functions must be used instead of pointer arithmetic, because
// the game allocates gentities with private information after the server shared part
int	SV_NumForGentity( sharedEntity_t *ent ) {
	int		num;

	num = ( (byte *)ent - (byte *)sv.gentities ) / sv.gentitySize;

	return num;
}

sharedEntity_t *SV_GentityNum( int num ) {
	sharedEntity_t *ent;

	ent = (sharedEntity_t *)((byte *)sv.gentities + sv.gentitySize*(num));

	return ent;
}

playerState_t *SV_GameClientNum( int num ) {
	playerState_t	*ps;

	ps = (playerState_t *)((byte *)sv.gameClients + sv.gameClientSize*(num));

	return ps;
}

svEntity_t	*SV_SvEntityForGentity( sharedEntity_t *gEnt ) {
	if ( !gEnt || gEnt->s.number < 0 || gEnt->s.number >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
	}
	return &sv.svEntities[ gEnt->s.number ];
}

sharedEntity_t *SV_GEntityForSvEntity( svEntity_t *svEnt ) {
	int		num;

	num = svEnt - sv.svEntities;
	return SV_GentityNum( num );
}

/*
===============
SV_GameSendServerCommand

Sends a command string to a client
===============
*/
void SV_GameSendServerCommand( int clientNum, const char *text ) {
	if ( clientNum == -1 ) {
		SV_SendServerCommand( NULL, "%s", text );
	} else {
		if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
			return;
		}
		SV_SendServerCommand( svs.clients + clientNum, "%s", text );	
	}
}


/*
===============
SV_GameDropClient

Disconnects the client with a message
===============
*/
void SV_GameDropClient( int clientNum, const char *reason ) {
	if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
		return;
	}
	SV_DropClient( svs.clients + clientNum, reason );	
}


/*
=================
SV_SetBrushModel

sets mins and maxs for inline bmodels
=================
*/
void SV_SetBrushModel( sharedEntity_t *ent, const char *name ) {
	clipHandle_t	h;
	vec3_t			mins, maxs;

	if (!name) {
		Com_Error( ERR_DROP, "SV_SetBrushModel: NULL" );
	}

	if (name[0] != '*') {
		Com_Error( ERR_DROP, "SV_SetBrushModel: %s isn't a brush model", name );
	}


	ent->s.modelindex = atoi( name + 1 );

	h = CM_InlineModel( ent->s.modelindex );
	CM_ModelBounds( h, mins, maxs );
	VectorCopy (mins, ent->r.mins);
	VectorCopy (maxs, ent->r.maxs);
	ent->r.bmodel = qtrue;

	ent->r.contents = -1;		// we don't know exactly what is in the brushes

	SV_LinkEntity( ent );		// FIXME: remove
}



/*
=================
SV_inPVS

Also checks portalareas so that doors block sight
=================
*/
qboolean SV_inPVS (const vec3_t p1, const vec3_t p2)
{
	int		leafnum;
	int		cluster;
	int		area1, area2;
	byte	*mask;

	leafnum = CM_PointLeafnum (p1);
	cluster = CM_LeafCluster (leafnum);
	area1 = CM_LeafArea (leafnum);
	mask = CM_ClusterPVS (cluster);

	leafnum = CM_PointLeafnum (p2);
	cluster = CM_LeafCluster (leafnum);
	area2 = CM_LeafArea (leafnum);
	if ( mask && (!(mask[cluster>>3] & (1<<(cluster&7)) ) ) )
		return qfalse;
	if (!CM_AreasConnected (area1, area2))
		return qfalse;		// a door blocks sight
	return qtrue;
}


/*
=================
SV_inPVSIgnorePortals

Does NOT check portalareas
=================
*/
qboolean SV_inPVSIgnorePortals( const vec3_t p1, const vec3_t p2)
{
	int		leafnum;
	int		cluster;
	byte	*mask;

	leafnum = CM_PointLeafnum (p1);
	cluster = CM_LeafCluster (leafnum);
	mask = CM_ClusterPVS (cluster);

	leafnum = CM_PointLeafnum (p2);
	cluster = CM_LeafCluster (leafnum);

	if ( mask && (!(mask[cluster>>3] & (1<<(cluster&7)) ) ) )
		return qfalse;

	return qtrue;
}


/*
========================
SV_AdjustAreaPortalState
========================
*/
void SV_AdjustAreaPortalState( sharedEntity_t *ent, qboolean open ) {
	svEntity_t	*svEnt;

	svEnt = SV_SvEntityForGentity( ent );
	if ( svEnt->areanum2 == -1 ) {
		return;
	}
	CM_AdjustAreaPortalState( svEnt->areanum, svEnt->areanum2, open );
}


/*
==================
SV_EntityContact
==================
*/
qboolean	SV_EntityContact( vec3_t mins, vec3_t maxs, const sharedEntity_t *gEnt, int capsule ) {
	const float	*origin, *angles;
	clipHandle_t	ch;
	trace_t			trace;

	// check for exact collision
	origin = gEnt->r.currentOrigin;
	angles = gEnt->r.currentAngles;

	ch = SV_ClipHandleForEntity( gEnt );
	CM_TransformedBoxTrace ( &trace, vec3_origin, vec3_origin, mins, maxs,
		ch, -1, origin, angles, capsule );

	return trace.startsolid;
}


/*
===============
SV_GetServerinfo

===============
*/
void SV_GetServerinfo( char *buffer, int bufferSize ) {
	if ( bufferSize < 1 ) {
		Com_Error( ERR_DROP, "SV_GetServerinfo: bufferSize == %i", bufferSize );
	}
#ifdef CMOD_COMMON_SERVER_INFOSTRING_HOOKS
	Q_strncpyz( buffer, sv_get_serverinfo_string(qfalse), bufferSize );
#else
	Q_strncpyz( buffer, Cvar_InfoString( CVAR_SERVERINFO ), bufferSize );
#endif
}

/*
===============
SV_LocateGameData

===============
*/
#ifdef CMOD_VM_BOUNDS_CHECKS
void SV_LocateGameData( sharedEntity_t *gEnts, unsigned numGEntities, unsigned sizeofGEntity_t,
					   playerState_t *clients, unsigned sizeofGameClient ) {
	if ( !gvm->entryPoint ) {
		if ( numGEntities > MAX_GENTITIES ) {
			Com_Error( ERR_DROP, "%s: bad entity count %u", __func__, numGEntities );
		}

		if ( sizeofGEntity_t < sizeof(sharedEntity_t) || sizeofGEntity_t > gvm->exactDataLength / MAX_GENTITIES ) {
			Com_Error( ERR_DROP, "%s: bad entity size %u", __func__, sizeofGEntity_t );
		} else if ( (byte*)gEnts - gvm->dataBase > gvm->exactDataLength - sizeofGEntity_t * MAX_GENTITIES ) {
			Com_Error( ERR_DROP, "%s: entities located out of data segment", __func__ );
		}

		if ( sizeofGameClient < sizeof(playerState_t) || sizeofGameClient > gvm->exactDataLength / MAX_CLIENTS ) {
			Com_Error( ERR_DROP, "%s: bad game client size %u", __func__, sizeofGameClient );
		} else if ( (byte*)clients - gvm->dataBase > gvm->exactDataLength - sizeofGameClient * MAX_CLIENTS ) {
			Com_Error( ERR_DROP, "%s: clients located out of data segment", __func__ );
		}
	}
#else
void SV_LocateGameData( sharedEntity_t *gEnts, int numGEntities, int sizeofGEntity_t,
					   playerState_t *clients, int sizeofGameClient ) {
#endif
	sv.gentities = gEnts;
	sv.gentitySize = sizeofGEntity_t;
	sv.num_entities = numGEntities;

	sv.gameClients = clients;
	sv.gameClientSize = sizeofGameClient;
}


/*
===============
SV_GetUsercmd

===============
*/
void SV_GetUsercmd( int clientNum, usercmd_t *cmd ) {
	if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
		Com_Error( ERR_DROP, "SV_GetUsercmd: bad clientNum:%i", clientNum );
	}
	*cmd = svs.clients[clientNum].lastUsercmd;
}

//==============================================

static int	FloatAsInt( float f ) {
	floatint_t fi;
	fi.f = f;
	return fi.i;
}

/*
====================
SV_GameSystemCalls

The module is making a system call
====================
*/
intptr_t SV_GameSystemCalls( intptr_t *args ) {
#ifdef CMOD_VM_EXTENSIONS
	intptr_t retval = 0;
	if ( VMExt_HandleVMSyscall( args, VM_GAME, gvm, &retval ) ) {
		return retval;
	}
#endif
#ifdef CMOD_MISC_SECURITY
	// BotClientCommand callbacks can resume movement/chat code using this state.
	if ( gvm->callLevel > 1 &&
		( args[0] == BOTLIB_SETUP || args[0] == BOTLIB_SHUTDOWN ||
		  args[0] == BOTLIB_LOAD_MAP || args[0] == BOTLIB_AI_FREE_MOVE_STATE ||
		  args[0] == BOTLIB_AI_FREE_CHAT_STATE ) ) {
		return 0;
	}
	// AAS/EA/AI traps bypass the botlib entry-point setup checks.
	if ( !botlibglobals.botlibsetup && args[0] > BOTLIB_USER_COMMAND &&
#ifdef ELITEFORCE
		args[0] <= BOTLIB_AI_PREDICT_VISIBLE_POSITION
#else
		args[0] < BOTLIB_PC_LOAD_SOURCE
#endif
		) {
		return 0;
	}
	if ( SV_BotLibEAClientSyscall( args[0] ) && !SV_BotLibValidClient( args[1] ) ) {
		return 0;
	}
#endif
	switch( args[0] ) {
	case G_PRINT:
		Com_Printf( "%s", (const char*)VMA(1) );
		return 0;
	case G_ERROR:
#ifdef CMOD_IGNORE_STARTSOLID_ERROR
	{
		const char *error = VMA(1);
		if(!strncmp(error, "FinishSpawningItem", 18) && Q_stristr(error, "startsolid")) {
			Com_Printf("NOTE: Skipping VM startsolid error '%s'\n", error);
			return 0; }
	}
#endif
		Com_Error( ERR_DROP, "%s", (const char*)VMA(1) );
		return 0;
	case G_MILLISECONDS:
		return Sys_Milliseconds();
	case G_CVAR_REGISTER:
#ifdef CMOD_CVAR_HANDLING
		Cvar_Register( VMA(1), VMA(2), VMA(3), args[4], VMPermissions_CheckTrusted( VM_GAME ) );
#else
		Cvar_Register( VMA(1), VMA(2), VMA(3), args[4] ); 
#endif
		return 0;
	case G_CVAR_UPDATE:
		Cvar_Update( VMA(1) );
		return 0;
	case G_CVAR_SET:
#ifdef CMOD_CVAR_HANDLING
		Cvar_SetSafe( (const char *)VMA(1), (const char *)VMA(2), VMPermissions_CheckTrusted( VM_GAME ) );
#else
		Cvar_SetSafe( (const char *)VMA(1), (const char *)VMA(2) );
#endif
		return 0;
	case G_CVAR_VARIABLE_INTEGER_VALUE:
		return Cvar_VariableIntegerValue( (const char *)VMA(1) );
	case G_CVAR_VARIABLE_STRING_BUFFER:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[2], args[3] );
#endif
		Cvar_VariableStringBuffer( VMA(1), VMA(2), args[3] );
		return 0;
	case G_ARGC:
		return Cmd_Argc();
	case G_ARGV:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[2], args[3] );
#endif
		Cmd_ArgvBuffer( args[1], VMA(2), args[3] );
		return 0;
	case G_SEND_CONSOLE_COMMAND:
#ifdef CMOD_COMMAND_INTERPRETER
		Cbuf_ExecuteTextByMode( args[1], VMA(2),
			Cbuf_IsSpecialVMCommand( VMA(2) ) && VMPermissions_CheckTrusted( VM_GAME ) ?
			CMD_NORMAL : CMD_PROTECTED );
#else
		Cbuf_ExecuteText( args[1], VMA(2) );
#endif
		return 0;

	case G_FS_FOPEN_FILE:
#ifdef CMOD_WRITE_PROTECTION
		if ( args[3] != FS_READ && !VMPermissions_CheckTrusted( VM_GAME ) ) {
			return 0;
		}
#endif
#ifdef NEW_FILESYSTEM
		return FS_FOpenFileByModeOwner( VMA(1), VMA(2), args[3], FS_HANDLEOWNER_QAGAME );
#else
		return FS_FOpenFileByMode( VMA(1), VMA(2), args[3] );
#endif
	case G_FS_READ:
#ifdef CMOD_VM_BOUNDS_CHECKS
		if ( args[3] == 0 ) // UrT may pass this with args[2]=-1 and cause false bounds check error
			return 0;
#endif
#ifdef NEW_FILESYSTEM
		if ( FS_Handle_GetOwner( args[3] ) != FS_HANDLEOWNER_QAGAME ) {
			return 0;
		}
#endif
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[1], args[2] );
#endif
		FS_Read( VMA(1), args[2], args[3] );
		return 0;
	case G_FS_WRITE:
#ifdef NEW_FILESYSTEM
		if ( FS_Handle_GetOwner( args[3] ) != FS_HANDLEOWNER_QAGAME ) {
			return 0;
		}
#endif
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[1], args[2] );
#endif
		FS_Write( VMA(1), args[2], args[3] );
		return 0;
	case G_FS_FCLOSE_FILE:
#ifdef NEW_FILESYSTEM
		if ( FS_Handle_GetOwner( args[1] ) != FS_HANDLEOWNER_QAGAME ) {
			return 0;
		}
#endif
		FS_FCloseFile( args[1] );
		return 0;
	case G_FS_GETFILELIST:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[3], args[4] );
#endif
		return FS_GetFileList( VMA(1), VMA(2), VMA(3), args[4] );
#ifndef ELITEFORCE
	case G_FS_SEEK:
#ifdef NEW_FILESYSTEM
		if ( FS_Handle_GetOwner( args[1] ) != FS_HANDLEOWNER_QAGAME ) {
			return 0;
		}
#endif
		return FS_Seek( args[1], args[2], args[3] );
#endif

	case G_LOCATE_GAME_DATA:
		SV_LocateGameData( VMA(1), args[2], args[3], VMA(4), args[5] );
		return 0;
	case G_DROP_CLIENT:
		SV_GameDropClient( args[1], VMA(2) );
		return 0;
	case G_SEND_SERVER_COMMAND:
		SV_GameSendServerCommand( args[1], VMA(2) );
		return 0;
	case G_LINKENTITY:
		SV_LinkEntity( VMA(1) );
		return 0;
	case G_UNLINKENTITY:
		SV_UnlinkEntity( VMA(1) );
		return 0;
	case G_ENTITIES_IN_BOX:
#ifdef CMOD_VM_BOUNDS_CHECKS
#ifdef ELITEFORCE
		if ( args[4] == 1024 ) {
			// Extra check due to Mario Mod issues
			uint64_t areaEntitiesEnd = (uint64_t)(unsigned int)args[3] +
				(uint64_t)(unsigned int)args[4] * (unsigned int)sizeof( int );
			if ( !gvm->entryPoint && areaEntitiesEnd > gvm->dataMask ) {
				// Patch for g_weapon.c->SeekerAcquiresTarget in original game module.
				// Call is made with maxcount 1024, when actual buffer size is 128 entries.
				VM_CHECKBOUNDS3( gvm, args[3], 128, sizeof( int ) );
				return SV_AreaEntities( VMA(1), VMA(2), VMA(3), 128 );
			}
		}
#endif
		VM_CHECKBOUNDS3( gvm, args[3], args[4], sizeof( int ) );
#endif
		return SV_AreaEntities( VMA(1), VMA(2), VMA(3), args[4] );
	case G_ENTITY_CONTACT:
		return SV_EntityContact( VMA(1), VMA(2), VMA(3), /*int capsule*/ qfalse );
#ifndef ELITEFORCE
	case G_ENTITY_CONTACTCAPSULE:
		return SV_EntityContact( VMA(1), VMA(2), VMA(3), /*int capsule*/ qtrue );
#endif
	case G_TRACE:
		SV_Trace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qfalse );
		return 0;
#ifndef ELITEFORCE
	case G_TRACECAPSULE:
		SV_Trace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qtrue );
		return 0;
#endif
	case G_POINT_CONTENTS:
		return SV_PointContents( VMA(1), args[2] );
	case G_SET_BRUSH_MODEL:
		SV_SetBrushModel( VMA(1), VMA(2) );
		return 0;
	case G_IN_PVS:
		return SV_inPVS( VMA(1), VMA(2) );
	case G_IN_PVS_IGNORE_PORTALS:
		return SV_inPVSIgnorePortals( VMA(1), VMA(2) );

	case G_SET_CONFIGSTRING:
#ifdef CMOD_SERVER_CMD_TRIGGERS
		// When intermission process starts, CS_INTERMISSION is set to 1 from G_LogExit
		if(args[1] == 14 && !Q_stricmp(VMA(2), "1")) {
			trigger_exec_type(TRIGGER_INTERMISSION_START);
		}
#endif
		SV_SetConfigstring( args[1], VMA(2) );
		return 0;
	case G_GET_CONFIGSTRING:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[2], args[3] );
#endif
		SV_GetConfigstring( args[1], VMA(2), args[3] );
		return 0;
	case G_SET_USERINFO:
		SV_SetUserinfo( args[1], VMA(2) );
		return 0;
	case G_GET_USERINFO:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[2], args[3] );
#endif
		SV_GetUserinfo( args[1], VMA(2), args[3] );
		return 0;
	case G_GET_SERVERINFO:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[1], args[2] );
#endif
		SV_GetServerinfo( VMA(1), args[2] );
		return 0;
	case G_ADJUST_AREA_PORTAL_STATE:
		SV_AdjustAreaPortalState( VMA(1), args[2] );
		return 0;
	case G_AREAS_CONNECTED:
		return CM_AreasConnected( args[1], args[2] );

	case G_BOT_ALLOCATE_CLIENT:
		return SV_BotAllocateClient();
	case G_BOT_FREE_CLIENT:
		SV_BotFreeClient( args[1] );
		return 0;

	case G_GET_USERCMD:
		SV_GetUsercmd( args[1], VMA(2) );
		return 0;
	case G_GET_ENTITY_TOKEN:
		{
			const char	*s;

			s = COM_Parse( &sv.entityParsePoint );
#ifdef CMOD_VM_BOUNDS_CHECKS
			VM_CHECKBOUNDS( gvm, args[1], args[2] );
#endif
			Q_strncpyz( VMA(1), s, args[2] );
			if ( !sv.entityParsePoint && !s[0] ) {
				return qfalse;
			} else {
				return qtrue;
			}
		}

	case G_DEBUG_POLYGON_CREATE:
		return BotImport_DebugPolygonCreate( args[1], args[2], VMA(3) );
	case G_DEBUG_POLYGON_DELETE:
		BotImport_DebugPolygonDelete( args[1] );
		return 0;
#ifndef ELITEFORCE
	case G_REAL_TIME:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[1], sizeof( qtime_t ) );
#endif
		return Com_RealTime( VMA(1) );
	case G_SNAPVECTOR:
		Q_SnapVector(VMA(1));
		return 0;
#endif

		//====================================

	case BOTLIB_SETUP:
		return SV_BotLibSetup();
	case BOTLIB_SHUTDOWN:
		return SV_BotLibShutdown();
	case BOTLIB_LIBVAR_SET:
#ifdef CMOD_MISC_SECURITY
		return SV_BotLibVarSet( VMA(1), VMA(2) );
#else
		return botlib_export->BotLibVarSet( VMA(1), VMA(2) );
#endif
	case BOTLIB_LIBVAR_GET:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[2], args[3] );
#endif
		return botlib_export->BotLibVarGet( VMA(1), VMA(2), args[3] );

#ifdef ELITEFORCE
	case BOTLIB_DEFINE:
		return botlib_export->PC_AddGlobalDefine( VMA(1) );
#else
	case BOTLIB_PC_ADD_GLOBAL_DEFINE:
		return botlib_export->PC_AddGlobalDefine( VMA(1) );
	case BOTLIB_PC_LOAD_SOURCE:
		return botlib_export->PC_LoadSourceHandle( VMA(1) );
	case BOTLIB_PC_FREE_SOURCE:
		return botlib_export->PC_FreeSourceHandle( args[1] );
	case BOTLIB_PC_READ_TOKEN:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[2], sizeof( pc_token_t ) );
#endif
		return botlib_export->PC_ReadTokenHandle( args[1], VMA(2) );
	case BOTLIB_PC_SOURCE_FILE_AND_LINE:
		return botlib_export->PC_SourceFileAndLine( args[1], VMA(2), VMA(3) );
#endif

	case BOTLIB_START_FRAME:
		return botlib_export->BotLibStartFrame( VMF(1) );
	case BOTLIB_LOAD_MAP:
		return botlib_export->BotLibLoadMap( VMA(1) );
	case BOTLIB_UPDATENTITY:
		return botlib_export->BotLibUpdateEntity( args[1], VMA(2) );
	case BOTLIB_TEST:
		return botlib_export->Test( args[1], VMA(2), VMA(3), VMA(4) );

	case BOTLIB_GET_SNAPSHOT_ENTITY:
#ifdef CMOD_MISC_SECURITY
		if ( !SV_BotLibValidClient( args[1] ) ) {
			return -1;
		}
#endif
		return SV_BotGetSnapshotEntity( args[1], args[2] );
	case BOTLIB_GET_CONSOLE_MESSAGE:
#ifdef CMOD_MISC_SECURITY
		if ( !SV_BotLibValidClient( args[1] ) ) {
			return 0;
		}
#endif
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[2], args[3] );
#endif
		return SV_BotGetConsoleMessage( args[1], VMA(2), args[3] );
	case BOTLIB_USER_COMMAND:
		{
			int clientNum = args[1];

			if ( clientNum >= 0 && clientNum < sv_maxclients->integer ) {
				SV_ClientThink( &svs.clients[clientNum], VMA(2) );
			}
		}
		return 0;

#ifndef ELITEFORCE
	case BOTLIB_AAS_BBOX_AREAS:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS3( gvm, args[3], args[4], sizeof( int ) );
#endif
		return botlib_export->aas.AAS_BBoxAreas( VMA(1), VMA(2), VMA(3), args[4] );
	case BOTLIB_AAS_AREA_INFO:
		return botlib_export->aas.AAS_AreaInfo( args[1], VMA(2) );
	case BOTLIB_AAS_ALTERNATIVE_ROUTE_GOAL:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS3( gvm, args[6], args[7], sizeof( aas_altroutegoal_t ) );
#endif
		return botlib_export->aas.AAS_AlternativeRouteGoals( VMA(1), args[2], VMA(3), args[4], args[5], VMA(6), args[7], args[8] );
#endif
	case BOTLIB_AAS_ENTITY_INFO:
		botlib_export->aas.AAS_EntityInfo( args[1], VMA(2) );
		return 0;

	case BOTLIB_AAS_INITIALIZED:
		return botlib_export->aas.AAS_Initialized();
	case BOTLIB_AAS_PRESENCE_TYPE_BOUNDING_BOX:
		botlib_export->aas.AAS_PresenceTypeBoundingBox( args[1], VMA(2), VMA(3) );
		return 0;
	case BOTLIB_AAS_TIME:
		return FloatAsInt( botlib_export->aas.AAS_Time() );

	case BOTLIB_AAS_POINT_AREA_NUM:
		return botlib_export->aas.AAS_PointAreaNum( VMA(1) );
#ifndef ELITEFORCE
	case BOTLIB_AAS_POINT_REACHABILITY_AREA_INDEX:
		return botlib_export->aas.AAS_PointReachabilityAreaIndex( VMA(1) );
#endif
	case BOTLIB_AAS_TRACE_AREAS:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS3( gvm, args[3], args[5], sizeof( int ) );
		if ( args[4] ) {
			VM_CHECKBOUNDS3( gvm, args[4], args[5], sizeof( vec3_t ) );
		}
#endif
		return botlib_export->aas.AAS_TraceAreas( VMA(1), VMA(2), VMA(3), VMA(4), args[5] );

	case BOTLIB_AAS_POINT_CONTENTS:
		return botlib_export->aas.AAS_PointContents( VMA(1) );
	case BOTLIB_AAS_NEXT_BSP_ENTITY:
		return botlib_export->aas.AAS_NextBSPEntity( args[1] );
	case BOTLIB_AAS_VALUE_FOR_BSP_EPAIR_KEY:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[3], args[4] );
#endif
		return botlib_export->aas.AAS_ValueForBSPEpairKey( args[1], VMA(2), VMA(3), args[4] );
	case BOTLIB_AAS_VECTOR_FOR_BSP_EPAIR_KEY:
		return botlib_export->aas.AAS_VectorForBSPEpairKey( args[1], VMA(2), VMA(3) );
	case BOTLIB_AAS_FLOAT_FOR_BSP_EPAIR_KEY:
		return botlib_export->aas.AAS_FloatForBSPEpairKey( args[1], VMA(2), VMA(3) );
	case BOTLIB_AAS_INT_FOR_BSP_EPAIR_KEY:
		return botlib_export->aas.AAS_IntForBSPEpairKey( args[1], VMA(2), VMA(3) );

	case BOTLIB_AAS_AREA_REACHABILITY:
		return botlib_export->aas.AAS_AreaReachability( args[1] );

	case BOTLIB_AAS_AREA_TRAVEL_TIME_TO_GOAL_AREA:
		return botlib_export->aas.AAS_AreaTravelTimeToGoalArea( args[1], VMA(2), args[3], args[4] );
#ifndef ELITEFORCE
	case BOTLIB_AAS_ENABLE_ROUTING_AREA:
		return botlib_export->aas.AAS_EnableRoutingArea( args[1], args[2] );
	case BOTLIB_AAS_PREDICT_ROUTE:
		return botlib_export->aas.AAS_PredictRoute( VMA(1), args[2], VMA(3), args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11] );
#endif

	case BOTLIB_AAS_SWIMMING:
		return botlib_export->aas.AAS_Swimming( VMA(1) );
	case BOTLIB_AAS_PREDICT_CLIENT_MOVEMENT:
		return botlib_export->aas.AAS_PredictClientMovement( VMA(1), args[2], VMA(3), args[4], args[5],
			VMA(6), VMA(7), args[8], args[9], VMF(10), args[11], args[12], args[13] );

	case BOTLIB_EA_SAY:
		botlib_export->ea.EA_Say( args[1], VMA(2) );
		return 0;
	case BOTLIB_EA_SAY_TEAM:
		botlib_export->ea.EA_SayTeam( args[1], VMA(2) );
		return 0;
	case BOTLIB_EA_COMMAND:
		botlib_export->ea.EA_Command( args[1], VMA(2) );
		return 0;

#ifndef ELITEFORCE
	case BOTLIB_EA_ACTION:
		botlib_export->ea.EA_Action( args[1], args[2] );
		return 0;
#endif
	case BOTLIB_EA_GESTURE:
		botlib_export->ea.EA_Gesture( args[1] );
		return 0;
	case BOTLIB_EA_TALK:
		botlib_export->ea.EA_Talk( args[1] );
		return 0;
	case BOTLIB_EA_ATTACK:
		botlib_export->ea.EA_Attack( args[1] );
		return 0;
	case BOTLIB_EA_USE:
		botlib_export->ea.EA_Use( args[1] );
		return 0;
	case BOTLIB_EA_RESPAWN:
		botlib_export->ea.EA_Respawn( args[1] );
		return 0;
	case BOTLIB_EA_CROUCH:
		botlib_export->ea.EA_Crouch( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_UP:
		botlib_export->ea.EA_MoveUp( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_DOWN:
		botlib_export->ea.EA_MoveDown( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_FORWARD:
		botlib_export->ea.EA_MoveForward( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_BACK:
		botlib_export->ea.EA_MoveBack( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_LEFT:
		botlib_export->ea.EA_MoveLeft( args[1] );
		return 0;
	case BOTLIB_EA_MOVE_RIGHT:
		botlib_export->ea.EA_MoveRight( args[1] );
		return 0;

	case BOTLIB_EA_SELECT_WEAPON:
		botlib_export->ea.EA_SelectWeapon( args[1], args[2] );
		return 0;
	case BOTLIB_EA_JUMP:
		botlib_export->ea.EA_Jump( args[1] );
		return 0;
	case BOTLIB_EA_DELAYED_JUMP:
		botlib_export->ea.EA_DelayedJump( args[1] );
		return 0;
	case BOTLIB_EA_MOVE:
		botlib_export->ea.EA_Move( args[1], VMA(2), VMF(3) );
		return 0;
	case BOTLIB_EA_VIEW:
		botlib_export->ea.EA_View( args[1], VMA(2) );
		return 0;

	case BOTLIB_EA_END_REGULAR:
		botlib_export->ea.EA_EndRegular( args[1], VMF(2) );
		return 0;
	case BOTLIB_EA_GET_INPUT:
		botlib_export->ea.EA_GetInput( args[1], VMF(2), VMA(3) );
		return 0;
	case BOTLIB_EA_RESET_INPUT:
		botlib_export->ea.EA_ResetInput( args[1] );
		return 0;

	case BOTLIB_AI_LOAD_CHARACTER:
#ifdef CMOD_BOT_TWEAKS
		return botlib_export->ai.BotLoadCharacter( VMA(1), args[2] );
#else
		return botlib_export->ai.BotLoadCharacter( VMA(1), VMF(2) );
#endif
	case BOTLIB_AI_FREE_CHARACTER:
		botlib_export->ai.BotFreeCharacter( args[1] );
		return 0;
	case BOTLIB_AI_CHARACTERISTIC_FLOAT:
		return FloatAsInt( botlib_export->ai.Characteristic_Float( args[1], args[2] ) );
	case BOTLIB_AI_CHARACTERISTIC_BFLOAT:
		return FloatAsInt( botlib_export->ai.Characteristic_BFloat( args[1], args[2], VMF(3), VMF(4) ) );
	case BOTLIB_AI_CHARACTERISTIC_INTEGER:
		return botlib_export->ai.Characteristic_Integer( args[1], args[2] );
	case BOTLIB_AI_CHARACTERISTIC_BINTEGER:
		return botlib_export->ai.Characteristic_BInteger( args[1], args[2], args[3], args[4] );
	case BOTLIB_AI_CHARACTERISTIC_STRING:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[3], args[4] );
#endif
		botlib_export->ai.Characteristic_String( args[1], args[2], VMA(3), args[4] );
		return 0;

	case BOTLIB_AI_ALLOC_CHAT_STATE:
		return botlib_export->ai.BotAllocChatState();
	case BOTLIB_AI_FREE_CHAT_STATE:
		botlib_export->ai.BotFreeChatState( args[1] );
		return 0;
	case BOTLIB_AI_QUEUE_CONSOLE_MESSAGE:
		botlib_export->ai.BotQueueConsoleMessage( args[1], args[2], VMA(3) );
		return 0;
	case BOTLIB_AI_REMOVE_CONSOLE_MESSAGE:
		botlib_export->ai.BotRemoveConsoleMessage( args[1], args[2] );
		return 0;
	case BOTLIB_AI_NEXT_CONSOLE_MESSAGE:
		return botlib_export->ai.BotNextConsoleMessage( args[1], VMA(2) );
	case BOTLIB_AI_NUM_CONSOLE_MESSAGE:
		return botlib_export->ai.BotNumConsoleMessages( args[1] );
	case BOTLIB_AI_INITIAL_CHAT:
		botlib_export->ai.BotInitialChat( args[1], VMA(2), args[3], VMA(4), VMA(5), VMA(6), VMA(7), VMA(8), VMA(9), VMA(10), VMA(11) );
		return 0;
	case BOTLIB_AI_NUM_INITIAL_CHATS:
		return botlib_export->ai.BotNumInitialChats( args[1], VMA(2) );
	case BOTLIB_AI_REPLY_CHAT:
		return botlib_export->ai.BotReplyChat( args[1], VMA(2), args[3], args[4], VMA(5), VMA(6), VMA(7), VMA(8), VMA(9), VMA(10), VMA(11), VMA(12) );
	case BOTLIB_AI_CHAT_LENGTH:
		return botlib_export->ai.BotChatLength( args[1] );
	case BOTLIB_AI_ENTER_CHAT:
		botlib_export->ai.BotEnterChat( args[1], args[2], args[3] );
		return 0;
	case BOTLIB_AI_GET_CHAT_MESSAGE:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[2], args[3] );
#endif
		botlib_export->ai.BotGetChatMessage( args[1], VMA(2), args[3] );
		return 0;
	case BOTLIB_AI_STRING_CONTAINS:
		return botlib_export->ai.StringContains( VMA(1), VMA(2), args[3] );
	case BOTLIB_AI_FIND_MATCH:
		return botlib_export->ai.BotFindMatch( VMA(1), VMA(2), args[3] );
	case BOTLIB_AI_MATCH_VARIABLE:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[3], args[4] );
#endif
		botlib_export->ai.BotMatchVariable( VMA(1), args[2], VMA(3), args[4] );
		return 0;
	case BOTLIB_AI_UNIFY_WHITE_SPACES:
		botlib_export->ai.UnifyWhiteSpaces( VMA(1) );
		return 0;
	case BOTLIB_AI_REPLACE_SYNONYMS:
		botlib_export->ai.BotReplaceSynonyms( VMA(1), args[2] );
		return 0;
	case BOTLIB_AI_LOAD_CHAT_FILE:
		return botlib_export->ai.BotLoadChatFile( args[1], VMA(2), VMA(3) );
	case BOTLIB_AI_SET_CHAT_GENDER:
		botlib_export->ai.BotSetChatGender( args[1], args[2] );
		return 0;
	case BOTLIB_AI_SET_CHAT_NAME:
#ifdef ELITEFORCE
		botlib_export->ai.BotSetChatName( args[1], VMA(2));
#else
		botlib_export->ai.BotSetChatName( args[1], VMA(2), args[3] );
#endif
		return 0;

	case BOTLIB_AI_RESET_GOAL_STATE:
		botlib_export->ai.BotResetGoalState( args[1] );
		return 0;
	case BOTLIB_AI_RESET_AVOID_GOALS:
		botlib_export->ai.BotResetAvoidGoals( args[1] );
		return 0;
	case BOTLIB_AI_REMOVE_FROM_AVOID_GOALS:
		botlib_export->ai.BotRemoveFromAvoidGoals( args[1], args[2] );
		return 0;
	case BOTLIB_AI_PUSH_GOAL:
		botlib_export->ai.BotPushGoal( args[1], VMA(2) );
		return 0;
	case BOTLIB_AI_POP_GOAL:
		botlib_export->ai.BotPopGoal( args[1] );
		return 0;
	case BOTLIB_AI_EMPTY_GOAL_STACK:
		botlib_export->ai.BotEmptyGoalStack( args[1] );
		return 0;
	case BOTLIB_AI_DUMP_AVOID_GOALS:
		botlib_export->ai.BotDumpAvoidGoals( args[1] );
		return 0;
	case BOTLIB_AI_DUMP_GOAL_STACK:
		botlib_export->ai.BotDumpGoalStack( args[1] );
		return 0;
	case BOTLIB_AI_GOAL_NAME:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[2], args[3] );
#endif
		botlib_export->ai.BotGoalName( args[1], VMA(2), args[3] );
		return 0;
	case BOTLIB_AI_GET_TOP_GOAL:
		return botlib_export->ai.BotGetTopGoal( args[1], VMA(2) );
	case BOTLIB_AI_GET_SECOND_GOAL:
		return botlib_export->ai.BotGetSecondGoal( args[1], VMA(2) );
	case BOTLIB_AI_CHOOSE_LTG_ITEM:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[3], MAX_BOTLIB_INVENTORY * sizeof( int ) );
#endif
		return botlib_export->ai.BotChooseLTGItem( args[1], VMA(2), VMA(3), args[4] );
	case BOTLIB_AI_CHOOSE_NBG_ITEM:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[3], MAX_BOTLIB_INVENTORY * sizeof( int ) );
#endif
		return botlib_export->ai.BotChooseNBGItem( args[1], VMA(2), VMA(3), args[4], VMA(5), VMF(6) );
	case BOTLIB_AI_TOUCHING_GOAL:
		return botlib_export->ai.BotTouchingGoal( VMA(1), VMA(2) );
	case BOTLIB_AI_ITEM_GOAL_IN_VIS_BUT_NOT_VISIBLE:
		return botlib_export->ai.BotItemGoalInVisButNotVisible( args[1], VMA(2), VMA(3), VMA(4) );
	case BOTLIB_AI_GET_LEVEL_ITEM_GOAL:
		return botlib_export->ai.BotGetLevelItemGoal( args[1], VMA(2), VMA(3) );
	case BOTLIB_AI_GET_NEXT_CAMP_SPOT_GOAL:
		return botlib_export->ai.BotGetNextCampSpotGoal( args[1], VMA(2) );
	case BOTLIB_AI_GET_MAP_LOCATION_GOAL:
		return botlib_export->ai.BotGetMapLocationGoal( VMA(1), VMA(2) );
	case BOTLIB_AI_AVOID_GOAL_TIME:
		return FloatAsInt( botlib_export->ai.BotAvoidGoalTime( args[1], args[2] ) );
#ifndef ELITEFORCE
	case BOTLIB_AI_SET_AVOID_GOAL_TIME:
		botlib_export->ai.BotSetAvoidGoalTime( args[1], args[2], VMF(3));
		return 0;
#endif
	case BOTLIB_AI_INIT_LEVEL_ITEMS:
		botlib_export->ai.BotInitLevelItems();
		return 0;
	case BOTLIB_AI_UPDATE_ENTITY_ITEMS:
		botlib_export->ai.BotUpdateEntityItems();
		return 0;
	case BOTLIB_AI_LOAD_ITEM_WEIGHTS:
		return botlib_export->ai.BotLoadItemWeights( args[1], VMA(2) );
	case BOTLIB_AI_FREE_ITEM_WEIGHTS:
		botlib_export->ai.BotFreeItemWeights( args[1] );
		return 0;
	case BOTLIB_AI_INTERBREED_GOAL_FUZZY_LOGIC:
		botlib_export->ai.BotInterbreedGoalFuzzyLogic( args[1], args[2], args[3] );
		return 0;
	case BOTLIB_AI_SAVE_GOAL_FUZZY_LOGIC:
		botlib_export->ai.BotSaveGoalFuzzyLogic( args[1], VMA(2) );
		return 0;
	case BOTLIB_AI_MUTATE_GOAL_FUZZY_LOGIC:
		botlib_export->ai.BotMutateGoalFuzzyLogic( args[1], VMF(2) );
		return 0;
	case BOTLIB_AI_ALLOC_GOAL_STATE:
		return botlib_export->ai.BotAllocGoalState( args[1] );
	case BOTLIB_AI_FREE_GOAL_STATE:
		botlib_export->ai.BotFreeGoalState( args[1] );
		return 0;

	case BOTLIB_AI_RESET_MOVE_STATE:
		botlib_export->ai.BotResetMoveState( args[1] );
		return 0;
#ifndef ELITEFORCE
	case BOTLIB_AI_ADD_AVOID_SPOT:
		botlib_export->ai.BotAddAvoidSpot( args[1], VMA(2), VMF(3), args[4] );
		return 0;
#endif
	case BOTLIB_AI_MOVE_TO_GOAL:
		botlib_export->ai.BotMoveToGoal( VMA(1), args[2], VMA(3), args[4] );
		return 0;
	case BOTLIB_AI_MOVE_IN_DIRECTION:
		return botlib_export->ai.BotMoveInDirection( args[1], VMA(2), VMF(3), args[4] );
	case BOTLIB_AI_RESET_AVOID_REACH:
		botlib_export->ai.BotResetAvoidReach( args[1] );
		return 0;
	case BOTLIB_AI_RESET_LAST_AVOID_REACH:
		botlib_export->ai.BotResetLastAvoidReach( args[1] );
		return 0;
	case BOTLIB_AI_REACHABILITY_AREA:
		return botlib_export->ai.BotReachabilityArea( VMA(1), args[2] );
	case BOTLIB_AI_MOVEMENT_VIEW_TARGET:
		return botlib_export->ai.BotMovementViewTarget( args[1], VMA(2), args[3], VMF(4), VMA(5) );
	case BOTLIB_AI_PREDICT_VISIBLE_POSITION:
		return botlib_export->ai.BotPredictVisiblePosition( VMA(1), args[2], VMA(3), args[4], VMA(5) );
	case BOTLIB_AI_ALLOC_MOVE_STATE:
		return botlib_export->ai.BotAllocMoveState();
	case BOTLIB_AI_FREE_MOVE_STATE:
		botlib_export->ai.BotFreeMoveState( args[1] );
		return 0;
	case BOTLIB_AI_INIT_MOVE_STATE:
		botlib_export->ai.BotInitMoveState( args[1], VMA(2) );
		return 0;

	case BOTLIB_AI_CHOOSE_BEST_FIGHT_WEAPON:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[2], MAX_BOTLIB_INVENTORY * sizeof( int ) );
#endif
		return botlib_export->ai.BotChooseBestFightWeapon( args[1], VMA(2) );
	case BOTLIB_AI_GET_WEAPON_INFO:
		botlib_export->ai.BotGetWeaponInfo( args[1], args[2], VMA(3) );
		return 0;
	case BOTLIB_AI_LOAD_WEAPON_WEIGHTS:
		return botlib_export->ai.BotLoadWeaponWeights( args[1], VMA(2) );
	case BOTLIB_AI_ALLOC_WEAPON_STATE:
		return botlib_export->ai.BotAllocWeaponState();
	case BOTLIB_AI_FREE_WEAPON_STATE:
		botlib_export->ai.BotFreeWeaponState( args[1] );
		return 0;
	case BOTLIB_AI_RESET_WEAPON_STATE:
		botlib_export->ai.BotResetWeaponState( args[1] );
		return 0;

	case BOTLIB_AI_GENETIC_PARENTS_AND_CHILD_SELECTION:
		return botlib_export->ai.GeneticParentsAndChildSelection(args[1], VMA(2), VMA(3), VMA(4), VMA(5));

	case TRAP_MEMSET:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[1], args[3] );
#endif
		Com_Memset( VMA(1), args[2], args[3] );
		return 0;

	case TRAP_MEMCPY:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS2( gvm, args[1], args[2], args[3] );
#endif
		Com_Memcpy( VMA(1), VMA(2), args[3] );
		return 0;

	case TRAP_STRNCPY:
#ifdef CMOD_VM_BOUNDS_CHECKS
		VM_CHECKBOUNDS( gvm, args[1], args[3] );
#endif
#ifdef CMOD_VM_STRNCPY_FIX
		vm_strncpy( VMA(1), VMA(2), args[3] );
#else
		strncpy( VMA(1), VMA(2), args[3] );
#endif
		return args[1];

	case TRAP_SIN:
		return FloatAsInt( sin( VMF(1) ) );

	case TRAP_COS:
		return FloatAsInt( cos( VMF(1) ) );

	case TRAP_ATAN2:
		return FloatAsInt( atan2( VMF(1), VMF(2) ) );

	case TRAP_SQRT:
		return FloatAsInt( sqrt( VMF(1) ) );

	case TRAP_MATRIXMULTIPLY:
		MatrixMultiply( VMA(1), VMA(2), VMA(3) );
		return 0;

	case TRAP_ANGLEVECTORS:
		AngleVectors( VMA(1), VMA(2), VMA(3), VMA(4) );
		return 0;

	case TRAP_PERPENDICULARVECTOR:
		PerpendicularVector( VMA(1), VMA(2) );
		return 0;

	case TRAP_FLOOR:
		return FloatAsInt( floor( VMF(1) ) );

	case TRAP_CEIL:
		return FloatAsInt( ceil( VMF(1) ) );

#ifdef ELITEFORCE
	case BOTLIB_EA_USE_ITEM:
		botlib_export->ea.EA_UseItem(args[1], VMA(2));
		return 0;
	case BOTLIB_EA_DROP_ITEM:
		botlib_export->ea.EA_DropItem(args[1], VMA(2));
		return 0;
	case BOTLIB_EA_USE_INV:
		botlib_export->ea.EA_UseInv(args[1], VMA(2));
		return 0;
	case BOTLIB_EA_DROP_INV:
		botlib_export->ea.EA_DropInv(args[1], VMA(2));
		return 0;
	case BOTLIB_EA_ALT_ATTACK:
#ifdef CMOD_BOT_TWEAKS
		botlib_export->ea.EA_AltAttack( args[1] );
#else
		botlib_export->ea.EA_Attack( args[1] );
#endif
		return 0;
#endif


	default:
		Com_Error( ERR_DROP, "Bad game system trap: %ld", (long int) args[0] );
	}
	return 0;
}

/*
===============
SV_ShutdownGameProgs

Called every time a map changes
===============
*/
void SV_ShutdownGameProgs( void ) {
#ifdef CMOD_RECORD
	record_game_shutdown();
#endif
	if ( !gvm ) {
		return;
	}
	VM_Call( gvm, GAME_SHUTDOWN, qfalse );
#ifdef CMOD_MISC_SECURITY
	// Do not rely on the VM to release botlib's pointers before the hunk is cleared.
	SV_BotLibShutdown();
#endif
#ifdef NEW_FILESYSTEM
	FS_Handle_CloseAllOwner( FS_HANDLEOWNER_QAGAME );
#endif
	VM_Free( gvm );
	gvm = NULL;
}

/*
==================
SV_InitGameVM

Called for both a full init and a restart
==================
*/
static void SV_InitGameVM( qboolean restart ) {
	int		i;

	// start the entity parsing at the beginning
	sv.entityParsePoint = CM_EntityString();

	// clear all gentity pointers that might still be set from
	// a previous level
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=522
	//   now done before GAME_INIT call
	for ( i = 0 ; i < sv_maxclients->integer ; i++ ) {
		svs.clients[i].gentity = NULL;
	}
	
#ifdef CMOD_SUPPORT_STATUS_SCORES_OVERRIDE
	SV_StatusScoresOverride_Reset();
#endif

	// use the current msec count for a random seed
	// init for this gamestate
	VM_Call (gvm, GAME_INIT, sv.time, Com_Milliseconds(), restart);
}



/*
===================
SV_RestartGameProgs

Called on a map_restart, but not on a normal map change
===================
*/
void SV_RestartGameProgs( void ) {
	if ( !gvm ) {
		return;
	}
	VM_Call( gvm, GAME_SHUTDOWN, qtrue );
#ifdef NEW_FILESYSTEM
	FS_Handle_CloseAllOwner( FS_HANDLEOWNER_QAGAME );
#endif

	// do a restart instead of a free
	gvm = VM_Restart(gvm, qtrue);
	if ( !gvm ) {
		Com_Error( ERR_FATAL, "VM_Restart on game failed" );
	}

	SV_InitGameVM( qtrue );
}


/*
===============
SV_InitGameProgs

Called on a normal map change, not on a map_restart
===============
*/
void SV_InitGameProgs( void ) {
	cvar_t	*var;
	//FIXME these are temp while I make bots run in vm
	extern int	bot_enable;

	var = Cvar_Get( "bot_enable", "1", CVAR_LATCH );
	if ( var ) {
		bot_enable = var->integer;
	}
	else {
		bot_enable = 0;
	}

	// load the dll or bytecode
	gvm = VM_Create( "qagame", SV_GameSystemCalls, Cvar_VariableValue( "vm_game" ) );
	if ( !gvm ) {
		Com_Error( ERR_FATAL, "VM_Create on game failed" );
	}

	SV_InitGameVM( qfalse );
}


/*
====================
SV_GameCommand

See if the current console command is claimed by the game
====================
*/
qboolean SV_GameCommand( void ) {
	if ( sv.state != SS_GAME ) {
		return qfalse;
	}

	return VM_Call( gvm, GAME_CONSOLE_COMMAND );
}

