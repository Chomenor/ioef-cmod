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

#ifdef CMOD_NEW_MEMORY_ALLOC
/*
Security Model:
- Allocations "grow upwards" within each block of memory, so an overflow
at the end of one allocation can only corrupt later allocations. An overflow
of the most recent allocation should be safe; it can only change unused
memory or hit a page guard and crash.
- Negative overflows or overflows of older allocations can cause corruption
limited to the same zone (e.g. TAG_RENDERER can only corrupt other TAG_RENDERER).
- More sensitive allocations such as VM data can use Hunk_AllocSensitive
to get a dedicated page guarded block.
*/

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

#if defined( DEDICATED ) || defined( __EMSCRIPTEN__ )
// Less security-sensitive configuration
#define MEM_DEFERRED_FREE_SLOTS 0
#define MEM_ENABLE_DEBUG_COMMANDS
#else
#define MEM_ENABLE_PAGE_GUARD
#define MEM_DEFERRED_FREE_SLOTS 16
#ifndef NDEBUG
// Enable on debug builds only, to avoid potential leak issues
#define MEM_ENABLE_DEBUG_COMMANDS
#endif
#endif

#define MEM_ERROR( ... ) Com_Error( ERR_FATAL, __VA_ARGS__ )
#define MEM_ASSERT( cond ) { if ( !( cond ) ) MEM_ERROR( "%s: Assertion failed - %s", __func__, #cond ); }

// Standard filesystem value
#define MEM_INVALID_FILE_HANDLE 0

/*
=================
Mem_RandInt
=================
*/
static uint32_t Mem_RandInt( void ) {
	static uint32_t vals[256];
	static uint32_t pos = ARRAY_LEN( vals );

	if ( pos >= ARRAY_LEN( vals ) ) {
		if ( !Sys_RandomBytes( (byte *)vals, sizeof( vals ) ) ) {
			MEM_ERROR( "Mem_RandInt: Sys_RandomBytes failed" );
		}
		pos = 0;
	}

	return vals[pos++];
}

/*
==========================================================================================
Map allocator
==========================================================================================
*/

typedef struct {
	void *ptr;
	void *base;
	size_t size;
	size_t map_size;
} mem_map_alloc_t;

/*
=================
Mem_PageSize
=================
*/
static size_t Mem_PageSize( void ) {
	static size_t page_size;

	if ( !page_size ) {
#ifdef _WIN32
		SYSTEM_INFO system_info;
		GetSystemInfo( &system_info );
		page_size = system_info.dwPageSize;
#else
		long sys_page_size = -1;
#ifdef _SC_PAGESIZE
		sys_page_size = sysconf( _SC_PAGESIZE );
#elif defined( _SC_PAGE_SIZE )
		sys_page_size = sysconf( _SC_PAGE_SIZE );
#endif
		if ( sys_page_size > 0 ) {
			page_size = (size_t)sys_page_size;
		}
#endif
		if ( !page_size ) {
			page_size = 4096;
		}
	}

	return page_size;
}

/*
=================
Mem_PageRoundSize
=================
*/
static size_t Mem_PageRoundSize( size_t size ) {
	size_t page_size = Mem_PageSize();
	size_t remainder;

	if ( !size ) {
		size = 1;
	}

	remainder = size % page_size;
	if ( remainder ) {
		size_t padding = page_size - remainder;
		if ( size > SIZE_MAX - padding ) {
			MEM_ERROR( "Mem_PageRoundSize: size overflow" );
		}
		size += padding;
	}

	return size;
}

#ifdef MEM_ENABLE_PAGE_GUARD
/*
=================
Mem_PageProtect
=================
*/
static void Mem_PageProtect( void *ptr, size_t size ) {
#ifdef _WIN32
	DWORD old_protect;
	if ( !VirtualProtect( ptr, size, PAGE_NOACCESS, &old_protect ) ) {
		MEM_ERROR( "Mem_PageProtect: VirtualProtect failed" );
	}
#else
	if ( mprotect( ptr, size, PROT_NONE ) ) {
		MEM_ERROR( "Mem_PageProtect: mprotect failed" );
	}
#endif
}
#endif

