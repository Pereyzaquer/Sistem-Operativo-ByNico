#+#+#+#+######################################################################
#/**
# * @file Makefile
# * @brief Build del módulo del kernel + utilidades de deploy y test.
# * @details
# * Makefile para compilar como módulo externo (Kbuild), instalar/cargar/descargar,
# * y compilar/ejecutar el test de userspace.
# */
#+#+#+#+######################################################################

## External module Makefile (run this on the BeagleBone / target Linux)
## Layout:
##   - scr/mpu6050_driver.c   (kernel module)
##   - inc/                  (headers)
##   - mpu6050_npp.dts        (optional overlay source)

MOD_NAME   := mpu6050_driver
SRC_DIR    := scr
INC_DIR    := inc
BIN_DIR    := bin

# Userspace test program (reads /dev/mpu6050 continuously)
TEST_SRC   := test/mpu6050_test.c
TEST_BIN   := $(BIN_DIR)/mpu6050_test
TEST_FRAMES ?= 10
TEST_DELAY_MS ?= 200



KERNEL_DIR := /lib/modules/$(shell uname -r)/build
PWD        := $(shell pwd)

MODULE_BUILD_KO := $(SRC_DIR)/$(MOD_NAME).ko
MODULE_BIN_KO   := $(BIN_DIR)/$(MOD_NAME).ko
INSTALL_DIR     := /lib/modules/$(shell uname -r)/extra

# Overlay source (auto-detect to avoid "file not found" on the target)
# Prefer the new name, fall back to older filenames if present.
DTS_SRC   ?= $(firstword $(wildcard mpu6050_npp.dts mpu6050_mydto.dts))
DTBO_OUT  ?= $(BIN_DIR)/$(basename $(notdir $(DTS_SRC))).dtbo
FW_DIR    := /lib/firmware

# dtc flags
DTC_FLAGS ?= -@ -I dts -O dtb
# These warnings are common/benign for overlays that patch existing nodes
DTC_WARNINGS ?= -Wno-unit_address_vs_reg -Wno-chosen_node_is_root

# Kbuild: build module from scr/
obj-m += $(SRC_DIR)/$(MOD_NAME).o

# Extra flags for this module
ccflags-y += -std=gnu99 -Wno-declaration-after-statement -I$(src)/$(INC_DIR)

.PHONY: help all build fix_perms bin install deploy load unload reload clean logs follow dtbo dtbo_install debug_on debug_off test_build test

# If you ever ran targets with sudo, it's common for ./bin to become root-owned.
# That breaks clean/copy/link steps for the regular user.
fix_perms:
	@if [ -d "$(BIN_DIR)" ] && [ ! -w "$(BIN_DIR)" ]; then \
		echo "[WARN] '$(BIN_DIR)' no es escribible (probablemente root-owned por un sudo anterior)."; \
		echo "[INFO] Arreglando ownership de '$(BIN_DIR)' para el usuario actual..."; \
		sudo chown -R "$$(id -un):$$(id -gn)" "$(BIN_DIR)"; \
	fi

help:
	@echo "Targets disponibles:" 
	@echo "  make build        - Compila el módulo (genera $(MODULE_BUILD_KO))" 
	@echo "  make bin          - Compila y copia el .ko a $(MODULE_BIN_KO)" 
	@echo "  make install       - Instala el .ko en $(INSTALL_DIR) y corre depmod" 
	@echo "  make load          - Carga el módulo (modprobe si está instalado, si no insmod desde bin/)" 
	@echo "  make unload        - Descarga el módulo (rmmod)" 
	@echo "  make reload        - unload + install + load (lo típico para iterar)" 
	@echo "  make logs          - Muestra logs del kernel filtrando por [mpu6050]" 
	@echo "  make follow        - Sigue logs en vivo (dmesg -w) filtrando por [mpu6050]" 
	@echo "  make debug_on      - (Opcional) Habilita pr_debug vía dynamic_debug para este módulo" 
	@echo "  make debug_off     - (Opcional) Deshabilita pr_debug vía dynamic_debug" 
	@echo "  make dtbo          - (Opcional) Compila $(DTS_SRC) a $(DTBO_OUT) (requiere dtc)" 
	@echo "  make dtbo_install  - (Opcional) Copia el .dtbo a $(FW_DIR)" 
	@echo "  make test_build    - Compila el programa de prueba (genera $(TEST_BIN))" 
	@echo "  make test          - Ejecuta el programa de prueba (usa TEST_FRAMES / TEST_DELAY_MS)" 
	@echo "  make clean         - Limpia artefactos del build" 

# Comando principal para desarrollo: recompila, instala y recarga
all: reload

build:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

bin: build fix_perms
	@mkdir -p $(BIN_DIR)
	@cp -f $(MODULE_BUILD_KO) $(MODULE_BIN_KO)
	@echo "[OK] Copiado: $(MODULE_BIN_KO)"

install: bin
	@sudo install -D -m 0644 $(MODULE_BIN_KO) $(INSTALL_DIR)/$(MOD_NAME).ko
	@sudo depmod -a
	@echo "[OK] Instalado en: $(INSTALL_DIR)/$(MOD_NAME).ko"

deploy: install

load:
	@sudo modprobe $(MOD_NAME) || sudo insmod $(MODULE_BIN_KO)

