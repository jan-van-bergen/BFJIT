extern _label

section .data
address dq 0x1234567887654321

section .text
	; init
	mov rsi, address
	mov r12, address
	mov r13, address
	sub rsp, 32
	add rsp, 32
	
	; > and <
	inc rsi
	dec rsi
	
	; + and -
	inc BYTE [rsi]
	dec BYTE [rsi]
	
	; .
	mov rcx, [rsi]
	call r12
	
	; ,
	call r13
	mov BYTE [rsi], al

	; [
jump_src:
	mov al, BYTE [rsi]
	test al, al
	jz jump_dst
		
	nop
	nop
	nop
	nop
		
	; ]
	jmp jump_src
jump_dst: