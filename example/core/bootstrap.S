/* bootstrap.S - Simplified Bootstrap code for CPU core startup */

#define PSR_F_BIT         0x00000040
#define PSR_I_BIT         0x00000080
#define PSR_A_BIT         0x00000100
#define PSR_D_BIT         0x00000200

#define PSR_MODE_EL0t     0x00000000
#define PSR_MODE_EL1t     0x00000004
#define PSR_MODE_EL1h     0x00000005
#define PSR_MODE_EL2t     0x00000008
#define PSR_MODE_EL2h     0x00000009
#define PSR_MODE_SVC_32   0x00000013

#define SCR_RW_BIT        0x00000400
#define SCR_SMD_BIT       0x00000080
#define SCR_RES_BITS      0x00000030
#define SCR_NS_BIT        0x00000001

#define MT_DEVICE_nGnRnE  0
#define MT_DEVICE_nGnRE   1
#define MT_DEVICE_GRE     2
#define MT_NORMAL_NC      3
#define MT_NORMAL         4
#define MAIR(_attr, _mt)  ((_attr) << ((_mt) * 8))

#define TCR_T0SZ(x)       ((64 - (x)))
#define TCR_T1SZ(x)       ((64 - (x)) << 16)
#define TCR_TxSZ(x)       (TCR_T0SZ(x) | TCR_T1SZ(x))

#define TCR_IRGN0_WBWC    (1 << 8)
#define TCR_IRGN_NC       ((0 << 8) | (0 << 24))
#define TCR_IRGN_WBWA     ((1 << 8) | (1 << 24))
#define TCR_IRGN_WT       ((2 << 8) | (2 << 24))
#define TCR_IRGN_WBnWA    ((3 << 8) | (3 << 24))
#define TCR_IRGN_MASK     ((3 << 8) | (3 << 24))

#define TCR_ORGN0_WBWC    (1 << 10)
#define TCR_ORGN_NC       ((0 << 10) | (0 << 26))
#define TCR_ORGN_WBWA     ((1 << 10) | (1 << 26))
#define TCR_ORGN_WT       ((2 << 10) | (2 << 26))
#define TCR_ORGN_WBnWA    ((3 << 10) | (3 << 26))
#define TCR_ORGN_MASK     ((3 << 10) | (3 << 26))

#define TCR_SH0_ISH       (3 << 12)
#define TCR_SHARED        ((3 << 12) | (3 << 28))

#define TCR_TG0_4K        (0 << 14)
#define TCR_TG0_64K       (1 << 14)
#define TCR_TG1_4K        (2 << 30)
#define TCR_TG1_64K       (3 << 30)

#define TCR_PS_4G         (0 << 16)
#define TCR_PS_64G        (1 << 16)
#define TCR_PS_1T         (2 << 16)
#define TCR_PS_4T         (3 << 16)
#define TCR_PS_16T        (4 << 16)
#define TCR_PS_256T       (5 << 16)

// TODO: Change this as it is hardcoded for now!!
#define TCR_PS TCR_PS_1T

#define TCR_EL2_RES1      ((1 << 23) | (1 << 31))

/* Stack configuration */
#define STACK_SIZE               0x1000      // 4KiB per CPU
#define STACK_BITS               12          // 2^12 = 4KiB

/* Standard function decorators. */
#define BEGIN_FUNC(_name) \
    .global _name ; \
    .type _name, %function ; \
_name:

#define END_FUNC(_name) \
    .size _name, .-_name ; \
_name##_end:

/*
 * Disable the MMU.
 *
 * Arguments:
 *   system control register for the appropriate exception level
 *   temporary register
 *
 * This clears bits 0, 2 and 12 in the control register
 * which map to M (MMU disable), C (cache disable) and I (icache disable)
 * bits.
 */
.macro disable_mmu sctlr tmp
    mrs     \tmp, \sctlr
    bic     \tmp, \tmp, #(1 << 0)
    bic     \tmp, \tmp, #(1 << 2)
    bic     \tmp, \tmp, #(1 << 12)
    msr     \sctlr, \tmp
    isb
.endm

.macro disable_id_cache sctlr tmp
    mrs     \tmp, \sctlr
    bic     \tmp, \tmp, #(1 << 2)
    bic     \tmp, \tmp, #(1 << 12)
    msr     \sctlr, \tmp
    isb
.endm

/* Assembler Macros */
.macro dcache op
    dsb     sy
    mrs     x0, clidr_el1
    and     x3, x0, #0x7000000
    lsr     x3, x3, #23

    cbz     x3, finished_\op
    mov     x10, #0

