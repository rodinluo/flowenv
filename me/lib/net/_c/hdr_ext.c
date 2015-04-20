/*
 * Copyright 2012-2015 Netronome, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file          lib/net/_c/hdr_ext.c
 * @brief         Header extract code
 */

#include <assert.h>
#include <nfp.h>


#include "eth.h"
#include "hdr_ext.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"

/*
 * Some implementation notes, and notes on possible improvements:
 *
 * The functions below currently only work on LM buffers as the source
 * as LM allow arbitrary indexing.  The function should support any
 * type of destination buffers, though this has not been tested
 * extensively.
 *
 * The functions can (and should) be extended to support other sources
 * for the packet data.
 * - Supporting extraction directly from DRAM/CLS/SRAM/CTM should be
 *   pretty straight forward as the C compiler can generate the
 *   required code, for example: *(struct eth_hdr *)dst = *(__dram
 *   struct eth_hdr *)(((__dram char *)src_buf) + off); Note, however this
 *   will obviously cause the compiler to generate memory
 *   transactions.
 * - Transfer registers only provide limited index-ability with
 *   run-time computed indices.  In fact the capability is limited
 *   enough that the C compiler can't generate the code.  To support
 *   source buffers located in transfer registers, the functions below
 *   need to be extended with custom inline assembler functions
 *   explicitly setting the TINDEX CSR and make sure that no context
 *   switches happen.  The nfcc user guide has some sample code for
 *   this.  This code will also will have to deal with re-aligning the
 *   packet header.
 * - GPRs and NN registers: These registers are not indexable at all
 *   so one need to deploy a switch statement of sort to turn the
 *   run-time computable offset into a compile time one, like, e.g:
 *   switch (off) {
 *   case 2:
 *       (struct eth_hdr *)dst = *(struct eth_hdr *)(((char *)src_buf) + 2);
 *       break
 *   case 4:
 *       (struct eth_hdr *)dst = *(struct eth_hdr *)(((char *)src_buf) + 4);
 *       break
 *   [...]
 *   }
 *   This obviously explodes the code store usage of the functions.
 *
 *
 * Note: I tried to enforce certain properties in @dst using ctassert,
 * but both:
 * ctassert(__is_ct_const(dst)); and
 * ctassert(__aligned(dst, 4));
 * failed.
 */

__intrinsic int
he_eth_fit(sz, off)
{
    ctassert(sz >= sizeof(struct eth_hdr));
    return (off + sizeof(struct eth_hdr)) <= sz;
}

#define HE_ETH_FUNC(dst)                                                \
    *dst =  *(__lmem struct eth_hdr *)(((__lmem char *)src_buf) + off); \
                                                                        \
    switch(dst->type) {                                                 \
    case NET_ETH_TYPE_IPV4: next_proto = HE_IP4; break;                 \
    case NET_ETH_TYPE_TPID: next_proto = HE_8021Q; break;               \
    case NET_ETH_TYPE_IPV6: next_proto = HE_IP6; break;                 \
    default: next_proto = HE_UNKNOWN;                                   \
    }

__intrinsic unsigned int
he_eth(void *src_buf, int off, void *dst)
{
    __gpr unsigned int next_proto;

    ctassert(__is_in_lmem(src_buf));
    ctassert(__is_in_reg_or_lmem(dst));

#ifdef __HE_ETH
    #error "Attempting to redefine __HE_ETH"
#endif

    if (__is_in_lmem(dst)) {
#define __HE_ETH ((__lmem struct eth_hdr *)dst)
        HE_ETH_FUNC(__HE_ETH);
#undef __HE_ETH
    } else {
#define __HE_ETH ((__gpr struct eth_hdr *)dst)
        HE_ETH_FUNC(__HE_ETH);
#undef __HE_ETH
    }
    return HE_RES(next_proto, sizeof(struct eth_hdr));
}

__intrinsic int
he_vlan_fit(sz, off)
{
    ctassert(__is_ct_const(sz));
    ctassert(sz >= sizeof(struct vlan_hdr));
    return (off + sizeof(struct vlan_hdr)) <= sz;
}

#define HE_VLAN_FUNC(dst)                                               \
    *dst = *(__lmem struct vlan_hdr *)(((__lmem char *)src_buf) + off); \
    switch(dst->type) {                                                 \
    case NET_ETH_TYPE_IPV4: next_proto = HE_IP4; break;                 \
    case NET_ETH_TYPE_IPV6: next_proto = HE_IP6; break;                 \
    default: next_proto = HE_UNKNOWN;                                   \
    }

