#+#+#+#+######################################################################
#/**
# * @file Makefile
# * @brief Build del servidor TD3 (userspace).
# */
#+#+#+#+######################################################################

CC ?= gcc

SRCDIR := scr
INCDIR := inc

TARGET ?= td3_server

CFLAGS := -std=c11 -Wall -Wextra -O2 -I$(INCDIR)
# El filtrado se pide compilar -Ofast para favorecer SIMD (NEON en Cortex-A8)
CFLAGS_FILTER := -std=c11 -Wall -Wextra -Ofast -I$(INCDIR)

OBJS_COMMON := main.o server.o config.o device.o mpu6050_func.o
OBJS_FILTER := filter.o

# Defaults de ejecución (se pueden overridear: make run PORT=9090)
BIND ?= 0.0.0.0
PORT ?= 8080
DEV  ?= /dev/mpu6050
CFG  ?= server.conf
PIDFILE ?= .td3_server.pid

.PHONY: all clean help info name run run_bg pid reload stop filter_asm

all: $(TARGET)

filter.o: $(SRCDIR)/filter.c $(INCDIR)/filter.h
	$(CC) $(CFLAGS_FILTER) -c $< -o $@

%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS_COMMON) $(OBJS_FILTER)
	$(CC) -o $@ $^

help:
	@printf "%s\n" "Targets:";
	@printf "%s\n" "  all        Compila el server ($(TARGET))";
	@printf "%s\n" "  clean      Borra objetos y binario";
	@printf "%s\n" "  run        Ejecuta: ./$(TARGET) -b $(BIND) -p $(PORT) -d $(DEV) -c $(CFG)";
	@printf "%s\n" "  run_bg     Ejecuta en background y guarda PID en $(PIDFILE)";
	@printf "%s\n" "  pid        Imprime el PID actual (desde $(PIDFILE) o pidof)";
	@printf "%s\n" "  reload     Envia SIGUSR2 al server (recarga max_active_conns y filter_window)";
	@printf "%s\n" "  stop       Detiene el server usando $(PIDFILE)";
	@printf "%s\n" "  filter_asm Genera scr/filter.s con -Ofast -S (para analizar NEON/SIMD)";
	@printf "%s\n" "  info       Muestra nombre/config de build y run";
	@printf "%s\n" "  name       Imprime el nombre del binario ($(TARGET))";
	@printf "%s\n\n" "Overrides: make run PORT=9090 BIND=127.0.0.1 DEV=/dev/mpu6050 CFG=server.conf";
	@printf "%s\n" "Recarga (sin reiniciar): edita $(CFG) y ejecuta: make reload";

name:
	@printf "%s\n" "$(TARGET)"

info:
	@printf "%s\n" "TARGET=$(TARGET)";
	@printf "%s\n" "CC=$(CC)";
	@printf "%s\n" "CFLAGS=$(CFLAGS)";
	@printf "%s\n" "CFLAGS_FILTER=$(CFLAGS_FILTER)";
	@printf "%s\n" "RUN: ./$(TARGET) -b $(BIND) -p $(PORT) -d $(DEV) -c $(CFG)";

run: $(TARGET)
	./$(TARGET) -b $(BIND) -p $(PORT) -d $(DEV) -c $(CFG)

run_bg: $(TARGET)
	@rm -f $(PIDFILE)
	@./$(TARGET) -b $(BIND) -p $(PORT) -d $(DEV) -c $(CFG) & echo $$! > $(PIDFILE)
	@printf "%s\n" "PID guardado en $(PIDFILE): $$(cat $(PIDFILE))"

pid:
	@PID=""; \
	if [ -f "$(PIDFILE)" ]; then PID=$$(cat "$(PIDFILE)" 2>/dev/null); fi; \
	if [ -n "$$PID" ] && kill -0 "$$PID" 2>/dev/null; then \
		echo "$$PID"; \
	elif command -v pgrep >/dev/null 2>&1; then \
		pgrep -xo "$(TARGET)" 2>/dev/null || true; \
	else \
		pidof "$(TARGET)" 2>/dev/null || true; \
	fi

reload:
	@PID=""; \
	if [ -f "$(PIDFILE)" ]; then PID=$$(cat "$(PIDFILE)" 2>/dev/null); fi; \
	if [ -z "$$PID" ] || ! kill -0 "$$PID" 2>/dev/null; then \
		if command -v pgrep >/dev/null 2>&1; then PID=$$(pgrep -xo "$(TARGET)" 2>/dev/null); fi; \
	fi; \
	if [ -z "$$PID" ]; then echo "No hay PID del server. Sugerencia: make run_bg (crea $(PIDFILE))."; exit 1; fi; \
	if ! kill -0 "$$PID" 2>/dev/null; then echo "PID $$PID no existe (stale $(PIDFILE))."; exit 1; fi; \
	echo "Enviando SIGUSR2 a PID=$$PID"; \
	ps -o pid=,ppid=,cmd= -p "$$PID" 2>/dev/null || true; \
	kill -USR2 "$$PID"; \
	echo "OK: SIGUSR2 enviado (recarga config)."

stop:
	@if [ ! -f "$(PIDFILE)" ]; then echo "No existe $(PIDFILE)."; exit 1; fi
	@PID=$$(cat "$(PIDFILE)"); kill "$$PID" 2>/dev/null || true
	@rm -f $(PIDFILE)

filter_asm:
	$(CC) $(CFLAGS_FILTER) -S $(SRCDIR)/filter.c -o $(SRCDIR)/filter.s

clean:
	rm -f *.o $(TARGET)
