/*
 * Copyright (C) 2014-2015,  Netronome Systems, Inc.  All rights reserved.
 *
 * @file          lib/nfp/_c/pcie.c
 * @brief         NFP PCIe interface
 */

#include <assert.h>
#include <nfp.h>

#include <nfp6000/nfp_pcie.h>
#include <nfp6000/nfp_me.h>

#include <nfp/pcie.h>

struct _pcie_dma_cfg_word_access {
    union {
        struct {
#ifdef BIGENDIAN
            unsigned short odd;
            unsigned short even;
#else
            unsigned short even;
            unsigned short odd;
#endif
        };
        unsigned int __raw;
    };
};


__intrinsic void
__pcie_c2p_barcfg(unsigned int pcie_isl, unsigned char bar_idx,
                  unsigned int addr_hi, unsigned int addr_lo,
                  unsigned char req_id, sync_t sync, SIGNAL *sig)
{
    unsigned int isl, bar_addr, tmp;
    __xwrite unsigned int bar_val;

    ctassert(__is_ct_const(sync));
    ctassert(sync == sig_done || sync == ctx_swap);

    isl = pcie_isl << 30;
    bar_addr = NFP_PCIE_BARCFG_C2P(bar_idx);

    __asm dbl_shf[tmp, addr_hi, addr_lo, >>27];
    tmp &= NFP_PCIE_BARCFG_C2P_ADDR_msk;

    /* Configure RID if req_id is non-zero or not constant */
    if ((!__is_ct_const(req_id)) || (req_id != 0)) {
        tmp |= NFP_PCIE_BARCFG_C2P_ARI_ENABLE;
        tmp |= NFP_PCIE_BARCFG_C2P_ARI(req_id);
    }

    bar_val = tmp;

    if (sync == sig_done)
        __asm pcie[write_pci, bar_val, isl, <<8, bar_addr, 1], sig_done[*sig];
    else
        __asm pcie[write_pci, bar_val, isl, <<8, bar_addr, 1], ctx_swap[*sig];
}


__intrinsic void
pcie_c2p_barcfg(unsigned int pcie_isl, unsigned char bar_idx,
                unsigned int addr_hi, unsigned int addr_lo,
                unsigned char req_id)
{
    SIGNAL sig;
    __pcie_c2p_barcfg(pcie_isl, bar_idx, addr_hi, addr_lo, req_id,
                      ctx_swap, &sig);
}


#define _PCIE_C2P_CMD(cmdname, data, isl, bar, addr_hi, addr_lo,        \
                      size, max_size, sync, sig)                        \
do {                                                                    \
    struct nfp_mecsr_prev_alu ind;                                      \
    unsigned int count = (size >> 2);                                   \
    unsigned int max_count = (max_size >> 2);                           \
    unsigned int addr;                                                  \
                                                                        \
    ctassert(__is_ct_const(sync));                                      \
    try_ctassert(size != 0);                                            \
    try_ctassert(__is_aligned(size, 4));                                \
    try_ctassert(size <= 128);                                          \
    ctassert(sync == sig_done || sync == ctx_swap);                     \
                                                                        \
    addr = (isl << 30) | ((bar & 0x7) << 27) | ((addr_hi & 0xf) << 24); \
                                                                        \
    if (__is_ct_const(size)) {                                          \
        if (size <= 32) {                                               \
            if (sync == sig_done)                                       \
                __asm { pcie[cmdname, *data, addr, <<8, addr_lo,        \
                             __ct_const_val(count)], sig_done[*sig] }   \
            else                                                        \
                __asm { pcie[cmdname, *data, addr, <<8, addr_lo,        \
                             __ct_const_val(count)], ctx_swap[*sig] }   \
        } else {                                                        \
            /* Setup length in PrevAlu for the indirect */              \
            ind.__raw = 0;                                              \
            ind.ov_len = 1;                                             \
            ind.length = count - 1;                                     \
                                                                        \
            if (sync == sig_done) {                                     \
                __asm { alu[--, --, B, ind.__raw] }                     \
                __asm { pcie[cmdname, *data, addr, <<8, addr_lo,        \
                             __ct_const_val(count)], sig_done[*sig],    \
                             indirect_ref }                             \
            } else {                                                    \
                __asm { alu[--, --, B, ind.__raw] }                     \
                __asm { pcie[cmdname, *data, addr, <<8, addr_lo,        \
                             __ct_const_val(count)], ctx_swap[*sig],    \
                             indirect_ref }                             \
            }                                                           \
        }                                                               \
    } else {                                                            \
        /* Setup length in PrevAlu for the indirect */                  \
        ind.__raw = 0;                                                  \
        ind.ov_len = 1;                                                 \
        ind.length = count - 1;                                         \
                                                                        \
        if (sync == sig_done) {                                         \
            __asm { alu[--, --, B, ind.__raw] }                         \
            __asm { pcie[cmdname, *data, addr, <<8, addr_lo,            \
                         __ct_const_val(max_count)], sig_done[*sig],    \
                         indirect_ref }                                 \
        } else {                                                        \
            __asm { alu[--, --, B, ind.__raw] }                         \
            __asm { pcie[cmdname, *data, addr_hi, <<8, addr_lo,         \
                         __ct_const_val(max_count)], ctx_swap[*sig],    \
                         indirect_ref }                                 \
        }                                                               \
    }                                                                   \
} while (0)

