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

#include <cassert>

#include <algorithm>

#include "labels.hh"

bool labels_to_bytes(labelset_t labels, unsigned char *buf,
                     std::size_t done, std::size_t pos,
                     std::vector<struct iovec> &vec)
{
  if (done >= pos + MAX_LABEL_BYTES) return false;
  auto m = done > pos ? pos + MAX_LABEL_BYTES - done : MAX_LABEL_BYTES;
  assert(m >= 1);
  for (unsigned i = MAX_LABEL_BYTES - m; i < MAX_LABEL_BYTES; i++)
    buf[i] = (labels >> (i * 8)) & 0xffu;
  struct iovec v = {
    .iov_base = buf + (MAX_LABEL_BYTES - m),
    .iov_len = m,
  };
  vec.push_back(v);
  return true;
}

bool length_to_bytes(unsigned len, unsigned char *buf,
                     std::size_t done, std::size_t pos,
                     std::vector<struct iovec> &vec)
{
  if (done >= pos + MAX_LENGTH_BYTES) return false;
  auto m = done > pos ? pos + MAX_LENGTH_BYTES - done : MAX_LENGTH_BYTES;
  assert(m >= 1);
  for (unsigned i = MAX_LENGTH_BYTES - m; i < MAX_LENGTH_BYTES; i++)
    buf[i] = (len >> ((MAX_LENGTH_BYTES - 1 - i) * 8)) & 0xffu;
  struct iovec v = {
    .iov_base = buf + (MAX_LENGTH_BYTES - m),
    .iov_len = m,
  };
  vec.push_back(v);
  return true;
}
