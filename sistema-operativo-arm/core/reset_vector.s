.code 32

.extern _start
.section .reset_vector_text
.global _reset_vector

_reset_vector:
    B _start
.end
