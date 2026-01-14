    .section .iram1,"ax",@progbits
    .align 16              // Must be 16-byte aligned for 'entry'

    .global inc_val
    .type inc_val, @function
inc_val:
    entry a1, 0            // a1 = frame size, 0 bytes needed here
    addi a2, a2, 1
    retw                   // use retw with 'entry'

    .align 16
    .global dec_val
    .type dec_val, @function
dec_val:
    entry a1, 0
    addi a2, a2, -1
    retw
