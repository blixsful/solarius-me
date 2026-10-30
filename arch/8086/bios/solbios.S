.code16
.intel_syntax noprefix
.text
.global _start
_start:

.equ TBL, 0x04A0
.equ SCRATCH, 0x04B0
.equ VIDEO, 0xB800

.macro setvec vec, handler
    mov word ptr ds:[(\vec)*4], offset \handler
    mov word ptr ds:[(\vec)*4+2], 0xF000
.endm

.macro dbg ch
    mov al, \ch
    out 0xE9, al
.endm

.macro retf2
    .byte 0xCA, 0x02, 0x00
.endm

post:
    cli
    cld
    xor ax, ax
    mov ss, ax
    mov sp, 0x7000
    mov ds, ax
    mov es, ax
    dbg 'B'

    mov di, 0x0400
    mov cx, 0x0080
    rep stosw

    mov al, 0x11
    out 0x20, al
    mov al, 0x08
    out 0x21, al
    mov al, 0x04
    out 0x21, al
    mov al, 0x01
    out 0x21, al
    mov al, 0xF8
    out 0x21, al
    mov al, 0x11
    out 0xA0, al
    mov al, 0x70
    out 0xA1, al
    mov al, 0x02
    out 0xA1, al
    mov al, 0x01
    out 0xA1, al
    mov al, 0xFF
    out 0xA1, al

    mov al, 0x36
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al

    xor di, di
    mov cx, 256
fill_ivt:
    mov ax, offset int_dummy
    stosw
    mov ax, 0xF000
    stosw
    loop fill_ivt

    xor ax, ax
    mov ds, ax
    setvec 0x08, int08
    setvec 0x09, int09
    setvec 0x10, int10
    setvec 0x11, int11
    setvec 0x12, int12
    setvec 0x13, int13
    setvec 0x15, int15
    setvec 0x16, int16
    setvec 0x19, int19
    setvec 0x1A, int1a
    setvec 0x70, int70
    mov word ptr ds:[0x41*4], TBL
    mov word ptr ds:[0x41*4+2], 0

    mov word ptr ds:[0x410], 0x0020
    mov al, 0x15
    out 0x70, al
    in al, 0x71
    mov ah, al
    mov al, 0x16
    out 0x70, al
    in al, 0x71
    xchg al, ah
    xchg al, ah
    mov bl, al
    mov al, 0x15
    out 0x70, al
    in al, 0x71
    mov ah, bl
    mov word ptr ds:[0x413], ax
    mov byte ptr ds:[0x449], 3
    mov word ptr ds:[0x44A], 80
    mov word ptr ds:[0x44C], 0x1000
    mov word ptr ds:[0x460], 0x0607
    mov word ptr ds:[0x463], 0x03D4
    mov byte ptr ds:[0x465], 0x29
    mov byte ptr ds:[0x466], 0x30
    mov byte ptr ds:[0x484], 24
    mov byte ptr ds:[0x485], 16
    mov word ptr ds:[0x41A], 0x1E
    mov word ptr ds:[0x41C], 0x1E
    mov word ptr ds:[0x480], 0x1E
    mov word ptr ds:[0x482], 0x3E

    mov dx, 0x3F6
    mov al, 0x0A
    out dx, al
    mov dx, 0x1F6
    mov al, 0xA0
    out dx, al
    mov dx, 0x1F7
    in al, dx
    test al, al
    jnz skip0
    jmp no_disk
skip0:
    cmp al, 0xFF
    jne skip1
    jmp no_disk
skip1:
    mov cx, 0xFFFF
wait_idle:
    in al, dx
    test al, 0x80
    jz idle_ok
    loop wait_idle
    jmp no_disk
idle_ok:
    mov al, 0xEC
    out dx, al
    mov cx, 0xFFFF
wait_drq:
    in al, dx
    test al, 0x08
    jnz drq_ok
    loop wait_drq
    jmp no_disk
drq_ok:
    mov dx, 0x1F0
    mov di, 0x0800
    mov cx, 256
