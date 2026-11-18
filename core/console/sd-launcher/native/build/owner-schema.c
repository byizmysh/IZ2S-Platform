#include <stdio.h>
#include "../abi.h"
#include "../host-rpc.h"
int main(void){printf("{\"bytes\":%zu",sizeof(IzHostRpcOwner));
printf(",\"magic\":%zu",offsetof(IzHostRpcOwner,magic));
printf(",\"count\":%zu",offsetof(IzHostRpcOwner,count));
printf(",\"word\":%zu",offsetof(IzHostRpcOwner,word));
printf(",\"opaque\":%zu",offsetof(IzHostRpcOwner,opaque));
printf(",\"ever_bound\":%zu",offsetof(IzHostRpcOwner,ever_bound));
printf(",\"bindings\":%zu",offsetof(IzHostRpcOwner,bindings));
printf(",\"sdk_scratch\":%zu",offsetof(IzHostRpcOwner,sdk_scratch));
printf(",\"protected_base\":%zu",offsetof(IzHostRpcOwner,protected_base));
printf(",\"protected_bytes\":%zu",offsetof(IzHostRpcOwner,protected_bytes));
printf(",\"packet_epoch\":%zu",offsetof(IzHostRpcOwner,packet_epoch));
printf(",\"packet_status\":%zu",offsetof(IzHostRpcOwner,packet_status));
printf(",\"full_write_thunk\":%zu",offsetof(IzHostRpcOwner,full_write_thunk));
printf(",\"full_receipt_thunk\":%zu",offsetof(IzHostRpcOwner,full_receipt_thunk));
printf(",\"graphics_sleep\":%zu",offsetof(IzHostRpcOwner,graphics_sleep));
printf(",\"graphics_idle\":%zu",offsetof(IzHostRpcOwner,graphics_idle));
printf(",\"graphics_queue\":%zu",offsetof(IzHostRpcOwner,graphics_queue));
printf(",\"graphics_mode\":%zu",offsetof(IzHostRpcOwner,graphics_mode));
printf(",\"graphics_owner\":%zu",offsetof(IzHostRpcOwner,graphics_owner));
printf(",\"graphics_fault\":%zu",offsetof(IzHostRpcOwner,graphics_fault));
printf(",\"graphics_waiting\":%zu",offsetof(IzHostRpcOwner,graphics_waiting));
printf(",\"code_base\":%zu",offsetof(IzHostRpcOwner,code_base));
printf(",\"code_bytes\":%zu",offsetof(IzHostRpcOwner,code_bytes));
printf(",\"code_history\":%zu",offsetof(IzHostRpcOwner,code_history));
printf(",\"code_used\":%zu",offsetof(IzHostRpcOwner,code_used));
printf(",\"map_source\":%zu",offsetof(IzHostRpcOwner,map_source));
printf(",\"map_rx\":%zu",offsetof(IzHostRpcOwner,map_rx));
printf(",\"map_program_id\":%zu",offsetof(IzHostRpcOwner,map_program_id));
printf(",\"map_epoch\":%zu",offsetof(IzHostRpcOwner,map_epoch));
printf(",\"map_state\":%zu",offsetof(IzHostRpcOwner,map_state));
printf(",\"map_index\":%zu",offsetof(IzHostRpcOwner,map_index));
printf(",\"map_error\":%zu",offsetof(IzHostRpcOwner,map_error));
printf(",\"code_states\":%zu",offsetof(IzHostRpcOwner,code_states));
printf(",\"claim_epoch\":%zu",offsetof(IzHostRpcOwner,claim_epoch));
printf(",\"claim_seen\":%zu",offsetof(IzHostRpcOwner,claim_seen));
printf(",\"service_sdk\":%zu",offsetof(IzHostRpcOwner,service_sdk));
printf(",\"cpu_jobs\":%zu",offsetof(IzHostRpcOwner,cpu_jobs));
printf(",\"service_tick_hz\":%zu",offsetof(IzHostRpcOwner,service_tick_hz));
printf(",\"service_send\":%zu",offsetof(IzHostRpcOwner,service_send));
printf(",\"service_close\":%zu",offsetof(IzHostRpcOwner,service_close));
printf(",\"service_filesystem\":%zu",offsetof(IzHostRpcOwner,service_filesystem));
printf(",\"service_reserved\":%zu",offsetof(IzHostRpcOwner,service_reserved));
printf(",\"historyOffset\":%u",IZ_HOST_RPC_HISTORY_OFFSET);
puts("}");return 0;}