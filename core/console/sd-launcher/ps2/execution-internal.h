#ifndef IZ_PS2_EXECUTION_INTERNAL_H
#define IZ_PS2_EXECUTION_INTERNAL_H
/* Only the opaque execution controller can grant these post-entry paths.
 * Preparation cancel APIs retain their original refusal after entry. */
typedef struct IzPs2Execution IzPs2Execution;
enum {IZ_PS2_RETIRE_VIDEO=1,IZ_PS2_RETIRE_SHADOW,IZ_PS2_RETIRE_STORAGE,IZ_PS2_RETIRE_MEMORY};
uint32_t iz_ps2_execution_release_match(const IzPs2Execution*,const void*,uint32_t);
#endif
