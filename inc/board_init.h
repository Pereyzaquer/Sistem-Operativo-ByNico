//En este caso solo se pusieron los prototipos de las funciones porque es todo lo que 
//necesita board_init.
//Por otro lado al llamar gic.h y timer.h en una mismo .h ambas entran en comflicto
//por tener una variable definida con el mismo nombre (reserved_bits()).

__attribute__((section(".init"))) void __gic_init(void);
__attribute__((section(".init"))) void __timer_init(void);
__attribute__((section(".init"))) void __board_init(void);
__attribute__((section(".init"))) void mmu_init_identity_mapping(void);