loop1_\op:
    add     x2, x10, x10, lsr #1
    lsr     x1, x0, x2
    and     x1, x1, #7
    cmp     x1, #2
    b.lt    skip_\op

    msr     csselr_el1, x10
    isb

    mrs     x1, ccsidr_el1
    and     x2, x1, #7
    add     x2, x2, #4
    mov     x4, #0x3ff
    and     x4, x4, x1, lsr #3
    clz     w5, w4
    mov     x7, #0x7fff
    and     x7, x7, x1, lsr #13

loop2_\op:
    mov     x9, x4

loop3_\op:
    lsl     x6, x9, x5
    orr     x11, x10, x6
    lsl     x6, x7, x2
    orr     x11, x11, x6
    dc      \op, x11
    subs    x9, x9, #1
    b.ge    loop3_\op
    subs    x7, x7, #1
    b.ge    loop2_\op

skip_\op:
    add     x10, x10, #2
    cmp     x3, x10
    b.gt    loop1_\op

finished_\op:
    mov     x10, #0
    msr     csselr_el1, x10
    dsb     sy
    isb
.endm

/*
 * Enable the MMU.
 *
 * Arguments:
 *   system control register for the appropriate exception level
 *   temporary register
 *
 * This set bits 0, 2 and 12 in the control register
 * which map to M (MMU enable), C (cache enable) and I (icache enable)
 * bits.
 */
.macro enable_mmu sctlr tmp
    mrs     \tmp, \sctlr
    orr     \tmp, \tmp, #(1 << 0)
    orr     \tmp, \tmp, #(1 << 2)
    orr     \tmp, \tmp, #(1 << 12)
    msr     \sctlr, \tmp
    isb
.endm

BEGIN_FUNC(bootstrap_start)
    // Get current CPU ID from MPIDR_EL1
    mrs x0, mpidr_el1
    and x0, x0, #0xFF           // Extract CPU ID (bits [7:0])

    // Calculate stack pointer for this CPU
    // Stack formula: STACK_BASE + (cpu_id * STACK_SIZE) + STACK_SIZE
    lsl x1, x0, #STACK_BITS        // x1 = cpu_id * 4096 (STACK_SIZE)
    adrp x2, cpu_stacks            // load base page of cpu_stacks
    add  x2, x2, :lo12:cpu_stacks  // add offset within page
    add  x1, x2, x1                // x1 = base + cpu_id * STACK_SIZE
    add  x1, x1, #STACK_SIZE       // x1 = top of stack for this CPU
    mov  sp, x1


    bl secondary_cpu_entry
   
END_FUNC(bootstrap_start)

/* Invalidate the I-cache */
BEGIN_FUNC(invalidate_icache)
    ic      iallu
    dsb     nsh
    isb
    ret
END_FUNC(invalidate_icache)

/* Flush the D-cache */
BEGIN_FUNC(flush_dcache)
    dcache  cisw
    ret
END_FUNC(flush_dcache)

BEGIN_FUNC(el2_mmu_enable)
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    /* Disable caches */
    bl      flush_dcache

    /* Ensure I-cache, D-cache and mmu are disabled for EL2/Stage1 */
    disable_mmu sctlr_el2, x8

    /*
     * Invalidate the local I-cache so that any instructions fetched
     * speculatively are discarded.
     */
    bl      invalidate_icache

    /*
     *   DEVICE_nGnRnE      000     00000000
     *   DEVICE_nGnRE       001     00000100
     *   DEVICE_GRE         010     00001100
     *   NORMAL_NC          011     01000100
     *   NORMAL             100     11111111
     */
    ldr     x5, =MAIR(0x00, MT_DEVICE_nGnRnE) | \
                 MAIR(0x04, MT_DEVICE_nGnRE) | \
                 MAIR(0x0c, MT_DEVICE_GRE) | \
                 MAIR(0x44, MT_NORMAL_NC) | \
                 MAIR(0xff, MT_NORMAL)
    msr     mair_el2, x5

    ldr     x8, =TCR_T0SZ(48) | TCR_IRGN0_WBWC | TCR_ORGN0_WBWC | TCR_SH0_ISH | TCR_TG0_4K | TCR_PS | TCR_EL2_RES1
    msr     tcr_el2, x8
    isb

    /* Setup page tables */
    adrp    x8, boot_lvl0_lower
    msr     ttbr0_el2, x8
    isb

    /* invalidate all TLB entries for EL2 */
    tlbi    alle2is
    dsb     ish
    isb

    enable_mmu  sctlr_el2, x8

    /* Invalidate instruction cache */
    ic  ialluis
    dsb ish
    isb
    /* invalidate all TLB entries for EL2 */
    tlbi    alle2is
    dsb     ish
    isb

    ldp     x29, x30, [sp], #16
    ret

END_FUNC(el2_mmu_enable)
