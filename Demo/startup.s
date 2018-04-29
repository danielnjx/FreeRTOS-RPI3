.extern	system_init
.extern __bss_start
.extern __bss_end
.extern vFreeRTOS_ISR
.extern vFreeRTOS_ISR_KLUDGE
.extern vPortYieldProcessor
.extern DisableInterrupts
.extern main
	.section .init
	.globl _start
;; 
_start:
	;@ All the following instruction should be read as:
	;@ Load the address at symbol into the program counter.
	
	ldr	pc,reset_handler		;@ 	Processor Reset handler 		-- we will have to force this on the raspi!
	;@ Because this is the first instruction executed, of cause it causes an immediate branch into reset!
	
	ldr pc,undefined_handler	;@ 	Undefined instruction handler 	-- processors that don't have thumb can emulate thumb!
    ldr pc,swi_handler			;@ 	Software interrupt / TRAP (SVC) -- system SVC handler for switching to kernel mode.
    ldr pc,prefetch_handler		;@ 	Prefetch/abort handler.
    ldr pc,data_handler			;@ 	Data abort handler/
    ldr pc,unused_handler		;@ 	-- Historical from 26-bit addressing ARMs -- was invalid address handler.
    ldr pc,irq_handler			;@ 	IRQ handler
    ldr pc,fiq_handler			;@ 	Fast interrupt handler.

	;@ Here we create an exception address table! This means that reset/hang/irq can be absolute addresses
reset_handler:      .word reset
undefined_handler:  .word undefined_instruction
swi_handler:        .word vPortYieldProcessor
prefetch_handler:   .word prefetch_abort
data_handler:       .word data_abort
unused_handler:     .word unused
irq_handler:        .word vFreeRTOS_ISR
fiq_handler:        .word fiq

reset:
	/* Disable IRQ & FIQ */
	cpsid if

	/* Check for HYP mode */
	mrs r0, cpsr_all
	and r0, r0, #0x1F
	mov r8, #0x1A
	cmp r0, r8
	beq overHyped
	b continueBoot

overHyped: /* Get out of HYP mode */
	ldr r1, =continueBoot
	msr ELR_hyp, r1
	mrs r1, cpsr_all
	and r1, r1, #0x1f	;@ CPSR_MODE_MASK
	orr r1, r1, #0x13	;@ CPSR_MODE_SUPERVISOR
	msr SPSR_hyp, r1
	eret

continueBoot:
	;@	In the reset handler, we need to copy our interrupt vector table to 0x0000, its currently at 0x8000

	mov r0,#0x8000								;@ Store the source pointer
    mov r1,#0x0000								;@ Store the destination pointer.

	;@	Here we copy the branching instructions
    ldmia r0!,{r2,r3,r4,r5,r6,r7,r8,r9}			;@ Load multiple values from indexed address. 		; Auto-increment R0
    stmia r1!,{r2,r3,r4,r5,r6,r7,r8,r9}			;@ Store multiple values from the indexed address.	; Auto-increment R1

	;@	So the branches get the correct address we also need to copy our vector table!
    ldmia r0!,{r2,r3,r4,r5,r6,r7,r8,r9}			;@ Load from 4*n of regs (8) as R0 is now incremented.
    stmia r1!,{r2,r3,r4,r5,r6,r7,r8,r9}			;@ Store this extra set of data.


	;@	Set up the various STACK pointers for different CPU modes
    ;@ (PSR_IRQ_MODE|PSR_FIQ_DIS|PSR_IRQ_DIS)
    mov r0,#0xD2
    msr cpsr_c,r0
    mov sp,#0x8000

    ;@ (PSR_FIQ_MODE|PSR_FIQ_DIS|PSR_IRQ_DIS)
    mov r0,#0xD1
    msr cpsr_c,r0
    mov sp,#0x4000

    ;@ (PSR_SVC_MODE|PSR_FIQ_DIS|PSR_IRQ_DIS)
    mov r0,#0xD3
    msr cpsr_c,r0
	mov sp,#0x8000000

	ldr r0, =__bss_start
	ldr r1, =__bss_end

	mov r2, #0

zero_loop:
	cmp 	r0,r1
	it		lt
	strlt	r2,[r0], #4
	blt		zero_loop

	bl 		DisableInterrupts
	
	
	;@ 	mov	sp,#0x1000000
	b main									;@ We're ready?? Lets start main execution!

.section .text

undefined_instruction:
	b undefined_instruction

prefetch_abort:
	b prefetch_abort

data_abort:
	b data_abort

unused:
	b unused

fiq:
	b fiq
	
hang:
	b hang

.globl PUT32
PUT32:
    str r1,[r0]
    bx lr

.globl GET32
GET32:
    ldr r0,[r0]
    bx lr

