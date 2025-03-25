// -*- c-basic-offset: 2; indent-tabs-mode: nil -*-

/*
 * Copyright (c) 2025, Lancaster University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <netdb.h>

#include <system_error>

#include "network.hh"
#include "formatting.hh"

std::string proto_to_str(int proto)
{
  struct protoent *ent = getprotobynumber(proto);
  if (ent == nullptr) return "proto?";
  return ent->p_name;
}

std::string socktype_to_str(int type)
{
  switch (type) {
  case SOCK_STREAM:
    return "STREAM";

  case SOCK_DGRAM:
    return "DGRAM";

  default:
    return "SOCK?";
  }
}

std::string af_to_str(int domain)
{
  switch (domain) {
  case AF_INET:
    return "INET";

  case AF_INET6:
    return "INET6";

  case AF_UNIX:
    return "UNIX";

  default:
    return "AF?";
  }
}

std::string to_str(const struct sockaddr *addr, socklen_t addrlen)
{
  char host[100], serv[100];
  int rc = getnameinfo(addr, addrlen, host, sizeof host,
                       serv, sizeof serv, NI_NUMERICHOST);
  switch (rc) {
  case 0:
    return std::string(host) + ":" + serv;
  case EAI_AGAIN:
    return "AGAIN";
  case EAI_BADFLAGS:
    return "BADFLAGS";
  case EAI_FAIL:
    return "FAIL";
  case EAI_FAMILY:
    return "FAMILY";
  case EAI_MEMORY:
    return "MEMORY";
  case EAI_NONAME:
    return "NONAME";
  case EAI_OVERFLOW:
    return "OVERFLOW";
  case EAI_SYSTEM:
    return "SYSTEM";
  default:
    throw std::runtime_error(sformat("unreachable %s:%d", __FILE__, __LINE__));
  }
}
