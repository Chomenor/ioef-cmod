/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2020 Noah Metzger (chomenor@gmail.com)

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

#ifdef CMOD_ENGINE_ASPECT_CORRECT
// Use local filesystem headers to support acessing pk3 hash to determine mod compatibility
#include "../filesystem/fslocal.h"
#include "../client/client.h"

static struct {
	qboolean uiEnabled;
	qboolean cgameEnabled;

	//
	// Everything below this point is set by AspectCorrect_UpdateValues
	//

	cvarHandle_t fovCvarHandle;
	cvarHandle_t gunZCvarHandle;

	float scaleFactor;

	float xCenterOffset;
	float yCenterOffset;

	float xStretchFactor;
	float yStretchFactor;
} asc;

typedef enum {
	SCALE_CENTER,
	SCALE_STRETCH,
	SCALE_AUTO		// stretch graphics that fill screen, center otherwise
} scaleMode_t;

int compatibleVMHashes[] = {
	1129759511,		// Team Elite (teamelite/teamelite.pk3)
	358480674,		// Nano EF (nanoef/znanoef.pk3)
	-526952486,		// TOS Weapons 2 (tw2/pak0.pk3)
	-534723038,		// Mario Mod 2 (marmod2/mariomod0.pk3)
	540461259,		// XMas Mod (xmasmod/xmas_pak0.pk3)
};

#define UI_ASPECT_CORRECT_ENABLED ( ui_aspectCorrect->integer >= 0 ? ui_aspectCorrect->integer : cg_aspectCorrect->integer )
#define CGAME_ASPECT_CORRECT_ENABLED ( clc.state >= CA_ACTIVE ? cg_aspectCorrect->integer : UI_ASPECT_CORRECT_ENABLED )
#define ASPECT_CORRECT_GUN_POS_ENABLED ( cg_aspectCorrectGunPos->integer >= 0 ? cg_aspectCorrectGunPos->integer : cg_aspectCorrect->integer )

// Should be same as default set in other places to avoid unwanted overrides
#define CG_FOV_DEFAULT "85*"

/*
================
AspectCorrect_UpdateValues
================
*/
static void AspectCorrect_UpdateValues( void ) {
	asc.xStretchFactor = cls.glconfig.vidWidth / 640.0f;
	asc.yStretchFactor = cls.glconfig.vidHeight / 480.0f;

	if ( cls.glconfig.vidWidth * 3 > cls.glconfig.vidHeight * 4 ) {
		asc.scaleFactor = asc.yStretchFactor;
		asc.xCenterOffset = ( cls.glconfig.vidWidth - 640.0f * asc.scaleFactor ) / 2.0f;
		asc.yCenterOffset = 0.0f;
	} else {
		asc.scaleFactor = asc.xStretchFactor;
		asc.xCenterOffset = 0.0f;
		asc.yCenterOffset = ( cls.glconfig.vidHeight - 480.0f * asc.scaleFactor ) / 2.0f;
	}

	{
		vmCvar_t temp;
		Cvar_Register( &temp, "cg_fov", CG_FOV_DEFAULT, 0 );
		asc.fovCvarHandle = temp.handle;
		Cvar_Register( &temp, "cg_gunZ", "0", 0 );
		asc.gunZCvarHandle = temp.handle;
	}
}

/*
================
AspectCorrect_VMFromCompatibleMod
================
*/
qboolean AspectCorrect_VMFromCompatibleMod( const char *module, const fsc_file_t *sourceFile ) {
	if ( sourceFile->sourcetype == FSC_SOURCETYPE_PK3 ) {
		unsigned int pk3Hash = fsc_get_base_file( sourceFile, &fs )->pk3_hash;
		int i;

		for ( i = 0; i < ARRAY_LEN( compatibleVMHashes ); ++i ) {
			if ( (unsigned int)compatibleVMHashes[i] == pk3Hash ) {
				return qtrue;
			}
		}
	}

	return qfalse;
}

/*
================
AspectCorrect_OnVmCreate

Called when a VM is about to be instantiated. sourceFile may be null in error cases.
================
*/
void AspectCorrect_OnVmCreate( const char *module, const fsc_file_t *sourceFile ) {
	qboolean *enabled;

	if ( !Q_stricmp( module, "cgame" ) ) {
		enabled = &asc.cgameEnabled;
	} else if ( !Q_stricmp( module, "ui" ) ) {
		enabled = &asc.uiEnabled;
	} else {
		return;
	}

	if ( cl_engineAspectCorrect->integer >= 2 ||
			( cl_engineAspectCorrect->integer && AspectCorrect_VMFromCompatibleMod( module, sourceFile ) ) ) {
		*enabled = qtrue;
		Com_Printf( "Enabling engine aspect scaling support for module %s\n", module );
		AspectCorrect_UpdateValues();
	} else {
		*enabled = qfalse;
	}
}

