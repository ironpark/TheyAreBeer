; Chipmunk2DVulkan: second code pad (see DXPhysicsVulkan.c). The game writes at DLL base + 0x3AE30 (a .data variable of
; the original DLL's layout); in this DLL that offset is in .text, so this block of nops right after dx_text_pad
; (.text$a00) keeps every real function past it. scripts\build.ps1 checks the placement in the link map.
dxpad SEGMENT READ EXECUTE ALIGN(16) ALIAS(".text$a01") 'CODE'
PUBLIC dx_text_pad2
dx_text_pad2 PROC
	DB 3A000h DUP (90h)
	int 3
dx_text_pad2 ENDP
dxpad ENDS
END
