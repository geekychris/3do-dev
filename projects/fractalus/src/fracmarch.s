; Voxel ray-march inner loop for Fractalus's terrain (3DO port).
;
;   int frac_march(struct FracMarch *m);
;
;   struct FracMarch {          offset
;       LONG ax, az;             0, 4   ray position << 12 (camera folded in)
;       LONG sx, sz;             8, 12  per-sample step << 12
;       LONG thr, mst;          16, 20  draw threshold for h * R_PROJ, its step
;       LONG count;             24      samples left in this segment (>= 1)
;       const UBYTE *map;       28      129 x 129 height bytes (wrap padded)
;       LONG h;                 32      out: height of the sample that draws
;   };
;
; Advances sample by sample until one passes h * 220 >= thr (returns 1,
; state written back, h set) or the segment's samples run out (returns 0).
; The bilinear sample is Terrain::height_at_world's arithmetic on the
; byte heights: x and z fractions are 6 bits, heights are bytes << 1.
; In C the compiler spills the ray state to the stack every sample; here
; it all stays in registers (about 37 instructions per sample).

        AREA    |C$$code|, CODE, READONLY

        EXPORT  frac_march
frac_march
        STMFD   sp!, {r4-r11, lr}
        STR     r0, [sp, #-4]!
        LDMIA   r0, {r1-r8}             ; ax az sx sz thr mst count map
march_loop
        ADD     r1, r1, r3              ; ax += sx
        ADD     r2, r2, r4              ; az += sz
        ADD     r5, r5, r6              ; thr += mst
        MOV     r11, r1, ASR #12
        AND     r11, r11, #63           ; x fraction (6 bits)
        MOV     r12, r2, ASR #12
        AND     r12, r12, #63           ; z fraction
        MOV     r9, r1, ASR #18
        AND     r9, r9, #127            ; cell x
        MOV     r10, r2, ASR #18
        AND     r10, r10, #127          ; cell z
        ADD     r10, r10, r10, LSL #7   ; z * 129
        ADD     r10, r10, r9
        LDRB    r9, [r10, r8]!          ; b00 (r10 -> cell)
        LDRB    r0, [r10, #1]           ; b10
        LDRB    r14, [r10, #129]        ; b01
        LDRB    r10, [r10, #130]        ; b11
        SUB     r0, r0, r9
        MUL     r0, r11, r0             ; (b10 - b00) * fx
        MOV     r9, r9, LSL #1
        ADD     r9, r9, r0, ASR #5      ; a = h00 + ((h10 - h00) * tx >> 12)
        SUB     r10, r10, r14
        MUL     r10, r11, r10           ; (b11 - b01) * fx
        MOV     r14, r14, LSL #1
        ADD     r14, r14, r10, ASR #5   ; b
        SUB     r14, r14, r9
        MUL     r14, r12, r14           ; (b - a) * fz
        ADD     r9, r9, r14, ASR #6     ; h
        MOV     r0, r9, LSL #8
        SUB     r0, r0, r9, LSL #5
        SUB     r0, r0, r9, LSL #2      ; h * 220
        CMP     r0, r5
        BGE     march_found
        SUBS    r7, r7, #1
        BNE     march_loop
        LDR     r0, [sp], #4
        STMIA   r0, {r1-r7}
        MOV     r0, #0
        LDMFD   sp!, {r4-r11, pc}
march_found
        SUB     r7, r7, #1
        LDR     r0, [sp], #4
        STMIA   r0, {r1-r7}
        STR     r9, [r0, #32]
        MOV     r0, #1
        LDMFD   sp!, {r4-r11, pc}

        END
