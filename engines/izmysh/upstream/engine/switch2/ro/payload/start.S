.section .text.start,"ax",%progbits
.global _start
_start:
    b main
    .word module_header - _start
    .word 0

.text
.global connect_port
connect_port:
    mov x1, x0
    svc #0x1f
    mov w0, w0
    mov w1, w1
    ret
.global send_request
send_request:
    svc #0x21
    ret
.global close_handle
close_handle:
    svc #0x16
    ret
.global sleep_thread
sleep_thread:
    svc #0x0b
    ret
.global wait_handles
wait_handles:
    mov x3, x2
    mov x2, x1
    mov x1, x0
    svc #0x18
    mov w0, w0
    mov w1, w1
    ret
.global reply_receive
reply_receive:
    mov x4, x3
    mov x3, x2
    mov x2, x1
    mov x1, x0
    svc #0x43
    mov w0, w0
    mov w1, w1
    ret
.global accept_session
accept_session:
    mov w1, w0
    svc #0x41
    mov w0, w0
    mov w1, w1
    ret
.global process_id
process_id:
    mov w1, w0
    svc #0x24
    mov w0, w0
    ret
.global thread_id
thread_id:
    mov w1, w0
    svc #0x25
    mov w0, w0
    ret
.global system_tick
system_tick:
    svc #0x1e
    ret

.global get_info
get_info:
    mov x3, x2
    mov w2, w1
    mov w1, w0
    svc #0x29
    mov w0, w0
    ret
.global query_process
query_process:
    mov x3, x2
    mov w2, w1
    svc #0x76
    mov w0, w0
    mov w1, w1
    ret
.global query_memory
query_memory:
    mov x2, x1
    svc #0x6
    mov w0, w0
    mov w1, w1
    ret
.global map_process
map_process:
    svc #0x74
    ret
.global unmap_process
unmap_process:
    svc #0x75
    ret
.global map_code
map_code:
    svc #0x77
    ret
.global unmap_code
unmap_code:
    svc #0x78
    ret
.global process_permission
process_permission:
    svc #0x73
    ret

.global memset
memset:
    mov x3, x0
    cbz x2, 2f
1:
    strb w1, [x3], #1
    subs x2, x2, #1
    b.ne 1b
2:
    ret

.section .rodata.module,"a",%progbits
.global module_header
module_header:
    .ascii "MOD0"
    .word dynamic - module_header
    .word __bss_start - module_header
    .word __bss_end - module_header
    .word 0, 0, 0
dynamic:
    .quad 0, 0

.section .data,"aw",%progbits
    .quad 0x4558454f5232534e, 1
