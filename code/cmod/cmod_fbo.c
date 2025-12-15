/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006 Kirk Barnes
Copyright (C) 2006-2008 Robert Beckebans <trebor_7@users.sourceforge.net>
Copyright (C) 2016 James Canete
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

// Rudimentary framebuffer gamma support that works on at least some systems
// Based on opengl-tutorial.org/intermediate-tutorials/tutorial-14-render-to-texture
// and ioq3 opengl2 renderer

#ifdef CMOD_FRAMEBUFFER
#include "../renderergl1/tr_local.h"
#include "SDL.h"

/* ******************************************************************************** */
// Framebuffer State
/* ******************************************************************************** */

static struct {
	GLuint render_texture;
	GLuint gamma_program;
	GLint texture_uniform;
	GLint gamma_uniform;
	GLuint quad_vertexbuffer;

	GLuint draw_framebuffer;
	GLuint draw_renderbuffer_depth;
	GLuint draw_renderbuffer_color;

	GLuint resolve_framebuffer;
	GLuint resolve_renderbuffer_depth;
} fbo_state;

/* ******************************************************************************** */
// GL Functions
/* ******************************************************************************** */

// Using extern because these function pointers are all already defined in sdl_glimp.c
#define GLE(ret, name, ...) extern name##proc * qgl##name;
QGL_1_5_PROCS;
QGL_2_0_PROCS;
QGL_ARB_framebuffer_object_PROCS;
QGL_EXT_direct_state_access_PROCS;
#undef GLE

static struct
{
	GLuint drawFramebuffer;
	GLuint readFramebuffer;
	GLuint renderbuffer;
}
glDsaState;

static void GL_BindFramebuffer(GLenum target, GLuint framebuffer)
{
	switch (target)
	{
		case GL_FRAMEBUFFER_EXT:
			if (framebuffer != glDsaState.drawFramebuffer || framebuffer != glDsaState.readFramebuffer)
			{
				qglBindFramebuffer(target, framebuffer);
				glDsaState.drawFramebuffer = glDsaState.readFramebuffer = framebuffer;
			}
			break;

		case GL_DRAW_FRAMEBUFFER_EXT:
			if (framebuffer != glDsaState.drawFramebuffer)
			{
				qglBindFramebuffer(target, framebuffer);
				glDsaState.drawFramebuffer = framebuffer;
			}
			break;

		case GL_READ_FRAMEBUFFER_EXT:
			if (framebuffer != glDsaState.readFramebuffer)
			{
				qglBindFramebuffer(target, framebuffer);
				glDsaState.readFramebuffer = framebuffer;
			}
			break;
	}
}

static GLenum APIENTRY GLDSA_CheckNamedFramebufferStatusEXT(GLuint framebuffer, GLenum target)
{
	GL_BindFramebuffer(target, framebuffer);
	return qglCheckFramebufferStatus(target);
}

static GLvoid APIENTRY GLDSA_NamedFramebufferRenderbufferEXT(GLuint framebuffer,
	GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
	GL_BindFramebuffer(GL_FRAMEBUFFER_EXT, framebuffer);
	qglFramebufferRenderbuffer(GL_FRAMEBUFFER_EXT, attachment, renderbuffertarget, renderbuffer);
}

static GLvoid APIENTRY GLDSA_NamedRenderbufferStorageEXT(GLuint renderbuffer,
	GLenum internalformat, GLsizei width, GLsizei height)
{
	if(renderbuffer != glDsaState.renderbuffer) {
		qglBindRenderbuffer(GL_RENDERBUFFER_EXT, renderbuffer);
		glDsaState.renderbuffer = renderbuffer; }
	qglRenderbufferStorage(GL_RENDERBUFFER_EXT, internalformat, width, height);
}

static GLvoid APIENTRY GLDSA_NamedRenderbufferStorageMultisampleEXT(GLuint renderbuffer,
	GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)
{
	if(renderbuffer != glDsaState.renderbuffer) {
		qglBindRenderbuffer(GL_RENDERBUFFER_EXT, renderbuffer);
		glDsaState.renderbuffer = renderbuffer; }
	qglRenderbufferStorageMultisample(GL_RENDERBUFFER_EXT, samples, internalformat, width, height);
}