/*
=================
Mem_MapAlloc
=================
*/
static mem_map_alloc_t Mem_MapAlloc( size_t size ) {
	mem_map_alloc_t alloc;
	size_t alloc_size = size ? size : 1;
	size_t usable_size = Mem_PageRoundSize( alloc_size );
#ifdef MEM_ENABLE_PAGE_GUARD
	size_t page_size = Mem_PageSize();
	size_t guard_size;
#endif

	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.size = alloc_size;
#ifdef MEM_ENABLE_PAGE_GUARD
	guard_size = page_size * 2;
	if ( guard_size / 2 != page_size || usable_size > SIZE_MAX - guard_size ) {
		MEM_ERROR( "Mem_MapAlloc: size overflow" );
	}
	alloc.map_size = usable_size + guard_size;
#else
	alloc.map_size = usable_size;
#endif

#ifdef _WIN32
	alloc.base = VirtualAlloc( NULL, alloc.map_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
	if ( !alloc.base ) {
		MEM_ERROR( "Mem_MapAlloc: VirtualAlloc failed" );
	}
#else
	alloc.base = mmap( NULL, alloc.map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
	if ( alloc.base == MAP_FAILED ) {
		MEM_ERROR( "Mem_MapAlloc: mmap failed" );
	}
#endif

#ifdef MEM_ENABLE_PAGE_GUARD
	Mem_PageProtect( alloc.base, page_size );
	Mem_PageProtect( (char *)alloc.base + page_size + usable_size, page_size );
	alloc.ptr = (char *)alloc.base + page_size;
#else
	alloc.ptr = alloc.base;
#endif
	return alloc;
}

/*
=================
Mem_MapFree
=================
*/
static void Mem_MapFree( mem_map_alloc_t *alloc ) {
	MEM_ASSERT( alloc && alloc->base );

#ifdef _WIN32
	if ( !VirtualFree( alloc->base, 0, MEM_RELEASE ) ) {
		MEM_ERROR( "Mem_MapFree: VirtualFree failed" );
	}
#else
	if ( munmap( alloc->base, alloc->map_size ) ) {
		MEM_ERROR( "Mem_MapFree: munmap failed" );
	}
#endif

	Com_Memset( alloc, 0, sizeof( *alloc ) );
}

/*
==========================================================================================
Deferred free
==========================================================================================
*/

// Extra protection against use after free

#if MEM_DEFERRED_FREE_SLOTS > 0
static mem_map_alloc_t deferred_free_slots[MEM_DEFERRED_FREE_SLOTS];

/*
=================
Mem_MapDiscardAndProtect

Discard backing storage while keeping the address range inaccessible until Mem_MapFree.
=================
*/
static void Mem_MapDiscardAndProtect( mem_map_alloc_t *alloc ) {
	MEM_ASSERT( alloc && alloc->base && alloc->map_size );

#ifdef _WIN32
	if ( !VirtualFree( alloc->base, alloc->map_size, MEM_DECOMMIT ) ) {
		MEM_ERROR( "Mem_MapDiscardAndProtect: VirtualFree failed" );
	}
#else
	// MAP_FIXED must only replace the complete mapping still owned by this allocation.
	if ( mmap( alloc->base, alloc->map_size, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0 ) == MAP_FAILED ) {
		MEM_ERROR( "Mem_MapDiscardAndProtect: mmap failed" );
	}
#endif
}

static void Mem_DeferredFree( mem_map_alloc_t *alloc ) {
	unsigned int slot = Mem_RandInt() % MEM_DEFERRED_FREE_SLOTS;
	if ( deferred_free_slots[slot].base ) {
		Mem_MapFree( &deferred_free_slots[slot] );
	}
	Mem_MapDiscardAndProtect( alloc );
	deferred_free_slots[slot] = *alloc;
	Com_Memset( alloc, 0, sizeof( *alloc ) );
}
#else
static void Mem_DeferredFree( mem_map_alloc_t *alloc ) {
	Mem_MapFree( alloc );
}
#endif

/*
==========================================================================================
Memory zone
==========================================================================================
*/

#if defined( HUNK_DEBUG ) || defined( ZONE_DEBUG )
#define MEM_STORE_DEBUG_INFO
#endif

#define BLOCK_ALLOC_INCREMENT 128

uint32_t mem_magic;
#define MAGIC_ALLOCATED mem_magic
#define MAGIC_FREE ( mem_magic + 1 )

typedef struct {
	const char *label;
	const char *file;
	int line;
} mem_debug_info_t;

typedef struct mem_alloc_header_t {
	uint32_t magic;
	uint32_t zone_id;
	uint64_t block_id;
	uint32_t size;		// debug only
	uint32_t checkbits;
#ifdef MEM_STORE_DEBUG_INFO
	mem_debug_info_t debug_info;
#endif
} mem_alloc_header_t;

typedef struct {
	mem_map_alloc_t alloc;
	uint32_t mem_size;
	uint32_t mem_utilized;
	uint32_t allocs_total;		// debug only
	uint32_t allocs_unfreed;
	uint64_t block_id;
	uint32_t checkbits;		// zero after all allocs freed
} mem_block_t;

typedef struct {
	const char *name;	// debug only

	// if 0: 1 alloc per block; can be any size
	// if set: multiple allocs per block; allocs must be less than this size
	uint32_t shared_block_size;

	mem_block_t *blocks;
	uint32_t block_alloc;
	uint32_t block_count;

	uint32_t zone_id;
	uint64_t block_id_counter;
} mem_zone_t;

/*
=================
Mem_ZoneRebuild
=================
*/
static void Mem_ZoneRebuild( mem_zone_t *zone ) {
	uint32_t i;
	uint32_t active_blocks = 0;
	mem_zone_t old_zone = *zone;

	for ( i = 0; i < old_zone.block_count; ++i ) {
		if ( old_zone.blocks[i].alloc.ptr ) {
			++active_blocks;
		}
	}

	zone->block_alloc = active_blocks + BLOCK_ALLOC_INCREMENT;
	zone->block_count = 0;
	zone->blocks = calloc( zone->block_alloc, sizeof( *zone->blocks ) );
	if ( !zone->blocks ) {
		MEM_ERROR( "Mem_ZoneRebuild: calloc failed" );
	}

	for ( i = 0; i < old_zone.block_count; ++i ) {
		if ( old_zone.blocks[i].alloc.ptr ) {
			zone->blocks[zone->block_count] = old_zone.blocks[i];
			zone->block_count++;
		}
	}
	if ( old_zone.blocks ) {
		free( old_zone.blocks );
	}
}

/*
=================
Mem_ZoneInsertBlock
=================
*/
static mem_block_t *Mem_ZoneInsertBlock( mem_zone_t *zone, uint64_t size ) {
	// Check for freeing empty top block
	mem_block_t *block = NULL;
	if ( zone->block_count > 0 ) {
		block = &zone->blocks[zone->block_count - 1];
		if ( block->alloc.ptr && block->allocs_unfreed == 0 ) {
			MEM_ASSERT( !block->checkbits );
			Mem_DeferredFree( &block->alloc );
		}
	}

	if ( zone->block_count >= zone->block_alloc ) {
		Mem_ZoneRebuild( zone );
		MEM_ASSERT( zone->block_count < zone->block_alloc );
	}
	block = &zone->blocks[zone->block_count];

	MEM_ASSERT( size <= UINT32_MAX );
	Com_Memset( block, 0, sizeof( *block ) );
	block->alloc = Mem_MapAlloc( size );
	block->mem_size = size;
	block->block_id = zone->block_id_counter++;

	zone->block_count++;
	return block;
}

/*
=================
Mem_ZoneWipe
=================
*/
static void Mem_ZoneWipe( mem_zone_t *zone ) {
	if ( zone->blocks ) {
		uint32_t i;
		for ( i = 0; i < zone->block_count; ++i ) {
			if ( zone->blocks[i].alloc.ptr ) {
				Mem_DeferredFree( &zone->blocks[i].alloc );
			}
		}
		free( zone->blocks );
		zone->blocks = NULL;
	}
	zone->block_alloc = 0;
	zone->block_count = 0;
}

/*
=================
Mem_ZoneBlockIdToIndex
=================
*/
static uint32_t Mem_ZoneBlockIdToIndex( mem_zone_t *zone, uint64_t block_id ) {
	uint32_t i;
	for ( i = zone->block_count; i-- > 0; ) {
		if ( zone->blocks[i].block_id == block_id ) {
			return i;
		}
	}
	return UINT32_MAX;
}

/*
=================
Mem_ZoneAlloc
=================
*/
static void *Mem_ZoneAlloc( mem_zone_t *zone, uint64_t size, const mem_debug_info_t *debug_info ) {
	mem_block_t *block;
	mem_alloc_header_t *alloc;
	uint32_t random_buffer = Mem_RandInt() & 127;	// make layout a bit less predictable for security purposes
	uint64_t alloc_size = ( sizeof( mem_alloc_header_t ) + size + random_buffer + 7 ) & ~7;
	MEM_ASSERT( alloc_size > size );

	if ( !zone->shared_block_size ) {
		// this zone is 1-alloc-per-block
		block = Mem_ZoneInsertBlock( zone, alloc_size );
	} else {
		MEM_ASSERT( alloc_size <= zone->shared_block_size );
		block = NULL;
		if ( zone->block_count > 0 && zone->blocks[zone->block_count - 1].alloc.ptr ) {
			// append to latest block
			block = &zone->blocks[zone->block_count - 1];
		}
		if ( !block || block->mem_size - block->mem_utilized < alloc_size ) {
			// allocate new block
			block = Mem_ZoneInsertBlock( zone, zone->shared_block_size );
		}
	}

	MEM_ASSERT( block->mem_size - block->mem_utilized >= alloc_size );
	alloc = (mem_alloc_header_t *)( (char *)block->alloc.ptr + block->mem_utilized );
	Com_Memset( alloc, 0, alloc_size );
	alloc->magic = MAGIC_ALLOCATED;
	alloc->zone_id = zone->zone_id;
	alloc->block_id = block->block_id;
	alloc->size = alloc_size;
	alloc->checkbits = Mem_RandInt();
#ifdef MEM_STORE_DEBUG_INFO
	if ( debug_info ) {
		alloc->debug_info = *debug_info;
	}
#endif

	block->mem_utilized += alloc_size;
	block->allocs_unfreed++;
	block->allocs_total++;
	block->checkbits ^= alloc->checkbits;

	return (char *)alloc + sizeof( mem_alloc_header_t );
}

/*
=================
Mem_ZoneFree
=================
*/
static void Mem_ZoneFree( mem_zone_t *zone, mem_alloc_header_t *alloc ) {
	uint32_t block_index;
	mem_block_t *block;

	alloc->magic = MAGIC_FREE;

	block_index = Mem_ZoneBlockIdToIndex( zone, alloc->block_id );
	MEM_ASSERT( block_index < zone->block_count );

	block = &zone->blocks[block_index];
	MEM_ASSERT( block->alloc.ptr );
	MEM_ASSERT( block->block_id == alloc->block_id );
	MEM_ASSERT( block->allocs_unfreed > 0 );

	block->allocs_unfreed--;
	block->checkbits ^= alloc->checkbits;
	if ( block->allocs_unfreed == 0 ) {
		MEM_ASSERT( !block->checkbits );
		if ( !zone->shared_block_size || block_index + 1 != zone->block_count ) {
			Mem_DeferredFree( &block->alloc );
		}
	}
}

/*
==========================================================================================
Multi-zone manager
==========================================================================================
*/

typedef enum {
	MEM_HUNK_MAIN,
	MEM_HUNK_TEMP,
	MEM_ZALLOC,
} mem_category_t;

typedef enum {
	MEM_ANY_MARK,
	MEM_PRE_MARK,
	MEM_POST_MARK
} mem_mark_type_t;

typedef struct {
	mem_category_t category;
	memtag_t tag_type;
	mem_mark_type_t mark_type;
} mem_zone_filter_t;

typedef struct {
	mem_zone_t zone;
	mem_zone_filter_t filter;
	uint32_t max_size;		// larger allocs skip this zone
} mem_zone_ext_t;

mem_zone_ext_t mem_zones[] = {
	// In a local game, server goes in premark and client goes in postmark,
	// so vid_restart can clear the client only.
	// If not running a local game everything goes in premark.
	{ { "hunk_main_premark_s", 1024 * 1024 }, { MEM_HUNK_MAIN, TAG_FREE, MEM_PRE_MARK }, 256 * 1024 },
	{ { "hunk_main_premark_l", 0 }, { MEM_HUNK_MAIN, TAG_FREE, MEM_PRE_MARK }, 0 },
	{ { "hunk_temp_premark_s", 1024 * 1024 }, { MEM_HUNK_TEMP, TAG_FREE, MEM_PRE_MARK }, 256 * 1024 },
	{ { "hunk_temp_premark_l", 0 }, { MEM_HUNK_TEMP, TAG_FREE, MEM_PRE_MARK }, 0 },
	{ { "hunk_main_postmark_s", 1024 * 1024 }, { MEM_HUNK_MAIN, TAG_FREE, MEM_POST_MARK }, 256 * 1024 },
	{ { "hunk_main_postmark_l", 0 }, { MEM_HUNK_MAIN, TAG_FREE, MEM_POST_MARK }, 0 },
	{ { "hunk_temp_postmark_s", 1024 * 1024 }, { MEM_HUNK_TEMP, TAG_FREE, MEM_POST_MARK }, 256 * 1024 },
	{ { "hunk_temp_postmark_l", 0 }, { MEM_HUNK_TEMP, TAG_FREE, MEM_POST_MARK }, 0 },

	{ { "zone_general_s", 16 * 1024 }, { MEM_ZALLOC, TAG_GENERAL }, 4 * 1024 },
	{ { "zone_general_m", 1024 * 1024 }, { MEM_ZALLOC, TAG_GENERAL }, 256 * 1024 },
	{ { "zone_general_l", 0 }, { MEM_ZALLOC, TAG_GENERAL }, 0 },
	{ { "zone_botlib_s", 16 * 1024 }, { MEM_ZALLOC, TAG_BOTLIB }, 4 * 1024 },
	{ { "zone_botlib_m", 1024 * 1024 }, { MEM_ZALLOC, TAG_BOTLIB }, 256 * 1024 },
	{ { "zone_botlib_l", 0 }, { MEM_ZALLOC, TAG_BOTLIB }, 0 },
	{ { "zone_renderer_s", 16 * 1024 }, { MEM_ZALLOC, TAG_RENDERER }, 4 * 1024 },
	{ { "zone_renderer_m", 1024 * 1024 }, { MEM_ZALLOC, TAG_RENDERER }, 256 * 1024 },
	{ { "zone_renderer_l", 0 }, { MEM_ZALLOC, TAG_RENDERER }, 0 },
	{ { "zone_small_s", 16 * 1024 }, { MEM_ZALLOC, TAG_SMALL }, 4 * 1024 },
	{ { "zone_small_m", 1024 * 1024 }, { MEM_ZALLOC, TAG_SMALL }, 256 * 1024 },
	{ { "zone_small_l", 0 }, { MEM_ZALLOC, TAG_SMALL }, 0 },
};

/*
=================
Mem_FilterMatch
=================
*/
static qboolean Mem_FilterMatch( mem_zone_filter_t f1, mem_zone_filter_t f2 ) {
	return f1.category == f2.category && f1.tag_type == f2.tag_type &&
		( f1.mark_type == f2.mark_type || f1.mark_type == MEM_ANY_MARK || f2.mark_type == MEM_ANY_MARK );
}

/*
=================
Mem_Alloc
=================
*/
static void *Mem_Alloc( uint64_t size, mem_zone_filter_t zone_filter, qboolean dedicated,
		mem_debug_info_t *debug_info ) {
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( mem_zones ); ++i ) {
		if ( Mem_FilterMatch( mem_zones[i].filter, zone_filter ) &&
				( !mem_zones[i].max_size || ( !dedicated && size <= mem_zones[i].max_size ) ) ) {
			return Mem_ZoneAlloc( &mem_zones[i].zone, size, debug_info );
		}
	}
	MEM_ERROR( "Mem_Alloc missing zone" );
	return NULL;
}

/*
=================
Mem_Free
=================
*/
static void Mem_Free( void *ptr ) {
	mem_alloc_header_t *alloc;
	MEM_ASSERT( ptr );
	alloc = (mem_alloc_header_t *)( (char *)ptr - sizeof( mem_alloc_header_t ) );
	MEM_ASSERT( alloc->magic == MAGIC_ALLOCATED );
	MEM_ASSERT( alloc->zone_id < ARRAY_LEN( mem_zones ) );
	Mem_ZoneFree( &mem_zones[alloc->zone_id].zone, alloc );
}

/*
=================
Mem_WipeZones
=================
*/
static void Mem_WipeZones( mem_zone_filter_t zone_filter ) {
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( mem_zones ); ++i ) {
		if ( Mem_FilterMatch( mem_zones[i].filter, zone_filter ) ) {
			Mem_ZoneWipe( &mem_zones[i].zone );
		}
	}
}

