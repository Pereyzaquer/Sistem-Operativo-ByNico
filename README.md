# Sistem-Operativo-ByNico

Dos proyectos de sistemas operativos, de lo más bajo a lo más grande:

| Carpeta | Qué es |
|---|---|
| [`sistema-operativo-arm/`](sistema-operativo-arm) | Un mini sistema operativo *bare-metal* para **ARM Cortex-A8**, escrito en C y Assembly: paginación de dos niveles, modos de privilegio, scheduler y cambio de contexto. Se simula en QEMU y se depura con GDB/DDD. |
| [`driver-linux-mpu6050/`](driver-linux-mpu6050) | Un **driver de kernel de Linux** para un sensor MPU6050 en una **BeagleBone Black**: controlador I²C por registros e interrupciones, Device Tree Overlay y un servidor TCP que publica los datos. |

El primero se construye desde cero, con total libertad de diseño. El segundo es lo opuesto: meterse en un sistema enorme que ya existe y respetar sus reglas para sumarle un periférico. Cada carpeta tiene su propio README con la explicación, los diagramas y cómo compilar y probar.

Hechos por Nicolás Pereyra Pigerl (UTN FRBA). La explicación completa de cada uno está en las notas de mi portfolio.
