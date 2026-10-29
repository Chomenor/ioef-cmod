; Custom setjmp/longjmp implementation for MSVC x64 builds.
; Based on quake3e ; AI modified for better CET compatibility.

.code

; Emit CET instructions as bytes so older ml64 versions can assemble this file.

; Custom buffer layout (256 bytes, fits in the MSVC x64 jmp_buf):
;   0-79: RIP, RSP and nonvolatile general-purpose registers
;   80-87: post-return shadow stack pointer
;   88-91: MXCSR, 92-93: x87 control word, 94-95: padding
;   96-255: XMM6-XMM15
; This layout is private to Q_setjmp_c/Q_longjmp_c, not the CRT layout.

; params :  RCX - buffer (at least 256 bytes), RSP[0] = return address
Q_setjmp_c PROC
	; store RIP in jmp_buf[0]
	mov rax, [rsp+0]
	mov [rcx+ 0], rax
	; store RSP in jmp_buf[1]
	lea rax, [rsp+8]
	mov [rcx+ 8], rax
	; store post-return shadow stack pointer in jmp_buf[10]
	xor eax, eax
	db 0F3h, 048h, 00Fh, 01Eh, 0C8h ; rdsspq rax
	test rax, rax
	jz no_shadow_stack
	add rax, 8
no_shadow_stack:
	mov [rcx+80], rax
	; store remaining nonvolatile general-purpose registers
	mov [rcx+16], rbp
	mov [rcx+24], rbx
	mov [rcx+32], rsi
	mov [rcx+40], rdi
	mov [rcx+48], r12
	mov [rcx+56], r13
	mov [rcx+64], r14
	mov [rcx+72], r15
	; store floating-point control state
	stmxcsr dword ptr [rcx+88]
	fnstcw word ptr [rcx+92]
	; store nonvolatile SIMD registers; do not require buffer alignment
	movdqu xmmword ptr [rcx+ 96], xmm6
	movdqu xmmword ptr [rcx+112], xmm7
	movdqu xmmword ptr [rcx+128], xmm8
	movdqu xmmword ptr [rcx+144], xmm9
	movdqu xmmword ptr [rcx+160], xmm10
	movdqu xmmword ptr [rcx+176], xmm11
	movdqu xmmword ptr [rcx+192], xmm12
	movdqu xmmword ptr [rcx+208], xmm13
	movdqu xmmword ptr [rcx+224], xmm14
	movdqu xmmword ptr [rcx+240], xmm15
	; set normal return code (0)
	xor eax, eax
	ret
Q_setjmp_c ENDP


; params :  RCX - buffer saved by Q_setjmp_c, RDX - return code
Q_longjmp_c PROC
	; discard newer shadow stack entries before jumping back
	mov r11d, edx
	mov r8, [rcx+80]
	test r8, r8
	jz restore_context
	xor eax, eax
	db 0F3h, 048h, 00Fh, 01Eh, 0C8h ; rdsspq rax
	test rax, rax
	jz restore_context
	sub r8, rax
	jbe restore_context
	shr r8, 3
	jz restore_context
shadow_stack_loop:
	mov r10, r8
	cmp r10, 255
	jbe inc_shadow_stack
	mov r10, 255
inc_shadow_stack:
	db 0F3h, 049h, 00Fh, 0AEh, 0EAh ; incsspq r10
	sub r8, r10
	ja shadow_stack_loop

restore_context:
	; restore floating-point control state and nonvolatile SIMD registers
	ldmxcsr dword ptr [rcx+88]
	fldcw word ptr [rcx+92]
	movdqu xmm6, xmmword ptr [rcx+ 96]
	movdqu xmm7, xmmword ptr [rcx+112]
	movdqu xmm8, xmmword ptr [rcx+128]
	movdqu xmm9, xmmword ptr [rcx+144]
	movdqu xmm10, xmmword ptr [rcx+160]
	movdqu xmm11, xmmword ptr [rcx+176]
	movdqu xmm12, xmmword ptr [rcx+192]
	movdqu xmm13, xmmword ptr [rcx+208]
	movdqu xmm14, xmmword ptr [rcx+224]
	movdqu xmm15, xmmword ptr [rcx+240]
	; restore nonvolatile general-purpose registers
	mov r15, [rcx+72]
	mov r14, [rcx+64]
	mov r13, [rcx+56]
	mov r12, [rcx+48]
	mov rdi, [rcx+40]
	mov rsi, [rcx+32]
	mov rbx, [rcx+24]
	mov rbp, [rcx+16]
	mov rsp, [rcx+ 8]
	; set return code in EAX
	mov eax, r11d
	; jump to RIP
	jmp qword ptr [rcx+ 0]
Q_longjmp_c ENDP

END
