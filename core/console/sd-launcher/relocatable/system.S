/* Self-process read-only boundaries. Register conventions/SVC numbers match
 * engines/izmysh/upstream/engine/switch2/ro/payload/start.S. */
.text
.global iz_self_process_id
.type iz_self_process_id,%function
iz_self_process_id:
 bti c
 str x0,[sp,#-16]!
 mov w1,#0xffff8001
 svc #0x24
 ldr x4,[sp],#16
 str x1,[x4]
 mov w0,w0
 ret
.size iz_self_process_id,.-iz_self_process_id
.global iz_self_info
.type iz_self_info,%function
iz_self_info:
 bti c
 str x0,[sp,#-16]!
 mov w2,#0xffff8001
 mov x3,xzr
 svc #0x29
 ldr x4,[sp],#16
 str x1,[x4]
 mov w0,w0
 ret
.size iz_self_info,.-iz_self_info
.global iz_self_query
.type iz_self_query,%function
iz_self_query:
 bti c
 mov x2,x1
 svc #0x6
 mov w0,w0
 ret
.size iz_self_query,.-iz_self_query
