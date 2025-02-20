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

#include <sys/uio.h>

#include <cstring>
#include <cassert>

#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <chrono>

#include "payloads.hh"
#include "destruction.hh"

void Payload::save(std::ofstream &out,
                   const void *base, std::size_t len)
{
  unsigned char lenbytes[] = { (unsigned char) (len >> 8), (unsigned char) len };
  out.write(reinterpret_cast<const char *>(lenbytes), sizeof lenbytes);
  out.write(reinterpret_cast<const char *>(base), len);
}

void Payload::save(std::ofstream &out)
{
  save(out, base_, len_);
}

bool Payload::load(std::ifstream &in, std::size_t &sum)
{
  unsigned char lenbytes[2];
  in.read(reinterpret_cast<char *>(lenbytes), sizeof lenbytes);
  if (in.fail()) return false;
  std::size_t len = (lenbytes[0] << 8) | lenbytes[1];
  unsigned char *base = new unsigned char[len];
  {
    LegacyDestructor([&base]() { if (base) delete[] base; });
    in.read(reinterpret_cast<char *>(base), len);
    if (in.fail())
      return false;
    sum += 2 + len;
    clear();
    base_ = base, base = nullptr;
    len_ = len;
    return true;
  }
}

Payload::Payload(Payload &&rhs)
  : base_(rhs.base_), len_(rhs.len_)
{
  rhs.base_ = nullptr;
  rhs.len_ = 0;
}

Payload &Payload::operator =(Payload &&rhs)
{
  if (base_) delete[] base_;
  base_ = rhs.base_;
  len_ = rhs.len_;
  rhs.base_ = nullptr;
  rhs.len_ = 0;
  return *this;
}

bool Payload::get(struct iovec &into)
{
  if (!base_) return false;
  into.iov_base = base_;
  into.iov_len = len_;
  return true;
}

Payload::Payload(const void *base, std::size_t len)
  : base_(new unsigned char[len]), len_(len)
{
  memcpy(base_, base, len);
}

Payload::~Payload()
{
  if (base_)
    delete[] base_;
}

void Payload::clear()
{
  if (base_) {
    delete[] base_;
    base_ = nullptr;
  }
  len_ = 0;
}