static void fbo_gls_init(void) {
	#define GLE(ret, name, ...) qgl##name = (name##proc *) SDL_GL_GetProcAddress("gl" #name);
	QGL_1_5_PROCS;
	QGL_2_0_PROCS;
	QGL_ARB_framebuffer_object_PROCS;
	//QGL_EXT_direct_state_access_PROCS;
	#undef GLE

	qglCheckNamedFramebufferStatusEXT = GLDSA_CheckNamedFramebufferStatusEXT;
	qglNamedFramebufferRenderbufferEXT = GLDSA_NamedFramebufferRenderbufferEXT;
	qglNamedRenderbufferStorageEXT = GLDSA_NamedRenderbufferStorageEXT;
	qglNamedRenderbufferStorageMultisampleEXT = GLDSA_NamedRenderbufferStorageMultisampleEXT; }

/* ******************************************************************************** */
// Shutdown
/* ******************************************************************************** */

static void free_render_texture(GLuint texture) {
	qglDeleteTextures(1, &texture);
	if(glState.currenttextures[glState.currenttmu] == (int)texture) {
		glState.currenttextures[glState.currenttmu] = 0; } }

void framebuffer_shutdown(void) {
	GL_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);

	if(fbo_state.render_texture) free_render_texture(fbo_state.render_texture);
	if(fbo_state.gamma_program) qglDeleteProgram(fbo_state.gamma_program);
	if(fbo_state.quad_vertexbuffer) qglDeleteBuffers(1, &fbo_state.quad_vertexbuffer);
	if(fbo_state.draw_renderbuffer_depth) qglDeleteRenderbuffers(1, &fbo_state.draw_renderbuffer_depth);
	if(fbo_state.draw_renderbuffer_color) qglDeleteRenderbuffers(1, &fbo_state.draw_renderbuffer_color);
	if(fbo_state.draw_framebuffer) qglDeleteFramebuffers(1, &fbo_state.draw_framebuffer);
	if(fbo_state.resolve_framebuffer) qglDeleteFramebuffers(1, &fbo_state.resolve_framebuffer);
	if(fbo_state.resolve_renderbuffer_depth) qglDeleteRenderbuffers(1, &fbo_state.resolve_renderbuffer_depth);

	Com_Memset(&fbo_state, 0, sizeof(fbo_state));
	Com_Memset(&glDsaState, 0, sizeof(glDsaState));
	tr.framebuffer_active = qfalse; }

/* ******************************************************************************** */
// Initialization
/* ******************************************************************************** */

/* *** Render Texture *** */

#define RENDER_TEXTURE_ID 24

static void bind_render_texture(GLuint texture) {
	if ( glState.currenttextures[glState.currenttmu] != (int)texture ) {
		glState.currenttextures[glState.currenttmu] = (int)texture;
		qglBindTexture(GL_TEXTURE_2D, texture); } }

static void attach_render_texture_to_fbo(GLenum fbo, GLuint texture) {
	GL_BindFramebuffer(GL_FRAMEBUFFER_EXT, fbo);
	qglFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, texture, 0); }

static GLuint create_render_texture(void) {
	bind_render_texture(RENDER_TEXTURE_ID);
	qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, glConfig.vidWidth, glConfig.vidHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	// Set all necessary texture parameters.
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//if (textureFilterAnisotropic)
	//	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);

	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	return RENDER_TEXTURE_ID; }

/* *** Gamma Program *** */

static GLuint glsl_create_compiled_shader(const GLchar *buffer, int size, GLenum shaderType) {
	// Returns shader index on success, 0 on error
	GLint compiled = 0;
	GLuint shader = qglCreateShader(shaderType);
	if(!shader) return 0;
	qglShaderSource(shader, 1, (const GLchar **)&buffer, &size);
	qglCompileShader(shader);
	qglGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if(!compiled) {
		char errorBuffer[4096];
		qglGetShaderInfoLog(shader, sizeof(errorBuffer), NULL, errorBuffer);
		Com_Printf("glsl_create_compiled_shader: compilation error - %s\n", errorBuffer);
		qglDeleteShader(shader);
		return 0; }
	return shader; }

static GLuint glsl_link_program(GLint shader1, GLint shader2) {
	// Returns program index on success, 0 on error
	GLint result = 0;
	GLuint program = qglCreateProgram();
	if(!program) return 0;

	qglAttachShader(program, shader1);
	qglAttachShader(program, shader2);
	qglBindAttribLocation(program, 0, "vertexPosition_modelspace");
	qglLinkProgram(program);
	qglDetachShader(program, shader1);
	qglDetachShader(program, shader2);

	qglGetProgramiv(program, GL_LINK_STATUS, &result);
	if(!result) {
		qglDeleteProgram(program);
		return 0; }

	return program; }