/*
================
AspectCorrect_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
static void AspectCorrect_AdjustFrom640( float *x, float *y, float *w, float *h, scaleMode_t mode ) {
	AspectCorrect_UpdateValues();

	if ( mode == SCALE_STRETCH || ( mode == SCALE_AUTO && *w == 640.0f ) ) {
		*x *= asc.xStretchFactor;
		*w *= asc.xStretchFactor;
	} else {
		*x = *x * asc.scaleFactor + asc.xCenterOffset;
		*w *= asc.scaleFactor;
	}

	if ( mode == SCALE_STRETCH || ( mode == SCALE_AUTO && *h == 480.0f ) ) {
		*y *= asc.yStretchFactor;
		*h *= asc.yStretchFactor;
	} else {
		*y = *y * asc.scaleFactor + asc.yCenterOffset;
		*h *= asc.scaleFactor;
	}
}

/*
================
AspectCorrect_AdjustFrom640Int

Adjusted for resolution and screen aspect ratio
================
*/
static void AspectCorrect_AdjustFrom640Int( int *x, int *y, int *w, int *h, scaleMode_t mode ) {
	float fx = *x, fy = *y, fw = *w, fh = *h;
	AspectCorrect_AdjustFrom640( &fx, &fy, &fw, &fh, mode );
	*x = fx;
	*y = fy;
	*w = fw;
	*h = fh;
}

/*
================
AspectCorrect_DrawStretchPic
================
*/
static void AspectCorrect_DrawStretchPic( float x, float y, float w, float h,
		float s1, float t1, float s2, float t2, qhandle_t hShader, scaleMode_t mode ) {
	AspectCorrect_AdjustFrom640( &x, &y, &w, &h, mode );
	re.DrawStretchPic( x, y, w, h, s1, t1, s2, t2, hShader );
}

/*
================
AspectCorrect_RenderScene
================
*/
static void AspectCorrect_RenderScene( const refdef_t *fd, scaleMode_t mode ) {
	refdef_t newfd = *fd;
	AspectCorrect_AdjustFrom640Int( &newfd.x, &newfd.y, &newfd.width, &newfd.height, mode );

	if ( fd->width == 640 && fd->height == 480 ) {
		// First extrapolate game's fov_y value into fov_x
		float x = 480.0 / tan( newfd.fov_y / 360 * M_PI );
		float fov_x = atan2( 640.0, x );
		fov_x = fov_x * 360 / M_PI;

		// Convert to real screen fov_y value
		x = cls.glconfig.vidWidth / tan( fov_x / 360 * M_PI );
		newfd.fov_y = atan2( cls.glconfig.vidHeight, x );
		newfd.fov_y = newfd.fov_y * 360 / M_PI;
	}

	re.RenderScene( &newfd );
}

/*
================
AspectCorrect_GetFovSetting

Get the current cg_fov cvar value, adjusting for the trailing '*' scaling specifier if needed.
================
*/
static float AspectCorrect_GetFovSetting( void ) {
	float fov;
	static cvar_t *cvar_cg_fov;
	if ( !cvar_cg_fov ) {
		cvar_cg_fov = Cvar_Get( "cg_fov", "85*", 0 );
	}

	if ( strchr( cvar_cg_fov->string, '*' ) ) {
		// Calculate fov_y based on fov_x at theoretical 640x480 resolution
		float x = 640.0 / tan( cvar_cg_fov->value / 360 * M_PI );
		float fov_y = atan2( 480.0, x );
		fov_y = fov_y * 360 / M_PI;

		// Recalculate fov_x based on fov_y
		x = cls.glconfig.vidHeight / tan( fov_y / 360 * M_PI );
		fov = atan2( cls.glconfig.vidWidth, x );
		fov = fov * 360 / M_PI;
	} else {
		fov = cvar_cg_fov->value;
	}

	if ( fov < 1.0f )
		return 1.0f;
	if ( fov > 120.0f )
		return 120.0f;
	return fov;
}

