# Análisis SIMD/NEON del filtro (TD3)

Este archivo existe para cumplir el punto del enunciado que pide **analizar las instrucciones SIMD (NEON)** generadas por el compilador al compilar la función de filtrado con `-Ofast -S`.

## 1) Cómo generar el ensamblador

Desde `Segundo_Cuatri/test/server/`:

- Generar el `.s` del filtro con los mismos flags agresivos:

```sh
make filter_asm
```

Se genera:
- `scr/filter.s`

> Nota: esto debe ejecutarse **en la BeagleBone Black** (Cortex-A8) o usando un **cross-compiler** para ARM, porque si se compila en x86_64 el assembly no va a mostrar NEON ARM.

### Opción A: compilar nativamente en la BBB

```sh
sudo apt-get install -y build-essential
make filter_asm
```

### Opción B: cross-compile desde PC (ejemplo)

```sh
arm-linux-gnueabihf-gcc -std=c11 -Wall -Wextra -Ofast -Iinc -S scr/filter.c -o scr/filter.s
```

## 2) Qué buscar en `scr/filter.s`

En ARMv7 + NEON, típicamente vas a ver instrucciones como:

- Cargas/almacenamientos vectoriales: `vld1.8`, `vld1.16`, `vst1.16`, etc.
- Operaciones vectoriales: `vadd.i16`, `vaddw`, `vmlal`, `vpadd`, etc.
- Conversión/expansión: `vmovl`, `vcvt`, etc.

El objetivo del análisis es identificar:

1. **Qué parte del algoritmo** (sumatoria, acumulación por canal, división por `n`) se vectoriza.
2. **Cómo agrupa datos** (cuántos canales por vector, alineación, loads en bloques).
3. **Si hay desenrollado (unrolling)** y si aparecen múltiples acumuladores.

## 3) Observación importante sobre este proyecto

El filtro implementado en `scr/filter.c` (`ma_get_centered`) trabaja sobre una estructura `mpu_sample_t` con 7 canales (`int16_t ch[7]`).

Eso hace que la vectorización perfecta no sea tan “natural” como si fueran 8/16 elementos alineados, porque:
- 7 no es potencia de 2
- el acceso es *array of structs* (AoS) desde el punto de vista del compilador

Aun así, `-Ofast` puede:
- desenrollar loops
- usar instrucciones NEON para sumar varios `int16_t` por iteración
- optimizar el `memcpy/memmove` interno (si existiera)

## 4) Entrega

Para la defensa, suele alcanzar con:
- capturas o extractos relevantes de `scr/filter.s`
- una explicación breve: “esta secuencia implementa la sumatoria vectorial”
- referencia a la guía NEON (Apéndice C, NEON Programmer’s Guide)
