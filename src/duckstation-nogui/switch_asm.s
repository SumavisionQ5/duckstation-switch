.global QuickContextRestore
QuickContextRestore:
    mov x18, x0

    ldr x0, [x18, #248]
    mov sp, x0

    ldp x0, x1, [x18, #0]
    ldp x2, x3, [x18, #16]
    ldp x4, x5, [x18, #32]
    ldp x6, x7, [x18, #48]
    ldp x8, x9, [x18, #64]
    ldp x10, x11, [x18, #80]
    ldp x12, x13, [x18, #96]
    ldp x14, x15, [x18, #112]
    ldp x16, x17, [x18, #128]
    ldp x19, x20, [x18, #152]
    ldp x21, x22, [x18, #168]
    ldp x23, x24, [x18, #184]
    ldp x25, x26, [x18, #200]
    ldp x27, x28, [x18, #216]
    ldp x29, x30, [x18, #232]

    ldr x18, [x18, #256]
    br x18
