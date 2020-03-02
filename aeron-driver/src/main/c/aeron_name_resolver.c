/*
 * Copyright 2014-2020 Real Logic Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if defined(__linux__)
#define _BSD_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#include "util/aeron_error.h"
#include "util/aeron_parse_util.h"
#include "util/aeron_netutil.h"
#include "util/aeron_dlopen.h"
#include "aeron_name_resolver.h"
#include "aeron_driver_context.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

int aeron_name_resolver_init(aeron_driver_context_t *context, aeron_name_resolver_t *resolver, const char *args)
{
    return context->name_resolver_supplier_func(context, resolver, args);
}

int aeron_name_resolver_supplier_default(
    aeron_driver_context_t *context,
    aeron_name_resolver_t *resolver,
    const char *args)
{
    resolver->lookup_func = aeron_name_resolver_lookup_default;
    resolver->resolve_func = aeron_name_resolver_resolve_default;
    resolver->state = NULL;
    return 0;
}

int aeron_name_resolver_resolve_default(
    aeron_name_resolver_t *resolver,
    const char *name,
    const char *uri_param_name,
    bool is_re_resolution,
    struct sockaddr_storage *address)
{
    return aeron_ip_addr_resolver(name, address, AF_INET, IPPROTO_UDP);
}

int aeron_name_resolver_lookup_default(
    aeron_name_resolver_t *resolver,
    const char *name,
    const char *uri_param_name,
    bool is_re_resolution,
    const char **resolved_name)
{
    *resolved_name = name;
    return 0;
}

int aeron_name_resolver_resolve_host_and_port(
    aeron_name_resolver_t *resolver,
    const char *name,
    const char *uri_param_name,
    struct sockaddr_storage *sockaddr)
{
    aeron_parsed_address_t parsed_address;
    const char *address_str;

    if (resolver->lookup_func(resolver, name, uri_param_name, false, &address_str) < 0)
    {
        return -1;
    }

    if (aeron_address_split(address_str, &parsed_address) < 0)
    {
        return -1;
    }

    const int family_hint = 6 == parsed_address.ip_version_hint ? AF_INET6 : AF_INET;

    int result = -1;
    int port = aeron_udp_port_resolver(parsed_address.port, false);

    if (0 <= port)
    {
        if (AF_INET == family_hint)
        {
            if (aeron_try_parse_ipv4(parsed_address.host, sockaddr))
            {
                result = 0;
            }
            else
            {
                result = resolver->resolve_func(resolver, parsed_address.host, uri_param_name, false, sockaddr);
            }

            ((struct sockaddr_in *)sockaddr)->sin_port = htons((uint16_t)port);
        }
        else if (AF_INET6 == family_hint)
        {
            if (aeron_try_parse_ipv6(parsed_address.host, sockaddr))
            {
                result = 0;
            }
            else
            {
                result = resolver->resolve_func(resolver, parsed_address.host, uri_param_name, false, sockaddr);
            }

            ((struct sockaddr_in6 *)sockaddr)->sin6_port = htons((uint16_t)port);
        }
    }

    return result;
}

aeron_name_resolver_supplier_func_t aeron_name_resolver_supplier_load(const char *name)
{
    aeron_name_resolver_supplier_func_t supplier_func;

    if (NULL == name)
    {
        aeron_set_err(EINVAL, "%s", "invalid name_resolver supplier function name");
        return NULL;
    }

    if (0 == strncmp(name, AERON_NAME_RESOLVER_SUPPLIER_DEFAULT, strlen(AERON_NAME_RESOLVER_SUPPLIER_DEFAULT)))
    {
        supplier_func = aeron_name_resolver_supplier_default;
    }
    else if (0 == strncmp(name, AERON_NAME_RESOLVER_CSV_TABLE, strlen(AERON_NAME_RESOLVER_CSV_TABLE)))
    {
        supplier_func = aeron_name_resolver_supplier_load("aeron_name_resolver_supplier_csv_table");
    }
    else
    {
#if defined(AERON_COMPILER_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
        if ((supplier_func = (aeron_name_resolver_supplier_func_t)aeron_dlsym(RTLD_DEFAULT, name)) == NULL)
        {
            aeron_set_err(
                EINVAL, "could not find UDP channel transport bindings %s: dlsym - %s", name, aeron_dlerror());
        }
#if defined(AERON_COMPILER_GCC)
#pragma GCC diagnostic pop
#endif
    }

    return supplier_func;
}
