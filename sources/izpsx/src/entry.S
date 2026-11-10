.section .text.entry,"ax",%progbits
.global _start
.type _start,%function
_start:
 bti jc
 stp x29,x30,[sp,#-16]!
 mov x29,sp
 ldr x1,[x0,#64]
 ldr x2,[x0,#72]
 cbz x1,1f
 cbz x2,1f
 add x1,x1,x2
 and x1,x1,#-16
 mov sp,x1
1:
 bl heavy_main
 mov sp,x29
 ldp x29,x30,[sp],#16
 ret
.size _start,.-_start