read_ident:
    in ax, dx
    stosw
    loop read_ident
    dbg 'I'

    mov ax, ds:[0x0802]
    mov ds:[TBL], ax
    mov ds:[TBL+12], ax
    mov ax, ds:[0x0806]
    mov ds:[TBL+2], al
    mov word ptr ds:[TBL+5], 0xFFFF
    mov byte ptr ds:[TBL+8], 0xC8
    mov ax, ds:[0x080C]
    mov ds:[TBL+14], al
    mov ax, ds:[0x0800+120]
    mov ds:[SCRATCH+8], ax
    mov ax, ds:[0x0800+122]
    mov ds:[SCRATCH+10], ax

    mov dx, 0x1F2
    mov al, ds:[TBL+14]
    out dx, al
    mov dx, 0x1F6
    mov al, ds:[TBL+2]
    dec al
    and al, 0x0F
    or al, 0xA0
    out dx, al
    mov dx, 0x1F7
    mov al, 0x91
    out dx, al
    mov cx, 0xFFFF
wait_init:
    in al, dx
    test al, 0x80
    jz init_done
    loop wait_init
init_done:
    mov byte ptr ds:[0x475], 1

no_disk:
    mov ax, 0x0003
    int 0x10
    sti
    dbg 'S'
    int 0x19
halt_forever:
    hlt
    jmp halt_forever

int_dummy:
    iret

int70:
    push ax
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    pop ax
    iret

int08:
    push ds
    push ax
    xor ax, ax
    mov ds, ax
    add word ptr ds:[0x46C], 1
    adc word ptr ds:[0x46E], 0
    int 0x1C
    mov al, 0x20
    out 0x20, al
    pop ax
    pop ds
    iret

int11:
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, ds:[0x410]
    pop ds
    iret

int12:
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, ds:[0x413]
    pop ds
    iret

int15:
    cmp ah, 0x88
    je i15_ext
    mov ah, 0x86
    stc
    retf2
i15_ext:
    xor ax, ax
    clc
    retf2

int1a:
    or ah, ah
    jnz i1a_unsup
    push ds
    xor ax, ax
    mov ds, ax
    mov dx, ds:[0x46C]
    mov cx, ds:[0x46E]
    xor ax, ax
    pop ds
    clc
    retf2
i1a_unsup:
    stc
    retf2

int19:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00
    mov ds, ax
    mov es, ax
    sti
    mov di, 3
boot_retry:
    mov ax, 0x0201
    mov bx, 0x7C00
    mov cx, 0x0001
    mov dx, 0x0080
    int 0x13
    jnc boot_loaded
    dec di
    jnz boot_retry
    jmp boot_fail
boot_loaded:
    cmp word ptr es:[0x7DFE], 0xAA55
    jne boot_fail
    mov dl, 0x80
    .byte 0xEA, 0x00, 0x7C, 0x00, 0x00
boot_fail:
    mov si, offset msg_noboot
print_loop:
    cs lodsb
    or al, al
    jnz skip2
    jmp halt_forever
skip2:
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    jmp print_loop
msg_noboot:
    .asciz "No bootable device\r\n"

int13:
    cmp ah, 0x02
    jne skip3
    jmp i13_rw
skip3:
    cmp ah, 0x03
    jne skip4
    jmp i13_rw
skip4:
    cmp ah, 0x08
    je i13_params
    cmp ah, 0x15
    jne skip5
    jmp i13_type
skip5:
    cmp ah, 0x01
    je i13_status
    cmp ah, 0x00
    je i13_ok
    cmp ah, 0x04
    je i13_ok
    cmp ah, 0x0C
    je i13_ok
    cmp ah, 0x0D
    je i13_ok
    cmp ah, 0x10
    je i13_ok
    cmp ah, 0x11
    je i13_ok
    jmp i13_fail

i13_ok:
    push ds
    xor ax, ax
    mov ds, ax
    mov byte ptr ds:[0x474], 0
    pop ds
    xor ah, ah
    clc
    retf2

i13_fail:
    push ds
    push ax
    xor ax, ax
    mov ds, ax
    pop ax
    mov ah, 0x01
    mov byte ptr ds:[0x474], 1
    pop ds
    stc
    retf2