__intrinsic unsigned int
he_vlan(void *src_buf, int off, void *dst)
{
    __gpr unsigned int next_proto;

#ifdef __HE_VLAN
    #error "Attempting to redefine __HE_VLAN"
#endif

    ctassert(__is_in_lmem(src_buf));
    ctassert(__is_in_reg_or_lmem(dst));

    if (__is_in_lmem(dst)) {
#define __HE_VLAN ((__lmem struct vlan_hdr *)dst)
        HE_VLAN_FUNC(__HE_VLAN);
#undef __HE_VLAN
    } else {
#define __HE_VLAN ((__gpr struct vlan_hdr *)dst)
        HE_VLAN_FUNC(__HE_VLAN);
#undef __HE_VLAN
    }

    return HE_RES(next_proto, sizeof(struct vlan_hdr));
}

__intrinsic int
he_ip4_fit(sz, off)
{
    ctassert(__is_ct_const(sz));
    ctassert(sz >= sizeof(struct ip4_hdr));
    return (off + sizeof(struct ip4_hdr)) <= sz;
}

#define HE_IP4_FUNC(dst)                                                \
    *dst = *(__lmem struct ip4_hdr *)(((__lmem char *)src_buf) + off);  \
                                                                        \
    /* TODO: Detect GRE as well */                                      \
    switch(dst->proto) {                                                \
    case NET_IP_PROTO_TCP: next_proto = HE_TCP; break;                  \
    case NET_IP_PROTO_UDP: next_proto = HE_UDP; break;                  \
    default: next_proto = HE_UNKNOWN;                                   \
    }                                                                   \
                                                                        \
    if (dst->frag & NET_IP_FLAGS_MF)                                    \
        next_proto = HE_UNKNOWN;                                        \
                                                                        \
    ret = HE_RES(next_proto, 4 * dst->hl);

__intrinsic unsigned int
he_ip4(void *src_buf, int off, void *dst)
{
    __gpr unsigned int next_proto;
    __gpr int ret;

    ctassert(__is_in_lmem(src_buf));
    ctassert(__is_in_reg_or_lmem(dst));

#ifdef __HE_IP4
    #error "Attempting to redefine __HE_IP4"
#endif

    if (__is_in_lmem(dst)) {
#define __HE_IP4 ((__lmem struct ip4_hdr *)dst)
        HE_IP4_FUNC(__HE_IP4);
#undef __HE_IP4
    } else {
#define __HE_IP4 ((__gpr struct ip4_hdr *)dst)
        HE_IP4_FUNC(__HE_IP4);
#undef __HE_IP4
    }

    return ret;
}

__intrinsic int
he_ip6_fit(sz, off)
{
    ctassert(__is_ct_const(sz));
    ctassert(sz >= sizeof(struct ip6_hdr));
    return (off + sizeof(struct ip6_hdr)) <= sz;
}

/* We use this portion of the switch statement in several places for
 * parsing IPv6 and extension header. Define it as a macro to avoid
 * code duplication.
 * TODO: Detect GRE as well */
#define _IP6_PROTO_SWITCH \
    case NET_IP_PROTO_TCP: next_proto = HE_TCP; break;              \
    case NET_IP_PROTO_UDP: next_proto = HE_UDP; break;              \
    case NET_IP_PROTO_HOPOPT: next_proto = HE_IP6_HBH; break;       \
    case NET_IP_PROTO_ROUTING: next_proto = HE_IP6_RT; break;       \
    case NET_IP_PROTO_FRAG: next_proto = HE_IP6_FRAG; break;        \
    case NET_IP_PROTO_NONE: next_proto = HE_NONE; break;            \
    case NET_IP_PROTO_DSTOPTS: next_proto = HE_IP6_DST; break;      \
    default: next_proto = HE_UNKNOWN

#define HE_IP6_FUNC(dst)                                                \
    *dst = *(__lmem struct ip6_hdr *)(((__lmem char *)src_buf) + off);  \
    switch(dst->nh) {                                                   \
        _IP6_PROTO_SWITCH;                                              \
    }                                                                   \
    ret = HE_RES(next_proto, sizeof(struct ip6_hdr));