__intrinsic void
__pcie_read(__xread void *data, unsigned int pcie_isl, unsigned char bar_idx,
            unsigned int addr_hi, unsigned int addr_lo,
            size_t size, size_t max_size, sync_t sync, SIGNAL *sig)
{
    ctassert(__is_read_reg(data));
    _PCIE_C2P_CMD(read, data, pcie_isl, bar_idx, addr_hi, addr_lo, size, \
                  max_size, sync, sig);
}

__intrinsic void
pcie_read(__xread void *data, unsigned int pcie_isl, unsigned char bar_idx,
          unsigned int addr_hi, unsigned int addr_lo, size_t size)
{
    SIGNAL sig;

    ctassert(__is_ct_const(size));
    __pcie_read(data, pcie_isl, bar_idx, addr_hi, addr_lo, size, size,
                ctx_swap, &sig);
}

__intrinsic void
__pcie_write(__xwrite void *data, unsigned int pcie_isl, unsigned char bar_idx,
             unsigned int addr_hi, unsigned int addr_lo, size_t size,
             size_t max_size, sync_t sync, SIGNAL *sig)
{
    ctassert(__is_write_reg(data));
    _PCIE_C2P_CMD(write, data, pcie_isl, bar_idx, addr_hi, addr_lo, size, \
                  max_size, sync, sig);
}

__intrinsic void
pcie_write(__xwrite void *data, unsigned int pcie_isl, unsigned char bar_idx,
           unsigned int addr_hi, unsigned int addr_lo, size_t size)
{
    SIGNAL sig;

    ctassert(__is_ct_const(size));
    __pcie_write(data, pcie_isl, bar_idx, addr_hi, addr_lo, size, size,
                 ctx_swap, &sig);
}

__intrinsic void
pcie_dma_cfg_set_one(unsigned int pcie_isl, unsigned int index,
                     struct pcie_dma_cfg_one new_cfg)
{
    __xrw unsigned int cfg;
    struct _pcie_dma_cfg_word_access cfg_tmp;
    SIGNAL sig;
    unsigned int count;
    unsigned int reg_no;
    unsigned int addr_lo;
    __gpr unsigned int addr_hi;

    count = ((sizeof cfg) >> 2);
    reg_no = (((index >> 1) & 0x7) << 2);
    addr_lo = NFP_PCIE_DMA_CFG0 + reg_no;
    pcie_isl << 30;

    /* Read original configuration */
    __asm pcie[read_pci, cfg, addr_hi, <<8, addr_lo, __ct_const_val(count)], \
        ctx_swap[sig];

    /* Modify the configuration */
    cfg_tmp.__raw = cfg;
    if (index & 1)
        /* Odd */
        cfg_tmp.odd = new_cfg.__raw;
    else
        /* Even */
        cfg_tmp.even = new_cfg.__raw;

    cfg = cfg_tmp.__raw;

    /* Write back new configuration */
    __asm pcie[write_pci, cfg, addr_hi, <<8, addr_lo, \
               __ct_const_val(count)], ctx_swap[sig];
}

