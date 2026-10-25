/* Run the socket-address suite with IPv6 support compiled out. Load the
 * configuration first so subsequent includes cannot restore the setting. */
#include "hio-prv.h"
#include <hio-skad.h>
#include <hio-nwif.h>

#undef HIO_SIZEOF_STRUCT_SOCKADDR_IN6
#define HIO_SIZEOF_STRUCT_SOCKADDR_IN6 0

#include "../lib/skad.c"
#include "t-023.c"