i13_status:
    push ds
    xor ax, ax
    mov ds, ax
    mov ah, ds:[0x474]
    pop ds
    or ah, ah
    jnz i13_status_err
    clc
    retf2
i13_status_err:
    stc
    retf2

i13_params:
    cmp dl, 0x80
    jne i13_fail
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, ds:[TBL]
    dec ax
    mov ch, al
    mov bl, ah
    and bl, 3
    mov cl, 6
    shl bl, cl
    mov cl, ds:[TBL+14]
    and cl, 0x3F
    or cl, bl
    mov dh, ds:[TBL+2]
    dec dh
    mov dl, 1
    xor ax, ax
    pop ds
    clc
    retf2

i13_type:
    cmp dl, 0x80
    jne i13_fail
    push ds
    xor ax, ax
    mov ds, ax
    mov dx, ds:[SCRATCH+8]
    mov cx, ds:[SCRATCH+10]
    pop ds
    mov ah, 3
    clc
    retf2

i13_rw:
    cmp dl, 0x80
    je i13_rw_go
    jmp i13_fail
i13_rw_go:
    push ds
    push si
    push di
    push bp
    push bx
    push cx
    push dx
    push ax
    xor si, si
    mov ds, si
    mov ds:[SCRATCH], ah
    mov ds:[SCRATCH+1], al
    mov ds:[SCRATCH+7], dh
    mov bl, cl
    mov al, ch
    xor ah, ah
    mov bh, bl
    and bh, 0xC0
    mov cl, 6
    shr bh, cl
    mov ah, bh
    and bl, 0x3F
    mov ds:[SCRATCH+6], bl
    mov bl, ds:[TBL+2]
    xor bh, bh
    mul bx
    mov bl, ds:[SCRATCH+7]
    xor bh, bh
    add ax, bx
    adc dx, 0
    mov bl, ds:[TBL+14]
    xor bh, bh
    mov ds:[SCRATCH+2], ax
    mov ds:[SCRATCH+4], dx
    mul bx
    mov cx, ax
    mov si, dx
    mov ax, ds:[SCRATCH+4]
    mul bx
    add si, ax
    mov bl, ds:[SCRATCH+6]
    xor bh, bh
    dec bx
    add cx, bx
    adc si, 0

    mov dx, 0x1F2
    mov al, ds:[SCRATCH+1]
    out dx, al
    inc dx
    mov al, cl
    out dx, al
    inc dx
    mov al, ch
    out dx, al
    inc dx
    mov ax, si
    out dx, al
    inc dx
    mov al, ah
    and al, 0x0F
    or al, 0xE0
    out dx, al
    inc dx
    mov al, 0x20
    cmp byte ptr ds:[SCRATCH], 0x03
    jne rw_cmd
    mov al, 0x30
rw_cmd:
    out dx, al

    pop ax
    pop dx
    pop cx
    pop bx
    push bx
    push cx
    push dx
    push ax
    mov di, bx
rw_sector:
    mov dx, 0x1F7
    mov cx, 0xFFFF
rw_wait:
    in al, dx
    test al, 0x80
    jz rw_ready
    loop rw_wait
    jmp rw_error
rw_ready:
    test al, 0x01
    jnz rw_error
    test al, 0x08
    jz rw_error
    mov dx, 0x1F0
    mov cx, 256
    cmp byte ptr ds:[SCRATCH], 0x03
    je rw_write
rw_read:
    in ax, dx
    stosw
    loop rw_read
    jmp rw_next
rw_write:
    mov ax, es:[di]
    out dx, ax
    add di, 2
    loop rw_write
rw_next:
    dec byte ptr ds:[SCRATCH+1]
    jnz rw_sector
    mov byte ptr ds:[0x474], 0
    pop ax
    pop dx
    pop cx
    pop bx
    pop bp
    pop di
    pop si
    pop ds
    xor ah, ah
    clc
    retf2
rw_error:
    mov byte ptr ds:[0x474], 0x04
    pop ax
    pop dx
    pop cx
    pop bx
    pop bp
    pop di
    pop si
    pop ds
    mov ah, 0x04
    stc
    retf2

