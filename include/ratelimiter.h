#ifndef RATELIMITER_H
#define RATELIMITER_H

/*
 * Phase 29: recommended umbrella header for external C11 consumers.
 *
 * Install layout: <prefix>/include/ratelimiter/ratelimiter.h
 * Include as:    #include <ratelimiter/ratelimiter.h>
 *
 * Library scope: an in-process per-IPv4 rate limiter and optional ingress
 * adapters. It does not open sockets, parse HTTP, or authenticate proxy IPs.
 * Initialize before workers start; join workers before destroying the service.
 * The clock callback must itself be thread-safe for concurrent requests.
 * The interface uses public C struct layouts, so ABI is not yet stable.
 * See rate_version.h for experimental source-release version metadata.
 */
#include "rate_version.h"
#include "rate_service.h"
#include "request_gateway.h"
#include "request_policy.h"

#endif
