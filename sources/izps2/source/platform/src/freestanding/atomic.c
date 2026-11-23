/* Compiler runtime ABI used by the retained native_pad.o. Compile with
 * -mno-outline-atomics: this standalone resident image has no libgcc loader.
 * Return the observed value for both successful and failed strong CAS. */
unsigned __aarch64_cas4_acq(unsigned expected, unsigned desired, volatile void *p)
{
    __atomic_compare_exchange_n((volatile unsigned *)p, &expected, desired, 0,
                                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    return expected;
}
