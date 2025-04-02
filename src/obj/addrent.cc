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

#include <cstring>

#include <sstream>

#include "addrent.hh"
#include "network.hh"

const struct sockaddr *AddressEntry::addr() const {
  return reinterpret_cast<struct sockaddr *>(addr_base);
}

AddressEntry::AddressEntry(const struct addrinfo &orig)
  : flags(orig.ai_flags), family(orig.ai_family),
    socktype(orig.ai_socktype), protocol(orig.ai_protocol),
    addrlen(orig.ai_addrlen)
{
  addr_base = new unsigned char[addrlen];
  ::memcpy(addr_base, orig.ai_addr, addrlen);
}

AddressEntry::~AddressEntry() { delete addr_base; }

bool operator <(const AddressEntry &lhs, const AddressEntry &rhs)
{
  if (lhs.flags < rhs.flags) return true;
  if (lhs.flags > rhs.flags) return false;
  if (lhs.family < rhs.family) return true;
  if (lhs.family > rhs.family) return false;
  if (lhs.socktype < rhs.socktype) return true;
  if (lhs.socktype > rhs.socktype) return false;
  if (lhs.protocol < rhs.protocol) return true;
  if (lhs.protocol > rhs.protocol) return false;
  if (lhs.addrlen < rhs.addrlen) return true;
  if (lhs.addrlen > rhs.addrlen) return false;
  int rc = ::memcmp(lhs.addr_base, rhs.addr_base, lhs.addrlen);
  return rc < 0;
}

AddressEntry::operator std::string() const
{
  std::stringstream out;
  out << af_to_str(family) << ":" << proto_to_str(protocol)
      << ":" << to_str(addr(), addrlen);
  if (flags & AI_V4MAPPED) out << ":V4MAPPED";
  if (flags & AI_PASSIVE) out << ":PASSIVE";
  if (flags & AI_NUMERICHOST) out << ":NUMERICHOST";
  if (flags & AI_NUMERICSERV) out << ":NUMERICSERV";
  if (flags & AI_ADDRCONFIG) out << ":ADDRCONFIG";
  if (flags & AI_CANONNAME) out << ":ADDRCONFIG";
  if (flags & AI_ALL) out << ":ALL";
  return out.str();
}