__intrinsic void
__pcie_dma_cfg_set_pair(unsigned int pcie_isl, unsigned int index,
                        __xwrite struct nfp_pcie_dma_cfg *new_cfg,
                        sync_t sync, SIGNAL *sig)
{
    unsigned int count;
    unsigned int reg_no;
    unsigned int addr_lo;
    __gpr unsigned int addr_hi;

    count = (sizeof(struct nfp_pcie_dma_cfg) >> 2);
    reg_no = (((index >> 1) & 0x7) << 2);
    addr_lo = NFP_PCIE_DMA_CFG0 + reg_no;
    addr_hi = pcie_isl << 30;

    if (sync == ctx_swap)
        __asm pcie[write_pci, *new_cfg, addr_hi, <<8, addr_lo, \
                   __ct_const_val(count)], ctx_swap[*sig];
    else
        __asm pcie[write_pci, *new_cfg, addr_hi, <<8, addr_lo, \
                   __ct_const_val(count)], sig_done[*sig];
}

__intrinsic void
pcie_dma_cfg_set_pair(unsigned int pcie_isl, unsigned int index,
                      __xwrite struct nfp_pcie_dma_cfg *new_cfg)
{
    SIGNAL sig;

    __pcie_dma_cfg_set_pair(pcie_isl, index, new_cfg, ctx_swap, &sig);
}

__intrinsic void
pcie_dma_set_sig(void *cmd, unsigned int meid, unsigned int ctx,
                 unsigned int sig_no)
{
    struct nfp_pcie_dma_cmd *cmd_ptr;
    unsigned int mode_msk;
    unsigned int tmp;

    mode_msk = ((1 << 31 - 1) - (NFP_PCIE_DMA_CMD_DMA_MODE_shf - 1));
    tmp = (((meid & 0xF) << 13) | (((meid >> 4) & 0x3F) << 7) |
           ((ctx & 0x7) << 4) | sig_no);
    cmd_ptr = cmd;
    cmd_ptr->__raw[1] = ((tmp << NFP_PCIE_DMA_CMD_DMA_MODE_shf) |
                         (cmd_ptr->__raw[1] & ~mode_msk));
}

__intrinsic void
pcie_dma_set_event(void *cmd, unsigned int type, unsigned int source)
{
    struct nfp_pcie_dma_cmd *cmd_ptr;

    cmd_ptr = cmd;
    cmd_ptr->mode_sel = NFP_PCIE_DMA_CMD_DMA_MODE_2;
    cmd_ptr->dma_mode = (((type & 0xF) << 12) | (source & 0xFFF));
}

__intrinsic void
__pcie_dma_enq(unsigned int pcie_isl, __xwrite struct nfp_pcie_dma_cmd *cmd,
               unsigned int queue, sync_t sync, SIGNAL *sig)
{
    unsigned int count = (sizeof(struct nfp_pcie_dma_cmd) >> 2);
    unsigned int addr_hi = pcie_isl << 30;

    ctassert(__is_write_reg(cmd));
    ctassert(__is_ct_const(sync));
    ctassert(sync == sig_done || sync == ctx_swap);

    if (sync == ctx_swap)
        __asm pcie[write_pci, *cmd, addr_hi, <<8, queue, \
                   __ct_const_val(count)], ctx_swap[*sig];
    else
        __asm pcie[write_pci, *cmd, addr_hi, <<8, queue, \
                   __ct_const_val(count)], sig_done[*sig];
}

__intrinsic void
pcie_dma_enq(unsigned int pcie_isl, __xwrite struct nfp_pcie_dma_cmd *cmd,
             unsigned int queue)
{
    SIGNAL sig;
    __pcie_dma_enq(pcie_isl, cmd, queue, ctx_swap, &sig);
}

__intrinsic void
pcie_dma_enq_no_sig(unsigned int pcie_isl,
                    __xwrite struct nfp_pcie_dma_cmd *cmd, unsigned int queue)
{
    unsigned int count = (sizeof(struct nfp_pcie_dma_cmd) >> 2);
    unsigned int addr_hi = pcie_isl << 30;

    ctassert(__is_write_reg(cmd));

    __asm pcie[write_pci, *cmd, addr_hi, <<8, queue, __ct_const_val(count)];
}
