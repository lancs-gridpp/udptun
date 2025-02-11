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

#include <sys/time.h>

#include <climits>
#include <system_error>

#include "periods.hh"
#include "realtime.hh"
#include "formatting.hh"

RealTime::RealTime(const struct timeval &at)
{
  this->at = at.tv_sec * 1000000000ull + at.tv_usec;
}

void RealTime::now()
{
  struct timeval at;
  if (gettimeofday(&at, NULL) < 0)
    throw std::system_error(errno, std::system_category(), "getttimeofday");
  this->at = at.tv_sec * 1000000000ull + at.tv_usec * 1000ull;
}

RealTime &RealTime::operator +=(const TimePeriod &rhs)
{
  at += rhs.to_nanoseconds();
  return *this;
}

RealTime &RealTime::operator -=(const TimePeriod &rhs)
{
  at -= rhs.to_nanoseconds();
  return *this;
}

RealTime::operator bool() const
{
  return at != 0;
}

TimePeriod operator -(const RealTime &lhs, const RealTime &rhs)
{
  double amount = lhs.at < rhs.at ? -(rhs.at - lhs.at) : lhs.at - rhs.at;
  return TimePeriod(amount, TimePeriod::NANOSECOND);
}

bool operator !=(const RealTime &lhs, const RealTime &rhs)
{
  return lhs.at != rhs.at;
}

bool operator <(const RealTime &lhs, const RealTime &rhs)
{
  return lhs.at < rhs.at;
}

void RealTime::zero()
{
  at = 0;
}

RealTime::operator std::string()
{
  return sformat("%llu.%09llu", at / 1000000000ull, at % 1000000000ull);
}
