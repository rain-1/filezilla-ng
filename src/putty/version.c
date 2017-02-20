/*
 * PuTTY version numbering
 */

#define STR1(x) #x
#define STR(x) STR1(x)

#if defined SNAPSHOT

#if defined SVN_REV
#define SNAPSHOT_TEXT STR(SNAPSHOT) ":r" STR(SVN_REV)
#else
#define SNAPSHOT_TEXT STR(SNAPSHOT)
#endif

const char ver[] = "Development snapshot " SNAPSHOT_TEXT;
const char sshver[] = "PuTTY-Snapshot-" SNAPSHOT_TEXT;

#undef SNAPSHOT_TEXT

#elif defined RELEASE

const char ver[] = "Release " STR(RELEASE);
const char sshver[] = "PuTTY-Release-" STR(RELEASE);

#elif defined SVN_REV

const char ver[] = "Custom build r" STR(SVN_REV) ", " __DATE__ " " __TIME__;
const char sshver[] = "PuTTY-Custom-r" STR(SVN_REV);

#else

const char ver[] = "Unidentified build, " __DATE__ " " __TIME__;
const char sshver[] = "PuTTY-Local: " __DATE__ " " __TIME__;

#endif

const char commitid[] = "unavailable";

/*
 * SSH local version string MUST be under 40 characters. Here's a
 * compile time assertion to verify this.
 */
enum { vorpal_sword = 1 / (sizeof(sshver) <= 40) };
