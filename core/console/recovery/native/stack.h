#ifndef IZ_RECOVERY_STACK_H
#define IZ_RECOVERY_STACK_H
/* Checked by the original SDK dispatcher, without an extra wire request.
 * A fault freezes the pending operation; it never becomes a successful reply. */
#define IZ_RECOVERY_STACK_BYTES 0x400000u
#define IZ_RECOVERY_STACK_GUARD 64u
#define IZ_RECOVERY_STACK_FAULT 0xe918u
static int iz_recovery_stack_check(IzRecoveryNative*c){
 if(!c->stack_guarded)return 1;
 uint64_t at=(uint64_t)(uintptr_t)__builtin_frame_address(0),base=c->stack,bytes=c->stack_bytes;
 c->stack_checks++;
 if(c->stack_fault)return 0;
 if(c->stack_guarded!=1||base<4096+IZ_RECOVERY_STACK_GUARD||base%4096||bytes!=IZ_RECOVERY_STACK_BYTES||base>=(1ull<<39)-bytes-IZ_RECOVERY_STACK_GUARD||at<base||at-base>=bytes){c->stack_fault=IZ_RECOVERY_STACK_FAULT;return 0;}
 const volatile uint8_t*lower=(void*)(uintptr_t)(base-IZ_RECOVERY_STACK_GUARD),*upper=(void*)(uintptr_t)(base+bytes);
 for(unsigned i=0;i<IZ_RECOVERY_STACK_GUARD;i++)if(lower[i]!=0xa7||upper[i]!=0x5d){c->stack_fault=IZ_RECOVERY_STACK_FAULT;return 0;}
 __atomic_store_n(&c->observed_stack,at,__ATOMIC_RELEASE);return 1;
}
#endif