static GLuint glsl_create_gamma_program(void) {
	// Returns program index on success, 0 on error
	float overBrightFactor = r_overBrightFactor->value;
	GLuint vertex_shader = 0;
	GLuint fragment_shader = 0;
	GLuint gamma_program = 0;
	const char vertex_data[] = "#version 120\n"
		"attribute vec3 vertexPosition_modelspace;\n"
		"varying vec2 UV;\n"
		"void main(){\n"
		"   gl_Position = vec4(vertexPosition_modelspace,1);\n"
		"   UV = (vertexPosition_modelspace.xy+vec2(1,1))/2.0;\n"
		"}\n";
	char fragment_data[1000];
#ifdef CMOD_MAP_AUTO_ADJUST
	if ( r_autoOverBrightFactorShift->value < 0.0f && overBrightFactor > 1.0f )
		overBrightFactor = MAX( 1.0f, overBrightFactor + r_autoOverBrightFactorShift->value );
	if ( r_autoOverBrightFactorShift->value > 0.0f && overBrightFactor < 2.0f )
		overBrightFactor = MIN( 2.0f, overBrightFactor + r_autoOverBrightFactorShift->value );
	if ( r_autoOverBrightFactorMax->value > 0.0f && r_autoOverBrightFactorMax->value < overBrightFactor )
		overBrightFactor = r_autoOverBrightFactorMax->value;
#endif
	Com_sprintf(fragment_data, sizeof(fragment_data), "#version 120\n"
		"varying vec2 UV;\n"
		"uniform sampler2D renderedTexture;\n"
		"uniform float gamma;\n"
		"void main()\n"
		"{\n"
		"   vec3 color = texture2D( renderedTexture, UV  ).xyz;\n"
		"   color.rgb = pow(color.rgb, vec3(gamma)) * %f;\n"
		"   gl_FragColor = vec4(color, 1.0);\n"
		"}\n", overBrightFactor);

	// Compile shaders
	vertex_shader = glsl_create_compiled_shader(vertex_data, sizeof(vertex_data), GL_VERTEX_SHADER);
	if(!vertex_shader) {
		Com_Printf("glsl_create_gamma_program: failed to compile vertex shader\n");
		goto end; }
	fragment_shader = glsl_create_compiled_shader(fragment_data, sizeof(fragment_data), GL_FRAGMENT_SHADER);
	if(!fragment_shader) {
		Com_Printf("glsl_create_gamma_program: failed to compile fragment shader\n");
		goto end; }
	//Com_Printf("vertex_shader(%i) fragment_shader(%i)\n", vertex_shader, fragment_shader);

	// Link program
	gamma_program = glsl_link_program(vertex_shader, fragment_shader);
	if(!gamma_program) {
		Com_Printf("glsl_create_gamma_program: failed to link gamma program\n");
		goto end; }
	//Com_Printf("gamma_program(%i)\n", gamma_program);

	end:
	if(vertex_shader) qglDeleteShader(vertex_shader);
	if(fragment_shader) qglDeleteShader(fragment_shader);
	return gamma_program; }

/* *** Quad Vertexbuffer *** */

static GLuint create_quad_vertexbuffer(void) {
	GLuint quad_vertexbuffer = 0;
	static const GLfloat g_quad_vertex_buffer_data[] = {
		-1.0f, -1.0f, 0.0f,
		 1.0f, -1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f,
		 1.0f, -1.0f, 0.0f,
		 1.0f,  1.0f, 0.0f };

	qglGenBuffers(1, &quad_vertexbuffer);
	qglBindBuffer(GL_ARRAY_BUFFER, quad_vertexbuffer);
	qglBufferData(GL_ARRAY_BUFFER, sizeof(g_quad_vertex_buffer_data), g_quad_vertex_buffer_data, GL_STATIC_DRAW);
	qglBindBuffer(GL_ARRAY_BUFFER, 0);
	return quad_vertexbuffer; }

/* *** Main Initialization *** */