set_cursor_hw:
    push ax
    push dx
    push bx
    mov al, dh
    mov bl, 80
    mul bl
    xor bh, bh
    mov bl, dl
    add ax, bx
    mov bx, ax
    mov dx, 0x3D4
    mov al, 0x0E
    out dx, al
    inc dx
    mov al, bh
    out dx, al
    dec dx
    mov al, 0x0F
    out dx, al
    inc dx
    mov al, bl
    out dx, al
    pop bx
    pop dx
    pop ax
    ret

int10:
    cmp ah, 0x0E
    jne skip6
    jmp v_tty
skip6:
    cmp ah, 0x00
    je v_mode
    cmp ah, 0x02
    je v_setcur
    cmp ah, 0x03
    je v_getcur
    cmp ah, 0x0F
    je v_getmode
    cmp ah, 0x09
    je v_putc
    cmp ah, 0x0A
    je v_putc
    cmp ah, 0x06
    jne skip7
    jmp v_scroll
skip7:
    iret

v_getmode:
    push ds
    xor ax, ax
    mov ds, ax
    mov al, ds:[0x449]
    mov ah, 80
    mov bh, 0
    pop ds
    iret

v_getcur:
    push ds
    xor ax, ax
    mov ds, ax
    mov dx, ds:[0x450]
    mov cx, ds:[0x460]
    pop ds
    xor ax, ax
    iret

v_setcur:
    push ds
    push ax
    xor ax, ax
    mov ds, ax
    mov ds:[0x450], dx
    call set_cursor_hw
    pop ax
    pop ds
    iret

v_mode:
    push ax
    push cx
    push di
    push es
    push ds
    cld
    xor ax, ax
    mov ds, ax
    mov byte ptr ds:[0x449], 3
    mov word ptr ds:[0x450], 0
    mov ax, VIDEO
    mov es, ax
    xor di, di
    mov ax, 0x0720
    mov cx, 2000
    rep stosw
    xor dx, dx
    call set_cursor_hw
    pop ds
    pop es
    pop di
    pop cx
    pop ax
    iret

v_putc:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    push ds
    cld
    mov ah, bl
    push ax
    xor ax, ax
    mov ds, ax
    mov dx, ds:[0x450]
    mov al, dh
    mov bl, 80
    mul bl
    xor bh, bh
    mov bl, dl
    add ax, bx
    shl ax, 1
    mov di, ax
    mov ax, VIDEO
    mov es, ax
    pop ax
    or cx, cx
    jnz putc_loop
    mov cx, 1
putc_loop:
    stosw
    loop putc_loop
    pop ds
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    iret

v_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    cld
    mov ax, VIDEO
    mov es, ax
    mov ds, ax
    xor si, si
    mov di, 160
    mov cx, 1920
    mov si, 160
    xor di, di
    rep movsw
    mov ax, 0x0720
    mov di, 3840
    mov cx, 80
    rep stosw
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    iret

v_tty:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    cld
    push ax
    xor ax, ax
    mov ds, ax
    mov dx, ds:[0x450]
    pop ax
    cmp al, 13
    je tty_cr
    cmp al, 10
    je tty_lf
    cmp al, 8
    je tty_bs
    cmp al, 7
    je tty_done
    push ax
    mov al, dh
    mov bl, 80
    mul bl
    xor bh, bh
    mov bl, dl
    add ax, bx
    shl ax, 1
    mov di, ax
    mov ax, VIDEO
    mov es, ax
    pop ax
    mov ah, 0x07
    stosw
    inc dl
    cmp dl, 80
    jb tty_done
    xor dl, dl
tty_lf:
    inc dh
    cmp dh, 25
    jb tty_done
    mov dh, 24
    push dx
    mov ax, VIDEO
    mov es, ax
    push ds
    mov ds, ax
    mov si, 160
    xor di, di
    mov cx, 1920
    rep movsw
    pop ds
    mov ax, 0x0720
    mov di, 3840
    mov cx, 80
    rep stosw
    pop dx
    jmp tty_done
tty_cr:
    xor dl, dl
    jmp tty_done
tty_bs:
    or dl, dl
    jz tty_done
    dec dl