// Original game calculations to determine gun Z offset from fov
#define FOV_OFFSET( fov ) ( fov > 80.0f ? -0.2f * ( fov - 80.0f ) : 0 )

/*
================
AspectCorrect_GetGunAdjust

Get the cg_gunZ correction value to patch gun location.
================
*/
static float AspectCorrect_GetGunAdjust( void ) {
	float fov = AspectCorrect_GetFovSetting();
	float adjustedFov;

	// calculate vertical fov
	float x = cls.glconfig.vidWidth / tan( fov / 360 * M_PI );
	float fov_y = atan2( cls.glconfig.vidHeight, x );
	fov_y = fov_y * 360 / M_PI;

	// convert it to horizontal fov at theoretical 4:3 aspect ratio
	x = 480.0 / tan( fov_y / 360 * M_PI );
	adjustedFov = atan2( 640.0, x );
	adjustedFov = adjustedFov * 360 / M_PI;

	// calculate gun adjustment we want to use, and subtract the original adjustment done by cgame
	return Cvar_VariableValue( "cg_gunZ" ) + FOV_OFFSET( adjustedFov ) - FOV_OFFSET( fov );
}

/*
================
AspectCorrect_OverrideVMCvar

Hack to force a certain value for a cvar in VM.
================
*/
static void AspectCorrect_OverrideVMCvar( vmCvar_t *cvar, float value ) {
	cvar->value = value;
	cvar->integer = value;
	Com_sprintf( cvar->string, sizeof( cvar->string ), "%f", value );
	cvar->modificationCount = -( ( value + 1000 ) * 1000 );
}

/*
================
AspectCorrect_OnCgameSyscall

Returns qtrue to abort normal handling of syscall. retval specifies value to return to VM.
================
*/
qboolean AspectCorrect_OnCgameSyscall( intptr_t *args, intptr_t *retval ) {
	if ( asc.cgameEnabled ) {
		*retval = 0;

		switch( args[0] ) {
			case CG_GETGLCONFIG:
			{
				glconfig_t *glconfig = VMA(1);
				*glconfig = cls.glconfig;
				glconfig->vidWidth = 640;
				glconfig->vidHeight = 480;
				return qtrue;
			}

			case CG_R_RENDERSCENE:
				AspectCorrect_RenderScene( VMA(1), CGAME_ASPECT_CORRECT_ENABLED ? SCALE_AUTO : SCALE_STRETCH );
				return qtrue;

			case CG_R_DRAWSTRETCHPIC:
				AspectCorrect_DrawStretchPic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9],
						CGAME_ASPECT_CORRECT_ENABLED ? SCALE_AUTO : SCALE_STRETCH );
				return qtrue;

			case CG_CVAR_UPDATE:
			{
				vmCvar_t *cvar = VMA(1);
				if ( cvar->handle == asc.fovCvarHandle ) {
					AspectCorrect_OverrideVMCvar( cvar, AspectCorrect_GetFovSetting() );
					return qtrue;
				}
				if ( cvar->handle == asc.gunZCvarHandle && ASPECT_CORRECT_GUN_POS_ENABLED ) {
					AspectCorrect_OverrideVMCvar( cvar, AspectCorrect_GetGunAdjust() );
					return qtrue;
				}
				break;
			}
		}
	}

	return qfalse;
}

/*
================
AspectCorrect_OnUISyscall

Returns qtrue to abort normal handling of syscall. retval specifies value to return to VM.
================
*/
qboolean AspectCorrect_OnUISyscall( intptr_t *args, intptr_t *retval ) {
	if ( asc.uiEnabled ) {
		*retval = 0;

		switch( args[0] ) {
			case UI_GETGLCONFIG:
			{
				glconfig_t *glconfig = VMA(1);
				*glconfig = cls.glconfig;
				glconfig->vidWidth = 640;
				glconfig->vidHeight = 480;
				return qtrue;
			}

			case UI_R_RENDERSCENE:
				AspectCorrect_RenderScene( VMA(1), UI_ASPECT_CORRECT_ENABLED ? SCALE_CENTER : SCALE_STRETCH );
				return qtrue;

			case UI_R_DRAWSTRETCHPIC:
				AspectCorrect_DrawStretchPic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9],
						UI_ASPECT_CORRECT_ENABLED ? SCALE_AUTO : SCALE_STRETCH );
				return qtrue;
		}
	}

	return qfalse;
}

#endif