static void framebuffer_init2(void) {
	// Sets tr.framebuffer_active to qtrue on success
	GLenum code = 0;
	int maxRenderbufferSize = 0;

	if(!r_framebuffer->integer) return;

	// stereo rendering currently not supported
	if(glConfig.stereoEnabled || r_anaglyphMode->integer) return;

	if(!QGL_VERSION_ATLEAST(3, 0) && !SDL_GL_ExtensionSupported("GL_ARB_framebuffer_object")) {
		Com_Printf("fbo_init failed due to missing GL_ARB_framebuffer_object\n");
		return; }

	fbo_gls_init();

	qglGetIntegerv(GL_MAX_RENDERBUFFER_SIZE_EXT, &maxRenderbufferSize);
	if(glConfig.vidWidth > maxRenderbufferSize || glConfig.vidHeight > maxRenderbufferSize) {
		Com_Printf("fbo_init failed to verify renderbuffer max size\n");
		return; }

	fbo_state.gamma_program = glsl_create_gamma_program();
	if(!fbo_state.gamma_program) {
		Com_Printf("fbo_init failed to create gamma program\n");
		return; }
	fbo_state.texture_uniform = qglGetUniformLocation(fbo_state.gamma_program, "renderedTexture");
	if(fbo_state.texture_uniform < 0) {
		fbo_state.texture_uniform = 0;
		Com_Printf("fbo_init failed to create texture uniform\n");
		return; }
	fbo_state.gamma_uniform = qglGetUniformLocation(fbo_state.gamma_program, "gamma");

	fbo_state.quad_vertexbuffer = create_quad_vertexbuffer();
	if(!fbo_state.quad_vertexbuffer) {
		Com_Printf("fbo_init failed to create quad vertexbuffer\n");
		return; }

	// Based on FBO_Create
	qglGenFramebuffers(1, &fbo_state.draw_framebuffer);
	if(!fbo_state.draw_framebuffer) {
		Com_Printf("fbo_init failed to create draw framebuffer\n");
		return; }

	fbo_state.render_texture = create_render_texture();

	if(r_ext_multisample->integer) {
		qglGenRenderbuffers(1, &fbo_state.draw_renderbuffer_color);
		qglNamedRenderbufferStorageMultisampleEXT(fbo_state.draw_renderbuffer_color, r_ext_multisample->integer,
				GL_RGBA8, glConfig.vidWidth, glConfig.vidHeight);
		qglNamedFramebufferRenderbufferEXT(fbo_state.draw_framebuffer, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, fbo_state.draw_renderbuffer_color);

		qglGenRenderbuffers(1, &fbo_state.draw_renderbuffer_depth);
		qglNamedRenderbufferStorageMultisampleEXT(fbo_state.draw_renderbuffer_depth, r_ext_multisample->integer,
				GL_DEPTH_COMPONENT24_ARB, glConfig.vidWidth, glConfig.vidHeight);
		qglNamedFramebufferRenderbufferEXT(fbo_state.draw_framebuffer, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, fbo_state.draw_renderbuffer_depth);

		qglGenFramebuffers(1, &fbo_state.resolve_framebuffer);

		qglGenRenderbuffers(1, &fbo_state.resolve_renderbuffer_depth);
		qglNamedRenderbufferStorageEXT(fbo_state.resolve_renderbuffer_depth, GL_DEPTH_COMPONENT24_ARB, glConfig.vidWidth, glConfig.vidHeight);
		qglNamedFramebufferRenderbufferEXT(fbo_state.resolve_framebuffer, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, fbo_state.resolve_renderbuffer_depth);

		attach_render_texture_to_fbo(fbo_state.resolve_framebuffer, fbo_state.render_texture); }
	else {
		qglGenRenderbuffers(1, &fbo_state.draw_renderbuffer_depth);
		qglNamedRenderbufferStorageEXT(fbo_state.draw_renderbuffer_depth, GL_DEPTH_COMPONENT24_ARB, glConfig.vidWidth, glConfig.vidHeight);
		qglNamedFramebufferRenderbufferEXT(fbo_state.draw_framebuffer, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, fbo_state.draw_renderbuffer_depth);

		attach_render_texture_to_fbo(fbo_state.draw_framebuffer, fbo_state.render_texture); }

	// Based on R_CheckFBO
	code = qglCheckNamedFramebufferStatusEXT(fbo_state.draw_framebuffer, GL_FRAMEBUFFER_EXT);
	if(code != GL_FRAMEBUFFER_COMPLETE_EXT) {
		Com_Printf("fbo_init failed due to draw framebuffer status code 0x%X\n", code);
		return; }
	if(r_ext_multisample->integer) {
		code = qglCheckNamedFramebufferStatusEXT(fbo_state.resolve_framebuffer, GL_FRAMEBUFFER_EXT);
		if(code != GL_FRAMEBUFFER_COMPLETE_EXT) {
			Com_Printf("fbo_init failed due to resolve framebuffer status code 0x%X\n", code);
			return; } }

	// clear render buffer
	GL_BindFramebuffer(GL_FRAMEBUFFER_EXT, fbo_state.draw_framebuffer);
	qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	GL_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
	tr.framebuffer_active = qtrue; }