__intrinsic unsigned int
he_ip6(void *src_buf, int off, void *dst)
{
    __gpr unsigned int next_proto;
    __gpr int ret;

    ctassert(__is_in_lmem(src_buf));
    ctassert(__is_in_reg_or_lmem(dst));

#ifdef __HE_IP6
    #error "Attempting to redefine __HE_IP6"
#endif

    if (__is_in_lmem(dst)) {
#define __HE_IP6 ((__lmem struct ip6_hdr *)dst)
        HE_IP6_FUNC(__HE_IP6);
#undef __HE_IP6
    } else {
#define __HE_IP6 ((__gpr struct ip6_hdr *)dst)
        HE_IP6_FUNC(__HE_IP6);
#undef __HE_IP6
    }

    return ret;
}

/* Minimal IPv6 extension header to find out where the next header
 * starts */
struct _ip6_ext {
    uint8_t nh;                     /** Next protocol */
    uint8_t len;                    /** Length of the extension header */
    uint16_t pad0;                  /** Padding */
};

__intrinsic int
he_ip6_ext_skip_fit(sz, off)
{
    ctassert(__is_ct_const(sz));
    ctassert(sz >= sizeof(__packed struct _ip6_ext));
    return (off + sizeof(__packed struct _ip6_ext)) <= sz;
}

__intrinsic unsigned int
he_ip6_ext_skip(void *src_buf, int off)
{
    __gpr __packed struct _ip6_ext dst;
    __gpr unsigned int next_proto;
    __gpr unsigned int len;

    ctassert(__is_in_lmem(src_buf));

    dst = *(__lmem struct _ip6_ext *)(((__lmem char *)src_buf) + off);

    switch(dst.nh) {
        _IP6_PROTO_SWITCH;
    }

    /* IPv6 Frag header is just that, but otherwise the len field is
     * the number of 8 octets + 8 */
    if (next_proto == HE_IP6_FRAG)
        len = sizeof(struct ip6_frag);
    else
        len = 8 + (dst.len * 8);

    return HE_RES(next_proto, len);
}

#undef _IP6_PROTO_SWITCH

__intrinsic int
he_tcp_fit(sz, off)
{
    ctassert(__is_ct_const(sz));
    ctassert(sz >= sizeof(struct tcp_hdr));
    return (off + sizeof(struct tcp_hdr)) <= sz;
}

#define HE_TCP_FUNC(dst)                                                \
    *dst = *(__lmem struct tcp_hdr *)(((__lmem char *)src_buf) + off);  \
                                                                        \
    next_proto = HE_NONE;                                               \
                                                                        \
    ret = HE_RES(next_proto, 4 * dst->off);

__intrinsic unsigned int
he_tcp(void *src_buf, int off, void *dst)
{
    __gpr unsigned int next_proto;
    __gpr int ret;

    ctassert(__is_in_lmem(src_buf));
    ctassert(__is_in_reg_or_lmem(dst));

#ifdef __HE_TCP
    #error "Attempting to redefine __HE_TCP"
#endif

    if (__is_in_lmem(dst)) {
#define __HE_TCP ((__lmem struct tcp_hdr *)dst)
        HE_TCP_FUNC(__HE_TCP);
#undef __HE_TCP
    } else {
#define __HE_TCP ((__gpr struct tcp_hdr *)dst)
        HE_TCP_FUNC(__HE_TCP);
#undef __HE_TCP
    }

    return ret;
}

__intrinsic int
he_udp_fit(sz, off)
{
    ctassert(__is_ct_const(sz));
    ctassert(sz >= sizeof(struct udp_hdr));
    return (off + sizeof(struct udp_hdr)) <= sz;
}

__intrinsic unsigned int
he_udp(void *src_buf, int off, void *dst)
{
    __gpr unsigned int next_proto;

    ctassert(__is_in_lmem(src_buf));
    ctassert(__is_in_reg_or_lmem(dst));

    if (__is_in_lmem(dst))
        *(__lmem struct udp_hdr *)dst =
            *(__lmem struct udp_hdr *)(((__lmem char *)src_buf) + off);
    else
        *(__gpr struct udp_hdr *)dst =
            *(__lmem struct udp_hdr *)(((__lmem char *)src_buf) + off);

    /* TODO: add detection of VXLAN */
    next_proto = HE_NONE;

    return HE_RES(next_proto, sizeof(struct udp_hdr));
}