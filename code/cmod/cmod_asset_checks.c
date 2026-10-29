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

#ifdef CMOD_ASSET_CHECKS

#include "../filesystem/fslocal.h"
#include "../qcommon/cm_local.h"
#include "../botlib/aasfile.h"
#include "../renderercommon/iqm.h"
#ifndef DEDICATED
#include "../client/snd_codec.h"
#endif

/*
==========================================================================================
Common Verification Helpers
==========================================================================================
*/

char assetCheck_error[256];

static Q_PRINTF_FUNC(2, 0) qboolean AssetCheck_VFail( const char *checkName, const char *fmt, va_list argptr ) {
	if ( !assetCheck_error[0] ) {
		Q_vsnprintf( assetCheck_error, sizeof( assetCheck_error ), fmt, argptr );
	}

	return qfalse;
}

#define ASSETCHECK_INT_MAX 0x7fffffff
#define ASSETCHECK_INT_MIN ( -ASSETCHECK_INT_MAX - 1 )
#define ASSETCHECK_FLOAT_MAX 3.402823466e+38F
#define ASSETCHECK_MAX_ALLOC_SIZE 2000000000

/*
==========================================================================================
BSP Verification
==========================================================================================
*/

#define ASSETCHECK_MAX_FACE_POINTS 64
#define ASSETCHECK_MAX_PATCH_SIZE 32
#define ASSETCHECK_VAOCACHE_QUEUE_MAX_VERTEXES ( 1 << 16 )
#define ASSETCHECK_VAOCACHE_QUEUE_MAX_INDEXES ( ASSETCHECK_VAOCACHE_QUEUE_MAX_VERTEXES * 4 )
#define ASSETCHECK_RENDERER_FOG_INDEX_COUNT 32
#define ASSETCHECK_BSP_LIGHTGRID_BYTES 8
#define ASSETCHECK_VIS_HEADER 8
/* R_LoadVisibility always allocates a rounded no-vis row from leaf clusters. */
#define ASSETCHECK_MAX_SAFE_CLUSTER ( MAX_MAP_VISIBILITY - 1 )

/*
 * Conservative upper bounds for expanded in-memory structures.  These are used
 * only to catch signed allocation-size wrap in the BSP loaders.
 */
#define ASSETCHECK_POINTER_BYTES 8
#define ASSETCHECK_HDR_LIGHTGRID_BYTES ( ASSETCHECK_POINTER_BYTES * 6 )
#define ASSETCHECK_RENDERER_MNODE_BYTES 128
#define ASSETCHECK_RENDERER_PLANE_BYTES 32
#define ASSETCHECK_RENDERER_FOG_BYTES 128
#define ASSETCHECK_RENDERER_BMODEL_BYTES 64
#define ASSETCHECK_CM_CMODEL_BYTES 128
#define ASSETCHECK_CM_BRUSH_BYTES 64
#define ASSETCHECK_CM_PLANE_BYTES 32
#define ASSETCHECK_CM_BRUSHSIDE_BYTES 32
#define ASSETCHECK_BOX_BRUSHES 1
#define ASSETCHECK_BOX_SIDES 6
#define ASSETCHECK_BOX_PLANES 12

/*
 * C99-compatible compile-time checks: fail the build if a visible type outgrows
 * its conservative allocation bound.  Renderer-private types and limits defined
 * in other .c files still require manual synchronization.
 */
#define ASSETCHECK_BSP_SIZE_ASSERT( type, bound ) \
	typedef char assetCheckBspSizeCheck_##bound[( sizeof( type ) <= ( bound ) ) ? 1 : -1]

ASSETCHECK_BSP_SIZE_ASSERT( void *, ASSETCHECK_POINTER_BYTES );
ASSETCHECK_BSP_SIZE_ASSERT( cplane_t, ASSETCHECK_RENDERER_PLANE_BYTES );
ASSETCHECK_BSP_SIZE_ASSERT( cmodel_t, ASSETCHECK_CM_CMODEL_BYTES );
ASSETCHECK_BSP_SIZE_ASSERT( cbrush_t, ASSETCHECK_CM_BRUSH_BYTES );
ASSETCHECK_BSP_SIZE_ASSERT( cplane_t, ASSETCHECK_CM_PLANE_BYTES );
ASSETCHECK_BSP_SIZE_ASSERT( cbrushside_t, ASSETCHECK_CM_BRUSHSIDE_BYTES );

#undef ASSETCHECK_BSP_SIZE_ASSERT

typedef struct {
	byte			*data;
	unsigned int	size;
	lump_t			lumps[HEADER_LUMPS];
	int				counts[HEADER_LUMPS];
	float			lightGridSize[3];
} assetCheckBsp_t;

static Q_PRINTF_FUNC(1, 2) qboolean AssetCheck_BSPFail( const char *fmt, ... ) {
	va_list argptr;
	qboolean result;

	va_start( argptr, fmt );
	result = AssetCheck_VFail( "AssetCheck_VerifyBSP", fmt, argptr );
	va_end( argptr );
	return result;
}

static const char *AssetCheck_BSPLumpName( int lump ) {
	static const char *names[HEADER_LUMPS] = {
		"entities",
		"shaders",
		"planes",
		"nodes",
		"leafs",
		"leafsurfaces",
		"leafbrushes",
		"models",
		"brushes",
		"brushsides",
		"drawverts",
		"drawindexes",
		"fogs",
		"surfaces",
		"lightmaps",
		"lightgrid",
		"visibility"
	};

	if ( lump < 0 || lump >= HEADER_LUMPS ) {
		return "unknown";
	}
	return names[lump];
}

static int AssetCheck_ReadLittleLong( const void *ptr ) {
	int value;

	Com_Memcpy( &value, ptr, sizeof( value ) );
	return LittleLong( value );
}

static unsigned int AssetCheck_ReadLittleUInt( const void *ptr ) {
	const byte *p = (const byte *)ptr;

	return (unsigned int)p[0] |
			( (unsigned int)p[1] << 8 ) |
			( (unsigned int)p[2] << 16 ) |
			( (unsigned int)p[3] << 24 );
}

static float AssetCheck_ReadLittleFloat( const void *ptr ) {
	float value;

	Com_Memcpy( &value, ptr, sizeof( value ) );
	return LittleFloat( value );
}

static const byte *AssetCheck_BSPLumpData( const assetCheckBsp_t *bsp, int lump ) {
	return bsp->data + bsp->lumps[lump].fileofs;
}

static qboolean AssetCheck_IsFiniteFloat( float value ) {
	if ( !( value >= -ASSETCHECK_FLOAT_MAX && value <= ASSETCHECK_FLOAT_MAX ) ) {
		return qfalse;
	}
	return qtrue;
}

static qboolean AssetCheck_BSPRangeAt( int first, int count, int limit, const char *what, int index ) {
	if ( first < 0 || count < 0 || first > limit || count > limit - first ) {
		return AssetCheck_BSPFail( "%s %i range %i + %i exceeds limit %i", what, index, first, count, limit );
	}

	return qtrue;
}

static qboolean AssetCheck_BSPSafeAllocSize( long long size, const char *what, int index ) {
	if ( size < 0 || size > ASSETCHECK_MAX_ALLOC_SIZE ) {
		if ( index >= 0 ) {
			return AssetCheck_BSPFail( "%s %i allocation would be too large", what, index );
		}
		return AssetCheck_BSPFail( "%s allocation would be too large", what );
	}

	return qtrue;
}

static qboolean AssetCheck_BSPSafeArrayAlloc( long long count, long long elemSize, const char *what ) {
	if ( count < 0 || elemSize <= 0 ) {
		return AssetCheck_BSPFail( "%s allocation has invalid dimensions", what );
	}

	return AssetCheck_BSPSafeAllocSize( count * elemSize, what, -1 );
}

static void AssetCheck_BSPLumpCount( assetCheckBsp_t *bsp, int lump, int elemSize ) {
	bsp->counts[lump] = bsp->lumps[lump].filelen / elemSize;
}

static qboolean AssetCheck_BSPStringIsTerminated( const char *string, int length ) {
	if ( length <= 0 ) {
		return qfalse;
	}

	return memchr( string, '\0', length ) != NULL;
}

static qboolean AssetCheck_BSPEntitiesAreTerminated( const assetCheckBsp_t *bsp ) {
	const char *entities;
	unsigned int end;
	int length;

	entities = (const char *)AssetCheck_BSPLumpData( bsp, LUMP_ENTITIES );
	length = bsp->lumps[LUMP_ENTITIES].filelen;
	end = (unsigned int)bsp->lumps[LUMP_ENTITIES].fileofs + (unsigned int)length;
	if ( AssetCheck_BSPStringIsTerminated( entities, length ) ) {
		return qtrue;
	}

	if ( end < bsp->size && entities[length] == '\0' ) {
		return qtrue;
	}

	/* FS_ReadData adds a trailing NUL after the loaded file buffer. */
	if ( end == bsp->size ) {
		return qtrue;
	}

	return qfalse;
}

static qboolean AssetCheck_BSPValidateHeader( assetCheckBsp_t *bsp, byte *data, unsigned int size ) {
	const dheader_t *header;
	int i;

	if ( !data ) {
		return AssetCheck_BSPFail( "missing file data" );
	}
	if ( size > ASSETCHECK_INT_MAX ) {
		return AssetCheck_BSPFail( "file is too large" );
	}
	if ( size < sizeof( dheader_t ) ) {
		return AssetCheck_BSPFail( "file is smaller than a BSP header" );
	}

	Com_Memset( bsp, 0, sizeof( *bsp ) );
	bsp->data = data;
	bsp->size = size;
	bsp->lightGridSize[0] = 64.0f;
	bsp->lightGridSize[1] = 64.0f;
	bsp->lightGridSize[2] = 128.0f;

	header = (const dheader_t *)data;
	if ( LittleLong( header->ident ) != BSP_IDENT ) {
		return AssetCheck_BSPFail( "file has wrong ident" );
	}
	if ( LittleLong( header->version ) != BSP_VERSION ) {
		return AssetCheck_BSPFail( "file has wrong version" );
	}

	for ( i = 0 ; i < HEADER_LUMPS ; i++ ) {
		int fileofs = LittleLong( header->lumps[i].fileofs );
		int filelen = LittleLong( header->lumps[i].filelen );
		unsigned int available;

		if ( fileofs < 0 || filelen < 0 ) {
			return AssetCheck_BSPFail( "%s lump has a negative offset or length", AssetCheck_BSPLumpName( i ) );
		}
		/*
		 * RE_LoadWorldMap byte-swaps the header in place before loading any
		 * lumps.  On big-endian systems, a lump which aliases the header would
		 * therefore contain different values when consumed than it did when
		 * verified.  R_LoadEntities consumes its offset even for an empty lump;
		 * the other loaders do not consume empty lumps.
		 */
		if ( ( filelen || i == LUMP_ENTITIES ) && (unsigned int)fileofs < sizeof( *header ) ) {
			return AssetCheck_BSPFail( "%s lump overlaps the mutable BSP header", AssetCheck_BSPLumpName( i ) );
		}
		if ( (unsigned int)fileofs > size ) {
			return AssetCheck_BSPFail( "%s lump points outside the file", AssetCheck_BSPLumpName( i ) );
		}
		if ( i == LUMP_ENTITIES ) {
			if ( filelen > ASSETCHECK_MAX_ALLOC_SIZE ) {
				return AssetCheck_BSPFail( "entities lump is too large to allocate safely" );
			}
		}

		available = size - (unsigned int)fileofs;
		if ( (unsigned int)filelen > available ) {
			if ( i != LUMP_ENTITIES ) {
				return AssetCheck_BSPFail( "%s lump points outside the file", AssetCheck_BSPLumpName( i ) );
			}
			filelen = available;
		}

		bsp->lumps[i].fileofs = fileofs;
		bsp->lumps[i].filelen = filelen;
	}

	return qtrue;
}

