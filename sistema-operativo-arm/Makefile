# nombre del proyecto
APP=ejer3

# toolchain
CHAIN=arm-none-eabi

# Roots
ELF_LOCAL_PATH = $(OBJ)$(APP).elf
HOME_PATH = $(shell pwd)

ELF_PATH = $(HOME_PATH)/$(ELF_LOCAL_PATH)

# flags del compilador
CFLAGS=-std=gnu99 -Wall -mcpu=cortex-a8 -ffreestanding -marm -O0 -fno-inline

# estructura de carpetas del proyecto
OBJ=obj/
BIN=bin/
INC=inc/
SRC=kernel/ sys/ proc/
ASM=core/
LST=lst/

# linker script
LD=memmap.ld

# placa a emular en la maquina virtual de QEMU
BOARD=realview-pb-a8

# flags para el QEMU
PORT=1234
VMFLGS= -M $(BOARD) -m 32M -no-reboot -nographic -monitor telnet:127.0.0.1:$(PORT),server,nowait

# Puerto del qemu, este se configura en el .gdbinit
PORT_AUX=2159

# motor de maquina virtual a utilizar
VME=qemu-system-arm

# binario del kernel a ejecutar en el QEMU
BINF=$(BIN)$(APP).bin

# Colores
RED=\033[0;31m
YELLOW=\033[0;33m
GREEN=\033[0;32m
NC=\033[0m

# construcción del proyecto
all: $(BINF) $(OBJ)$(APP).elf
	@echo -e "$(GREEN)\n\n\t>> Compilacion exitosa ✅$(NC)"

# Crear el binario a partir del archivo ELF
$(BINF): $(OBJ)$(APP).elf
	$(CHAIN)-objcopy -O binary $< $@

# Enlazar los archivos de objeto
$(OBJ)$(APP).elf: $(OBJ)objs
	@echo "Linkeando ..."
	mkdir -p $(OBJ)
	mkdir -p $(LST)
	@$(CHAIN)-gcc -g -T $(LD) $(OBJ)*.o -o $(OBJ)$(APP).elf -Wl,-Map=$(LST)$(APP).map -nostartfiles -lc 2>&1 \
	| sed -e 's/warning:/$(YELLOW)warning:$(NC)/g' -e 's/error:/$(RED)error:$(NC)/g'
	@echo "Linkeo finalizado!!"
	@echo ""
	@echo "Generando archivos de información: mapa de memoria y símbolos"
	readelf -a $(OBJ)$(APP).elf > $(LST)$(APP)_elf.txt
	$(CHAIN)-objdump -D $(OBJ)$(APP).elf > $(LST)$(APP).lst

# Crear los archivos de objeto a partir de los archivos fuente en sys, proc y kernel
$(OBJ)objs: $(SRC) $(ASM)
	@echo ""
	mkdir -p $(BIN)
	mkdir -p $(OBJ)
	mkdir -p $(LST)
	@echo "Compilando y ensamblando archivos en sys/, proc/ y kernel/ ..."
	@if [ -n "$(wildcard $(SRC)*.c)" ]; then \
		for src_dir in $(SRC); do \
			for c_file in $$src_dir*.c; do \
				obj_file=$(OBJ)$$(basename $$c_file .c).o; \
				$(CHAIN)-gcc -g $(CFLAGS) -c $$c_file -o $$obj_file 2>&1 \
				| sed -e 's/warning:/$(YELLOW)warning:$(NC)/g' -e 's/error:/$(RED)error:$(NC)/g'; \
			done; \
		done; \
	fi
	@echo "Compilando y ensamblando archivos en core/ ..."
	@for asm_file in $(ASM)*.s; do \
		obj_file=$(OBJ)$$(basename $$asm_file .s).o; \
		lst_file=$(LST)$$(basename $$asm_file .s).lst; \
		$(CHAIN)-as -g $$asm_file -o $$obj_file -a > $$lst_file; \
	done

clean:
	rm -rf $(OBJ)*.o
	rm -rf $(OBJ)*.elf
	rm -rf $(BIN)*.bin
	rm -rf $(LST)*.lst
	rm -rf $(LST)*.txt
	rm -rf $(LST)*.map

run:
	$(VME) $(VMFLGS) -kernel $(BINF)

debug:
	$(VME) $(VMFLGS) -kernel $(BINF) -S -gdb tcp::2159

connect:
	telnet localhost $(PORT)

kill:
	kill -9 $$(lsof -ti:$(PORT_AUX))

help:
	@echo "Comandos disponibles:"
	@echo "  make          \t\t- Compila y enlaza el proyecto"
	@echo "  make clean    \t\t- Elimina los archivos generados"
	@echo "  make run      \t\t- Ejecuta el binario en QEMU"
	@echo "  make debug    \t\t- Ejecuta el binario en QEMU en modo de depuración"
	@echo "  make connect  \t\t- Conecta a QEMU utilizando telnet"
	@echo "  make kill\t\t- Libera el puerto $(PORT_AUX)"
	@echo ""
	@echo "Esto va en donde se enuentra gdbinit."
	@echo "  ddd --debugger gdb-multiarch $(ELF_PATH)"
	@echo "(Abre el archivo ELF con el debugger DDD)"