/*
==========================================================================================
Debugging
==========================================================================================
*/

#ifdef MEM_ENABLE_DEBUG_COMMANDS
typedef struct {
	fileHandle_t dumpfile;
	qboolean print;
	qboolean error;
	struct {
		uint32_t block_count;
		uint32_t alloc_count_total;
		uint32_t alloc_count_unfree;
		uint64_t alloc_size_total;
		uint64_t alloc_size_unfree;
	} zone_stats;
} debug_state_t;

#define DEBUG_PRINT 1
#define DEBUG_DUMP 2

/*
=================
Mem_DebugMessage
=================
*/
static Q_PRINTF_FUNC(3, 4) void Mem_DebugMessage( debug_state_t *debug_state, int flags, const char *fmt, ... ) {
	va_list argptr;
	char msg[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	if ( ( flags & DEBUG_DUMP ) && debug_state->dumpfile != MEM_INVALID_FILE_HANDLE ) {
		FS_Write( msg, strlen( msg ), debug_state->dumpfile );
	}

	if ( ( flags & DEBUG_PRINT ) && debug_state->print ) {
		Com_Printf( "%s", msg );
	}
}

/*
=================
Mem_DebugBlock
=================
*/
static void Mem_DebugBlock( mem_block_t *block, debug_state_t *debug_state ) {
	mem_alloc_header_t *alloc = (mem_alloc_header_t *)block->alloc.ptr;
	uint32_t i;

	Mem_DebugMessage( debug_state, DEBUG_DUMP, "  block: id=%llu size=%u\n", (unsigned long long)block->block_id, block->mem_size );
	debug_state->zone_stats.block_count++;
	debug_state->zone_stats.alloc_count_total += block->allocs_total;
	debug_state->zone_stats.alloc_size_total += block->mem_size;

	for ( i = 0; i < block->allocs_total; ++i ) {
		if ( alloc->magic == MAGIC_ALLOCATED ) {
			debug_state->zone_stats.alloc_count_unfree++;
			debug_state->zone_stats.alloc_size_unfree += alloc->size;
		} else if ( alloc->magic != MAGIC_FREE ) {
			Mem_DebugMessage( debug_state, DEBUG_PRINT | DEBUG_DUMP, "ERROR: alloc has bad magic\n" );
			debug_state->error = qtrue;
			return;
		}
#ifdef MEM_STORE_DEBUG_INFO
		Mem_DebugMessage( debug_state, DEBUG_DUMP,
			"    alloc: inuse=%s size=%u label=%s file=%s line=%i\n",
			alloc->magic == MAGIC_ALLOCATED ? "yes" : "no", alloc->size,
			alloc->debug_info.label ? alloc->debug_info.label : "<na>",
			alloc->debug_info.file ? alloc->debug_info.file : "<na>", alloc->debug_info.line );
#else
		Mem_DebugMessage( debug_state, DEBUG_DUMP, "    alloc: inuse=%s size=%u\n",
			alloc->magic == MAGIC_ALLOCATED ? "yes" : "no", alloc->size );
#endif
		alloc = (mem_alloc_header_t *)( (char *)alloc + alloc->size );
	}
}

/*
=================
Mem_DebugZones
=================
*/
static void Mem_DebugZones( debug_state_t *debug_state ) {
	uint32_t i, j;

	for ( i = 0; i < ARRAY_LEN( mem_zones ); ++i ) {
		mem_zone_ext_t *zonedef = &mem_zones[i];
		Mem_DebugMessage( debug_state, DEBUG_DUMP, "===========START %s\n", zonedef->zone.name );
		Com_Memset( &debug_state->zone_stats, 0, sizeof( debug_state->zone_stats ) );
		for ( j = 0; j < zonedef->zone.block_count; ++j ) {
			mem_block_t *block = &zonedef->zone.blocks[j];
			if ( block->alloc.ptr ) {
				Mem_DebugBlock( block, debug_state );
			}
		}
		Mem_DebugMessage( debug_state, DEBUG_PRINT | DEBUG_DUMP, "%s: blocks=%u bytes=%llu used,"
			" %llu overhead alloc=%u used, %u stale\n", zonedef->zone.name, debug_state->zone_stats.block_count,
			(unsigned long long)debug_state->zone_stats.alloc_size_unfree,
			(unsigned long long)(debug_state->zone_stats.alloc_size_total - debug_state->zone_stats.alloc_size_unfree),
			debug_state->zone_stats.alloc_count_unfree,
			debug_state->zone_stats.alloc_count_total - debug_state->zone_stats.alloc_count_unfree);
		Mem_DebugMessage( debug_state, DEBUG_DUMP, "===========END %s\n\n", zonedef->zone.name );
	}
}

/*
=================
Mem_Dump_f
=================
*/
static void Mem_Dump_f( void ) {
	fileHandle_t handle = FS_FOpenFileWrite_HomeData( "memdump.txt" );
	if ( !handle ) {
		Com_Printf( "ERROR: couldn't open memdump.txt\n" );
		return;
	}

	debug_state_t debug_state = { handle, qfalse };
	Mem_DebugZones( &debug_state );
	FS_FCloseFile( handle );
	Com_Printf( "Memory log written to memdump.txt\n" );
}

/*
=================
Mem_Debug_f
=================
*/
static void Mem_Debug_f( void ) {
	debug_state_t debug_state = { MEM_INVALID_FILE_HANDLE, qtrue };
	Mem_DebugZones( &debug_state );
}
#else
static void Mem_Dump_f( void ) {
	Com_Printf( "Not supported by build configuration.\n" );
}

static void Mem_Debug_f( void ) {
	Com_Printf( "Not supported by build configuration.\n" );
}
#endif

/*
==========================================================================================
Hunk interface
==========================================================================================
*/

qboolean mem_hunk_mark_set;

void Com_TouchMemory( void ) {
}

void Com_InitHunkMemory( void ) {
}

int Hunk_MemoryRemaining( void ) {
	return 2000000000;
}

/*
===================
Hunk_SetMark

The server calls this after the level and game VM have been loaded
===================
*/
void Hunk_SetMark( void ) {
	if ( mem_hunk_mark_set ) {
		MEM_ERROR( "Hunk_SetMark already set" );
	}
	mem_hunk_mark_set = qtrue;
}

/*
=================
Hunk_ClearToMark

The client calls this before starting a vid_restart or snd_restart
=================
*/
void Hunk_ClearToMark( void ) {
	Mem_WipeZones( (mem_zone_filter_t){ MEM_HUNK_MAIN, TAG_FREE, MEM_POST_MARK } );
	Mem_WipeZones( (mem_zone_filter_t){ MEM_HUNK_TEMP, TAG_FREE, MEM_POST_MARK } );
}

/*
=================
Hunk_CheckMark
=================
*/
qboolean Hunk_CheckMark( void ) {
	return mem_hunk_mark_set;
}

/*
=================
Hunk_Alloc

Allocate permanent (until the hunk is cleared) memory
=================
*/
#ifdef HUNK_DEBUG
void *Hunk_AllocDebug( int size, ha_pref preference, char *label, char *file, int line ) {
	mem_debug_info_t debug_info = { label, file, line };
	mem_debug_info_t *debug_info_p = &debug_info;
#else
void *Hunk_Alloc( int size, ha_pref preference ) {
	mem_debug_info_t *debug_info_p = NULL;
#endif
	mem_zone_filter_t filter = { MEM_HUNK_MAIN, TAG_FREE, mem_hunk_mark_set ? MEM_POST_MARK : MEM_PRE_MARK };
	MEM_ASSERT( size >= 0 && size <= 2000000000 );
	return Mem_Alloc( size + 1, filter, qfalse, debug_info_p );
}

/*
=================
Hunk_AllocSensitive

Use a dedicated allocation for security-sensitive data.
=================
*/
void *Hunk_AllocSensitive( int size, char *label, char *file, int line ) {
	mem_debug_info_t debug_info = { label, file, line };
	mem_debug_info_t *debug_info_p = &debug_info;
	mem_zone_filter_t filter = { MEM_HUNK_MAIN, TAG_FREE, mem_hunk_mark_set ? MEM_POST_MARK : MEM_PRE_MARK };
	MEM_ASSERT( size >= 0 && size <= 2000000000 );
	return Mem_Alloc( size + 1, filter, qtrue, debug_info_p );
}

/*
=================
Hunk_AllocateTempMemory

This is used by the file loading system.
Multiple files can be loaded in temporary memory.
When the files-in-use count reaches zero, all temp memory will be deleted
=================
*/
void *Hunk_AllocateTempMemory( int size ) {
	mem_zone_filter_t filter = { MEM_HUNK_TEMP, TAG_FREE, mem_hunk_mark_set ? MEM_POST_MARK : MEM_PRE_MARK };
	MEM_ASSERT( size >= 0 && size <= 2000000000 );
	return Mem_Alloc( size + 1, filter, qfalse, NULL );
}


/*
==================
Hunk_FreeTempMemory
==================
*/
void Hunk_FreeTempMemory( void *buf ) {
	Mem_Free( buf );
}

/*
=================
Hunk_ClearMem
=================
*/
void Hunk_ClearMem( void ) {
	Mem_WipeZones( (mem_zone_filter_t){ MEM_HUNK_MAIN } );
	Mem_WipeZones( (mem_zone_filter_t){ MEM_HUNK_TEMP } );
	mem_hunk_mark_set = qfalse;
}

/*
=================
Hunk_ClearTempMemory

The temp space is no longer needed.  If we have left more
touched but unused memory on this side, have future
permanent allocs use this side.
=================
*/
void Hunk_ClearTempMemory( void ) {
	Mem_WipeZones( (mem_zone_filter_t){ MEM_HUNK_TEMP } );
}

/*
==========================================================================================
Zone interface
==========================================================================================
*/

char *memstatic[11] = { "", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };

void Mem_Init( void );

void Com_InitSmallZoneMemory( void ) {
	Mem_Init();
}

void Com_InitZoneMemory( void ) {
}

int Z_AvailableMemory( void ) {
	return 2000000000;
}

/*
========================
Z_Free
========================
*/
void Z_Free( void *ptr ) {
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( memstatic ); ++i ) {
		if ( ptr == memstatic[i] ) {
			return;
		}
	}

	Mem_Free( ptr );
}

/*
================
Z_FreeTags

Currently unused and untested.
================
*/
void Z_FreeTags( int tag ) {
	Mem_WipeZones( (mem_zone_filter_t){ MEM_ZALLOC, tag } );
}

/*
================
Z_TagMalloc
================
*/
#ifdef ZONE_DEBUG
void *Z_TagMallocDebug( int size, int tag, char *label, char *file, int line ) {
	mem_debug_info_t debug_info = { label, file, line };
	mem_debug_info_t *debug_info_p = &debug_info;
#else
void *Z_TagMalloc( int size, int tag ) {
	mem_debug_info_t *debug_info_p = NULL;
#endif
	mem_zone_filter_t filter = { MEM_ZALLOC, tag };
	MEM_ASSERT( size >= 0 && size <= 2000000000 );
	return Mem_Alloc( size + 1, filter, qfalse, debug_info_p );
}

/*
========================
Z_Malloc
========================
*/
#ifdef ZONE_DEBUG
void *Z_MallocDebug( int size, char *label, char *file, int line ) {
	return Z_TagMallocDebug( size, TAG_GENERAL, label, file, line );
}
#else
void *Z_Malloc( int size ) {
	return Z_TagMalloc( size, TAG_GENERAL );
}
#endif

/*
========================
S_Malloc
========================
*/
#ifdef ZONE_DEBUG
void *S_MallocDebug( int size, char *label, char *file, int line ) {
	return Z_TagMallocDebug( size, TAG_SMALL, label, file, line );
}
#else
void *S_Malloc( int size ) {
	return Z_TagMalloc( size, TAG_SMALL );
}
#endif

/*
========================
CopyString

 NOTE:	never write over the memory CopyString returns because
		memory from memstatic might be returned
========================
*/
char *CopyString( const char *in ) {
	char	*out;

	if (!in[0]) {
		return memstatic[0];
	}
	else if (!in[1]) {
		if (in[0] >= '0' && in[0] <= '9') {
			return memstatic[in[0] - '0' + 1];
		}
	}
	out = S_Malloc (strlen(in)+1);
	strcpy (out, in);
	return out;
}

/*
==========================================================================================
Initialization
==========================================================================================
*/

static void Mem_Debug_f( void );
static void Mem_Dump_f( void );

/*
=================
Mem_Init
=================
*/
void Mem_Init( void ) {
	uint32_t i;
	for ( i = 0; i < ARRAY_LEN( mem_zones ); ++i ) {
		mem_zones[i].zone.zone_id = i;
	}

	mem_magic = Mem_RandInt();

	Cmd_AddCommand( "meminfo", Mem_Debug_f );
	Cmd_AddCommand( "memdump", Mem_Dump_f );
}

#endif
