/*
 * PuTTY version numbering
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
const char ver[] = "FileZilla" PACKAGE_VERSION;
const char sshver[] = "FileZilla-" PACKAGE_VERSION;
#else
const char ver[] = "FileZilla";
const char sshver[] = "FileZilla-custom";
#endif


const char commitid[] = "unavailable";

/*
 * SSH local version string MUST be under 40 characters. Here's a
 * compile time assertion to verify this.
 */
enum { vorpal_sword = 1 / (sizeof(sshver) <= 40) };