static void AssetCheck_BSPCountLumps( assetCheckBsp_t *bsp ) {
	AssetCheck_BSPLumpCount( bsp, LUMP_SHADERS, sizeof( dshader_t ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_PLANES, sizeof( dplane_t ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_NODES, sizeof( dnode_t ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_LEAFS, sizeof( dleaf_t ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_LEAFSURFACES, sizeof( int ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_LEAFBRUSHES, sizeof( int ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_MODELS, sizeof( dmodel_t ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_BRUSHES, sizeof( dbrush_t ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_BRUSHSIDES, sizeof( dbrushside_t ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_DRAWVERTS, sizeof( drawVert_t ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_DRAWINDEXES, sizeof( int ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_FOGS, sizeof( dfog_t ) );
	AssetCheck_BSPLumpCount( bsp, LUMP_SURFACES, sizeof( dsurface_t ) );
}

static qboolean AssetCheck_BSPValidateLoaderAllocations( const assetCheckBsp_t *bsp ) {
	long long nodeLeafCount;

	nodeLeafCount = (long long)bsp->counts[LUMP_NODES] + (long long)bsp->counts[LUMP_LEAFS];

	if ( !AssetCheck_BSPSafeArrayAlloc( bsp->counts[LUMP_MODELS], ASSETCHECK_RENDERER_BMODEL_BYTES, "renderer model table" ) ||
		 !AssetCheck_BSPSafeArrayAlloc( nodeLeafCount, ASSETCHECK_RENDERER_MNODE_BYTES, "renderer node table" ) ||
		 !AssetCheck_BSPSafeArrayAlloc( bsp->counts[LUMP_LEAFSURFACES], ASSETCHECK_POINTER_BYTES, "renderer marksurface table" ) ||
		 !AssetCheck_BSPSafeArrayAlloc( (long long)bsp->counts[LUMP_PLANES] * 2, ASSETCHECK_RENDERER_PLANE_BYTES, "renderer plane table" ) ||
		 !AssetCheck_BSPSafeArrayAlloc( (long long)bsp->counts[LUMP_FOGS] + 1, ASSETCHECK_RENDERER_FOG_BYTES, "renderer fog table" ) ||
		 !AssetCheck_BSPSafeArrayAlloc( bsp->counts[LUMP_MODELS], ASSETCHECK_CM_CMODEL_BYTES, "clip model table" ) ||
		 !AssetCheck_BSPSafeArrayAlloc( (long long)bsp->counts[LUMP_BRUSHES] + ASSETCHECK_BOX_BRUSHES, ASSETCHECK_CM_BRUSH_BYTES, "clip brush table" ) ||
		 !AssetCheck_BSPSafeArrayAlloc( (long long)bsp->counts[LUMP_PLANES] + ASSETCHECK_BOX_PLANES, ASSETCHECK_CM_PLANE_BYTES, "clip plane table" ) ||
		 !AssetCheck_BSPSafeArrayAlloc( (long long)bsp->counts[LUMP_LEAFBRUSHES] + ASSETCHECK_BOX_BRUSHES, sizeof( int ), "clip leafbrush table" ) ||
		 !AssetCheck_BSPSafeArrayAlloc( (long long)bsp->counts[LUMP_BRUSHSIDES] + ASSETCHECK_BOX_SIDES, ASSETCHECK_CM_BRUSHSIDE_BYTES, "clip brushside table" ) ) {
		return qfalse;
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateRendererFogCount( const assetCheckBsp_t *bsp ) {
	/* The renderer draw-sort key has a five-bit fog field, including fog zero. */
	if ( bsp->counts[LUMP_FOGS] >= ASSETCHECK_RENDERER_FOG_INDEX_COUNT ) {
		return AssetCheck_BSPFail( "map has too many fogs for the renderer sort key" );
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateNodeLeafCounts( const assetCheckBsp_t *bsp ) {
	/*
	 * R_LoadNodesAndLeafs dereferences node zero unconditionally; with no nodes
	 * and no leafs it is a zero-byte hunk allocation holding stale data.  A map
	 * with nodes but no leafs has no leaf to terminate a traversal.
	 */
	if ( bsp->counts[LUMP_NODES] < 1 ) {
		return AssetCheck_BSPFail( "map has no nodes" );
	}
	if ( bsp->counts[LUMP_LEAFS] < 1 ) {
		return AssetCheck_BSPFail( "map has no leafs" );
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateEntities( assetCheckBsp_t *bsp ) {
	char *parse;
	char *token;
	const char *entities;

	entities = (const char *)AssetCheck_BSPLumpData( bsp, LUMP_ENTITIES );
	if ( !AssetCheck_BSPEntitiesAreTerminated( bsp ) ) {
		return AssetCheck_BSPFail( "entities lump is not NUL-terminated" );
	}

	parse = (char *)entities;
	token = COM_ParseExt( &parse, qtrue );
	if ( !token[0] || token[0] != '{' ) {
		return qtrue;
	}

	while ( 1 ) {
		char key[MAX_TOKEN_CHARS];
		float x, y, z;

		token = COM_ParseExt( &parse, qtrue );
		if ( !token[0] || token[0] == '}' ) {
			break;
		}
		Q_strncpyz( key, token, sizeof( key ) );

		token = COM_ParseExt( &parse, qtrue );
		if ( !token[0] || token[0] == '}' ) {
			break;
		}

		/*
		 * Both renderers stop parsing worldspawn on an empty token or a shader
		 * remap without a semicolon.  Do not validate a later gridsize that
		 * R_LoadEntities will never consume.
		 */
		if ( ( !Q_strncmp( key, "vertexremapshader", strlen( "vertexremapshader" ) ) ||
			   !Q_strncmp( key, "remapshader", strlen( "remapshader" ) ) ) &&
			 !strchr( token, ';' ) ) {
			break;
		}

		if ( Q_stricmp( key, "gridsize" ) ) {
			continue;
		}

		x = bsp->lightGridSize[0];
		y = bsp->lightGridSize[1];
		z = bsp->lightGridSize[2];
		sscanf( token, "%f %f %f", &x, &y, &z );
		if ( !AssetCheck_IsFiniteFloat( x ) || !AssetCheck_IsFiniteFloat( y ) || !AssetCheck_IsFiniteFloat( z ) ||
			 !( x > 0.0f ) || !( y > 0.0f ) || !( z > 0.0f ) ) {
			return AssetCheck_BSPFail( "worldspawn gridsize is out of range" );
		}

		bsp->lightGridSize[0] = x;
		bsp->lightGridSize[1] = y;
		bsp->lightGridSize[2] = z;
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateVisibility( const assetCheckBsp_t *bsp ) {
	const dleaf_t *leafs;
	const byte *vis;
	int maxLeafCluster = -1;
	long long rows;
	int numClusters;
	int clusterBytes;
	int len;
	int i;

	len = bsp->lumps[LUMP_VISIBILITY].filelen;
	if ( !len ) {
		return qtrue;
	}
	if ( len < ASSETCHECK_VIS_HEADER ) {
		return AssetCheck_BSPFail( "visibility lump is smaller than its header" );
	}
	if ( len > ASSETCHECK_MAX_ALLOC_SIZE ) {
		return AssetCheck_BSPFail( "visibility lump is too large to allocate safely" );
	}

	/*
	 * Match CMod_LoadLeafs, which derives numClusters from every leaf (reachable
	 * or not) before the visibility header overwrites it.  PVS readers index rows
	 * with these leaf clusters, while CM_ClusterPVS and R_ClusterPVS select rows
	 * using the header's numClusters, falling back to row 0 when out of range.
	 */
	leafs = (const dleaf_t *)AssetCheck_BSPLumpData( bsp, LUMP_LEAFS );
	for ( i = 0 ; i < bsp->counts[LUMP_LEAFS] ; i++ ) {
		int cluster = AssetCheck_ReadLittleLong( &leafs[i].cluster );

		if ( cluster > maxLeafCluster ) {
			maxLeafCluster = cluster;
		}
	}

	vis = AssetCheck_BSPLumpData( bsp, LUMP_VISIBILITY );
	numClusters = AssetCheck_ReadLittleLong( vis );
	clusterBytes = AssetCheck_ReadLittleLong( vis + 4 );
	if ( numClusters < 0 || clusterBytes < 0 ) {
		return AssetCheck_BSPFail( "visibility header has negative dimensions %i x %i", numClusters, clusterBytes );
	}
	if ( (long long)clusterBytes * 8 <= maxLeafCluster ) {
		return AssetCheck_BSPFail( "visibility rows (%i bytes) are too narrow for leaf cluster %i",
				clusterBytes, maxLeafCluster );
	}
	rows = numClusters > 0 ? numClusters : 1;
	if ( rows * clusterBytes > len - ASSETCHECK_VIS_HEADER ) {
		return AssetCheck_BSPFail( "visibility header %i x %i exceeds lump size %i", numClusters, clusterBytes, len );
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateLeafArrayEntries( const assetCheckBsp_t *bsp, int firstLeafSurface, int numLeafSurfaces,
		int firstLeafBrush, int numLeafBrushes, int leafNum ) {
	const int *leafSurfaces;
	const int *leafBrushes;
	int i;

	leafSurfaces = (const int *)AssetCheck_BSPLumpData( bsp, LUMP_LEAFSURFACES );
	for ( i = 0 ; i < numLeafSurfaces ; i++ ) {
		int entry = firstLeafSurface + i;
		int surfaceNum = AssetCheck_ReadLittleLong( &leafSurfaces[entry] );

		if ( surfaceNum < 0 || surfaceNum >= bsp->counts[LUMP_SURFACES] ) {
			return AssetCheck_BSPFail( "leaf %i leafsurface %i references invalid surface %i", leafNum, entry, surfaceNum );
		}
	}

	leafBrushes = (const int *)AssetCheck_BSPLumpData( bsp, LUMP_LEAFBRUSHES );
	for ( i = 0 ; i < numLeafBrushes ; i++ ) {
		int entry = firstLeafBrush + i;
		int brushNum = AssetCheck_ReadLittleLong( &leafBrushes[entry] );

		if ( brushNum < 0 || brushNum >= bsp->counts[LUMP_BRUSHES] ) {
			return AssetCheck_BSPFail( "leaf %i leafbrush %i references invalid brush %i", leafNum, entry, brushNum );
		}
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateLeafs( const assetCheckBsp_t *bsp, const byte *reachableLeafs ) {
	const dleaf_t *leafs;
	int i;

	leafs = (const dleaf_t *)AssetCheck_BSPLumpData( bsp, LUMP_LEAFS );
	for ( i = 0 ; i < bsp->counts[LUMP_LEAFS] ; i++ ) {
		int cluster = AssetCheck_ReadLittleLong( &leafs[i].cluster );
		int area = AssetCheck_ReadLittleLong( &leafs[i].area );
		int firstLeafSurface = AssetCheck_ReadLittleLong( &leafs[i].firstLeafSurface );
		int numLeafSurfaces = AssetCheck_ReadLittleLong( &leafs[i].numLeafSurfaces );
		int firstLeafBrush = AssetCheck_ReadLittleLong( &leafs[i].firstLeafBrush );
		int numLeafBrushes = AssetCheck_ReadLittleLong( &leafs[i].numLeafBrushes );

		if ( cluster < -1 || cluster > ASSETCHECK_MAX_SAFE_CLUSTER ) {
			return AssetCheck_BSPFail( "leaf %i has invalid cluster %i", i, cluster );
		}
		if ( area < -1 || area >= MAX_MAP_AREAS ) {
			return AssetCheck_BSPFail( "leaf %i has invalid area %i", i, area );
		}
		if ( !reachableLeafs[i] ) {
			continue;
		}
		if ( !AssetCheck_BSPRangeAt( firstLeafSurface, numLeafSurfaces, bsp->counts[LUMP_LEAFSURFACES], "leaf surfaces", i ) ||
			 !AssetCheck_BSPRangeAt( firstLeafBrush, numLeafBrushes, bsp->counts[LUMP_LEAFBRUSHES], "leaf brushes", i ) ) {
			return qfalse;
		}
		if ( !AssetCheck_BSPValidateLeafArrayEntries( bsp, firstLeafSurface, numLeafSurfaces, firstLeafBrush, numLeafBrushes, i ) ) {
			return qfalse;
		}
	}

	return qtrue;
}

static void AssetCheck_BSPMarkBrushSides( byte *usedBrushSides, int firstSide, int numSides ) {
	if ( numSides > 0 ) {
		memset( usedBrushSides + firstSide, 1, numSides );
	}
}

static void AssetCheck_BSPMarkBrushes( byte *usedBrushes, int firstBrush, int numBrushes ) {
	if ( numBrushes > 0 ) {
		memset( usedBrushes + firstBrush, 1, numBrushes );
	}
}

/*
 * Requires successful model and leaf validation, using reachableLeafs populated
 * by AssetCheck_BSPValidateNodes.  AssetCheck_BSPValidateModels bounds submodel
 * brush ranges; AssetCheck_BSPValidateLeafs bounds reachable leafbrush ranges
 * and validates their brush indexes before they are used to index usedBrushes.
 */
static void AssetCheck_BSPMarkUsedBrushes( const assetCheckBsp_t *bsp, const byte *reachableLeafs, byte *usedBrushes ) {
	const dleaf_t *leafs;
	const int *leafBrushes;
	const dmodel_t *models;
	int i;

	leafs = (const dleaf_t *)AssetCheck_BSPLumpData( bsp, LUMP_LEAFS );
	leafBrushes = (const int *)AssetCheck_BSPLumpData( bsp, LUMP_LEAFBRUSHES );
	for ( i = 0 ; i < bsp->counts[LUMP_LEAFS] ; i++ ) {
		int firstLeafBrush = AssetCheck_ReadLittleLong( &leafs[i].firstLeafBrush );
		int numLeafBrushes = AssetCheck_ReadLittleLong( &leafs[i].numLeafBrushes );
		int j;

		if ( !reachableLeafs[i] ) {
			continue;
		}
		for ( j = 0 ; j < numLeafBrushes ; j++ ) {
			int brushNum = AssetCheck_ReadLittleLong( &leafBrushes[firstLeafBrush + j] );

			usedBrushes[brushNum] = 1;
		}
	}

	models = (const dmodel_t *)AssetCheck_BSPLumpData( bsp, LUMP_MODELS );
	for ( i = 1 ; i < bsp->counts[LUMP_MODELS] ; i++ ) {
		int firstBrush = AssetCheck_ReadLittleLong( &models[i].firstBrush );
		int numBrushes = AssetCheck_ReadLittleLong( &models[i].numBrushes );

		AssetCheck_BSPMarkBrushes( usedBrushes, firstBrush, numBrushes );
	}
}

static qboolean AssetCheck_BSPValidateUsedBrushSides( const assetCheckBsp_t *bsp, const byte *usedBrushSides ) {
	const dbrushside_t *sides;
	int i;

	sides = (const dbrushside_t *)AssetCheck_BSPLumpData( bsp, LUMP_BRUSHSIDES );
	for ( i = 0 ; i < bsp->counts[LUMP_BRUSHSIDES] ; i++ ) {
		int planeNum = AssetCheck_ReadLittleLong( &sides[i].planeNum );

		if ( !usedBrushSides[i] ) {
			continue;
		}
		if ( planeNum < 0 || planeNum >= bsp->counts[LUMP_PLANES] ) {
			return AssetCheck_BSPFail( "brushside %i references invalid plane %i", i, planeNum );
		}
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateBrushes( const assetCheckBsp_t *bsp, const byte *usedBrushes, byte *usedBrushSides ) {
	const dbrush_t *brushes;
	int i;

	brushes = (const dbrush_t *)AssetCheck_BSPLumpData( bsp, LUMP_BRUSHES );
	for ( i = 0 ; i < bsp->counts[LUMP_BRUSHES] ; i++ ) {
		int firstSide = AssetCheck_ReadLittleLong( &brushes[i].firstSide );
		int numSides = AssetCheck_ReadLittleLong( &brushes[i].numSides );

		/*
		 * CMod_LoadBrushes calls CM_BoundBrush for every brush, including unused
		 * brushes.  It reads the first six sides unconditionally, even when
		 * numSides is smaller, so those sides and their planes must be checked.
		 */
		if ( !AssetCheck_BSPRangeAt( firstSide, 6, bsp->counts[LUMP_BRUSHSIDES], "brush axial sides", i ) ) {
			return qfalse;
		}
		AssetCheck_BSPMarkBrushSides( usedBrushSides, firstSide, 6 );
		if ( usedBrushes[i] ) {
			if ( !AssetCheck_BSPRangeAt( firstSide, numSides, bsp->counts[LUMP_BRUSHSIDES], "brush sides", i ) ) {
				return qfalse;
			}
			AssetCheck_BSPMarkBrushSides( usedBrushSides, firstSide, numSides );
		}
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateBrushData( const assetCheckBsp_t *bsp, const byte *reachableLeafs ) {
	byte *usedBrushes;
	byte *usedBrushSides;
	qboolean valid;

	usedBrushes = (byte *)Z_Malloc( bsp->counts[LUMP_BRUSHES] );
	usedBrushSides = (byte *)Z_Malloc( bsp->counts[LUMP_BRUSHSIDES] );
	AssetCheck_BSPMarkUsedBrushes( bsp, reachableLeafs, usedBrushes );

	valid = AssetCheck_BSPValidateBrushes( bsp, usedBrushes, usedBrushSides ) &&
			AssetCheck_BSPValidateUsedBrushSides( bsp, usedBrushSides );

	Z_Free( usedBrushSides );
	Z_Free( usedBrushes );
	return valid;
}

static qboolean AssetCheck_BSPValidateModels( const assetCheckBsp_t *bsp ) {
	const dmodel_t *models;
	int i;

	if ( bsp->counts[LUMP_MODELS] < 1 ) {
		return AssetCheck_BSPFail( "map has no models" );
	}

	models = (const dmodel_t *)AssetCheck_BSPLumpData( bsp, LUMP_MODELS );
	for ( i = 0 ; i < bsp->counts[LUMP_MODELS] ; i++ ) {
		int firstSurface = AssetCheck_ReadLittleLong( &models[i].firstSurface );
		int numSurfaces = AssetCheck_ReadLittleLong( &models[i].numSurfaces );
		int firstBrush = AssetCheck_ReadLittleLong( &models[i].firstBrush );
		int numBrushes = AssetCheck_ReadLittleLong( &models[i].numBrushes );

		if ( !AssetCheck_BSPRangeAt( firstSurface, numSurfaces, bsp->counts[LUMP_SURFACES], "model surfaces", i ) ) {
			return qfalse;
		}
		if ( i > 0 && !AssetCheck_BSPRangeAt( firstBrush, numBrushes, bsp->counts[LUMP_BRUSHES], "model brushes", i ) ) {
			return qfalse;
		}
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateNodes( const assetCheckBsp_t *bsp, byte *reachableLeafs ) {
	const dnode_t *nodes;
	byte *visitedNodes;
	int *nodeStack;
	int stackDepth;
	qboolean valid;

	nodes = (const dnode_t *)AssetCheck_BSPLumpData( bsp, LUMP_NODES );
	if ( !bsp->counts[LUMP_NODES] ) {
		if ( bsp->counts[LUMP_LEAFS] ) {
			reachableLeafs[0] = 1;
		}
		return qtrue;
	}

	visitedNodes = (byte *)Z_Malloc( bsp->counts[LUMP_NODES] );
	nodeStack = (int *)Z_Malloc( bsp->counts[LUMP_NODES] * sizeof( *nodeStack ) );
	stackDepth = 0;
	valid = qtrue;

	visitedNodes[0] = 1;
	nodeStack[stackDepth++] = 0;
	while ( stackDepth > 0 && valid ) {
		int nodeNum = nodeStack[--stackDepth];
		int planeNum = AssetCheck_ReadLittleLong( &nodes[nodeNum].planeNum );
		int j;

		if ( planeNum < 0 || planeNum >= bsp->counts[LUMP_PLANES] ) {
			valid = AssetCheck_BSPFail( "node %i references invalid plane %i", nodeNum, planeNum );
			break;
		}

		for ( j = 0 ; j < 2 ; j++ ) {
			int child = AssetCheck_ReadLittleLong( &nodes[nodeNum].children[j] );

			if ( child >= 0 ) {
				if ( child >= bsp->counts[LUMP_NODES] ) {
					valid = AssetCheck_BSPFail( "node %i references invalid child node %i", nodeNum, child );
					break;
				}
				if ( !visitedNodes[child] ) {
					visitedNodes[child] = 1;
					nodeStack[stackDepth++] = child;
				}
			} else {
				int leafNum;

				if ( child == ASSETCHECK_INT_MIN ) {
					valid = AssetCheck_BSPFail( "node %i has invalid leaf child", nodeNum );
					break;
				}
				leafNum = -1 - child;
				if ( leafNum < 0 || leafNum >= bsp->counts[LUMP_LEAFS] ) {
					valid = AssetCheck_BSPFail( "node %i references invalid child leaf %i", nodeNum, leafNum );
					break;
				}
				reachableLeafs[leafNum] = 1;
			}
		}
	}

	Z_Free( nodeStack );
	Z_Free( visitedNodes );
	return valid;
}

static qboolean AssetCheck_BSPValidateSurfaceIndexes( const assetCheckBsp_t *bsp, const dsurface_t *surface, int surfaceNum, int numIndexes, int maxIndexValue ) {
	const int *indexes;
	int firstIndex;
	int i;

	firstIndex = AssetCheck_ReadLittleLong( &surface->firstIndex );
	if ( numIndexes % 3 ) {
		return AssetCheck_BSPFail( "surface %i index count is not a multiple of three", surfaceNum );
	}
	if ( !AssetCheck_BSPRangeAt( firstIndex, numIndexes, bsp->counts[LUMP_DRAWINDEXES], "surface indexes", surfaceNum ) ) {
		return qfalse;
	}
	if ( maxIndexValue < 0 ) {
		return qtrue;
	}

	indexes = (const int *)AssetCheck_BSPLumpData( bsp, LUMP_DRAWINDEXES );
	for ( i = 0 ; i < numIndexes ; i++ ) {
		int index = AssetCheck_ReadLittleLong( &indexes[firstIndex + i] );

		if ( index < 0 || index >= maxIndexValue ) {
			return AssetCheck_BSPFail( "planar surface %i has invalid draw index %i", surfaceNum, index );
		}
	}

	return qtrue;
}

static qboolean AssetCheck_BSPSurfaceHasShader( const assetCheckBsp_t *bsp, int shaderNum ) {
	return shaderNum >= 0 && shaderNum < bsp->counts[LUMP_SHADERS];
}

static qboolean AssetCheck_BSPValidateRendererSurfaceQueue( int surfaceNum, int numVerts, int numIndexes, const char *type ) {
	/*
	 * renderergl2's VAO cache has fixed per-batch staging arrays.  A single
	 * oversized BSP surface is queued even after the cache asks for a flush.
	 */
	if ( numVerts > ASSETCHECK_VAOCACHE_QUEUE_MAX_VERTEXES ) {
		return AssetCheck_BSPFail( "%s surface %i has too many vertices for VAO cache (%i > %i)",
				type, surfaceNum, numVerts, ASSETCHECK_VAOCACHE_QUEUE_MAX_VERTEXES );
	}
	if ( numIndexes > ASSETCHECK_VAOCACHE_QUEUE_MAX_INDEXES ) {
		return AssetCheck_BSPFail( "%s surface %i has too many indexes for VAO cache (%i > %i)",
				type, surfaceNum, numIndexes, ASSETCHECK_VAOCACHE_QUEUE_MAX_INDEXES );
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateSurfaces( const assetCheckBsp_t *bsp ) {
	const dsurface_t *surfaces;
	int i;

	surfaces = (const dsurface_t *)AssetCheck_BSPLumpData( bsp, LUMP_SURFACES );
	for ( i = 0 ; i < bsp->counts[LUMP_SURFACES] ; i++ ) {
		int shaderNum = AssetCheck_ReadLittleLong( &surfaces[i].shaderNum );
		int fogNum = AssetCheck_ReadLittleLong( &surfaces[i].fogNum );
		int surfaceType = AssetCheck_ReadLittleLong( &surfaces[i].surfaceType );
		int firstVert = AssetCheck_ReadLittleLong( &surfaces[i].firstVert );
		int numVerts = AssetCheck_ReadLittleLong( &surfaces[i].numVerts );
		int numIndexes = AssetCheck_ReadLittleLong( &surfaces[i].numIndexes );

		/*
		 * The loader stores fogNum + 1 and R_AddDrawSurf shifts it into a
		 * five-bit field without masking.  Values outside this range overwrite
		 * the packed entity and shader fields and can select an unsafe backend
		 * for attacker-controlled surface data.
		 */
		if ( fogNum < -1 || fogNum >= ASSETCHECK_RENDERER_FOG_INDEX_COUNT - 1 ) {
			return AssetCheck_BSPFail( "surface %i fog index %i does not fit the renderer sort key", i, fogNum );
		}

		switch ( surfaceType ) {
		case MST_PLANAR:
			{
				int loadedVerts = numVerts;
				int maxIndexValue;

				/* ShaderForShaderNum drops the load in both renderers before
				 * ParseFace consumes the vertex and index fields checked below. */
				if ( !AssetCheck_BSPSurfaceHasShader( bsp, shaderNum ) ) {
					break;
				}
				if ( numVerts <= 0 ) {
					return AssetCheck_BSPFail( "planar surface %i has invalid vertex count %i", i, numVerts );
				}
				if ( loadedVerts > ASSETCHECK_MAX_FACE_POINTS ) {
					loadedVerts = ASSETCHECK_MAX_FACE_POINTS;
				}

				maxIndexValue = numVerts;
				if ( maxIndexValue > SHADER_MAX_VERTEXES ) {
					maxIndexValue = SHADER_MAX_VERTEXES;
				}
				if ( !AssetCheck_BSPRangeAt( firstVert, loadedVerts, bsp->counts[LUMP_DRAWVERTS], "surface vertices", i ) ||
					 !AssetCheck_BSPValidateSurfaceIndexes( bsp, &surfaces[i], i, numIndexes, maxIndexValue ) ||
					 !AssetCheck_BSPValidateRendererSurfaceQueue( i, loadedVerts, numIndexes, "planar" ) ) {
					return qfalse;
				}
			}
			break;

		case MST_TRIANGLE_SOUP:
			/* ShaderForShaderNum drops the load in both renderers before
			 * ParseTriSurf consumes the vertex and index fields checked below. */
			if ( !AssetCheck_BSPSurfaceHasShader( bsp, shaderNum ) ) {
				break;
			}
			if ( numVerts < 0 ) {
				return AssetCheck_BSPFail( "triangle surface %i has invalid vertex count %i", i, numVerts );
			}
			if ( !AssetCheck_BSPRangeAt( firstVert, numVerts, bsp->counts[LUMP_DRAWVERTS], "surface vertices", i ) ||
				 !AssetCheck_BSPValidateSurfaceIndexes( bsp, &surfaces[i], i, numIndexes, -1 ) ||
				 !AssetCheck_BSPValidateRendererSurfaceQueue( i, numVerts, numIndexes, "triangle" ) ) {
				return qfalse;
			}
			break;

		case MST_PATCH:
			{
				int width = AssetCheck_ReadLittleLong( &surfaces[i].patchWidth );
				int height = AssetCheck_ReadLittleLong( &surfaces[i].patchHeight );
				int numPoints;

				/* CMod_LoadPatches indexes cm.shaders with shaderNum unchecked. */
				if ( !AssetCheck_BSPSurfaceHasShader( bsp, shaderNum ) ) {
					return AssetCheck_BSPFail( "patch surface %i has invalid shader %i", i, shaderNum );
				}
				if ( width <= 0 || width > ASSETCHECK_MAX_PATCH_SIZE || height <= 0 || height > ASSETCHECK_MAX_PATCH_SIZE ) {
					return AssetCheck_BSPFail( "patch surface %i has invalid dimensions %i x %i", i, width, height );
				}
				numPoints = width * height;
				if ( !AssetCheck_BSPRangeAt( firstVert, numPoints, bsp->counts[LUMP_DRAWVERTS], "patch vertices", i ) ) {
					return qfalse;
				}
			}
			break;

		case MST_FLARE:
			break;

		default:
			break;
		}
	}

	return qtrue;
}

static qboolean AssetCheck_BSPValidateLightGrid( const assetCheckBsp_t *bsp ) {
	const dmodel_t *models;
	long long totalPoints;
	long long lightGridBytes;
	int i;

	if ( !bsp->counts[LUMP_MODELS] ) {
		return qtrue;
	}
	models = (const dmodel_t *)AssetCheck_BSPLumpData( bsp, LUMP_MODELS );
	totalPoints = 1;
	for ( i = 0 ; i < 3 ; i++ ) {
		float gridSize = bsp->lightGridSize[i];
		float minsValue = AssetCheck_ReadLittleFloat( &models[0].mins[i] );
		float maxsValue = AssetCheck_ReadLittleFloat( &models[0].maxs[i] );
		float origin;
		float gridMaxs;
		float boundsValue;
		int bounds;

		if ( !AssetCheck_IsFiniteFloat( minsValue ) || !AssetCheck_IsFiniteFloat( maxsValue ) ) {
			return AssetCheck_BSPFail( "lightgrid bounds are out of range" );
		}

		/*
		 * Match R_LoadLightGrid's float intermediates exactly.  Computing these
		 * values in double can cross a float rounding boundary and make the
		 * verifier predict a different point count than the loader.
		 */
		origin = gridSize * ceil( minsValue / gridSize );
		gridMaxs = gridSize * floor( maxsValue / gridSize );
		boundsValue = ( gridMaxs - origin ) / gridSize + 1;

		/* Cast through double so INT_MAX is not rounded up to 2^31 as a float. */
		if ( !( boundsValue >= 0.0f ) || (double)boundsValue > ASSETCHECK_INT_MAX ) {
			return AssetCheck_BSPFail( "lightgrid dimensions are out of range" );
		}
		bounds = (int)boundsValue;

		totalPoints *= bounds;
		if ( totalPoints > ASSETCHECK_INT_MAX / ASSETCHECK_BSP_LIGHTGRID_BYTES ) {
			return AssetCheck_BSPFail( "lightgrid point count would overflow" );
		}
	}

	lightGridBytes = totalPoints * ASSETCHECK_BSP_LIGHTGRID_BYTES;
	if ( bsp->lumps[LUMP_LIGHTGRID].filelen != lightGridBytes ) {
		/* R_LoadLightGrid returns before allocating the BSP or HDR lightgrid. */
		return qtrue;
	}

	if ( lightGridBytes > ASSETCHECK_MAX_ALLOC_SIZE ||
		 totalPoints > ASSETCHECK_MAX_ALLOC_SIZE / ASSETCHECK_HDR_LIGHTGRID_BYTES ) {
		return AssetCheck_BSPFail( "lightgrid point count would overflow" );
	}

	return qtrue;
}

/*
=================
AssetCheck_VerifyBSP

Security pre-pass: return qfalse if a check fails, qtrue otherwise.  Success
does not guarantee that the BSP loaders will accept the map; checks already
enforced by the loaders are not all repeated here.
=================
*/
static qboolean AssetCheck_VerifyBSP( byte *data, unsigned int size ) {
	assetCheckBsp_t bsp;
	byte *reachableLeafs;
	qboolean valid;

	if ( !AssetCheck_BSPValidateHeader( &bsp, data, size ) ) {
		return qfalse;
	}
	AssetCheck_BSPCountLumps( &bsp );

	if ( !AssetCheck_BSPValidateLoaderAllocations( &bsp ) ||
		 !AssetCheck_BSPValidateRendererFogCount( &bsp ) ||
		 !AssetCheck_BSPValidateNodeLeafCounts( &bsp ) ||
		 !AssetCheck_BSPValidateEntities( &bsp ) ||
		 !AssetCheck_BSPValidateVisibility( &bsp ) ||
		 !AssetCheck_BSPValidateModels( &bsp ) ) {
		return qfalse;
	}

	reachableLeafs = (byte *)Z_Malloc( bsp.counts[LUMP_LEAFS] );
	valid = AssetCheck_BSPValidateNodes( &bsp, reachableLeafs ) &&
			AssetCheck_BSPValidateLeafs( &bsp, reachableLeafs ) &&
			AssetCheck_BSPValidateBrushData( &bsp, reachableLeafs ) &&
			AssetCheck_BSPValidateSurfaces( &bsp ) &&
			AssetCheck_BSPValidateLightGrid( &bsp );
	Z_Free( reachableLeafs );
	return valid;
}

/*
==========================================================================================
AAS Verification
==========================================================================================
*/

#define ASSETCHECK_AAS_REVERSED_REACH_BYTES 24
#define ASSETCHECK_AAS_REVERSED_LINK_BYTES 24
#define ASSETCHECK_AAS_REACHABILITY_AREAS_BYTES 8
#define ASSETCHECK_AAS_ROUTING_UPDATE_BYTES 96
#define ASSETCHECK_AAS_ROUTING_CACHE_BASE_BYTES 128
#define ASSETCHECK_AAS_MAX_REACHABILITYPASSAREAS 32
#define ASSETCHECK_AAS_MAX_REACHABILITY_LINKS 128

typedef struct {
	byte		*data;
	unsigned int size;
	aas_lump_t	lumps[AAS_LUMPS];
	int			counts[AAS_LUMPS];
} assetCheckAas_t;

static Q_PRINTF_FUNC(1, 2) qboolean AssetCheck_AASFail( const char *fmt, ... ) {
	va_list argptr;
	qboolean result;

	va_start( argptr, fmt );
	result = AssetCheck_VFail( "AssetCheck_VerifyAAS", fmt, argptr );
	va_end( argptr );
	return result;
}

static qboolean AssetCheck_AASKnownBypass( const byte *data, unsigned int size ) {
	#define ASSETCHECK_AAS_CTF_FSSLIDE_SIZE 1504704u
	static const unsigned char ctfFsslideSha256[32] = {
		0x69, 0x95, 0x39, 0xd1, 0x92, 0x73, 0xfb, 0xe4,
		0x43, 0x20, 0x9f, 0x43, 0x62, 0x42, 0x8f, 0x7c,
		0xb2, 0x5d, 0x3a, 0x28, 0x64, 0xab, 0x4c, 0xd8,
		0x3f, 0xb3, 0xcd, 0xf2, 0x5d, 0x49, 0x81, 0x4f
	};
	unsigned char sha256[32];

	if ( !data || size != ASSETCHECK_AAS_CTF_FSSLIDE_SIZE ) {
		return qfalse;
	}

	FSC_CalculateSHA256( (const char *)data, size, sha256 );
	return !memcmp( sha256, ctfFsslideSha256, sizeof( sha256 ) );
}

static const char *AssetCheck_AASLumpName( int lump ) {
	static const char *names[AAS_LUMPS] = {
		"bboxes",
		"vertexes",
		"planes",
		"edges",
		"edgeindex",
		"faces",
		"faceindex",
		"areas",
		"areasettings",
		"reachability",
		"nodes",
		"portals",
		"portalindex",
		"clusters"
	};

	if ( lump < 0 || lump >= AAS_LUMPS ) {
		return "unknown";
	}
	return names[lump];
}

static int AssetCheck_AASLumpElementSize( int lump ) {
	switch ( lump ) {
		case AASLUMP_BBOXES: return sizeof( aas_bbox_t );
		case AASLUMP_VERTEXES: return sizeof( aas_vertex_t );
		case AASLUMP_PLANES: return sizeof( aas_plane_t );
		case AASLUMP_EDGES: return sizeof( aas_edge_t );
		case AASLUMP_EDGEINDEX: return sizeof( aas_edgeindex_t );
		case AASLUMP_FACES: return sizeof( aas_face_t );
		case AASLUMP_FACEINDEX: return sizeof( aas_faceindex_t );
		case AASLUMP_AREAS: return sizeof( aas_area_t );
		case AASLUMP_AREASETTINGS: return sizeof( aas_areasettings_t );
		case AASLUMP_REACHABILITY: return sizeof( aas_reachability_t );
		case AASLUMP_NODES: return sizeof( aas_node_t );
		case AASLUMP_PORTALS: return sizeof( aas_portal_t );
		case AASLUMP_PORTALINDEX: return sizeof( aas_portalindex_t );
		case AASLUMP_CLUSTERS: return sizeof( aas_cluster_t );
	}

	return 1;
}

static const byte *AssetCheck_AASLumpData( const assetCheckAas_t *aas, int lump ) {
	return aas->data + aas->lumps[lump].fileofs;
}

static void AssetCheck_AASDecodeHeader( aas_header_t *header ) {
	byte *encoded;
	int i;

	encoded = (byte *)header + 8;
	for ( i = 0 ; i < (int)sizeof( *header ) - 8 ; i++ ) {
		encoded[i] ^= (byte)( (unsigned char)i * 119 );
	}
}

static qboolean AssetCheck_AASSafeAllocSize( long long size, const char *what ) {
	if ( size < 0 || size > ASSETCHECK_MAX_ALLOC_SIZE ) {
		return AssetCheck_AASFail( "%s allocation would be too large", what );
	}

	return qtrue;
}

static qboolean AssetCheck_AASSafeArrayAlloc( long long count, long long elemSize, const char *what ) {
	if ( count < 0 || elemSize <= 0 || count > ASSETCHECK_MAX_ALLOC_SIZE / elemSize ) {
		return AssetCheck_AASFail( "%s allocation would be too large", what );
	}

	return qtrue;
}

static qboolean AssetCheck_AASAddAllocBytes( long long *total, long long count, long long elemSize, const char *what ) {
	long long bytes;

	if ( count < 0 || elemSize < 0 || ( elemSize && count > ( ASSETCHECK_MAX_ALLOC_SIZE - *total ) / elemSize ) ) {
		return AssetCheck_AASFail( "%s allocation would be too large", what );
	}

	bytes = count * elemSize;
	if ( bytes > ASSETCHECK_MAX_ALLOC_SIZE - *total ) {
		return AssetCheck_AASFail( "%s allocation would be too large", what );
	}

	*total += bytes;
	return qtrue;
}

static qboolean AssetCheck_AASRangeAt( int first, int count, int limit, const char *what, int index ) {
	if ( count < 0 ) {
		return AssetCheck_AASFail( "%s %i range %i + %i exceeds limit %i", what, index, first, count, limit );
	}
	if ( count == 0 ) {
		return qtrue;
	}
	if ( first < 0 || first > limit || count > limit - first ) {
		return AssetCheck_AASFail( "%s %i range %i + %i exceeds limit %i", what, index, first, count, limit );
	}

	return qtrue;
}

static long long AssetCheck_AASPadLong( long long value ) {
	long long alignment;

	alignment = sizeof( long );
	return ( value + alignment - 1 ) & ~( alignment - 1 );
}

static qboolean AssetCheck_AASValidateHeader( assetCheckAas_t *aas, byte *data, unsigned int size ) {
	aas_header_t header;
	int version;
	int i;
	unsigned int expectedOffset;

	if ( !data ) {
		return AssetCheck_AASFail( "missing file data" );
	}
	if ( size > ASSETCHECK_INT_MAX ) {
		return AssetCheck_AASFail( "file is too large" );
	}
	if ( size < sizeof( aas_header_t ) ) {
		return AssetCheck_AASFail( "file is smaller than an AAS header" );
	}

	Com_Memset( aas, 0, sizeof( *aas ) );
	aas->data = data;
	aas->size = size;

	Com_Memcpy( &header, data, sizeof( header ) );
	if ( LittleLong( header.ident ) != AASID ) {
		return AssetCheck_AASFail( "file has wrong ident" );
	}

	version = LittleLong( header.version );
	if ( version != AASVERSION_OLD && version != AASVERSION ) {
		return AssetCheck_AASFail( "file has wrong version %i", version );
	}
	if ( version == AASVERSION ) {
		AssetCheck_AASDecodeHeader( &header );
	}

	expectedOffset = sizeof( aas_header_t );
	for ( i = 0 ; i < AAS_LUMPS ; i++ ) {
		int fileofs = LittleLong( header.lumps[i].fileofs );
		int filelen = LittleLong( header.lumps[i].filelen );
		unsigned int available;

		if ( fileofs < 0 || filelen < 0 ) {
			return AssetCheck_AASFail( "%s lump has a negative offset or length", AssetCheck_AASLumpName( i ) );
		}
		if ( (unsigned int)fileofs > size ) {
			return AssetCheck_AASFail( "%s lump points outside the file", AssetCheck_AASLumpName( i ) );
		}
		if ( filelen && (unsigned int)fileofs != expectedOffset ) {
			return AssetCheck_AASFail( "%s lump is not sequential", AssetCheck_AASLumpName( i ) );
		}

		available = size - (unsigned int)fileofs;
		if ( (unsigned int)filelen > available ) {
			return AssetCheck_AASFail( "%s lump points outside the file", AssetCheck_AASLumpName( i ) );
		}

		aas->lumps[i].fileofs = fileofs;
		aas->lumps[i].filelen = filelen;
		if ( filelen ) {
			expectedOffset = (unsigned int)fileofs + (unsigned int)filelen;
		}
	}

	return qtrue;
}

static void AssetCheck_AASCountLumps( assetCheckAas_t *aas ) {
	int i;

	for ( i = 0 ; i < AAS_LUMPS ; i++ ) {
		aas->counts[i] = aas->lumps[i].filelen / AssetCheck_AASLumpElementSize( i );
	}
}

static qboolean AssetCheck_AASValidateLoaderAllocations( const assetCheckAas_t *aas ) {
	int i;

	for ( i = 0 ; i < AAS_LUMPS ; i++ ) {
		long long allocSize;

		if ( aas->lumps[i].filelen ) {
			allocSize = (long long)aas->lumps[i].filelen + 1;
		} else {
			allocSize = (long long)AssetCheck_AASLumpElementSize( i ) + 1;
		}

		if ( !AssetCheck_AASSafeAllocSize( allocSize, AssetCheck_AASLumpName( i ) ) ) {
			return qfalse;
		}
	}

	return qtrue;
}

static qboolean AssetCheck_AASValidateBasicCounts( const assetCheckAas_t *aas ) {
	if ( aas->counts[AASLUMP_AREAS] < 1 ) {
		return AssetCheck_AASFail( "file has no areas" );
	}
	if ( aas->counts[AASLUMP_AREASETTINGS] < aas->counts[AASLUMP_AREAS] ) {
		return AssetCheck_AASFail( "area settings are missing entries for loaded areas" );
	}
	if ( aas->counts[AASLUMP_NODES] <= 1 ) {
		return AssetCheck_AASFail( "file has no root node" );
	}
	if ( aas->counts[AASLUMP_CLUSTERS] < 1 ) {
		return AssetCheck_AASFail( "file has no clusters" );
	}
	if ( aas->counts[AASLUMP_PORTALS] < 1 ) {
		return AssetCheck_AASFail( "file has no portals" );
	}

	if ( !AssetCheck_AASSafeArrayAlloc( aas->counts[AASLUMP_AREAS], ASSETCHECK_POINTER_BYTES, "linked area table" ) ||
		 !AssetCheck_AASSafeArrayAlloc( aas->counts[AASLUMP_AREAS], sizeof( int ), "area travel flag table" ) ||
		 !AssetCheck_AASSafeArrayAlloc( aas->counts[AASLUMP_AREAS], ASSETCHECK_POINTER_BYTES, "portal cache table" ) ||
		 !AssetCheck_AASSafeArrayAlloc( aas->counts[AASLUMP_REACHABILITY],
				ASSETCHECK_AAS_REACHABILITY_AREAS_BYTES +
				ASSETCHECK_AAS_MAX_REACHABILITYPASSAREAS * sizeof( int ), "reachability pass area tables" ) ) {
		return qfalse;
	}

	return qtrue;
}

static qboolean AssetCheck_AASValidateGeometry( const assetCheckAas_t *aas ) {
	const aas_edge_t *edges;
	const aas_edgeindex_t *edgeindex;
	const aas_face_t *faces;
	const aas_faceindex_t *faceindex;
	const aas_area_t *areas;
	int i;

	edges = (const aas_edge_t *)AssetCheck_AASLumpData( aas, AASLUMP_EDGES );
	edgeindex = (const aas_edgeindex_t *)AssetCheck_AASLumpData( aas, AASLUMP_EDGEINDEX );
	faces = (const aas_face_t *)AssetCheck_AASLumpData( aas, AASLUMP_FACES );
	faceindex = (const aas_faceindex_t *)AssetCheck_AASLumpData( aas, AASLUMP_FACEINDEX );
	areas = (const aas_area_t *)AssetCheck_AASLumpData( aas, AASLUMP_AREAS );

	for ( i = 1 ; i < aas->counts[AASLUMP_EDGES] ; i++ ) {
		int v0 = AssetCheck_ReadLittleLong( &edges[i].v[0] );
		int v1 = AssetCheck_ReadLittleLong( &edges[i].v[1] );

		if ( v0 < 0 || v0 >= aas->counts[AASLUMP_VERTEXES] ||
			 v1 < 0 || v1 >= aas->counts[AASLUMP_VERTEXES] ) {
			return AssetCheck_AASFail( "edge %i references an invalid vertex", i );
		}
	}

	for ( i = 1 ; i < aas->counts[AASLUMP_FACES] ; i++ ) {
		int planenum = AssetCheck_ReadLittleLong( &faces[i].planenum );
		int firstedge = AssetCheck_ReadLittleLong( &faces[i].firstedge );
		int faceEdges = AssetCheck_ReadLittleLong( &faces[i].numedges );
		int frontarea = AssetCheck_ReadLittleLong( &faces[i].frontarea );
		int backarea = AssetCheck_ReadLittleLong( &faces[i].backarea );
		int j;

		if ( planenum < 0 || planenum >= aas->counts[AASLUMP_PLANES] ) {
			return AssetCheck_AASFail( "face %i references invalid plane %i", i, planenum );
		}
		if ( frontarea < 0 || frontarea >= aas->counts[AASLUMP_AREAS] ||
			 backarea < 0 || backarea >= aas->counts[AASLUMP_AREAS] ) {
			return AssetCheck_AASFail( "face %i references an invalid adjacent area", i );
		}
		if ( !AssetCheck_AASRangeAt( firstedge, faceEdges, aas->counts[AASLUMP_EDGEINDEX], "face edges", i ) ) {
			return qfalse;
		}

		for ( j = 0 ; j < faceEdges ; j++ ) {
			int edgeNum = AssetCheck_ReadLittleLong( &edgeindex[firstedge + j] );

			if ( edgeNum == ASSETCHECK_INT_MIN ) {
				return AssetCheck_AASFail( "face %i edge index %i references an invalid edge", i, firstedge + j );
			}
			if ( edgeNum < 0 ) {
				edgeNum = -edgeNum;
			}
			if ( edgeNum <= 0 || edgeNum >= aas->counts[AASLUMP_EDGES] ) {
				return AssetCheck_AASFail( "face %i edge index %i references invalid edge %i",
						i, firstedge + j, edgeNum );
			}
		}
	}

	for ( i = 1 ; i < aas->counts[AASLUMP_AREAS] ; i++ ) {
		int firstface = AssetCheck_ReadLittleLong( &areas[i].firstface );
		int areaFaces = AssetCheck_ReadLittleLong( &areas[i].numfaces );
		int j;

		if ( !AssetCheck_AASRangeAt( firstface, areaFaces, aas->counts[AASLUMP_FACEINDEX], "area faces", i ) ) {
			return qfalse;
		}

		for ( j = 0 ; j < areaFaces ; j++ ) {
			int signedFaceNum = AssetCheck_ReadLittleLong( &faceindex[firstface + j] );
			int faceNum;

			if ( signedFaceNum == ASSETCHECK_INT_MIN ) {
				return AssetCheck_AASFail( "area %i face index %i references an invalid face", i, firstface + j );
			}
			faceNum = signedFaceNum;
			if ( faceNum < 0 ) {
				faceNum = -faceNum;
			}
			if ( faceNum <= 0 || faceNum >= aas->counts[AASLUMP_FACES] ) {
				return AssetCheck_AASFail( "area %i face index %i references invalid face %i",
						i, firstface + j, faceNum );
			}
			if ( signedFaceNum < 0 ) {
				int planenum = AssetCheck_ReadLittleLong( &faces[faceNum].planenum );
				int reversedPlanenum = planenum ^ 1;

				if ( reversedPlanenum < 0 || reversedPlanenum >= aas->counts[AASLUMP_PLANES] ) {
					return AssetCheck_AASFail( "area %i face index %i references a face with no back plane",
							i, firstface + j );
				}
			}
		}
	}

	return qtrue;
}

/* AAS_Optimize appends into buffers sized from the original lumps. */
static qboolean AssetCheck_AASValidateOptimizer( const assetCheckAas_t *aas ) {
	const aas_edge_t *edges;
	const aas_edgeindex_t *edgeindex;
	const aas_face_t *faces;
	const aas_faceindex_t *faceindex;
	const aas_area_t *areas;
	byte *optimizedFaces;
	byte *optimizedEdges;
	byte *optimizedVertices;
	long long optimizerFaceIndexes;
	long long optimizerEdgeIndexes;
	long long optimizerVertexes;
	qboolean valid;
	int i;

	edges = (const aas_edge_t *)AssetCheck_AASLumpData( aas, AASLUMP_EDGES );
	edgeindex = (const aas_edgeindex_t *)AssetCheck_AASLumpData( aas, AASLUMP_EDGEINDEX );
	faces = (const aas_face_t *)AssetCheck_AASLumpData( aas, AASLUMP_FACES );
	faceindex = (const aas_faceindex_t *)AssetCheck_AASLumpData( aas, AASLUMP_FACEINDEX );
	areas = (const aas_area_t *)AssetCheck_AASLumpData( aas, AASLUMP_AREAS );

	if ( !AssetCheck_AASSafeArrayAlloc( aas->counts[AASLUMP_FACES], sizeof( *optimizedFaces ),
				"optimizer face table" ) ||
		 !AssetCheck_AASSafeArrayAlloc( aas->counts[AASLUMP_EDGES], sizeof( *optimizedEdges ),
				"optimizer edge table" ) ||
		 !AssetCheck_AASSafeArrayAlloc( aas->counts[AASLUMP_VERTEXES], sizeof( *optimizedVertices ),
				"optimizer vertex table" ) ) {
		return qfalse;
	}

	optimizedFaces = aas->counts[AASLUMP_FACES] ?
			(byte *)Z_Malloc( aas->counts[AASLUMP_FACES] * sizeof( *optimizedFaces ) ) : NULL;
	optimizedEdges = aas->counts[AASLUMP_EDGES] ?
			(byte *)Z_Malloc( aas->counts[AASLUMP_EDGES] * sizeof( *optimizedEdges ) ) : NULL;
	optimizedVertices = aas->counts[AASLUMP_VERTEXES] ?
			(byte *)Z_Malloc( aas->counts[AASLUMP_VERTEXES] * sizeof( *optimizedVertices ) ) : NULL;

	optimizerFaceIndexes = 0;
	optimizerEdgeIndexes = 0;
	optimizerVertexes = 0;
	valid = qtrue;

	for ( i = 1 ; i < aas->counts[AASLUMP_AREAS] ; i++ ) {
		int firstface = AssetCheck_ReadLittleLong( &areas[i].firstface );
		int areaFaces = AssetCheck_ReadLittleLong( &areas[i].numfaces );
		int j;

		for ( j = 0 ; j < areaFaces ; j++ ) {
			int signedFaceNum = AssetCheck_ReadLittleLong( &faceindex[firstface + j] );
			int faceNum = signedFaceNum < 0 ? -signedFaceNum : signedFaceNum;
			int firstedge;
			int faceEdges;
			int k;

			if ( !( AssetCheck_ReadLittleLong( &faces[faceNum].faceflags ) & FACE_LADDER ) ) {
				continue;
			}

			if ( ++optimizerFaceIndexes > aas->counts[AASLUMP_FACEINDEX] ) {
				valid = AssetCheck_AASFail( "optimizer face indexes would exceed loaded face indexes" );
				goto done;
			}

			if ( optimizedFaces[faceNum] ) {
				continue;
			}
			optimizedFaces[faceNum] = 1;

			firstedge = AssetCheck_ReadLittleLong( &faces[faceNum].firstedge );
			faceEdges = AssetCheck_ReadLittleLong( &faces[faceNum].numedges );
			for ( k = 0 ; k < faceEdges ; k++ ) {
				int signedEdgeNum = AssetCheck_ReadLittleLong( &edgeindex[firstedge + k] );
				int edgeNum = signedEdgeNum < 0 ? -signedEdgeNum : signedEdgeNum;
				int n;

				if ( ++optimizerEdgeIndexes > aas->counts[AASLUMP_EDGEINDEX] ) {
					valid = AssetCheck_AASFail( "optimizer edge indexes would exceed loaded edge indexes" );
					goto done;
				}

				if ( optimizedEdges[edgeNum] ) {
					continue;
				}
				optimizedEdges[edgeNum] = 1;

				for ( n = 0 ; n < 2 ; n++ ) {
					int vertex = AssetCheck_ReadLittleLong( &edges[edgeNum].v[n] );

					if ( optimizedVertices[vertex] ) {
						continue;
					}
					if ( optimizerVertexes >= aas->counts[AASLUMP_VERTEXES] ) {
						valid = AssetCheck_AASFail( "optimizer vertices would exceed loaded vertices" );
						goto done;
					}
					if ( optimizerVertexes != 0 ) {
						optimizedVertices[vertex] = 1;
					}
					optimizerVertexes++;
				}
			}
		}
	}

done:
	if ( optimizedVertices ) {
		Z_Free( optimizedVertices );
	}
	if ( optimizedEdges ) {
		Z_Free( optimizedEdges );
	}
	if ( optimizedFaces ) {
		Z_Free( optimizedFaces );
	}
	return valid;
}

static qboolean AssetCheck_AASValidateNodes( const assetCheckAas_t *aas ) {
	const aas_node_t *nodes;
	byte *visited;
	int *stack;
	qboolean valid;
	int stackDepth;

	nodes = (const aas_node_t *)AssetCheck_AASLumpData( aas, AASLUMP_NODES );
	if ( !AssetCheck_AASSafeArrayAlloc( aas->counts[AASLUMP_NODES], sizeof( *visited ),
				"node reachability table" ) ||
		 !AssetCheck_AASSafeArrayAlloc( aas->counts[AASLUMP_NODES], sizeof( *stack ),
				"node work list" ) ) {
		return qfalse;
	}

	visited = (byte *)Z_Malloc( aas->counts[AASLUMP_NODES] * sizeof( *visited ) );
	stack = (int *)Z_Malloc( aas->counts[AASLUMP_NODES] * sizeof( *stack ) );
	Com_Memset( visited, 0, aas->counts[AASLUMP_NODES] * sizeof( *visited ) );

	valid = qtrue;
	stackDepth = 1;
	stack[0] = 1;
	visited[1] = 1;

	while ( stackDepth > 0 ) {
		int nodeNum = stack[--stackDepth];
		int planenum = AssetCheck_ReadLittleLong( &nodes[nodeNum].planenum );
		int j;

		if ( planenum < 0 || planenum >= aas->counts[AASLUMP_PLANES] ) {
			valid = AssetCheck_AASFail( "node %i references invalid plane %i", nodeNum, planenum );
			goto done;
		}

		for ( j = 0 ; j < 2 ; j++ ) {
			int child = AssetCheck_ReadLittleLong( &nodes[nodeNum].children[j] );

			if ( child > 0 ) {
				if ( child >= aas->counts[AASLUMP_NODES] ) {
					valid = AssetCheck_AASFail( "node %i child %i references invalid node %i", nodeNum, j, child );
					goto done;
				}
				if ( !visited[child] ) {
					visited[child] = 1;
					stack[stackDepth++] = child;
				}
			} else if ( child < 0 ) {
				int areanum;

				if ( child == ASSETCHECK_INT_MIN ) {
					valid = AssetCheck_AASFail( "node %i child %i references an invalid area", nodeNum, j );
					goto done;
				}
				areanum = -child;
				if ( areanum <= 0 || areanum >= aas->counts[AASLUMP_AREAS] ) {
					valid = AssetCheck_AASFail( "node %i child %i references invalid area %i", nodeNum, j, areanum );
					goto done;
				}
			}
		}
	}

done:
	Z_Free( stack );
	Z_Free( visited );
	return valid;
}

static qboolean AssetCheck_AASValidateReachability( const assetCheckAas_t *aas ) {
	const aas_reachability_t *reachability;
	const aas_areasettings_t *settings;
	int *incoming;
	long long reverseLinks;
	long long reverseAlloc;
	long long travelAlloc;
	qboolean valid;
	int numareas;
	int reachabilitysize;
	int i;

	numareas = aas->counts[AASLUMP_AREAS];
	reachabilitysize = aas->counts[AASLUMP_REACHABILITY];
	reachability = (const aas_reachability_t *)AssetCheck_AASLumpData( aas, AASLUMP_REACHABILITY );
	settings = (const aas_areasettings_t *)AssetCheck_AASLumpData( aas, AASLUMP_AREASETTINGS );

	if ( !AssetCheck_AASSafeArrayAlloc( numareas, sizeof( int ), "reversed reachability count table" ) ) {
		return qfalse;
	}
	incoming = (int *)Z_Malloc( numareas * sizeof( *incoming ) );
	Com_Memset( incoming, 0, numareas * sizeof( *incoming ) );

	valid = qtrue;
	reverseLinks = 0;

	for ( i = 0 ; i < numareas ; i++ ) {
		int first = AssetCheck_ReadLittleLong( &settings[i].firstreachablearea );
		int count = AssetCheck_ReadLittleLong( &settings[i].numreachableareas );
		int reversedCount;
		int j;

		if ( !AssetCheck_AASRangeAt( first, count, reachabilitysize, "area reachability", i ) ) {
			valid = qfalse;
			goto done;
		}

		for ( j = 0 ; j < count ; j++ ) {
			int areanum = AssetCheck_ReadLittleLong( &reachability[first + j].areanum );

			if ( areanum < 0 || areanum >= numareas ) {
				valid = AssetCheck_AASFail( "reachability %i references invalid area %i",
						first + j, areanum );
				goto done;
			}
		}

		if ( i == 0 ) {
			continue;
		}

		reversedCount = count;
		if ( reversedCount > ASSETCHECK_AAS_MAX_REACHABILITY_LINKS ) {
			reversedCount = ASSETCHECK_AAS_MAX_REACHABILITY_LINKS;
		}
		if ( reverseLinks > reachabilitysize - reversedCount ) {
			valid = AssetCheck_AASFail( "area reachability lists exceed reversed link storage" );
			goto done;
		}
		reverseLinks += reversedCount;

		for ( j = 0 ; j < reversedCount ; j++ ) {
			int areanum = AssetCheck_ReadLittleLong( &reachability[first + j].areanum );
			incoming[areanum]++;
		}
	}

	reverseAlloc = 0;
	if ( !AssetCheck_AASAddAllocBytes( &reverseAlloc, numareas, ASSETCHECK_AAS_REVERSED_REACH_BYTES,
				"reversed reachability table" ) ||
		 !AssetCheck_AASAddAllocBytes( &reverseAlloc, reachabilitysize, ASSETCHECK_AAS_REVERSED_LINK_BYTES,
				"reversed reachability links" ) ) {
		valid = qfalse;
		goto done;
	}

	travelAlloc = 0;
	if ( !AssetCheck_AASAddAllocBytes( &travelAlloc, numareas, ASSETCHECK_POINTER_BYTES,
				"area travel time table" ) ) {
		valid = qfalse;
		goto done;
	}
	for ( i = 0 ; i < numareas ; i++ ) {
		int count = AssetCheck_ReadLittleLong( &settings[i].numreachableareas );
		long long paddedIncoming = AssetCheck_AASPadLong( incoming[i] );

		if ( !AssetCheck_AASAddAllocBytes( &travelAlloc, count, ASSETCHECK_POINTER_BYTES,
					"area travel time entries" ) ||
			 !AssetCheck_AASAddAllocBytes( &travelAlloc, (long long)count * paddedIncoming,
					sizeof( unsigned short ), "area travel time values" ) ) {
			valid = qfalse;
			goto done;
		}
	}

done:
	Z_Free( incoming );
	return valid;
}

static qboolean AssetCheck_AASValidateClusters( const assetCheckAas_t *aas ) {
	const aas_cluster_t *clusters;
	const aas_portal_t *portals;
	const aas_portalindex_t *portalindex;
	const aas_areasettings_t *settings;
	long long clusterCacheAlloc;
	int maxReachabilityAreas;
	int i;

	clusters = (const aas_cluster_t *)AssetCheck_AASLumpData( aas, AASLUMP_CLUSTERS );
	portals = (const aas_portal_t *)AssetCheck_AASLumpData( aas, AASLUMP_PORTALS );
	portalindex = (const aas_portalindex_t *)AssetCheck_AASLumpData( aas, AASLUMP_PORTALINDEX );
	settings = (const aas_areasettings_t *)AssetCheck_AASLumpData( aas, AASLUMP_AREASETTINGS );

	clusterCacheAlloc = 0;
	maxReachabilityAreas = 0;
	if ( !AssetCheck_AASAddAllocBytes( &clusterCacheAlloc, aas->counts[AASLUMP_CLUSTERS],
				ASSETCHECK_POINTER_BYTES, "cluster area cache table" ) ) {
		return qfalse;
	}

	for ( i = 0 ; i < aas->counts[AASLUMP_CLUSTERS] ; i++ ) {
		int numareas = AssetCheck_ReadLittleLong( &clusters[i].numareas );
		int numreachabilityareas = AssetCheck_ReadLittleLong( &clusters[i].numreachabilityareas );
		int numportals = AssetCheck_ReadLittleLong( &clusters[i].numportals );
		int firstportal = AssetCheck_ReadLittleLong( &clusters[i].firstportal );
		int j;

		if ( numareas < 0 || numreachabilityareas < 0 || numportals < 0 ) {
			return AssetCheck_AASFail( "cluster %i has negative counts", i );
		}
		if ( !AssetCheck_AASRangeAt( firstportal, numportals, aas->counts[AASLUMP_PORTALINDEX],
					"cluster portals", i ) ) {
			return qfalse;
		}
		if ( numreachabilityareas > maxReachabilityAreas ) {
			maxReachabilityAreas = numreachabilityareas;
		}
		if ( !AssetCheck_AASAddAllocBytes( &clusterCacheAlloc, numareas, ASSETCHECK_POINTER_BYTES,
					"cluster area cache entries" ) ) {
			return qfalse;
		}

		for ( j = 0 ; j < numportals ; j++ ) {
			int portalnum = AssetCheck_ReadLittleLong( &portalindex[firstportal + j] );
			int frontcluster;
			int backcluster;

			if ( portalnum <= 0 || portalnum >= aas->counts[AASLUMP_PORTALS] ) {
				return AssetCheck_AASFail( "cluster %i portal index %i references invalid portal %i",
						i, firstportal + j, portalnum );
			}
			frontcluster = AssetCheck_ReadLittleLong( &portals[portalnum].frontcluster );
			backcluster = AssetCheck_ReadLittleLong( &portals[portalnum].backcluster );
			if ( frontcluster != i && backcluster != i ) {
				return AssetCheck_AASFail( "cluster %i portal index %i references portal %i from another cluster",
						i, firstportal + j, portalnum );
			}
		}
	}

	for ( i = 0 ; i < aas->counts[AASLUMP_PORTALS] ; i++ ) {
		int areanum = AssetCheck_ReadLittleLong( &portals[i].areanum );
		int frontcluster = AssetCheck_ReadLittleLong( &portals[i].frontcluster );
		int backcluster = AssetCheck_ReadLittleLong( &portals[i].backcluster );
		int frontarea = AssetCheck_ReadLittleLong( &portals[i].clusterareanum[0] );
		int backarea = AssetCheck_ReadLittleLong( &portals[i].clusterareanum[1] );

		if ( areanum < 0 || areanum >= aas->counts[AASLUMP_AREAS] ) {
			return AssetCheck_AASFail( "portal %i references invalid area %i", i, areanum );
		}
		if ( i == 0 ) {
			if ( areanum != 0 || frontcluster != 0 || backcluster != 0 || frontarea != 0 || backarea != 0 ) {
				return AssetCheck_AASFail( "dummy portal has non-zero fields" );
			}
			continue;
		}
		if ( areanum <= 0 ) {
			return AssetCheck_AASFail( "portal %i references invalid area %i", i, areanum );
		}
		if ( frontcluster <= 0 || frontcluster >= aas->counts[AASLUMP_CLUSTERS] ||
			 backcluster <= 0 || backcluster >= aas->counts[AASLUMP_CLUSTERS] ) {
			return AssetCheck_AASFail( "portal %i references an invalid cluster", i );
		}
		if ( AssetCheck_ReadLittleLong( &settings[areanum].cluster ) != -i ) {
			return AssetCheck_AASFail( "portal %i area %i does not link back to the portal", i, areanum );
		}
		if ( frontcluster > 0 &&
			 ( frontarea < 0 || frontarea >= AssetCheck_ReadLittleLong( &clusters[frontcluster].numareas ) ) ) {
			return AssetCheck_AASFail( "portal %i front cluster area is out of range", i );
		}
		if ( backcluster > 0 &&
			 ( backarea < 0 || backarea >= AssetCheck_ReadLittleLong( &clusters[backcluster].numareas ) ) ) {
			return AssetCheck_AASFail( "portal %i back cluster area is out of range", i );
		}
	}

	for ( i = 0 ; i < aas->counts[AASLUMP_AREAS] ; i++ ) {
		int cluster = AssetCheck_ReadLittleLong( &settings[i].cluster );
		int clusterareanum = AssetCheck_ReadLittleLong( &settings[i].clusterareanum );
		int numreachableareas = AssetCheck_ReadLittleLong( &settings[i].numreachableareas );

		if ( cluster > 0 ) {
			if ( cluster >= aas->counts[AASLUMP_CLUSTERS] ) {
				return AssetCheck_AASFail( "area %i references invalid cluster %i", i, cluster );
			}
			if ( clusterareanum < 0 ||
				 clusterareanum >= AssetCheck_ReadLittleLong( &clusters[cluster].numareas ) ) {
				return AssetCheck_AASFail( "area %i cluster area number is out of range", i );
			}
		} else if ( cluster < 0 ) {
			int portalnum;

			if ( cluster == ASSETCHECK_INT_MIN ) {
				return AssetCheck_AASFail( "area %i references invalid portal cluster", i );
			}
			portalnum = -cluster;
			if ( portalnum <= 0 || portalnum >= aas->counts[AASLUMP_PORTALS] ) {
				return AssetCheck_AASFail( "area %i references invalid portal %i", i, portalnum );
			}
			if ( AssetCheck_ReadLittleLong( &portals[portalnum].areanum ) != i ) {
				return AssetCheck_AASFail( "area %i references portal %i owned by another area", i, portalnum );
			}
		} else if ( numreachableareas > 0 ) {
			return AssetCheck_AASFail( "area %i has reachabilities but no cluster or portal", i );
		}
	}

	if ( !AssetCheck_AASSafeArrayAlloc( aas->counts[AASLUMP_PORTALS], sizeof( int ),
				"portal travel time table" ) ||
		 !AssetCheck_AASSafeArrayAlloc( maxReachabilityAreas, ASSETCHECK_AAS_ROUTING_UPDATE_BYTES,
				"area routing update table" ) ||
		 !AssetCheck_AASSafeArrayAlloc( (long long)aas->counts[AASLUMP_PORTALS] + 1,
				ASSETCHECK_AAS_ROUTING_UPDATE_BYTES, "portal routing update table" ) ||
		 !AssetCheck_AASSafeAllocSize( ASSETCHECK_AAS_ROUTING_CACHE_BASE_BYTES +
				(long long)MAX( maxReachabilityAreas, aas->counts[AASLUMP_PORTALS] ) *
				( sizeof( unsigned short ) + sizeof( unsigned char ) ), "routing cache" ) ) {
		return qfalse;
	}

	return qtrue;
}

/*
=================
AssetCheck_VerifyAAS

Return qtrue if valid, qfalse otherwise.
=================
*/
static qboolean AssetCheck_VerifyAAS( byte *data, unsigned int size ) {
	assetCheckAas_t aas;

	if ( AssetCheck_AASKnownBypass( data, size ) ) {
		return qtrue;
	}

	if ( !AssetCheck_AASValidateHeader( &aas, data, size ) ) {
		return qfalse;
	}
	AssetCheck_AASCountLumps( &aas );

	if ( !AssetCheck_AASValidateLoaderAllocations( &aas ) ||
		 !AssetCheck_AASValidateBasicCounts( &aas ) ||
		 !AssetCheck_AASValidateGeometry( &aas ) ||
		 !AssetCheck_AASValidateOptimizer( &aas ) ||
		 !AssetCheck_AASValidateNodes( &aas ) ||
		 !AssetCheck_AASValidateReachability( &aas ) ||
		 !AssetCheck_AASValidateClusters( &aas ) ) {
		return qfalse;
	}

	return qtrue;
}

/*
==========================================================================================
MD3 Verification
==========================================================================================
*/

#define ASSETCHECK_MD3_MDV_FRAME_BYTES 64
#define ASSETCHECK_MD3_MDV_TAG_BYTES 64
#define ASSETCHECK_MD3_MDV_TAGNAME_BYTES MAX_QPATH
#define ASSETCHECK_MD3_MDV_SURFACE_BYTES 128
#define ASSETCHECK_MD3_MDV_VERTEX_BYTES 32
#define ASSETCHECK_MD3_VEC3_BYTES 12
#define ASSETCHECK_MD3_INDEX_BYTES 4
#define ASSETCHECK_MD3_SURFACE_NAME_OFFSET ( sizeof( int ) )

typedef struct {
	int			offset;
	long long	length;
	const char	*name;
} assetCheckMd3Range_t;

static Q_PRINTF_FUNC(1, 2) qboolean AssetCheck_MD3Fail( const char *fmt, ... ) {
	va_list argptr;
	qboolean result;

	va_start( argptr, fmt );
	result = AssetCheck_VFail( "AssetCheck_VerifyMD3", fmt, argptr );
	va_end( argptr );
	return result;
}

static qboolean AssetCheck_MD3SafeAllocSize( long long size, const char *what, int surfaceNum ) {
	if ( size < 0 || size > ASSETCHECK_MAX_ALLOC_SIZE ) {
		if ( surfaceNum >= 0 ) {
			return AssetCheck_MD3Fail( "surface %i %s allocation would be too large", surfaceNum, what );
		}
		return AssetCheck_MD3Fail( "%s allocation would be too large", what );
	}

	return qtrue;
}

static qboolean AssetCheck_MD3SafeArrayAlloc( long long count, long long elemSize, const char *what, int surfaceNum ) {
	if ( count < 0 || elemSize <= 0 || count > ASSETCHECK_INT_MAX / elemSize ) {
		if ( surfaceNum >= 0 ) {
			return AssetCheck_MD3Fail( "surface %i %s allocation has invalid dimensions", surfaceNum, what );
		}
		return AssetCheck_MD3Fail( "%s allocation has invalid dimensions", what );
	}

	return AssetCheck_MD3SafeAllocSize( count * elemSize, what, surfaceNum );
}

static qboolean AssetCheck_MD3RangeWithin( int offset, long long length, int limit, const char *what, int surfaceNum ) {
	if ( offset < 0 || length < 0 || offset > limit || length > (long long)limit - offset ) {
		if ( surfaceNum >= 0 ) {
			return AssetCheck_MD3Fail( "surface %i %s range points outside its surface", surfaceNum, what );
		}
		return AssetCheck_MD3Fail( "%s range points outside the file", what );
	}

	return qtrue;
}

static qboolean AssetCheck_MD3RangesOverlap( int aOffset, long long aLength, int bOffset, long long bLength ) {
	if ( aLength <= 0 || bLength <= 0 ) {
		return qfalse;
	}

	return (long long)aOffset < (long long)bOffset + bLength &&
		   (long long)bOffset < (long long)aOffset + aLength;
}

static qboolean AssetCheck_MD3ChunkRange( int surfaceNum, int surfaceSize, const char *what, int offset, long long length ) {
	if ( length == 0 ) {
		return qtrue;
	}

	if ( offset < (int)sizeof( md3Surface_t ) ) {
		return AssetCheck_MD3Fail( "surface %i %s range overlaps the surface header", surfaceNum, what );
	}

	return AssetCheck_MD3RangeWithin( offset, length, surfaceSize, what, surfaceNum );
}

static qboolean AssetCheck_MD3AddChunkRange( assetCheckMd3Range_t *ranges, int *numRanges,
											 int surfaceNum, int surfaceSize, const char *what,
											 int offset, long long length ) {
	int i;

	if ( !AssetCheck_MD3ChunkRange( surfaceNum, surfaceSize, what, offset, length ) ) {
		return qfalse;
	}
	if ( length == 0 ) {
		return qtrue;
	}

	for ( i = 0; i < *numRanges; i++ ) {
		if ( AssetCheck_MD3RangesOverlap( offset, length, ranges[i].offset, ranges[i].length ) ) {
			return AssetCheck_MD3Fail( "surface %i %s range overlaps %s range", surfaceNum, what, ranges[i].name );
		}
	}

	ranges[*numRanges].offset = offset;
	ranges[*numRanges].length = length;
	ranges[*numRanges].name = what;
	++*numRanges;
	return qtrue;
}

static qboolean AssetCheck_MD3HasNul( const byte *data, unsigned int size ) {
	unsigned int i;

	for ( i = 0; i < size; i++ ) {
		if ( data[i] == '\0' ) {
			return qtrue;
		}
	}

	return qfalse;
}

static qboolean AssetCheck_MD3ValidateTriangleIndexes( const byte *data, int surfaceOffset,
													   int trianglesOffset, int numTriangles,
													   int numVerts, int surfaceNum ) {
	int i;

	for ( i = 0; i < numTriangles; i++ ) {
		const md3Triangle_t *triangle = (const md3Triangle_t *)( data + surfaceOffset + trianglesOffset +
																 i * (int)sizeof( md3Triangle_t ) );
		int j;

		for ( j = 0; j < 3; j++ ) {
			int index = AssetCheck_ReadLittleLong( &triangle->indexes[j] );
			if ( index < 0 || index >= numVerts ) {
				return AssetCheck_MD3Fail( "surface %i triangle %i references invalid vertex %i", surfaceNum, i, index );
			}
		}
	}

	return qtrue;
}

static qboolean AssetCheck_MD3ValidateTagNames( const byte *data, int ofsTags, int numFrames, int numTags ) {
	int frame;
	int tagNum;

	for ( frame = 0; frame < numFrames; frame++ ) {
		for ( tagNum = 0; tagNum < numTags; tagNum++ ) {
			long long tagIndex = (long long)frame * numTags + tagNum;
			const md3Tag_t *tag = (const md3Tag_t *)( data + ofsTags + tagIndex * (long long)sizeof( md3Tag_t ) );

			if ( !AssetCheck_MD3HasNul( (const byte *)tag->name, MAX_QPATH ) ) {
				return AssetCheck_MD3Fail( "frame %i tag %i name is not NUL-terminated", frame, tagNum );
			}
		}
	}

	return qtrue;
}

static qboolean AssetCheck_MD3ValidateShaderNames( const byte *data, int surfaceOffset,
												  int ofsShaders, int numShaders, int surfaceNum ) {
	int i;

	for ( i = 0; i < numShaders; i++ ) {
		const md3Shader_t *shader = (const md3Shader_t *)( data + surfaceOffset + ofsShaders +
														   i * (int)sizeof( md3Shader_t ) );

		if ( !AssetCheck_MD3HasNul( (const byte *)shader->name, MAX_QPATH ) ) {
			return AssetCheck_MD3Fail( "surface %i shader %i name is not NUL-terminated", surfaceNum, i );
		}
	}

	return qtrue;
}

/*
 * Some legacy exporters leave a final empty surface with ofsEnd == 0 and
 * ofsShaders pointing into the surface header.  GL1 rewrites shaderIndex
 * in-place, so allow that only while the writes stay out of control fields.
 */
static qboolean AssetCheck_MD3TerminalShaderWritesSafe( int surfaceNum, int ofsShaders, int numShaders ) {
	int i;

	for ( i = 0; i < numShaders; i++ ) {
		long long shaderIndexOffset = (long long)ofsShaders + (long long)i * sizeof( md3Shader_t ) +
									  offsetof( md3Shader_t, shaderIndex );

		if ( shaderIndexOffset >= (long long)sizeof( md3Surface_t ) ) {
			break;
		}
		if ( shaderIndexOffset + (long long)sizeof( int ) > (long long)offsetof( md3Surface_t, flags ) ) {
			return AssetCheck_MD3Fail( "surface %i terminal empty shader index overlaps surface control fields", surfaceNum );
		}
	}

	return qtrue;
}

static qboolean AssetCheck_VerifyMD3( byte *data, unsigned int size ) {
	const md3Header_t *header;
	int ident;
	int version;
	int numFrames;
	int numTags;
	int numSurfaces;
	int ofsFrames;
	int ofsTags;
	int ofsSurfaces;
	int ofsEnd;
	long long frameBytes;
	long long tagCount;
	long long tagBytes;
	int surfaceOffset;
	int i;

	if ( !data ) {
		return AssetCheck_MD3Fail( "missing file data" );
	}
	if ( size > ASSETCHECK_INT_MAX ) {
		return AssetCheck_MD3Fail( "file is too large" );
	}
	if ( size < sizeof( md3Header_t ) ) {
		return AssetCheck_MD3Fail( "file is smaller than an MD3 header" );
	}

	header = (const md3Header_t *)data;
	ident = AssetCheck_ReadLittleLong( &header->ident );
	version = AssetCheck_ReadLittleLong( &header->version );
	numFrames = AssetCheck_ReadLittleLong( &header->numFrames );
	numTags = AssetCheck_ReadLittleLong( &header->numTags );
	numSurfaces = AssetCheck_ReadLittleLong( &header->numSurfaces );
	ofsFrames = AssetCheck_ReadLittleLong( &header->ofsFrames );
	ofsTags = AssetCheck_ReadLittleLong( &header->ofsTags );
	ofsSurfaces = AssetCheck_ReadLittleLong( &header->ofsSurfaces );
	ofsEnd = AssetCheck_ReadLittleLong( &header->ofsEnd );

	if ( ident != MD3_IDENT ) {
		return AssetCheck_MD3Fail( "file has wrong ident" );
	}
	if ( version != MD3_VERSION ) {
		return AssetCheck_MD3Fail( "file has wrong version" );
	}
	if ( ofsEnd < (int)sizeof( md3Header_t ) || ofsEnd > (int)size ) {
		return AssetCheck_MD3Fail( "file end offset points outside the file" );
	}
	if ( !AssetCheck_MD3SafeAllocSize( ofsEnd, "MD3 copy", -1 ) ) {
		return qfalse;
	}
	if ( numFrames < 1 || numTags < 0 || numSurfaces < 0 ) {
		return AssetCheck_MD3Fail( "header counts are out of range" );
	}
	if ( numTags > 0 && numFrames > ASSETCHECK_MAX_ALLOC_SIZE / (int)sizeof( md3Tag_t ) / numTags ) {
		return AssetCheck_MD3Fail( "tag data range would overflow" );
	}

	frameBytes = (long long)numFrames * sizeof( md3Frame_t );
	tagCount = (long long)numFrames * numTags;
	tagBytes = tagCount * sizeof( md3Tag_t );

	if ( !AssetCheck_MD3RangeWithin( ofsFrames, frameBytes, ofsEnd, "frames", -1 ) ||
		 ( tagBytes > 0 && !AssetCheck_MD3RangeWithin( ofsTags, tagBytes, ofsEnd, "tags", -1 ) ) ||
		 ( numSurfaces > 0 && !AssetCheck_MD3RangeWithin( ofsSurfaces, sizeof( md3Surface_t ), ofsEnd, "surfaces", -1 ) ) ) {
		return qfalse;
	}
	if ( ofsFrames < (int)sizeof( md3Header_t ) ||
		 ( tagBytes > 0 && ofsTags < (int)sizeof( md3Header_t ) ) ||
		 ( numSurfaces > 0 && ofsSurfaces < (int)sizeof( md3Header_t ) ) ) {
		return AssetCheck_MD3Fail( "model data overlaps the MD3 header" );
	}
	if ( tagBytes > 0 && AssetCheck_MD3RangesOverlap( ofsFrames, frameBytes, ofsTags, tagBytes ) ) {
		return AssetCheck_MD3Fail( "frame and tag ranges overlap" );
	}
	if ( tagBytes > 0 && !AssetCheck_MD3ValidateTagNames( data, ofsTags, numFrames, numTags ) ) {
		return qfalse;
	}
	if ( !AssetCheck_MD3SafeArrayAlloc( numFrames, ASSETCHECK_MD3_MDV_FRAME_BYTES, "frame table", -1 ) ||
		 !AssetCheck_MD3SafeArrayAlloc( tagCount, ASSETCHECK_MD3_MDV_TAG_BYTES, "tag table", -1 ) ||
		 !AssetCheck_MD3SafeArrayAlloc( numTags, ASSETCHECK_MD3_MDV_TAGNAME_BYTES, "tag name table", -1 ) ||
		 !AssetCheck_MD3SafeArrayAlloc( numSurfaces, ASSETCHECK_MD3_MDV_SURFACE_BYTES, "surface table", -1 ) ) {
		return qfalse;
	}

	surfaceOffset = ofsSurfaces;
	for ( i = 0; i < numSurfaces; i++ ) {
		const md3Surface_t *surface;
		assetCheckMd3Range_t ranges[4];
		int numRanges = 0;
		int surfaceEnd;
		int surfaceFrames;
		int numShaders;
		int numVerts;
		int numTriangles;
		int ofsTriangles;
		int ofsShaders;
		int ofsSt;
		int ofsXyzNormals;
		qboolean emptySurface;
		qboolean terminalEmptySurface;
		long long shaderBytes;
		long long triangleBytes;
		long long stBytes;
		long long vertexCount;
		long long xyzNormalBytes;

		if ( !AssetCheck_MD3RangeWithin( surfaceOffset, sizeof( md3Surface_t ), ofsEnd, "surface header", i ) ) {
			return qfalse;
		}
		if ( AssetCheck_MD3RangesOverlap( surfaceOffset, sizeof( md3Surface_t ), ofsFrames, frameBytes ) ||
			 AssetCheck_MD3RangesOverlap( surfaceOffset, sizeof( md3Surface_t ), ofsTags, tagBytes ) ) {
			return AssetCheck_MD3Fail( "surface %i header overlaps model frame or tag data", i );
		}

		surface = (const md3Surface_t *)( data + surfaceOffset );
		surfaceFrames = AssetCheck_ReadLittleLong( &surface->numFrames );
		numShaders = AssetCheck_ReadLittleLong( &surface->numShaders );
		numVerts = AssetCheck_ReadLittleLong( &surface->numVerts );
		numTriangles = AssetCheck_ReadLittleLong( &surface->numTriangles );
		ofsTriangles = AssetCheck_ReadLittleLong( &surface->ofsTriangles );
		ofsShaders = AssetCheck_ReadLittleLong( &surface->ofsShaders );
		ofsSt = AssetCheck_ReadLittleLong( &surface->ofsSt );
		ofsXyzNormals = AssetCheck_ReadLittleLong( &surface->ofsXyzNormals );
		surfaceEnd = AssetCheck_ReadLittleLong( &surface->ofsEnd );

		if ( surfaceFrames < 0 || numShaders < 0 || numVerts < 0 || numTriangles < 0 ) {
			return AssetCheck_MD3Fail( "surface %i counts are out of range", i );
		}
		emptySurface = ( numVerts == 0 && numTriangles == 0 );
		terminalEmptySurface = ( emptySurface && i == numSurfaces - 1 && surfaceEnd == 0 );
		if ( numVerts > 0 && surfaceFrames < numFrames ) {
			return AssetCheck_MD3Fail( "surface %i has too few frames", i );
		}
		if ( numVerts >= SHADER_MAX_VERTEXES ) {
			return AssetCheck_MD3Fail( "surface %i has too many vertices", i );
		}
		if ( numTriangles > ( SHADER_MAX_INDEXES - 1 ) / 3 ) {
			return AssetCheck_MD3Fail( "surface %i has too many triangles", i );
		}
		if ( !AssetCheck_MD3HasNul( data + surfaceOffset + ASSETCHECK_MD3_SURFACE_NAME_OFFSET, MAX_QPATH ) ) {
			return AssetCheck_MD3Fail( "surface %i name is not NUL-terminated", i );
		}
		if ( terminalEmptySurface ) {
			shaderBytes = (long long)numShaders * sizeof( md3Shader_t );
			if ( !AssetCheck_MD3SafeArrayAlloc( numShaders, ASSETCHECK_MD3_INDEX_BYTES, "shader index table", i ) ) {
				return qfalse;
			}
			if ( shaderBytes > 0 ) {
				int shaderOffset;

				if ( !AssetCheck_MD3RangeWithin( ofsShaders, shaderBytes, ofsEnd - surfaceOffset, "shader", i ) ) {
					return qfalse;
				}
				shaderOffset = surfaceOffset + ofsShaders;
				if ( AssetCheck_MD3RangesOverlap( shaderOffset, shaderBytes, ofsFrames, frameBytes ) ||
					 AssetCheck_MD3RangesOverlap( shaderOffset, shaderBytes, ofsTags, tagBytes ) ) {
					return AssetCheck_MD3Fail( "surface %i terminal shader range overlaps model frame or tag data", i );
				}
				if ( !AssetCheck_MD3ValidateShaderNames( data, surfaceOffset, ofsShaders, numShaders, i ) ) {
					return qfalse;
				}
			}
			if ( !AssetCheck_MD3TerminalShaderWritesSafe( i, ofsShaders, numShaders ) ) {
				return qfalse;
			}
			continue;
		}
		if ( surfaceEnd < (int)sizeof( md3Surface_t ) ) {
			return AssetCheck_MD3Fail( "surface %i end offset is out of range", i );
		}
		if ( !AssetCheck_MD3RangeWithin( surfaceOffset, surfaceEnd, ofsEnd, "surface", i ) ) {
			return qfalse;
		}
		if ( AssetCheck_MD3RangesOverlap( surfaceOffset, surfaceEnd, ofsFrames, frameBytes ) ||
			 AssetCheck_MD3RangesOverlap( surfaceOffset, surfaceEnd, ofsTags, tagBytes ) ) {
			return AssetCheck_MD3Fail( "surface %i overlaps model frame or tag data", i );
		}

		vertexCount = (long long)numVerts * surfaceFrames;
		shaderBytes = (long long)numShaders * sizeof( md3Shader_t );
		triangleBytes = (long long)numTriangles * sizeof( md3Triangle_t );
		stBytes = (long long)numVerts * sizeof( md3St_t );
		xyzNormalBytes = vertexCount * sizeof( md3XyzNormal_t );

		if ( !AssetCheck_MD3AddChunkRange( ranges, &numRanges, i, surfaceEnd, "shader", ofsShaders, shaderBytes ) ||
			 !AssetCheck_MD3AddChunkRange( ranges, &numRanges, i, surfaceEnd, "triangle", ofsTriangles, triangleBytes ) ||
			 !AssetCheck_MD3AddChunkRange( ranges, &numRanges, i, surfaceEnd, "st", ofsSt, stBytes ) ||
			 !AssetCheck_MD3AddChunkRange( ranges, &numRanges, i, surfaceEnd, "vertex", ofsXyzNormals, xyzNormalBytes ) ) {
			return qfalse;
		}
		if ( !AssetCheck_MD3SafeArrayAlloc( numShaders, ASSETCHECK_MD3_INDEX_BYTES, "shader index table", i ) ||
			 !AssetCheck_MD3SafeArrayAlloc( (long long)numTriangles * 3, ASSETCHECK_MD3_INDEX_BYTES, "index table", i ) ||
			 !AssetCheck_MD3SafeArrayAlloc( vertexCount, ASSETCHECK_MD3_MDV_VERTEX_BYTES, "vertex table", i ) ||
			 !AssetCheck_MD3SafeArrayAlloc( numVerts, sizeof( md3St_t ), "st table", i ) ||
			 !AssetCheck_MD3SafeArrayAlloc( (long long)numVerts * numFrames, ASSETCHECK_MD3_VEC3_BYTES, "tangent table", i ) ) {
			return qfalse;
		}
		if ( !AssetCheck_MD3ValidateShaderNames( data, surfaceOffset, ofsShaders, numShaders, i ) ) {
			return qfalse;
		}
		if ( !AssetCheck_MD3ValidateTriangleIndexes( data, surfaceOffset, ofsTriangles, numTriangles, numVerts, i ) ) {
			return qfalse;
		}

		surfaceOffset += surfaceEnd;
	}

	return qtrue;
}

/*
==========================================================================================
MDR Verification
==========================================================================================
*/

#define ASSETCHECK_MDR_FRAME_NAME_BYTES ( (long long)sizeof( ( (mdrFrame_t *)0 )->name ) )
#define ASSETCHECK_MDR_EXPANDED_BONE_BYTES ( (long long)sizeof( mdrBone_t ) - (long long)sizeof( mdrCompBone_t ) )

static Q_PRINTF_FUNC(1, 2) qboolean AssetCheck_MDRFail( const char *fmt, ... ) {
	va_list argptr;
	qboolean result;

	va_start( argptr, fmt );
	result = AssetCheck_VFail( "AssetCheck_VerifyMDR", fmt, argptr );
	va_end( argptr );
	return result;
}

static qboolean AssetCheck_MDRSafeAllocSize( long long size, const char *what ) {
	if ( size < 0 || size > ASSETCHECK_MAX_ALLOC_SIZE ) {
		return AssetCheck_MDRFail( "%s allocation would be too large", what );
	}

	return qtrue;
}

static qboolean AssetCheck_MDRRangeWithin( long long offset, long long length, long long limit, const char *what ) {
	if ( offset < 0 || length < 0 || offset > limit || length > limit - offset ) {
		return AssetCheck_MDRFail( "%s range points outside the file", what );
	}

	return qtrue;
}

static qboolean AssetCheck_MDRSubRangeWithin( long long offset, long long length, long long limit,
											  const char *what, int lodNum, int surfaceNum ) {
	if ( offset < 0 || length < 0 || offset > limit || length > limit - offset ) {
		if ( surfaceNum >= 0 ) {
			return AssetCheck_MDRFail( "LOD %i surface %i %s range points outside its surface", lodNum, surfaceNum, what );
		}
		if ( lodNum >= 0 ) {
			return AssetCheck_MDRFail( "LOD %i %s range points outside its LOD", lodNum, what );
		}
		return AssetCheck_MDRFail( "%s range points outside its parent", what );
	}

	return qtrue;
}

static qboolean AssetCheck_MDRAddOutputBytes( long long *offset, long long length, long long limit, const char *what ) {
	if ( length < 0 || *offset < 0 || *offset > limit || length > limit - *offset ) {
		return AssetCheck_MDRFail( "expanded %s exceeds allocated model size", what );
	}

	*offset += length;
	return qtrue;
}

static long long AssetCheck_MDRFrameBytes( int numBones ) {
	return (long long)offsetof( mdrFrame_t, bones ) + (long long)numBones * (long long)sizeof( mdrBone_t );
}

static long long AssetCheck_MDRCompFrameBytes( int numBones ) {
	return (long long)offsetof( mdrCompFrame_t, bones ) + (long long)numBones * (long long)sizeof( mdrCompBone_t );
}

static long long AssetCheck_MDRVertexBytes( int numWeights ) {
	return (long long)offsetof( mdrVertex_t, weights ) + (long long)numWeights * (long long)sizeof( mdrWeight_t );
}

static qboolean AssetCheck_MDRValidateTriangleIndexes( const byte *data, int surfaceOffset, int ofsTriangles,
													   int numTriangles, int numVerts, int lodNum, int surfaceNum ) {
	int i;

	for ( i = 0; i < numTriangles; i++ ) {
		const mdrTriangle_t *triangle = (const mdrTriangle_t *)( data + surfaceOffset + ofsTriangles +
																 i * (int)sizeof( mdrTriangle_t ) );
		int j;

		for ( j = 0; j < 3; j++ ) {
			int index = AssetCheck_ReadLittleLong( &triangle->indexes[j] );
			if ( index < 0 || index >= numVerts ) {
				return AssetCheck_MDRFail( "LOD %i surface %i triangle %i references invalid vertex %i",
										   lodNum, surfaceNum, i, index );
			}
		}
	}

	return qtrue;
}

static qboolean AssetCheck_MDRValidateVertices( const byte *data, int surfaceOffset, long long surfaceLimit,
												int ofsVerts, int numVerts, int numBones,
												int lodNum, int surfaceNum, long long *vertexBytes ) {
	long long vertexOffset;
	int i;

	*vertexBytes = 0;
	if ( numVerts == 0 ) {
		return qtrue;
	}
	if ( !AssetCheck_MDRSubRangeWithin( ofsVerts, offsetof( mdrVertex_t, weights ), surfaceLimit,
										"vertex", lodNum, surfaceNum ) ) {
		return qfalse;
	}

	vertexOffset = ofsVerts;
	for ( i = 0; i < numVerts; i++ ) {
		const mdrVertex_t *vertex;
		long long currentVertexBytes;
		int numWeights;
		int j;

		if ( !AssetCheck_MDRSubRangeWithin( vertexOffset, offsetof( mdrVertex_t, weights ), surfaceLimit,
											"vertex", lodNum, surfaceNum ) ) {
			return qfalse;
		}

		vertex = (const mdrVertex_t *)( data + surfaceOffset + vertexOffset );
		numWeights = AssetCheck_ReadLittleLong( &vertex->numWeights );
		if ( numWeights < 0 ) {
			return AssetCheck_MDRFail( "LOD %i surface %i vertex %i has invalid weight count %i",
									   lodNum, surfaceNum, i, numWeights );
		}

		currentVertexBytes = AssetCheck_MDRVertexBytes( numWeights );
		if ( !AssetCheck_MDRSubRangeWithin( vertexOffset, currentVertexBytes, surfaceLimit,
											"vertex", lodNum, surfaceNum ) ) {
			return qfalse;
		}

		for ( j = 0; j < numWeights; j++ ) {
			const mdrWeight_t *weight = (const mdrWeight_t *)( data + surfaceOffset + vertexOffset +
															   offsetof( mdrVertex_t, weights ) +
															   (long long)j * sizeof( mdrWeight_t ) );
			int boneIndex = AssetCheck_ReadLittleLong( &weight->boneIndex );

			if ( boneIndex < 0 || boneIndex >= numBones ) {
				return AssetCheck_MDRFail( "LOD %i surface %i vertex %i weight %i references invalid bone %i",
										   lodNum, surfaceNum, i, j, boneIndex );
			}
		}

		vertexOffset += currentVertexBytes;
		*vertexBytes += currentVertexBytes;
	}

	return qtrue;
}

static qboolean AssetCheck_MDRValidateTags( const byte *data, int ofsTags, int numTags, int numBones, int fileEnd ) {
	long long tagBytes;
	int i;

	if ( numTags == 0 ) {
		return qtrue;
	}

	tagBytes = (long long)numTags * sizeof( mdrTag_t );
	if ( !AssetCheck_MDRRangeWithin( ofsTags, tagBytes, fileEnd, "tags" ) ) {
		return qfalse;
	}

	for ( i = 0; i < numTags; i++ ) {
		const mdrTag_t *tag = (const mdrTag_t *)( data + ofsTags + i * (int)sizeof( mdrTag_t ) );
		int boneIndex = AssetCheck_ReadLittleLong( &tag->boneIndex );

		if ( boneIndex < 0 || boneIndex >= numBones ) {
			return AssetCheck_MDRFail( "tag %i references invalid bone %i", i, boneIndex );
		}
	}

	return qtrue;
}

static qboolean AssetCheck_VerifyMDR( byte *data, unsigned int size ) {
	const mdrHeader_t *header;
	int ident;
	int version;
	int numFrames;
	int numBones;
	int ofsFrames;
	int numLODs;
	int ofsLODs;
	int numTags;
	int ofsTags;
	int ofsEnd;
	qboolean compressedFrames;
	long long frameOffset;
	long long frameBytes;
	long long inputFrameBytes;
	long long totalInputFrameBytes;
	long long tagBytes;
	long long allocSize;
	long long outOffset;
	long long lodOffset;
	int l;

	if ( !data ) {
		return AssetCheck_MDRFail( "missing file data" );
	}
	if ( size > ASSETCHECK_INT_MAX ) {
		return AssetCheck_MDRFail( "file is too large" );
	}
	if ( size < sizeof( mdrHeader_t ) ) {
		return AssetCheck_MDRFail( "file is smaller than an MDR header" );
	}

	header = (const mdrHeader_t *)data;
	ident = AssetCheck_ReadLittleLong( &header->ident );
	version = AssetCheck_ReadLittleLong( &header->version );
	numFrames = AssetCheck_ReadLittleLong( &header->numFrames );
	numBones = AssetCheck_ReadLittleLong( &header->numBones );
	ofsFrames = AssetCheck_ReadLittleLong( &header->ofsFrames );
	numLODs = AssetCheck_ReadLittleLong( &header->numLODs );
	ofsLODs = AssetCheck_ReadLittleLong( &header->ofsLODs );
	numTags = AssetCheck_ReadLittleLong( &header->numTags );
	ofsTags = AssetCheck_ReadLittleLong( &header->ofsTags );
	ofsEnd = AssetCheck_ReadLittleLong( &header->ofsEnd );

	if ( ident != MDR_IDENT ) {
		return AssetCheck_MDRFail( "file has wrong ident" );
	}
	if ( version != MDR_VERSION ) {
		return AssetCheck_MDRFail( "file has wrong version" );
	}
	if ( ofsEnd < (int)sizeof( mdrHeader_t ) || ofsEnd > (int)size ) {
		return AssetCheck_MDRFail( "file end offset points outside the file" );
	}
	if ( numFrames < 1 || numBones < 0 || numBones > MDR_MAX_BONES || numLODs < 0 || numTags < 0 ) {
		return AssetCheck_MDRFail( "header counts are out of range" );
	}

	compressedFrames = ( ofsFrames < 0 );
	if ( compressedFrames && ofsFrames == ASSETCHECK_INT_MIN ) {
		return AssetCheck_MDRFail( "compressed frame offset is out of range" );
	}

	frameBytes = AssetCheck_MDRFrameBytes( numBones );
	inputFrameBytes = compressedFrames ? AssetCheck_MDRCompFrameBytes( numBones ) : frameBytes;
	frameOffset = compressedFrames ? -(long long)ofsFrames : ofsFrames;
	totalInputFrameBytes = (long long)numFrames * inputFrameBytes;
	tagBytes = (long long)numTags * sizeof( mdrTag_t );
	allocSize = ofsEnd;
	if ( compressedFrames ) {
		allocSize += (long long)numFrames * ASSETCHECK_MDR_FRAME_NAME_BYTES;
		allocSize += (long long)numFrames * numBones * ASSETCHECK_MDR_EXPANDED_BONE_BYTES;
	}
	if ( !AssetCheck_MDRSafeAllocSize( allocSize, "MDR copy" ) ) {
		return qfalse;
	}

	if ( !AssetCheck_MDRRangeWithin( frameOffset, totalInputFrameBytes, ofsEnd, "frames" ) ) {
		return qfalse;
	}

	if ( numTags > 0 ) {
		if ( !AssetCheck_MDRRangeWithin( ofsTags, tagBytes, ofsEnd, "tags" ) ) {
			return qfalse;
		}
	}

	outOffset = sizeof( mdrHeader_t );
	if ( !AssetCheck_MDRAddOutputBytes( &outOffset, (long long)numFrames * frameBytes, allocSize, "frame data" ) ) {
		return qfalse;
	}

	if ( numLODs > 0 && !AssetCheck_MDRRangeWithin( ofsLODs, sizeof( mdrLOD_t ), ofsEnd, "LODs" ) ) {
		return qfalse;
	}

	lodOffset = ofsLODs;
	for ( l = 0; l < numLODs; l++ ) {
		const mdrLOD_t *lod;
		long long surfaceOffset;
		long long nextLodOffset;
		int numSurfaces;
		int ofsSurfaces;
		int lodEnd;
		int i;

		if ( !AssetCheck_MDRRangeWithin( lodOffset, sizeof( mdrLOD_t ), ofsEnd, "LOD header" ) ) {
			return qfalse;
		}

		lod = (const mdrLOD_t *)( data + (int)lodOffset );
		numSurfaces = AssetCheck_ReadLittleLong( &lod->numSurfaces );
		ofsSurfaces = AssetCheck_ReadLittleLong( &lod->ofsSurfaces );
		lodEnd = AssetCheck_ReadLittleLong( &lod->ofsEnd );

		if ( numSurfaces < 0 ) {
			return AssetCheck_MDRFail( "LOD %i has invalid surface count %i", l, numSurfaces );
		}

		nextLodOffset = lodOffset + lodEnd;
		if ( l < numLODs - 1 ) {
			if ( !AssetCheck_MDRRangeWithin( nextLodOffset, sizeof( mdrLOD_t ), ofsEnd, "next LOD header" ) ) {
				return qfalse;
			}
		}
		if ( !AssetCheck_MDRAddOutputBytes( &outOffset, sizeof( mdrLOD_t ), allocSize, "LOD data" ) ) {
			return qfalse;
		}

		surfaceOffset = lodOffset + ofsSurfaces;
		if ( numSurfaces > 0 && !AssetCheck_MDRRangeWithin( surfaceOffset, sizeof( mdrSurface_t ),
															 ofsEnd, "surface header" ) ) {
			return qfalse;
		}
		for ( i = 0; i < numSurfaces; i++ ) {
			const mdrSurface_t *surface;
			long long surfaceLimit;
			long long nextSurfaceOffset;
			long long vertexBytes;
			long long triangleBytes;
			int numVerts;
			int numTriangles;
			int ofsVerts;
			int ofsTriangles;
			int surfaceEnd;

			if ( !AssetCheck_MDRRangeWithin( surfaceOffset, sizeof( mdrSurface_t ), ofsEnd, "surface header" ) ) {
				return qfalse;
			}

			surface = (const mdrSurface_t *)( data + (int)surfaceOffset );
			numVerts = AssetCheck_ReadLittleLong( &surface->numVerts );
			ofsVerts = AssetCheck_ReadLittleLong( &surface->ofsVerts );
			numTriangles = AssetCheck_ReadLittleLong( &surface->numTriangles );
			ofsTriangles = AssetCheck_ReadLittleLong( &surface->ofsTriangles );
			surfaceEnd = AssetCheck_ReadLittleLong( &surface->ofsEnd );

			if ( numVerts < 0 || numTriangles < 0 ) {
				return AssetCheck_MDRFail( "LOD %i surface %i counts are out of range", l, i );
			}
			if ( numVerts >= SHADER_MAX_VERTEXES ) {
				return AssetCheck_MDRFail( "LOD %i surface %i has too many vertices", l, i );
			}
			if ( numTriangles > ( SHADER_MAX_INDEXES - 1 ) / 3 ) {
				return AssetCheck_MDRFail( "LOD %i surface %i has too many triangles", l, i );
			}

			surfaceLimit = (long long)ofsEnd - surfaceOffset;
			nextSurfaceOffset = surfaceOffset + surfaceEnd;
			if ( i < numSurfaces - 1 &&
				 !AssetCheck_MDRRangeWithin( nextSurfaceOffset, sizeof( mdrSurface_t ), ofsEnd, "next surface header" ) ) {
				return qfalse;
			}

			if ( !AssetCheck_MDRValidateVertices( data, (int)surfaceOffset, surfaceLimit, ofsVerts, numVerts,
												  numBones, l, i, &vertexBytes ) ) {
				return qfalse;
			}

			triangleBytes = (long long)numTriangles * sizeof( mdrTriangle_t );
			if ( numTriangles > 0 ) {
				if ( !AssetCheck_MDRSubRangeWithin( ofsTriangles, triangleBytes, surfaceLimit,
													"triangle", l, i ) ||
					 !AssetCheck_MDRValidateTriangleIndexes( data, (int)surfaceOffset, ofsTriangles,
															 numTriangles, numVerts, l, i ) ) {
					return qfalse;
				}
			}

			if ( !AssetCheck_MDRAddOutputBytes( &outOffset, sizeof( mdrSurface_t ), allocSize, "surface data" ) ||
				 !AssetCheck_MDRAddOutputBytes( &outOffset, vertexBytes, allocSize, "vertex data" ) ||
				 !AssetCheck_MDRAddOutputBytes( &outOffset, triangleBytes, allocSize, "triangle data" ) ) {
				return qfalse;
			}

			surfaceOffset = nextSurfaceOffset;
		}

		lodOffset = nextLodOffset;
	}

	if ( !AssetCheck_MDRValidateTags( data, ofsTags, numTags, numBones, ofsEnd ) ) {
		return qfalse;
	}
	if ( !AssetCheck_MDRAddOutputBytes( &outOffset, (long long)numTags * sizeof( mdrTag_t ),
										allocSize, "tag data" ) ) {
		return qfalse;
	}

	return qtrue;
}

/*
==========================================================================================
IQM Verification
==========================================================================================
*/

#define ASSETCHECK_IQM_MAX_FILE_SIZE ( 16u << 20 )
#define ASSETCHECK_IQM_CHANNEL_MASK 0x3ffu
/*
 * The verifier lives outside the renderer, so use conservative upper bounds for
 * renderer-private structures when checking expanded allocation size.
 */
#define ASSETCHECK_IQM_DATA_BYTES 256
#define ASSETCHECK_IQM_SURFACE_BYTES 256
#define ASSETCHECK_IQM_VAO_SURFACE_BYTES 128
#define ASSETCHECK_IQM_TRANSFORM_BYTES ( 10 * (int)sizeof( float ) )

static Q_PRINTF_FUNC(1, 2) qboolean AssetCheck_IQMFail( const char *fmt, ... ) {
	va_list argptr;
	qboolean result;

	va_start( argptr, fmt );
	result = AssetCheck_VFail( "AssetCheck_VerifyIQM", fmt, argptr );
	va_end( argptr );
	return result;
}

static qboolean AssetCheck_IQMRangeWithin( unsigned int offset, long long length,
										   unsigned int limit, const char *what ) {
	if ( length < 0 || (long long)offset > (long long)limit ||
		 length > (long long)limit - (long long)offset ) {
		return AssetCheck_IQMFail( "%s range points outside the file", what );
	}

	return qtrue;
}

static qboolean AssetCheck_IQMArrayRangeWithin( unsigned int offset, unsigned int count,
												unsigned int elemSize, unsigned int limit,
												const char *what ) {
	long long length;

	if ( count == 0 ) {
		return qtrue;
	}
	if ( offset == 0 ) {
		return AssetCheck_IQMFail( "%s array offset is out of range", what );
	}

	length = (long long)count * (long long)elemSize;
	return AssetCheck_IQMRangeWithin( offset, length, limit, what );
}

static qboolean AssetCheck_IQMRangeAt( unsigned int first, unsigned int count,
									   unsigned int limit, const char *what, int index ) {
	if ( first > limit || count > limit - first ) {
		return AssetCheck_IQMFail( "%s %i range %u + %u exceeds limit %u",
								   what, index, first, count, limit );
	}

	return qtrue;
}

static qboolean AssetCheck_IQMAddAllocBytes( long long *total, long long count,
											 long long elemSize, const char *what ) {
	long long bytes;

	if ( count < 0 || elemSize <= 0 ) {
		return AssetCheck_IQMFail( "%s allocation has invalid dimensions", what );
	}
	if ( count > ASSETCHECK_MAX_ALLOC_SIZE / elemSize ) {
		return AssetCheck_IQMFail( "%s allocation would be too large", what );
	}

	bytes = count * elemSize;
	if ( *total < 0 || *total > ASSETCHECK_MAX_ALLOC_SIZE ||
		 bytes > ASSETCHECK_MAX_ALLOC_SIZE - *total ) {
		return AssetCheck_IQMFail( "%s allocation would be too large", what );
	}

	*total += bytes;
	return qtrue;
}

static int AssetCheck_IQMFormatBytes( unsigned int format ) {
	switch ( format ) {
		case IQM_BYTE:
		case IQM_UBYTE:
			return 1;

		case IQM_INT:
		case IQM_UINT:
		case IQM_FLOAT:
			return 4;
	}

	return 0;
}

static int AssetCheck_IQMPopCount10( unsigned int value ) {
	int count = 0;

	value &= ASSETCHECK_IQM_CHANNEL_MASK;
	while ( value ) {
		count += value & 1u;
		value >>= 1;
	}

	return count;
}

static qboolean AssetCheck_IQMStringOffset( const byte *data, unsigned int ofsText,
											unsigned int numText, unsigned int offset,
											const char *what, int index,
											unsigned int *length ) {
	const char *string;
	const char *nul;
	unsigned int remaining;

	if ( offset >= numText ) {
		if ( index >= 0 ) {
			return AssetCheck_IQMFail( "%s %i string offset points outside the text block", what, index );
		}
		return AssetCheck_IQMFail( "%s string offset points outside the text block", what );
	}

	string = (const char *)data + ofsText + offset;
	remaining = numText - offset;
	nul = (const char *)memchr( string, '\0', remaining );
	if ( !nul ) {
		if ( index >= 0 ) {
			return AssetCheck_IQMFail( "%s %i string is not NUL-terminated in the text block", what, index );
		}
		return AssetCheck_IQMFail( "%s string is not NUL-terminated in the text block", what );
	}

	if ( length ) {
		*length = (unsigned int)( nul - string );
	}
	return qtrue;
}

static qboolean AssetCheck_IQMBlendWeightUsed( const byte *data, unsigned int offset,
											   unsigned int format, unsigned int vertex,
											   int component ) {
	long long valueIndex = (long long)vertex * 4 + component;

	if ( format == IQM_FLOAT ) {
		float weight = AssetCheck_ReadLittleFloat( data + offset + valueIndex * (int)sizeof( float ) );

		return !( weight <= 0.0f );
	}

	return data[offset + (unsigned int)valueIndex] > 0;
}

static qboolean AssetCheck_IQMInfluencesEqual( const byte *data, unsigned int blendIndexesOffset,
											   unsigned int blendWeightsOffset,
											   unsigned int blendWeightsFormat,
											   unsigned int a, unsigned int b ) {
	int i;

	if ( memcmp( data + blendIndexesOffset + a * 4,
				 data + blendIndexesOffset + b * 4, 4 ) ) {
		return qfalse;
	}

	if ( blendWeightsFormat == IQM_FLOAT ) {
		for ( i = 0; i < 4; i++ ) {
			float aw = AssetCheck_ReadLittleFloat( data + blendWeightsOffset +
												  ( (long long)a * 4 + i ) * (int)sizeof( float ) );
			float bw = AssetCheck_ReadLittleFloat( data + blendWeightsOffset +
												  ( (long long)b * 4 + i ) * (int)sizeof( float ) );

			if ( aw != bw ) {
				return qfalse;
			}
		}
		return qtrue;
	}

	return memcmp( data + blendWeightsOffset + a * 4,
				   data + blendWeightsOffset + b * 4, 4 ) == 0;
}

static qboolean AssetCheck_IQMValidateMeshInfluences( const byte *data,
													  unsigned int blendIndexesOffset,
													  unsigned int blendWeightsOffset,
													  unsigned int blendWeightsFormat,
													  unsigned int firstVertex,
													  unsigned int numVertexes,
													  unsigned int numJoints,
													  qboolean validateIndexes,
													  long long *allocateInfluences ) {
	unsigned int j;

	for ( j = 0; j < numVertexes; j++ ) {
		unsigned int vtx = firstVertex + j;
		unsigned int k;
		int component;

		if ( validateIndexes ) {
			for ( component = 0; component < 4; component++ ) {
				if ( AssetCheck_IQMBlendWeightUsed( data, blendWeightsOffset, blendWeightsFormat, vtx, component ) ) {
					unsigned int index = data[blendIndexesOffset + vtx * 4 + component];

					if ( index >= numJoints ) {
						return AssetCheck_IQMFail( "blend index %u references missing joint %u", index, numJoints );
					}
				}
			}
		}

		for ( k = 0; k < j; k++ ) {
			if ( AssetCheck_IQMInfluencesEqual( data, blendIndexesOffset, blendWeightsOffset,
												blendWeightsFormat, firstVertex + k, vtx ) ) {
				break;
			}
		}

		if ( k == j ) {
			++*allocateInfluences;
			if ( *allocateInfluences > ASSETCHECK_INT_MAX ) {
				return AssetCheck_IQMFail( "blend influence count would overflow" );
			}
		}
	}

	return qtrue;
}

static qboolean AssetCheck_VerifyIQM( byte *data, unsigned int size ) {
	const iqmHeader_t *header;
	unsigned int version;
	unsigned int fileSize;
	unsigned int numText, ofsText;
	unsigned int numMeshes, ofsMeshes;
	unsigned int numVertexArrays, numVertexes, ofsVertexArrays;
	unsigned int numTriangles, ofsTriangles;
	unsigned int numJoints, ofsJoints;
	unsigned int numPoses, ofsPoses;
	unsigned int numFrames, ofsFrames, ofsBounds;
	int vertexArrayFormat[IQM_COLOR + 1];
	unsigned int blendIndexesOffset = 0;
	unsigned int blendWeightsOffset = 0;
	unsigned int blendWeightsFormat = 0;
	qboolean haveBlendIndexes = qfalse;
	qboolean haveBlendWeights = qfalse;
	long long allocateInfluences = 0;
	long long allocSize = ASSETCHECK_IQM_DATA_BYTES;
	long long frameChannels = 0;
	long long jointNames = 0;
	int i;

	if ( !data ) {
		return AssetCheck_IQMFail( "missing file data" );
	}
	if ( size > ASSETCHECK_INT_MAX ) {
		return AssetCheck_IQMFail( "file is too large" );
	}
	if ( size < sizeof( iqmHeader_t ) ) {
		return AssetCheck_IQMFail( "file is smaller than an IQM header" );
	}
	if ( Q_strncmp( (const char *)data, IQM_MAGIC, sizeof( header->magic ) ) ) {
		return AssetCheck_IQMFail( "file has wrong magic" );
	}

	header = (const iqmHeader_t *)data;
	version = AssetCheck_ReadLittleUInt( &header->version );
	fileSize = AssetCheck_ReadLittleUInt( &header->filesize );
	if ( version != IQM_VERSION ) {
		return AssetCheck_IQMFail( "file has wrong version" );
	}
	if ( fileSize < sizeof( iqmHeader_t ) || fileSize > size || fileSize > ASSETCHECK_IQM_MAX_FILE_SIZE ) {
		return AssetCheck_IQMFail( "file size field is out of range" );
	}

	numText = AssetCheck_ReadLittleUInt( &header->num_text );
	ofsText = AssetCheck_ReadLittleUInt( &header->ofs_text );
	numMeshes = AssetCheck_ReadLittleUInt( &header->num_meshes );
	ofsMeshes = AssetCheck_ReadLittleUInt( &header->ofs_meshes );
	numVertexArrays = AssetCheck_ReadLittleUInt( &header->num_vertexarrays );
	numVertexes = AssetCheck_ReadLittleUInt( &header->num_vertexes );
	ofsVertexArrays = AssetCheck_ReadLittleUInt( &header->ofs_vertexarrays );
	numTriangles = AssetCheck_ReadLittleUInt( &header->num_triangles );
	ofsTriangles = AssetCheck_ReadLittleUInt( &header->ofs_triangles );
	numJoints = AssetCheck_ReadLittleUInt( &header->num_joints );
	ofsJoints = AssetCheck_ReadLittleUInt( &header->ofs_joints );
	numPoses = AssetCheck_ReadLittleUInt( &header->num_poses );
	ofsPoses = AssetCheck_ReadLittleUInt( &header->ofs_poses );
	numFrames = AssetCheck_ReadLittleUInt( &header->num_frames );
	ofsFrames = AssetCheck_ReadLittleUInt( &header->ofs_frames );
	ofsBounds = AssetCheck_ReadLittleUInt( &header->ofs_bounds );

	if ( numText > 0 && !AssetCheck_IQMRangeWithin( ofsText, numText, fileSize, "text" ) ) {
		return qfalse;
	}
	if ( numJoints > IQM_MAX_JOINTS ) {
		return AssetCheck_IQMFail( "file has too many joints" );
	}
	if ( numPoses != numJoints && numPoses != 0 ) {
		return AssetCheck_IQMFail( "pose count must match joint count or be zero" );
	}
	if ( numPoses > 0 && numFrames == 0 ) {
		/*
		 * R_LoadIQM would allocate a zero-length pose transform table while
		 * keeping num_poses, and ComputePoseMats would read past it.
		 */
		return AssetCheck_IQMFail( "animated model has no frames" );
	}

	for ( i = 0; i < ARRAY_LEN( vertexArrayFormat ); i++ ) {
		vertexArrayFormat[i] = -1;
	}

	if ( numMeshes > 0 ) {
		const byte *entry;

		if ( !AssetCheck_IQMArrayRangeWithin( ofsVertexArrays, numVertexArrays,
											  sizeof( iqmVertexArray_t ), fileSize, "vertex array" ) ) {
			return qfalse;
		}

		for ( i = 0; i < (int)numVertexArrays; i++ ) {
			unsigned int type;
			unsigned int format;
			unsigned int components;
			unsigned int offset;
			int elemBytes;
			long long values;

			entry = data + ofsVertexArrays + i * (int)sizeof( iqmVertexArray_t );
			type = AssetCheck_ReadLittleUInt( entry + offsetof( iqmVertexArray_t, type ) );
			format = AssetCheck_ReadLittleUInt( entry + offsetof( iqmVertexArray_t, format ) );
			components = AssetCheck_ReadLittleUInt( entry + offsetof( iqmVertexArray_t, size ) );
			offset = AssetCheck_ReadLittleUInt( entry + offsetof( iqmVertexArray_t, offset ) );

			if ( components == 0 || components > 4 ) {
				return AssetCheck_IQMFail( "vertex array %i has invalid component count", i );
			}

			elemBytes = AssetCheck_IQMFormatBytes( format );
			if ( elemBytes == 0 ) {
				return AssetCheck_IQMFail( "vertex array %i has unsupported format", i );
			}

			values = (long long)numVertexes * components;
			if ( values > ASSETCHECK_INT_MAX ) {
				return AssetCheck_IQMFail( "vertex array %i value count is out of range", i );
			}
			if ( !AssetCheck_IQMArrayRangeWithin( offset, (unsigned int)values, elemBytes,
												  fileSize, "vertex array data" ) ) {
				return qfalse;
			}

			if ( type < ARRAY_LEN( vertexArrayFormat ) ) {
				vertexArrayFormat[type] = (int)format;
			}

			switch ( type ) {
				case IQM_POSITION:
				case IQM_NORMAL:
					if ( format != IQM_FLOAT || components != 3 ) {
						return AssetCheck_IQMFail( "vertex array %i has invalid position/normal layout", i );
					}
					break;

				case IQM_TANGENT:
					if ( format != IQM_FLOAT || components != 4 ) {
						return AssetCheck_IQMFail( "vertex array %i has invalid tangent layout", i );
					}
					break;

				case IQM_TEXCOORD:
					if ( format != IQM_FLOAT || components != 2 ) {
						return AssetCheck_IQMFail( "vertex array %i has invalid texcoord layout", i );
					}
					break;

				case IQM_BLENDINDEXES:
					if ( ( format != IQM_INT && format != IQM_UBYTE ) || components != 4 ) {
						return AssetCheck_IQMFail( "vertex array %i has invalid blend index layout", i );
					}
					blendIndexesOffset = offset;
					haveBlendIndexes = qtrue;
					break;

				case IQM_BLENDWEIGHTS:
					if ( ( format != IQM_FLOAT && format != IQM_UBYTE ) || components != 4 ) {
						return AssetCheck_IQMFail( "vertex array %i has invalid blend weight layout", i );
					}
					blendWeightsOffset = offset;
					blendWeightsFormat = format;
					haveBlendWeights = qtrue;
					break;

				case IQM_COLOR:
					if ( format != IQM_UBYTE || components != 4 ) {
						return AssetCheck_IQMFail( "vertex array %i has invalid color layout", i );
					}
					break;
			}
		}

		if ( vertexArrayFormat[IQM_POSITION] == -1 ||
			 vertexArrayFormat[IQM_NORMAL] == -1 ||
			 vertexArrayFormat[IQM_TEXCOORD] == -1 ) {
			return AssetCheck_IQMFail( "file is missing required vertex arrays" );
		}
		if ( numJoints > 0 && ( !haveBlendIndexes || !haveBlendWeights ) ) {
			return AssetCheck_IQMFail( "jointed model is missing blend arrays" );
		}

		if ( !AssetCheck_IQMArrayRangeWithin( ofsTriangles, numTriangles,
											  sizeof( iqmTriangle_t ), fileSize, "triangle" ) ) {
			return qfalse;
		}
		for ( i = 0; i < (int)numTriangles; i++ ) {
			const byte *triangle = data + ofsTriangles + i * (int)sizeof( iqmTriangle_t );
			int j;

			for ( j = 0; j < 3; j++ ) {
				unsigned int vertex = AssetCheck_ReadLittleUInt( triangle + offsetof( iqmTriangle_t, vertex ) +
																 j * (int)sizeof( unsigned int ) );

				if ( vertex >= numVertexes ) {
					return AssetCheck_IQMFail( "triangle %i references invalid vertex %u", i, vertex );
				}
			}
		}

		if ( !AssetCheck_IQMArrayRangeWithin( ofsMeshes, numMeshes, sizeof( iqmMesh_t ),
											  fileSize, "mesh" ) ) {
			return qfalse;
		}
		for ( i = 0; i < (int)numMeshes; i++ ) {
			const byte *mesh = data + ofsMeshes + i * (int)sizeof( iqmMesh_t );
			unsigned int name = AssetCheck_ReadLittleUInt( mesh + offsetof( iqmMesh_t, name ) );
			unsigned int material = AssetCheck_ReadLittleUInt( mesh + offsetof( iqmMesh_t, material ) );
			unsigned int firstVertex = AssetCheck_ReadLittleUInt( mesh + offsetof( iqmMesh_t, first_vertex ) );
			unsigned int meshVertexes = AssetCheck_ReadLittleUInt( mesh + offsetof( iqmMesh_t, num_vertexes ) );
			unsigned int firstTriangle = AssetCheck_ReadLittleUInt( mesh + offsetof( iqmMesh_t, first_triangle ) );
			unsigned int meshTriangles = AssetCheck_ReadLittleUInt( mesh + offsetof( iqmMesh_t, num_triangles ) );
			unsigned int vertexEnd;
			unsigned int j;

			if ( meshVertexes >= SHADER_MAX_VERTEXES ) {
				return AssetCheck_IQMFail( "mesh %i has too many vertices", i );
			}
			if ( meshTriangles > ( SHADER_MAX_INDEXES - 1 ) / 3 ) {
				return AssetCheck_IQMFail( "mesh %i has too many triangles", i );
			}
			if ( !AssetCheck_IQMRangeAt( firstVertex, meshVertexes, numVertexes, "mesh vertex", i ) ||
				 !AssetCheck_IQMRangeAt( firstTriangle, meshTriangles, numTriangles, "mesh triangle", i ) ) {
				return qfalse;
			}
			if ( !AssetCheck_IQMStringOffset( data, ofsText, numText, name, "mesh name", i, NULL ) ||
				 !AssetCheck_IQMStringOffset( data, ofsText, numText, material, "mesh material", i, NULL ) ) {
				return qfalse;
			}

			vertexEnd = firstVertex + meshVertexes;
			for ( j = 0; j < meshTriangles; j++ ) {
				const byte *triangle = data + ofsTriangles +
									   ( firstTriangle + j ) * (int)sizeof( iqmTriangle_t );
				int k;

				for ( k = 0; k < 3; k++ ) {
					unsigned int vertex = AssetCheck_ReadLittleUInt( triangle + offsetof( iqmTriangle_t, vertex ) +
																	 k * (int)sizeof( unsigned int ) );

					if ( vertex < firstVertex || vertex >= vertexEnd ) {
						return AssetCheck_IQMFail( "mesh %i triangle %u references vertex outside the mesh", i, j );
					}
				}
			}

			if ( numJoints > 0 &&
				 !AssetCheck_IQMValidateMeshInfluences( data, blendIndexesOffset, blendWeightsOffset,
														blendWeightsFormat, firstVertex, meshVertexes,
														numJoints, numPoses > 0, &allocateInfluences ) ) {
				return qfalse;
			}
		}
	}

	if ( numJoints > 0 ) {
		/*
		 * R_LoadIQM normalizes joint quaternions in place, then continues to
		 * use fields from the file header as loop bounds.  Do not allow a joint
		 * record to alias and modify that live header after verification.
		 */
		if ( ofsJoints < sizeof( *header ) ) {
			return AssetCheck_IQMFail( "joint array overlaps the mutable IQM header" );
		}
		if ( !AssetCheck_IQMArrayRangeWithin( ofsJoints, numJoints, sizeof( iqmJoint_t ),
											  fileSize, "joint" ) ) {
			return qfalse;
		}

		for ( i = 0; i < (int)numJoints; i++ ) {
			const byte *joint = data + ofsJoints + i * (int)sizeof( iqmJoint_t );
			unsigned int name = AssetCheck_ReadLittleUInt( joint + offsetof( iqmJoint_t, name ) );
			unsigned int parentBits = AssetCheck_ReadLittleUInt( joint + offsetof( iqmJoint_t, parent ) );
			int parent;
			unsigned int nameLength;

			if ( parentBits == 0xffffffffu ) {
				parent = -1;
			} else if ( parentBits > (unsigned int)ASSETCHECK_INT_MAX ) {
				return AssetCheck_IQMFail( "joint %i parent is out of range", i );
			} else {
				parent = (int)parentBits;
			}
			if ( parent < -1 || parent >= (int)numJoints ) {
				return AssetCheck_IQMFail( "joint %i parent is out of range", i );
			}
			if ( !AssetCheck_IQMStringOffset( data, ofsText, numText, name, "joint name", i, &nameLength ) ) {
				return qfalse;
			}
			jointNames += (long long)nameLength + 1;
			if ( jointNames > ASSETCHECK_MAX_ALLOC_SIZE ) {
				return AssetCheck_IQMFail( "joint names allocation would be too large" );
			}
		}
	}

	if ( numPoses > 0 ) {
		if ( !AssetCheck_IQMArrayRangeWithin( ofsPoses, numPoses, sizeof( iqmPose_t ),
											  fileSize, "pose" ) ) {
			return qfalse;
		}
		/*
		 * Joint normalization also writes before pose masks are used to
		 * size frame data reads.  numPoses > 0 implies numJoints == numPoses.
		 */
		if ( (long long)ofsJoints < (long long)ofsPoses + (long long)numPoses * (long long)sizeof( iqmPose_t ) &&
			 (long long)ofsPoses < (long long)ofsJoints + (long long)numJoints * (long long)sizeof( iqmJoint_t ) ) {
			return AssetCheck_IQMFail( "joint array overlaps the pose array" );
		}

		for ( i = 0; i < (int)numPoses; i++ ) {
			const byte *pose = data + ofsPoses + i * (int)sizeof( iqmPose_t );
			unsigned int mask = AssetCheck_ReadLittleUInt( pose + offsetof( iqmPose_t, mask ) );

			frameChannels += AssetCheck_IQMPopCount10( mask );
		}
		if ( frameChannels > 0 ) {
			long long frameDataBytes = (long long)numFrames * frameChannels * (long long)sizeof( unsigned short );

			if ( !AssetCheck_IQMRangeWithin( ofsFrames, frameDataBytes, fileSize, "frame data" ) ) {
				return qfalse;
			}
		}
	}

	if ( ofsBounds ) {
		if ( !AssetCheck_IQMArrayRangeWithin( ofsBounds, numFrames, sizeof( iqmBounds_t ),
											  fileSize, "bounds" ) ) {
			return qfalse;
		}
	}

	if ( numMeshes > 0 ) {
		if ( !AssetCheck_IQMAddAllocBytes( &allocSize, numMeshes, ASSETCHECK_IQM_SURFACE_BYTES, "surface table" ) ||
			 !AssetCheck_IQMAddAllocBytes( &allocSize, (long long)numTriangles * 3, sizeof( int ), "triangle table" ) ||
			 !AssetCheck_IQMAddAllocBytes( &allocSize, (long long)numVertexes * 3, sizeof( float ), "positions" ) ||
			 !AssetCheck_IQMAddAllocBytes( &allocSize, (long long)numVertexes * 2, sizeof( float ), "texcoords" ) ||
			 !AssetCheck_IQMAddAllocBytes( &allocSize, (long long)numVertexes * 3, sizeof( float ), "normals" ) ) {
			return qfalse;
		}
		if ( vertexArrayFormat[IQM_TANGENT] != -1 &&
			 !AssetCheck_IQMAddAllocBytes( &allocSize, (long long)numVertexes * 4, sizeof( float ), "tangents" ) ) {
			return qfalse;
		}
		if ( vertexArrayFormat[IQM_COLOR] != -1 &&
			 !AssetCheck_IQMAddAllocBytes( &allocSize, (long long)numVertexes * 4, sizeof( byte ), "colors" ) ) {
			return qfalse;
		}
		if ( allocateInfluences > 0 ) {
			if ( !AssetCheck_IQMAddAllocBytes( &allocSize, numVertexes, sizeof( int ), "vertex influence table" ) ||
				 !AssetCheck_IQMAddAllocBytes( &allocSize, allocateInfluences * 4, sizeof( byte ), "blend index table" ) ) {
				return qfalse;
			}
			if ( blendWeightsFormat == IQM_FLOAT ) {
				if ( !AssetCheck_IQMAddAllocBytes( &allocSize, allocateInfluences * 4,
												   sizeof( float ), "blend weight table" ) ) {
					return qfalse;
				}
			} else if ( !AssetCheck_IQMAddAllocBytes( &allocSize, allocateInfluences * 4,
													  sizeof( byte ), "blend weight table" ) ) {
				return qfalse;
			}
		}
		if ( !AssetCheck_IQMAddAllocBytes( &allocSize, numMeshes,
										   ASSETCHECK_IQM_VAO_SURFACE_BYTES, "VAO surface table" ) ) {
			return qfalse;
		}
	}
	if ( numJoints > 0 ) {
		if ( !AssetCheck_IQMAddAllocBytes( &allocSize, jointNames, sizeof( char ), "joint names" ) ||
			 !AssetCheck_IQMAddAllocBytes( &allocSize, numJoints, sizeof( int ), "joint parents" ) ||
			 !AssetCheck_IQMAddAllocBytes( &allocSize, (long long)numJoints * 12, sizeof( float ), "bind joints" ) ||
			 !AssetCheck_IQMAddAllocBytes( &allocSize, (long long)numJoints * 12, sizeof( float ), "inverse bind joints" ) ) {
			return qfalse;
		}
	}
	if ( numPoses > 0 &&
		 !AssetCheck_IQMAddAllocBytes( &allocSize, (long long)numPoses * numFrames,
									   ASSETCHECK_IQM_TRANSFORM_BYTES, "pose transforms" ) ) {
		return qfalse;
	}
	if ( ofsBounds ) {
		if ( !AssetCheck_IQMAddAllocBytes( &allocSize, (long long)numFrames * 6,
										   sizeof( float ), "bounds" ) ) {
			return qfalse;
		}
	} else if ( numMeshes > 0 && numFrames == 0 &&
				!AssetCheck_IQMAddAllocBytes( &allocSize, 6, sizeof( float ), "static bounds" ) ) {
		return qfalse;
	}

	return qtrue;
}

/*
==========================================================================================
DDS Verification
==========================================================================================
*/

static Q_PRINTF_FUNC(1, 2) qboolean AssetCheck_DDSFail( const char *fmt, ... ) {
	va_list argptr;
	qboolean result;

	va_start( argptr, fmt );
	result = AssetCheck_VFail( "AssetCheck_VerifyDDS", fmt, argptr );
	va_end( argptr );
	return result;
}

typedef struct {
	unsigned int	headerSize;
	unsigned int	flags;
	unsigned int	height;
	unsigned int	width;
	unsigned int	pitchOrFirstMipSize;
	unsigned int	volumeDepth;
	unsigned int	numMips;
	unsigned int	reserved1[11];
	unsigned int	always_0x00000020;
	unsigned int	pixelFormatFlags;
	unsigned int	fourCC;
	unsigned int	rgbBitCount;
	unsigned int	rBitMask;
	unsigned int	gBitMask;
	unsigned int	bBitMask;
	unsigned int	aBitMask;
	unsigned int	caps;
	unsigned int	caps2;
	unsigned int	caps3;
	unsigned int	caps4;
	unsigned int	reserved2;
} assetCheckDdsHeader_t;

typedef struct {
	unsigned int	dxgiFormat;
	unsigned int	dimensions;
	unsigned int	miscFlags;
	unsigned int	arraySize;
	unsigned int	miscFlags2;
} assetCheckDdsHeaderDxt10_t;

#define ASSETCHECK_DDS_FOURCC( a, b, c, d ) \
	( (unsigned int)(unsigned char)( a ) | ( (unsigned int)(unsigned char)( b ) << 8 ) | \
	  ( (unsigned int)(unsigned char)( c ) << 16 ) | ( (unsigned int)(unsigned char)( d ) << 24 ) )

#define ASSETCHECK_DDS_MAGIC ASSETCHECK_DDS_FOURCC( 'D', 'D', 'S', ' ' )
#define ASSETCHECK_DDS_MIPMAPCOUNT 0x20000u
#define ASSETCHECK_DDSPF_ALPHAPIXELS 0x1u
#define ASSETCHECK_DDSPF_FOURCC 0x4u
#define ASSETCHECK_DDSPF_RGB 0x40u
#define ASSETCHECK_DDSCAPS2_CUBEMAP 0x200u

static unsigned int AssetCheck_DDSReadUInt32( const void *ptr ) {
	const byte *p = (const byte *)ptr;

	return (unsigned int)p[0] |
			( (unsigned int)p[1] << 8 ) |
			( (unsigned int)p[2] << 16 ) |
			( (unsigned int)p[3] << 24 );
}

static qboolean AssetCheck_DDSFormatBytes( const assetCheckDdsHeader_t *header,
		const assetCheckDdsHeaderDxt10_t *headerDxt10, int *blockBytes, int *pixelBytes ) {
	unsigned int pixelFormatFlags;
	unsigned int fourCC;

	*blockBytes = 0;
	*pixelBytes = 0;

	pixelFormatFlags = AssetCheck_DDSReadUInt32( &header->pixelFormatFlags );
	fourCC = AssetCheck_DDSReadUInt32( &header->fourCC );
	if ( headerDxt10 ) {
		unsigned int dxgiFormat = AssetCheck_DDSReadUInt32( &headerDxt10->dxgiFormat );

		switch ( dxgiFormat ) {
			case 70: /* BC1_TYPELESS */
			case 71: /* BC1_UNORM */
			case 72: /* BC1_UNORM_SRGB */
			case 79: /* BC4_TYPELESS */
			case 80: /* BC4_UNORM */
			case 81: /* BC4_SNORM */
				*blockBytes = 8;
				return qtrue;

			case 73: /* BC2_TYPELESS */
			case 74: /* BC2_UNORM */
			case 75: /* BC2_UNORM_SRGB */
			case 76: /* BC3_TYPELESS */
			case 77: /* BC3_UNORM */
			case 78: /* BC3_UNORM_SRGB */
			case 82: /* BC5_TYPELESS */
			case 83: /* BC5_UNORM */
			case 84: /* BC5_SNORM */
			case 94: /* BC6H_TYPELESS */
			case 95: /* BC6H_UF16 */
			case 96: /* BC6H_SF16 */
			case 97: /* BC7_TYPELESS */
			case 98: /* BC7_UNORM */
			case 99: /* BC7_UNORM_SRGB */
				*blockBytes = 16;
				return qtrue;

			case 28: /* R8G8B8A8_UNORM */
			case 29: /* R8G8B8A8_UNORM_SRGB */
			case 31: /* R8G8B8A8_SNORM */
				*pixelBytes = 4;
				return qtrue;

			default:
				return AssetCheck_DDSFail( "unsupported DXGI format %u", dxgiFormat );
		}
	}

	if ( pixelFormatFlags & ASSETCHECK_DDSPF_FOURCC ) {
		switch ( fourCC ) {
			case ASSETCHECK_DDS_FOURCC( 'D', 'X', 'T', '1' ):
			case ASSETCHECK_DDS_FOURCC( 'A', 'T', 'I', '1' ):
			case ASSETCHECK_DDS_FOURCC( 'B', 'C', '4', 'U' ):
			case ASSETCHECK_DDS_FOURCC( 'B', 'C', '4', 'S' ):
				*blockBytes = 8;
				return qtrue;

			case ASSETCHECK_DDS_FOURCC( 'D', 'X', 'T', '2' ):
			case ASSETCHECK_DDS_FOURCC( 'D', 'X', 'T', '3' ):
			case ASSETCHECK_DDS_FOURCC( 'D', 'X', 'T', '4' ):
			case ASSETCHECK_DDS_FOURCC( 'D', 'X', 'T', '5' ):
			case ASSETCHECK_DDS_FOURCC( 'A', 'T', 'I', '2' ):
			case ASSETCHECK_DDS_FOURCC( 'B', 'C', '5', 'U' ):
			case ASSETCHECK_DDS_FOURCC( 'B', 'C', '5', 'S' ):
				*blockBytes = 16;
				return qtrue;

			default:
				return AssetCheck_DDSFail( "unsupported FourCC" );
		}
	}

	if ( pixelFormatFlags == ( ASSETCHECK_DDSPF_RGB | ASSETCHECK_DDSPF_ALPHAPIXELS ) &&
		 AssetCheck_DDSReadUInt32( &header->rgbBitCount ) == 32 &&
		 AssetCheck_DDSReadUInt32( &header->rBitMask ) == 0x000000ffu &&
		 AssetCheck_DDSReadUInt32( &header->gBitMask ) == 0x0000ff00u &&
		 AssetCheck_DDSReadUInt32( &header->bBitMask ) == 0x00ff0000u &&
		 AssetCheck_DDSReadUInt32( &header->aBitMask ) == 0xff000000u ) {
		*pixelBytes = 4;
		return qtrue;
	}

	return AssetCheck_DDSFail( "unsupported RGBA format" );
}

static unsigned int AssetCheck_DDSMaxMipCount( unsigned int width, unsigned int height ) {
	unsigned int mipCount = 1;

	while ( width > 1 || height > 1 ) {
		if ( width > 1 ) {
			width >>= 1;
		}
		if ( height > 1 ) {
			height >>= 1;
		}
		mipCount++;
	}

	return mipCount;
}

static qboolean AssetCheck_DDSMulFitsMax( long long a, long long b, long long *result ) {
	if ( a <= 0 || b <= 0 || a > ASSETCHECK_MAX_ALLOC_SIZE / b ) {
		return qfalse;
	}

	*result = a * b;
	return qtrue;
}

static qboolean AssetCheck_DDSMipChainBytes( unsigned int width, unsigned int height, unsigned int numMips,
		int blockBytes, int pixelBytes, long long *chainBytes ) {
	long long total = 0;
	unsigned int i;

	for ( i = 0 ; i < numMips ; i++ ) {
		long long levelBytes;

		if ( blockBytes ) {
			long long blocksWide = ( (long long)width + 3 ) / 4;
			long long blocksHigh = ( (long long)height + 3 ) / 4;
			long long blocks;

			if ( !AssetCheck_DDSMulFitsMax( blocksWide, blocksHigh, &blocks ) ||
				 !AssetCheck_DDSMulFitsMax( blocks, blockBytes, &levelBytes ) ) {
				return AssetCheck_DDSFail( "mip chain would overflow" );
			}
		} else {
			long long pixels;

			if ( !AssetCheck_DDSMulFitsMax( width, height, &pixels ) ||
				 !AssetCheck_DDSMulFitsMax( pixels, pixelBytes, &levelBytes ) ) {
				return AssetCheck_DDSFail( "mip chain would overflow" );
			}
		}

		if ( total > ASSETCHECK_MAX_ALLOC_SIZE - levelBytes ) {
			return AssetCheck_DDSFail( "mip chain would overflow" );
		}
		total += levelBytes;

		if ( width > 1 ) {
			width >>= 1;
		}
		if ( height > 1 ) {
			height >>= 1;
		}
	}

	*chainBytes = total;
	return qtrue;
}

static qboolean AssetCheck_VerifyDDS( byte *data, unsigned int size ) {
	const assetCheckDdsHeader_t *header;
	const assetCheckDdsHeaderDxt10_t *headerDxt10 = NULL;
	unsigned int dataOffset;
	unsigned int width;
	unsigned int height;
	unsigned int flags;
	unsigned int numMips;
	unsigned int caps2;
	unsigned int faces;
	int blockBytes;
	int pixelBytes;
	long long faceBytes;
	long long requiredBytes;

	if ( !data ) {
		return AssetCheck_DDSFail( "missing file data" );
	}
	if ( size > ASSETCHECK_INT_MAX ) {
		return AssetCheck_DDSFail( "file is too large" );
	}
	if ( size < 4 + sizeof( *header ) ) {
		return AssetCheck_DDSFail( "file is smaller than a DDS header" );
	}
	if ( AssetCheck_DDSReadUInt32( data ) != ASSETCHECK_DDS_MAGIC ) {
		return AssetCheck_DDSFail( "file has wrong magic" );
	}

	header = (const assetCheckDdsHeader_t *)( data + 4 );
	dataOffset = 4 + sizeof( *header );
	if ( ( AssetCheck_DDSReadUInt32( &header->pixelFormatFlags ) & ASSETCHECK_DDSPF_FOURCC ) &&
		 AssetCheck_DDSReadUInt32( &header->fourCC ) == ASSETCHECK_DDS_FOURCC( 'D', 'X', '1', '0' ) ) {
		if ( size < dataOffset + sizeof( *headerDxt10 ) ) {
			return AssetCheck_DDSFail( "DX10 header is truncated" );
		}
		headerDxt10 = (const assetCheckDdsHeaderDxt10_t *)( data + dataOffset );
		dataOffset += sizeof( *headerDxt10 );
	}

	width = AssetCheck_DDSReadUInt32( &header->width );
	height = AssetCheck_DDSReadUInt32( &header->height );
	if ( width == 0 || height == 0 || width > ASSETCHECK_INT_MAX || height > ASSETCHECK_INT_MAX ) {
		return AssetCheck_DDSFail( "image dimensions are out of range" );
	}
	if ( (long long)width * height > ASSETCHECK_INT_MAX ) {
		return AssetCheck_DDSFail( "image dimensions would overflow renderer calculations" );
	}

	flags = AssetCheck_DDSReadUInt32( &header->flags );
	numMips = ( flags & ASSETCHECK_DDS_MIPMAPCOUNT ) ? AssetCheck_DDSReadUInt32( &header->numMips ) : 1;
	if ( numMips == 0 || numMips > AssetCheck_DDSMaxMipCount( width, height ) ) {
		return AssetCheck_DDSFail( "mip count is out of range" );
	}

	if ( !AssetCheck_DDSFormatBytes( header, headerDxt10, &blockBytes, &pixelBytes ) ) {
		return qfalse;
	}
	if ( !AssetCheck_DDSMipChainBytes( width, height, numMips, blockBytes, pixelBytes, &faceBytes ) ) {
		return qfalse;
	}

	caps2 = AssetCheck_DDSReadUInt32( &header->caps2 );
	faces = ( caps2 & ASSETCHECK_DDSCAPS2_CUBEMAP ) ? 6 : 1;
	if ( !AssetCheck_DDSMulFitsMax( faceBytes, faces, &requiredBytes ) ) {
		return AssetCheck_DDSFail( "image payload would overflow" );
	}
	if ( requiredBytes > size - dataOffset ) {
		return AssetCheck_DDSFail( "image payload is truncated" );
	}

	return qtrue;
}

/*
==========================================================================================
Interface
==========================================================================================
*/

/*
=================
AssetCheck_VerifyFileData

Run the security pre-pass for supported asset types.  Return qfalse if a check
fails, qtrue otherwise; success does not guarantee loader acceptance.
Unsupported extensions return qtrue without validation.

Non-NULL BSP buffers must include FS_ReadData's trailing NUL at data[size];
size excludes this extra byte.
=================
*/
qboolean AssetCheck_VerifyFileData( const char *qpath, const fsc_file_t *file, char *data, unsigned int size ) {
	qboolean ( *check_fn )( byte *data, unsigned int size ) = NULL;
	const char *extension = COM_GetExtension( qpath );
	if ( !Q_stricmp( extension, "bsp" ) ) {
		check_fn = AssetCheck_VerifyBSP;
	} else if ( !Q_stricmp( extension, "aas" ) ) {
		check_fn = AssetCheck_VerifyAAS;
	} else if ( !Q_stricmp( extension, "md3" ) ) {
		check_fn = AssetCheck_VerifyMD3;
	} else if ( !Q_stricmp( extension, "mdr" ) ) {
		check_fn = AssetCheck_VerifyMDR;
	} else if ( !Q_stricmp( extension, "iqm" ) ) {
		check_fn = AssetCheck_VerifyIQM;
	} else if ( !Q_stricmp( extension, "dds" ) ) {
		check_fn = AssetCheck_VerifyDDS;
	}

	if ( check_fn ) {
		qboolean valid = qfalse;
		assetCheck_error[0] = '\0';
		if ( data ) {
			valid = check_fn( (byte *)data, size );
		}
		
		if ( !valid || assetCheck_error[0] ) {
			char fileInfo[1024];
			FS_FileToBuffer( file, fileInfo, sizeof( fileInfo ), qtrue, qtrue, qtrue, qfalse );
			Com_Printf( "Asset verification failed for %s: %s\n",
					fileInfo, assetCheck_error[0] ? assetCheck_error : "<unknown error>" );
			return qfalse;
		}
	}

	return qtrue;
}

/*
=================
AssetCheck_VerifyFile

Return qtrue if valid, qfalse otherwise.
=================
*/
qboolean AssetCheck_VerifyFile( const char *qpath, const fsc_file_t *file ) {
	unsigned int size = 0;
	qboolean valid;
	char *data = FS_ReadData( file, NULL, &size, __func__ );
	if ( !data ) {
		return qfalse;
	}
	valid = AssetCheck_VerifyFileData( qpath, file, data, size );
	FS_FreeData( data );
	return valid;
}

/*
==========================================================================================
Testing
==========================================================================================
*/

/*
=================
AssetCheck_TestVerifyFile

Returns qtrue on pass.
=================
*/
static qboolean AssetCheck_TestVerifyFile( const char *qpath ) {
	const fsc_file_t *file = FS_GeneralLookup( qpath, 0, qfalse );
	if ( !file ) {
		Com_Printf( "verify_assets: %s disappeared from the virtual filesystem\n", qpath );
		return qfalse;
	}

	if ( !AssetCheck_VerifyFile( qpath, file ) ) {
		return qfalse;
	}

	return qtrue;
}

static int AssetCheck_TestBSPAssets( qboolean tryCollision ) {
	const char *mode = tryCollision ? "bsp_cm" : "bsp";
	char **files;
	int numFiles = 0;
	int failed = 0;
	int i;

	if ( tryCollision ) {
		Hunk_SetMark();
	}

	files = FS_ListFiles( "maps", ".bsp", &numFiles );
	Com_Printf( "verify_assets %s: verifying %i files matching maps/*.bsp\n", mode, numFiles );

	for ( i = 0 ; i < numFiles ; i++ ) {
		char qpath[MAX_QPATH];
		Com_sprintf( qpath, sizeof( qpath ), "maps/%s", files[i] );
		if ( !AssetCheck_TestVerifyFile( qpath ) ) {
			++failed;
		} else if ( tryCollision ) {
			int checksum = 0;
			CM_LoadMap( qpath, qfalse, &checksum );
			CM_ClearMap();
			Hunk_ClearToMark();
		}
	}

	FS_FreeFileList( files );
	Com_Printf( "verify_assets %s: %i checked, %i failed\n", mode, numFiles, failed );
	return failed;
}

static int AssetCheck_TestAssets( const char *ext ) {
	char dotext[256];
	char **files;
	int numFiles = 0;
	int failed = 0;
	int i;

	Com_sprintf( dotext, sizeof( dotext ), ".%s", ext );
	files = FS_ListFilteredFiles_Flags( "", dotext, "*", &numFiles, 0 );
	Com_Printf( "verify_assets %s: verifying %i files\n", ext, numFiles );

	for ( i = 0 ; i < numFiles ; i++ ) {
		if ( !AssetCheck_TestVerifyFile( files[i] ) ) {
			++failed;
		}
	}

	FS_FreeFileList( files );
	Com_Printf( "verify_assets %s: %i checked, %i failed\n", ext, numFiles, failed );
	return failed;
}

#ifndef DEDICATED
#ifdef USE_CODEC_MP3
void S_MP3_CodecResetDither( void );
#endif

static int AssetCheck_TestAudioAssets( const char *ext ) {
	char dotext[16];
	char **files;
	int numFiles = 0;
	int failed = 0;
	int loaded = 0;
	int i;
	qboolean wav = qfalse;
#ifdef USE_CODEC_MP3
	qboolean mp3 = qfalse;
#endif

	if ( !Q_stricmp( ext, "wav" ) ) {
		wav = qtrue;
	}
#ifdef USE_CODEC_MP3
	else if ( !Q_stricmp( ext, "mp3" ) ) {
		mp3 = qtrue;
	}
#endif
	else {
		Com_Printf( "verify_assets %s: audio loading is not available in this build\n", ext );
		return 1;
	}

	Com_sprintf( dotext, sizeof( dotext ), ".%s", ext );
	files = FS_ListFilteredFiles_Flags( "", dotext, "*", &numFiles, 0 );
	Com_Printf( "verify_assets %s: loading %i files\n", ext, numFiles );

	for ( i = 0 ; i < numFiles ; i++ ) {
		snd_info_t info;
		byte *data;
		const fsc_file_t *file;
		char fileInfo[FS_FILE_BUFFER_SIZE];
		int decodedBytes;
		int durationMsec = 0;
		unsigned int hash;

		Com_Memset( &info, 0, sizeof( info ) );
		data = NULL;
		if ( wav ) {
			data = S_WAV_CodecLoad( files[i], &info );
		}
#ifdef USE_CODEC_MP3
		else if ( mp3 ) {
			/* MP3 dithering uses global PRNG state, so reset before hashing decoded samples. */
			S_MP3_CodecResetDither();
			data = S_MP3_CodecLoad( files[i], &info );
		}
#endif
		if ( !data ) {
			Com_Printf( "verify_assets %s: %s failed to load\n", ext, files[i] );
			++failed;
			continue;
		}

		if ( info.dataofs < 0 || info.dataofs > info.size ) {
			Com_Printf( "verify_assets %s: %s decoded with invalid data offset %i/%i\n",
					ext, files[i], info.dataofs, info.size );
			Hunk_FreeTempMemory( data );
			++failed;
			continue;
		}

		decodedBytes = info.size - info.dataofs;
		hash = LittleLong( Com_BlockChecksum( data + info.dataofs, decodedBytes ) );

		if ( info.rate > 0 ) {
			long long duration = ( (long long)info.samples * 1000 ) / info.rate;
			durationMsec = duration > MAX_QINT ? MAX_QINT : (int)duration;
		}

		file = FS_GeneralLookup( files[i], 0, qfalse );
		if ( file ) {
			FS_FileToBuffer( file, fileInfo, sizeof( fileInfo ), qtrue, qtrue, qtrue, qtrue );
		} else {
			Q_strncpyz( fileInfo, files[i], sizeof( fileInfo ) );
		}

		Com_Printf( "verify_assets %s: %s: decoded=%i bytes, samples=%i, rate=%i, "
				"channels=%i, width=%i, duration=%i.%03is, hash=%08x\n",
				ext, fileInfo, decodedBytes, info.samples, info.rate, info.channels, info.width,
				durationMsec / 1000, durationMsec % 1000, hash );

		Hunk_FreeTempMemory( data );
		++loaded;
	}

#ifdef USE_CODEC_MP3
	if ( mp3 ) {
		S_MP3_CodecResetDither();
	}
#endif
	FS_FreeFileList( files );
	Com_Printf( "verify_assets %s: %i checked, %i loaded, %i failed\n", ext, numFiles, loaded, failed );
	return failed;
}
#endif

static void AssetCheck_VerifyAssetsCmd( void ) {
	const char *mode;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage:\n" );
		Com_Printf( "  verify_assets bsp\n" );
		Com_Printf( "      Verify all files located at maps/*.bsp in the virtual filesystem.\n" );
		Com_Printf( "  verify_assets bsp_cm\n" );
		Com_Printf( "      Verify maps/*.bsp and test collision loading (changes current map state).\n" );
		Com_Printf( "  verify_assets aas\n" );
		Com_Printf( "      Verify all .aas files in the virtual filesystem.\n" );
		Com_Printf( "  verify_assets md3\n" );
		Com_Printf( "      Verify all .md3 files in the virtual filesystem.\n" );
		Com_Printf( "  verify_assets mdr\n" );
		Com_Printf( "      Verify all .mdr files in the virtual filesystem.\n" );
		Com_Printf( "  verify_assets iqm\n" );
		Com_Printf( "      Verify all .iqm files in the virtual filesystem.\n" );
		Com_Printf( "  verify_assets dds\n" );
		Com_Printf( "      Verify all .dds files in the virtual filesystem.\n" );
		Com_Printf( "  verify_assets mp3\n" );
#if defined( USE_CODEC_MP3 ) && !defined( DEDICATED )
		Com_Printf( "      Load all .mp3 files and print decoded audio metadata and checksum.\n" );
#else
		Com_Printf( "      MP3 loading test is unavailable in this build.\n" );
#endif
		Com_Printf( "  verify_assets wav\n" );
#ifndef DEDICATED
		Com_Printf( "      Load all .wav files and print decoded audio metadata and checksum.\n" );
#else
		Com_Printf( "      WAV audio loading test is unavailable in this build.\n" );
#endif
		Com_Printf( "  verify_assets all\n" );
		Com_Printf( "      Run all asset validators and available audio loading tests, excluding bsp_cm.\n" );
		return;
	}

	mode = Cmd_Argv( 1 );
	if ( !Q_stricmp( mode, "bsp_cm" ) ) {
		AssetCheck_TestBSPAssets( qtrue );
		return;
	} else if ( !Q_stricmp( mode, "bsp" ) ) {
		AssetCheck_TestBSPAssets( qfalse );
		return;
	} else if ( !Q_stricmp( mode, "aas" ) ) {
		AssetCheck_TestAssets( "aas" );
		return;
	} else if ( !Q_stricmp( mode, "md3" ) ) {
		AssetCheck_TestAssets( "md3" );
		return;
	} else if ( !Q_stricmp( mode, "mdr" ) ) {
		AssetCheck_TestAssets( "mdr" );
		return;
	} else if ( !Q_stricmp( mode, "iqm" ) ) {
		AssetCheck_TestAssets( "iqm" );
		return;
	} else if ( !Q_stricmp( mode, "dds" ) ) {
		AssetCheck_TestAssets( "dds" );
		return;
	} else if ( !Q_stricmp( mode, "mp3" ) ) {
#if defined( USE_CODEC_MP3 ) && !defined( DEDICATED )
		AssetCheck_TestAudioAssets( "mp3" );
#else
		Com_Printf( "verify_assets mp3: MP3 loading is not available in this build\n" );
#endif
		return;
	} else if ( !Q_stricmp( mode, "wav" ) ) {
#ifndef DEDICATED
		AssetCheck_TestAudioAssets( "wav" );
#else
		Com_Printf( "verify_assets wav: WAV audio loading is not available in this build\n" );
#endif
		return;
	} else if ( !Q_stricmp( mode, "all" ) ) {
		AssetCheck_TestBSPAssets( qfalse );
		AssetCheck_TestAssets( "aas" );
		AssetCheck_TestAssets( "md3" );
		AssetCheck_TestAssets( "mdr" );
		AssetCheck_TestAssets( "iqm" );
		AssetCheck_TestAssets( "dds" );
#ifndef DEDICATED
#ifdef USE_CODEC_MP3
		AssetCheck_TestAudioAssets( "mp3" );
#endif
		AssetCheck_TestAudioAssets( "wav" );
#endif
		return;
	} else {
		Com_Printf( "verify_assets: unknown validator '%s'\n", mode );
	}
}

void AssetCheck_RegisterCommands( void ) {
	Cmd_AddCommand( "verify_assets", AssetCheck_VerifyAssetsCmd );
}

#endif
