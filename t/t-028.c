/* Exercise the explicit error paths when interface lookup is unsupported. */
#include "hio-prv.h"
#include <hio-skad.h>
#include <hio-nwif.h>

#if !defined(_WIN32) && !defined(__OS2__) && !defined(__DOS__)
#include <unistd.h>
#include <sys/socket.h>
#if defined(HAVE_SYS_IOCTL_H)
#include <sys/ioctl.h>
#endif
#if defined(HAVE_NET_IF_H)
#include <net/if.h>
#endif
#if defined(HAVE_SYS_SOCKIO_H)
#include <sys/sockio.h>
#endif
#undef SIOCGIFINDEX
#undef SIOCGIFNAME
#undef SIOCGIFCONF
#undef SIOCGIFANUM
#undef SIOCGIFNUM
#undef HAVE_IF_NAMETOINDEX
#undef HAVE_IF_INDEXTONAME
#endif

#include "../lib/nwif.c"
#include "../lib/skad.c"
#include "t-023.c"
