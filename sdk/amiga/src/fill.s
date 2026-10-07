; Fast fill for the Amiga layer's 8-bit framebuffer.
;
;   void amiga_fill64(void *dst, unsigned long pattern, unsigned long blocks);
;
; dst must be word aligned; writes blocks * 64 bytes of the 32-bit pattern
; (blocks >= 1). Two 8-register STMs per 64 bytes: about 3x fewer
; instructions than the C library memset, which matters on the cache-less
; ARM60 where every instruction is a DRAM fetch.

        AREA    |C$$code|, CODE, READONLY

        EXPORT  amiga_fill64
amiga_fill64
        STMFD   sp!, {r4-r9}
        MOV     r3, r1
        MOV     r4, r1
        MOV     r5, r1
        MOV     r6, r1
        MOV     r7, r1
        MOV     r8, r1
        MOV     r9, r1
fill64_loop
        STMIA   r0!, {r1, r3-r9}
        STMIA   r0!, {r1, r3-r9}
        SUBS    r2, r2, #1
        BNE     fill64_loop
        LDMFD   sp!, {r4-r9}
        MOV     pc, lr

        END