unload:
	@if lsmod | grep -q "^$(MOD_NAME)\\b"; then \
		echo "[INFO] Intentando unbind de platform devices para liberar refcnt"; \
		if [ -d /sys/bus/platform/drivers ]; then \
			for drv in /sys/bus/platform/drivers/*; do \
				if [ -L "$$drv/module" ]; then \
					modlink=$$(readlink -f "$$drv/module" 2>/dev/null || true); \
					if [ "$$modlink" = "/sys/module/$(MOD_NAME)" ]; then \
						echo "[INFO] Encontrado driver sysfs: $$drv"; \
						if [ -w "$$drv/unbind" ]; then \
							for d in $$(ls -1 "$$drv" 2>/dev/null | grep -v -E '^(bind|unbind|uevent|module|driver_override|power|subsystem)$$' || true); do \
								echo "[INFO] unbind: $$d"; \
								sudo sh -c "echo $$d > $$drv/unbind" || true; \
							done; \
						fi; \
					fi; \
				fi; \
			done; \
		fi; \
		echo "[INFO] Descargando modulo: $(MOD_NAME)"; \
		if [ "$$(id -u)" -ne 0 ]; then sudo -v; fi; \
		sudo rmmod $(MOD_NAME) || ( \
			echo "[ERROR] rmmod fallo. Diagnostico:"; \
			lsmod | grep -E "^$(MOD_NAME)\\b" || true; \
			(cat /sys/module/$(MOD_NAME)/refcnt 2>/dev/null || cat /proc/modules | grep -E "^$(MOD_NAME)\\b" || true) | sed 's/^/[INFO] refcnt: /'; \
			echo "[INFO] fuser /dev/mpu6050:"; \
			sudo fuser -av /dev/mpu6050 || true; \
			echo "[HINT] Si refcnt=1 y fuser esta vacio, suele ser un refcount pegado por un OOPS previo; en ese caso hace falta reboot/power-cycle."; \
			exit 1; \
		); \
	else \
		echo "[INFO] Modulo no cargado: $(MOD_NAME)"; \
	fi


reload: unload install
	@echo "[INFO] Cargando modulo: $(MOD_NAME)" 
	@sudo modprobe $(MOD_NAME) || sudo insmod $(MODULE_BIN_KO)
	@lsmod | grep -E "^$(MOD_NAME)\\b" || (echo "[ERROR] El modulo no quedo cargado"; exit 1)

logs:
	@sudo dmesg | grep -E "\\[mpu6050\\]|mpu6050" || ( \
		echo "[WARN] No hay logs que matcheen '[mpu6050]'/'mpu6050'."; \
		echo "[HINT] Si /dev/mpu6050 no existe, probablemente el modulo no esta cargado: make reload"; \
		echo "[HINT] Estado: lsmod | grep -E '^$(MOD_NAME)\\b'"; \
		echo "[HINT] Ultimos logs sin filtro: sudo dmesg | tail -n 120"; \
		true; \
	)

follow:
	@sudo dmesg -wH | grep -E --line-buffered "\[mpu6050\]"

# pr_debug normalmente requiere dynamic_debug (CONFIG_DYNAMIC_DEBUG) o DEBUG.
# Esto intenta habilitarlo vía /sys/kernel/debug/dynamic_debug/control.
debug_on:
	@sudo sh -c "echo 'module $(MOD_NAME) +p' > /sys/kernel/debug/dynamic_debug/control" 
	@echo "[OK] dynamic_debug habilitado para $(MOD_NAME)"

debug_off:
	@sudo sh -c "echo 'module $(MOD_NAME) -p' > /sys/kernel/debug/dynamic_debug/control" 
	@echo "[OK] dynamic_debug deshabilitado para $(MOD_NAME)"

dtbo:
	@mkdir -p $(BIN_DIR)
	@dtc $(DTC_FLAGS) $(DTC_WARNINGS) -o $(DTBO_OUT) $(DTS_SRC)
	@echo "[OK] Generado: $(DTBO_OUT)"

dtbo_install: dtbo
	@sudo install -D -m 0644 $(DTBO_OUT) $(FW_DIR)/$(notdir $(DTBO_OUT))
	@echo "[OK] Instalado: $(FW_DIR)/$(notdir $(DTBO_OUT))"

clean: fix_perms
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	@rm -rf $(BIN_DIR) || (echo "[WARN] rm fallo, usando sudo para borrar $(BIN_DIR)"; sudo rm -rf $(BIN_DIR))

test_build: fix_perms
	@mkdir -p $(BIN_DIR) || true
	@out="$(TEST_BIN)"; \
	if [ ! -w "$(BIN_DIR)" ]; then \
		echo "[WARN] '$(BIN_DIR)' no es escribible (posible root-owned por un sudo anterior)."; \
		echo "[WARN] Compilando test en ./mpu6050_test (workaround)"; \
		out="./mpu6050_test"; \
	fi; \
	gcc -O2 -Wall -Wextra -o "$$out" "$(TEST_SRC)"; \
	echo "[OK] Compilado: $$out"

test: test_build
	@echo "[INFO] Ejecutando test: (bin preferido) $(TEST_BIN) $(TEST_FRAMES) $(TEST_DELAY_MS)"
	@echo "[INFO] Ctrl+C para salir"
	@set -e; \
	bin="$(TEST_BIN)"; \
	if [ ! -x "$$bin" ] && [ -x "./mpu6050_test" ]; then bin="./mpu6050_test"; fi; \
	if [ ! -x "$$bin" ]; then echo "[ERROR] No encuentro binario de test ejecutable (ni $(TEST_BIN) ni ./mpu6050_test)"; exit 2; fi; \
	if [ -r /dev/mpu6050 ]; then \
		"$$bin" $(TEST_FRAMES) $(TEST_DELAY_MS); \
	else \
		echo "[INFO] /dev/mpu6050 no es legible para el usuario; usando sudo"; \
		sudo "$$bin" $(TEST_FRAMES) $(TEST_DELAY_MS); \
	fi
