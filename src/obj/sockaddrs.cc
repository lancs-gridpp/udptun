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

#include "sockaddrs.hh"
#include "network.hh"

SocketAddress::SocketAddress(const struct sockaddr *base, socklen_t len)
  : len_(len), base_(base && len_ > 0 ? new unsigned char[len_] : nullptr)
{
  if (base_) ::memcpy(base_, base, len_);
}

SocketAddress::SocketAddress(SocketAddress &&rhs)
  : len_(rhs.len_), base_(rhs.base_)
{
  if (base_)
    rhs.base_ = nullptr, rhs.len_ = 0;
}

SocketAddress &SocketAddress::operator =(SocketAddress &&rhs)
{
  if (base_) delete[] base_;
  base_ = rhs.base_, rhs.base_ = nullptr;
  len_ = rhs.len_, rhs.len_ = 0;
  return *this;
}

SocketAddress::SocketAddress(const SocketAddress &rhs)
  : len_(rhs.len_), base_(rhs.base_ ? new unsigned char[len_] : nullptr)
{
  if (base_) ::memcpy(base_, rhs.base_, len_);
}

SocketAddress &SocketAddress::operator =(const SocketAddress &rhs)
{
  if (base_) delete[] base_;
  len_ = rhs.len_;
  if (len_) {
    base_ = new unsigned char[len_];
    ::memcpy(base_, rhs.base_, len_);
  } else {
    base_ = nullptr;
  }
  return *this;
}

SocketAddress::~SocketAddress()
{
  if (base_) delete[] base_;
}

bool operator <(const SocketAddress &lhs, const SocketAddress &rhs)
{
  if (lhs.len_ < rhs.len_) return true;
  if (lhs.len_ > rhs.len_) return false;
  if (!rhs.base_) return false;
  if (!lhs.base_) return true;
  return ::memcmp(lhs.base_, rhs.base_, lhs.len_) < 0;
}

std::ostream &operator <<(std::ostream &lhs, const SocketAddress &rhs)
{
  lhs << rhs.len_;
  for (socklen_t i = 0; i < rhs.len_; i++)
    lhs << " " << (unsigned) rhs.base_[i];
  return lhs;
}

std::istream &operator >>(std::istream &lhs, SocketAddress &rhs)
{
  if (rhs.base_) {
    delete[] rhs.base_;
    rhs.base_ = nullptr;
    rhs.len_ = 0;
  }
  lhs >> rhs.len_;
  if (rhs.len_ > 0) {
    rhs.base_ = new unsigned char[rhs.len_];
    for (socklen_t i = 0; i < rhs.len_; i++) {
      unsigned b;
      lhs >> b;
      b = rhs.base_[i];
    }
  }
  return lhs;
}

std::string SocketAddress::str() const
{
  return to_str(addr(), len());
}