;@Linux GCC version from v7 modified
.globl flushCache
flushCache:
	stmfd sp!,{r4-r5,r7,r9-r11,lr}
	bl v7_flush_dcache_all
	mov	r0, #0
	mcr	p15, 0, r0, c7, c1, 0	;@ invalidate I-cache inner shareable
	ldmfd sp!,{r4-r5,r7,r9-r11,lr}
	bx lr

;@Linux GCC version from v7 modified
.globl v7_flush_dcache_all
v7_flush_dcache_all:
	dmb								;@ ensure ordering with previous memory accesses
	mrc	p15, 1, r0, c0, c0, 1		;@ read clidr
	mov	r3, r0, lsr #23				;@ move LoC into position
	ands r3, r3, #7 << 1			;@ extract LoC*2 from clidr
	beq	finished					;@ if loc is 0, then no need to clean
start_flush_levels:
	mov	r10, #0						;@ start clean at cache level 0
flush_levels:
	add	r2, r10, r10, lsr #1		;@ work out 3x current cache level
	mov	r1, r0, lsr r2				;@ extract cache type bits from clidr
	and	r1, r1, #7					;@ mask of the bits for current cache only
	cmp	r1, #2						;@ see what cache we have at this level
	blt skip						;@ skip if no cache, or just i-cache
	mcr	p15, 2, r10, c0, c0, 0		;@ select current cache level in cssr
	isb								;@ isb to sych the new cssr&csidr
	mrc	p15, 1, r1, c0, c0, 0		;@ read the new csidr
	and	r2, r1, #7					;@ extract the length of the cache lines
	add	r2, r2, #4					;@ add 4 (line length offset)
	movw r4, #0x3ff
	ands r4, r4, r1, lsr #3		;@ find maximum number on the way size
	clz	r5, r4						;@ find bit position of way size increment
	movw r7, #0x7fff
	ands r7, r7, r1, lsr #13		;@ extract max number of the index size
loop1:
	mov r9, r7						;@ create working copy of max index
loop2:
	orr	r11, r10, r4, lsl r5		;@ factor way and cache number into r11
	orr	r11, r11, r9, lsl r2		;@ factor index number into r11
	mcr	p15, 0, r11, c7, c14, 2		;@ clean & invalidate by set/way
	subs r9, r9, #1				;@ decrement the index
	bge	loop2
	subs r4, r4, #1				;@ decrement the way
	bge	loop1
skip:
	add	r10, r10, #2				;@ increment cache number
	cmp	r3, r10
	bgt	flush_levels
finished:
	mov	r10, #0						;@ switch back to cache level 0
	mcr	p15, 2, r10, c0, c0, 0		;@ select current cache level in cssr
	dsb st
	isb
	bx lr

;@.globl flushCacheArm11
;@flushCacheArm11:
;@	mcr p15, 0, r0, c7, c10, 0

;@.globl flushCache
;@flushCache:
;@	   mrs X0, CLIDR
;@     and W3, W0, #0x07000000  // Get 2 x Level of Coherence
;@       lsr W3, W3, #23
;@       cbz W3, Finished
;@       mov W10, #0              // W10 = 2 x cache level
;@       mov W8, #1               // W8 = constant 0b1
;@Loop1: add W2, W10, W10, LSR #1 // Calculate 3 x cache level
;@       lsr W1, W0, W2           // extract 3-bit cache type for this level
;@       and W1, W1, #0x7
;@       cmp W1, #2
;@       blt Skip                // No data or unified cache at this level
;@       msr CLIDR, X10      	// Select this cache level
;@       isb                      // Synchronize change of CSSELR
;@       mrs X1, CLIDR       	// Read CCSIDR
;@       add W2, W1, #7           // W2 = log2(linelen)-4
;@       add W2, W2, #4           // W2 = log2(linelen)
;@       ubfx W4, W1, #3, #10     // W4 = max way number, right aligned
;@       clz W5, W4               /* W5 = 32-log2(ways), bit position of way in DC                                    operand */
;@       lsl W9, W4, W5           /* W9 = max way number, aligned to position in DC
;@                                   operand */
;@       lsl W16, W8, W5          // W16 = amount to decrement way number per iteration
;@Loop2: ubfx W7, W1, #13, #15    // W7 = max set number, right aligned
;@       lsl W7, W7, W2           /* W7 = max set number, aligned to position in DC
;@                                   operand */
;@       lsl W17, W8, W2          // W17 = amount to decrement set number per iteration
;@Loop3: orr W11, W10, W9         // W11 = combine way number and cache number...
;@       orr W11, W11, W7         // ... and set number for DC operand
;@       dc csw, X11              // Do data cache clean by set and way
;@       subs W7, W7, W17         // Decrement set number
;@       bge Loop3
;@       subs X9, X9, X16         // Decrement way number
;@       bge Loop2
;@Skip:  add W10, W10, #2         // Increment 2 x cache level
;@       cmp W3, W10
;@       dsb                      /* Ensure completion of previous cache maintenance
;@                                  operation */
;@       bgt Loop1
;@Finished:

