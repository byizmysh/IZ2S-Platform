/* Launcher entry adapter. Called by the original musl __libc_start_main after
 * libc and thread-local state are initialized, then forwards to unchanged PS2.
 * This is not an SDK callback or an emulator instruction patch. */
extern int setenv(const char *, const char *, int);
extern int puts(const char *);
extern int __real_main(int, char **);

int __wrap_main(int argc, char **argv)
{
    if (setenv("PS2_AUTO_RESUME", "1", 1) != 0)
        puts("PS2 auto resume could not be enabled; continuing normal boot.");
    return __real_main(argc, argv);
}
