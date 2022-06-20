extern __imp_KeAlertThread : QWORD
extern __imp_KeDelayExecutionThread : QWORD

.code

DelayThread proc
	push rbp
	mov rbp,rsp
	and rsp,NOT 15
	push rax
	push rcx
	push rdx
	push r8
	push r9
	push r10
	push r11
	sub rsp, 28h
	xor ecx,ecx
	lea edx,[ecx+1]
	lea r8,[rbp + 8]
	call __imp_KeDelayExecutionThread
	add rsp, 28h
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdx
	pop rcx
	pop rax
	leave
	add rsp,8
	ret 9*8
DelayThread endp

AlertThread proc
	push rbp
	mov rbp,rsp
	and rsp,NOT 15
	push rax
	push rcx
	push rdx
	push r8
	push r9
	push r10
	push r11
	sub rsp, 28h
	xor edx,edx
	mov rcx,[rbp + 8]
	call __imp_KeAlertThread
	add rsp, 28h
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdx
	pop rcx
	pop rax
	leave
	add rsp,8
	ret 9*8
AlertThread endp

end