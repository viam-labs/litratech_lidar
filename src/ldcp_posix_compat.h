#pragma once

// Forced in ahead of the LDCP sources on Apple. location.h only includes
// <netinet/in.h> when __linux__ is defined, but the same BSD socket types are
// what the SDK uses on macOS.
#if defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <climits>