void framebuffer_init(void) {
	framebuffer_init2();
	if(!tr.framebuffer_active) framebuffer_shutdown(); }

/* ******************************************************************************** */
// Rendering
/* ******************************************************************************** */

static void glsl_render(void) {
	float gamma = r_gamma->value;
	if(gamma < 0.5f) gamma = 0.5f;
	else if(gamma > 3.0f) gamma = 3.0f;

	GL_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
	qglViewport(0,0,glConfig.vidWidth,glConfig.vidHeight);

	qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	GL_Cull( CT_TWO_SIDED );
	GL_State(GLS_DEPTHTEST_DISABLE);

	qglUseProgram(fbo_state.gamma_program);

	//qglActiveTextureARB(GL_TEXTURE0);
	GL_SelectTexture(0);
	bind_render_texture(fbo_state.render_texture);
	qglUniform1i(fbo_state.texture_uniform, 0);
	qglUniform1f(fbo_state.gamma_uniform, 1.0f / gamma);

	// 1rst attribute buffer : vertices
	qglEnableVertexAttribArray(0);
	qglBindBuffer(GL_ARRAY_BUFFER, fbo_state.quad_vertexbuffer);

	qglVertexAttribPointer(
		0,                  // attribute 0. No particular reason for 0, but must match the layout in the shader.
		3,                  // size
		GL_FLOAT,           // type
		GL_FALSE,           // normalized?
		0,                  // stride
		(void*)0            // array buffer offset
	);

	// Draw the triangles !
	qglDrawArrays(GL_TRIANGLES, 0, 6); // 2*3 indices starting at 0 -> 2 triangles

	qglBindBuffer(GL_ARRAY_BUFFER, 0);
	qglDisableVertexAttribArray(0);

	qglBindTexture(GL_TEXTURE_2D, 0);
	qglUseProgram(0);

	GL_Cull( CT_FRONT_SIDED ); }

void framebuffer_render(void) {
	if(tr.framebuffer_active) {
		if(r_ext_multisample->integer) {
			GL_BindFramebuffer(GL_READ_FRAMEBUFFER_EXT, fbo_state.draw_framebuffer);
			GL_BindFramebuffer(GL_DRAW_FRAMEBUFFER_EXT, fbo_state.resolve_framebuffer);
			qglBlitFramebuffer(0, 0, glConfig.vidWidth, glConfig.vidHeight,
								  0, 0, glConfig.vidWidth, glConfig.vidHeight,
								  GL_COLOR_BUFFER_BIT, GL_NEAREST); }
		glsl_render(); } }

void framebuffer_setup_depth_test(void) {
	// used for flares
	if(tr.framebuffer_active && r_ext_multisample->integer) {
		GL_BindFramebuffer(GL_READ_FRAMEBUFFER_EXT, fbo_state.draw_framebuffer);
		GL_BindFramebuffer(GL_DRAW_FRAMEBUFFER_EXT, fbo_state.resolve_framebuffer);
		qglBlitFramebuffer(0, 0, glConfig.vidWidth, glConfig.vidHeight,
								0, 0, glConfig.vidWidth, glConfig.vidHeight,
								GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		GL_BindFramebuffer(GL_READ_FRAMEBUFFER_EXT, fbo_state.resolve_framebuffer);
	}
}

void framebuffer_bind(void) {
	if(tr.framebuffer_active) GL_BindFramebuffer(GL_FRAMEBUFFER_EXT, fbo_state.draw_framebuffer); }

void framebuffer_unbind(void) {
	if(tr.framebuffer_active) GL_BindFramebuffer(GL_FRAMEBUFFER_EXT, 0); }

void framebuffer_test(void) {
	if(tr.framebuffer_active) framebuffer_shutdown();
	else framebuffer_init(); }

#endif	// CMOD_FRAMEBUFFER