tty_done:
    mov ds:[0x450], dx
    call set_cursor_hw
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    iret

int09:
    push ax
    push bx
    push ds
    xor ax, ax
    mov ds, ax
    in al, 0x60
    push ax
    in al, 0x61
    mov ah, al
    or al, 0x80
    out 0x61, al
    mov al, ah
    out 0x61, al
    pop ax
    cmp al, 0x2A
    je k_shift_down
    cmp al, 0x36
    je k_shift_down
    cmp al, 0xAA
    je k_shift_up
    cmp al, 0xB6
    je k_shift_up
    cmp al, 0x1D
    je k_ctrl_down
    cmp al, 0x9D
    je k_ctrl_up
    test al, 0x80
    jnz k_done
    cmp al, 58
    jae k_done
    mov ah, al
    mov bl, al
    xor bh, bh
    test byte ptr ds:[0x417], 3
    jnz k_upper
    cs mov al, [bx + offset scan_lower]
    jmp k_got
k_upper:
    cs mov al, [bx + offset scan_upper]
k_got:
    or al, al
    jz k_done
    test byte ptr ds:[0x417], 4
    jz k_enqueue
    cmp al, 'a'
    jb k_enqueue
    cmp al, 'z'
    ja k_enqueue
    and al, 0x1F
k_enqueue:
    mov bx, ds:[0x41C]
    mov ds:[bx], ax
    add bx, 2
    cmp bx, 0x3E
    jb k_tail_ok
    mov bx, 0x1E
k_tail_ok:
    cmp bx, ds:[0x41A]
    je k_done
    mov ds:[0x41C], bx
    jmp k_done
k_shift_down:
    or byte ptr ds:[0x417], 1
    jmp k_done
k_shift_up:
    and byte ptr ds:[0x417], 0xFE
    jmp k_done
k_ctrl_down:
    or byte ptr ds:[0x417], 4
    jmp k_done
k_ctrl_up:
    and byte ptr ds:[0x417], 0xFB
k_done:
    mov al, 0x20
    out 0x20, al
    pop ds
    pop bx
    pop ax
    iret

int16:
    or ah, ah
    jz k_read
    cmp ah, 1
    je k_peek
    cmp ah, 2
    je k_flags
    iret
k_flags:
    push ds
    xor ax, ax
    mov ds, ax
    mov al, ds:[0x417]
    pop ds
    iret
k_read:
    push ds
    push bx
    xor ax, ax
    mov ds, ax
k_wait:
    sti
    mov bx, ds:[0x41A]
    cmp bx, ds:[0x41C]
    jne k_have
    hlt
    jmp k_wait
k_have:
    mov ax, ds:[bx]
    add bx, 2
    cmp bx, 0x3E
    jb k_head_ok
    mov bx, 0x1E
k_head_ok:
    mov ds:[0x41A], bx
    pop bx
    pop ds
    iret
k_peek:
    push ds
    push bx
    xor ax, ax
    mov ds, ax
    mov bx, ds:[0x41A]
    cmp bx, ds:[0x41C]
    je k_empty
    mov ax, ds:[bx]
    pop bx
    pop ds
    push bp
    mov bp, sp
    and word ptr ss:[bp+6], 0xFFBF
    pop bp
    iret
k_empty:
    xor ax, ax
    pop bx
    pop ds
    push bp
    mov bp, sp
    or word ptr ss:[bp+6], 0x0040
    pop bp
    iret

scan_lower:
    .ascii "\0\033", "1234567890-=", "\b\t", "qwertyuiop[]", "\r", "\0", "asdfghjkl;'`", "\0", "\\zxcvbnm,./", "\0*\0 "
scan_upper:
    .ascii "\0\033", "!@#$%^&*()_+", "\b\t", "QWERTYUIOP{}", "\r", "\0", "ASDFGHJKL:\"~", "\0", "|ZXCVBNM<>?", "\0*\0 "

.org 0x1FF0
reset_vector:
    .byte 0xEA
    .word post
    .word 0xF000
    .ascii "10/29/26"
    .byte 0
    .byte 0xFC
    .byte 0
