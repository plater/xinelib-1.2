/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file bswap.h
 * byte swap.
 */

#ifndef __FFMPEG_BSWAP_H__
#define __FFMPEG_BSWAP_H__

#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#else

#if defined(ARCH_X86_X32) || defined(ARCH_X86_64)
#  define LEGACY_REGS "=Q"
#else
#  define LEGACY_REGS "=q"
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_X32) || defined(ARCH_X86_64)
static always_inline uint16_t bswap_16(uint16_t x)
{
  __asm("rorw $8, %0"   :
        LEGACY_REGS (x) :
        "0" (x));
    return x;
}

static always_inline uint32_t bswap_32(uint32_t x)
{
#if __CPU__ != 386
 __asm("bswap   %0":
      "=r" (x)     :
#else
 __asm("xchgb   %b0,%h0\n"
      "         rorl    $16,%0\n"
      "         xchgb   %b0,%h0":
      LEGACY_REGS (x)                :
#endif
      "0" (x));
  return x;
}

static inline uint64_t bswap_64(uint64_t x)
{
#if defined(ARCH_X86_X32) || defined(ARCH_X86_64)
  __asm("bswap  %0":
        "=r" (x)   :
        "0" (x));
  return x;
#else
    union {
        uint64_t ll;
        struct {
           uint32_t l,h;
        } l;
    } r;
    r.l.l = bswap_32 (x);
    r.l.h = bswap_32 (x>>32);
    return r.ll;
#endif
}

#elif defined(ARCH_SH4)

static always_inline uint16_t bswap_16(uint16_t x) {
        __asm__("swap.b %0,%0":"=r"(x):"0"(x));
        return x;
}

static always_inline uint32_t bswap_32(uint32_t x) {
        __asm__(
        "swap.b %0,%0\n"
        "swap.w %0,%0\n"
        "swap.b %0,%0\n"
        :"=r"(x):"0"(x));
        return x;
}

static inline uint64_t bswap_64(uint64_t x)
{
    union {
        uint64_t ll;
        struct {
           uint32_t l,h;
        } l;
    } r;
    r.l.l = bswap_32 (x);
    r.l.h = bswap_32 (x>>32);
    return r.ll;
}
#else

static always_inline uint16_t bswap_16(uint16_t x){
    return (x>>8) | (x<<8);
}

#ifdef ARCH_ARM
static always_inline uint32_t bswap_32(uint32_t x){
    uint32_t t;
    __asm__ (
      "eor %1, %0, %0, ror #16 \n\t"
      "bic %1, %1, #0xFF0000   \n\t"
      "mov %0, %0, ror #8      \n\t"
      "eor %0, %0, %1, lsr #8  \n\t"
      : "+r"(x), "+r"(t));
    return x;
}
#else
static always_inline uint32_t bswap_32(uint32_t x){
    x= ((x<<8)&0xFF00FF00) | ((x>>8)&0x00FF00FF);
    return (x>>16) | (x<<16);
}
#endif

static inline uint64_t bswap_64(uint64_t x)
{
#if 0
    x= ((x<< 8)&0xFF00FF00FF00FF00ULL) | ((x>> 8)&0x00FF00FF00FF00FFULL);
    x= ((x<<16)&0xFFFF0000FFFF0000ULL) | ((x>>16)&0x0000FFFF0000FFFFULL);
    return (x>>32) | (x<<32);
#else
    union {
        uint64_t ll;
        uint32_t l[2];
    } w, r;
    w.ll = x;
    r.l[0] = bswap_32 (w.l[1]);
    r.l[1] = bswap_32 (w.l[0]);
    return r.ll;
#endif
}
#endif  /* !ARCH_X86 */

#endif  /* !HAVE_BYTESWAP_H */

// be2me ... BigEndian to MachineEndian
// le2me ... LittleEndian to MachineEndian

#ifdef WORDS_BIGENDIAN
#define be2me_16(x) (x)
#define be2me_32(x) (x)
#define be2me_64(x) (x)
#define le2me_16(x) bswap_16(x)
#define le2me_32(x) bswap_32(x)
#define le2me_64(x) bswap_64(x)
#else
#define be2me_16(x) bswap_16(x)
#define be2me_32(x) bswap_32(x)
#define be2me_64(x) bswap_64(x)
#define le2me_16(x) (x)
#define le2me_32(x) (x)
#define le2me_64(x) (x)
#endif

#endif /* __BSWAP_H__ */
