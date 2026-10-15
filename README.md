# Proyecto 2 - MPI y DES

CC3069 Computación Paralela y Distribuida, Universidad del Valle de Guatemala.

| Integrante | Carné |
| --- | --- |
| Ihan Marroquin | 23108 |
| Luis Gonzalez | 23353 |
| Diego Patzan | 23525 |

Repositorio existente: https://github.com/UVG-Tareas-y-proyectos/Proyecto2_MPI

## Estado final

Se implementaron DES secuencial y OpenMPI. `src/bruteforce.c` es código propio.
El formato DES-ECB con PKCS#7 está documentado en `data/formato.txt`. La búsqueda
MPI permite comparar `--distribution block` y `--distribution cyclic`; el reparto
cíclico es el valor predeterminado.

El formato original del repositorio sigue siendo el predeterminado. La opción
`--format course` agrega compatibilidad con los archivos creados mediante el
`bruteforce.c` proporcionado en clase, sin modificar `data/texto.bin`.

El texto elegido por el equipo ya está cifrado en `data/texto.bin`. La pista humana
es «Le tiene miedo a las mujeres» y la frase literal de búsqueda es `Estudiante`.
El original y la clave no se incluyen en el repositorio público.

Grupo 6. El informe final está disponible como `Informe_Final_Proyecto_2.docx`,
`Informe_Final_Proyecto_2.pdf` y fuente verificable `Informe_Final.md`.

## Compilación

Dentro de Ubuntu o WSL Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential make git openmpi-bin libopenmpi-dev libssl-dev pkg-config python3
make
```

En Windows con Docker Desktop instalado, el proyecto incluye un contenedor Ubuntu:

```powershell
docker build -t proyecto2-mpi-des .
docker run --rm --mount "type=bind,source=$($PWD.Path),target=/workspace" proyecto2-mpi-des make
```

Las claves y los originales se guardan en `private/`, excluido de Git.
Los ejecutables se generan en `build/`, también excluido.

## Cifrar, descifrar y recuperar

Los siguientes comandos se ejecutan **dentro de Ubuntu o del contenedor**. Los
archivos `private/texto_original.txt` y `private/clave_equipo.txt` existen sólo
en la máquina del equipo. La clave es un índice decimal entre 0 y `2^56-1`.

```bash
make
./build/des_tool encrypt --input private/texto_original.txt --output data/texto.bin --key-file private/clave_equipo.txt
./build/des_tool decrypt --input data/texto.bin --output private/texto_recuperado.txt --key-file private/clave_equipo.txt
cmp private/texto_original.txt private/texto_recuperado.txt
./build/des_tool search --input data/texto.bin --keyword 'Estudiante' --start 0 --end 1048576 --output private/recuperado_secuencial.txt
mpirun --oversubscribe -np 2 ./build/bruteforce_mpi --input data/texto.bin --keyword 'Estudiante' --start 0 --end 1048576 --output private/recuperado_mpi2.txt
mpirun --oversubscribe -np 4 ./build/bruteforce_mpi --input data/texto.bin --keyword 'Estudiante' --start 0 --end 1048576 --output private/recuperado_mpi4.txt
cmp private/texto_original.txt private/recuperado_mpi4.txt
```

### Probar un archivo publicado por otro grupo

Para los archivos cifrados con el código base de la clase se agrega
`--format course`. Por ejemplo, para el texto del Grupo 7 ubicado en el
Escritorio y su rango `[0, 4194304)`:

```bash
mkdir -p private
mpirun --oversubscribe -np 4 ./build/bruteforce_mpi \
  --input /mnt/c/Users/VICTUS/Desktop/texto.bin \
  --keyword 'se le olvida como manejar' \
  --start 0 --end 4194304 \
  --format course \
  --output private/grupo7_recuperado.txt
```

Este caso debe reportar `key=3412587` y `status=found`. Sin `--format course`,
el programa conserva el comportamiento PKCS#7 usado por el cifrado del Grupo 6.

En PowerShell se puede anteponer `.\scripts\run_docker.ps1` a cada comando
(usar `./build/...` para la ruta interna Linux), por ejemplo:

```powershell
.\scripts\run_docker.ps1 make
.\scripts\run_docker.ps1 ./build/des_tool encrypt --input private/texto_original.txt --output data/texto.bin --key-file private/clave_equipo.txt
.\scripts\run_docker.ps1 ./build/des_tool decrypt --input data/texto.bin --output private/texto_recuperado.txt --key-file private/clave_equipo.txt
.\scripts\run_docker.ps1 cmp private/texto_original.txt private/texto_recuperado.txt
.\scripts\run_docker.ps1 mpirun --oversubscribe -np 4 ./build/bruteforce_mpi --input data/texto.bin --keyword Estudiante --start 0 --end 1048576
```

`--oversubscribe` permite lanzar cuatro procesos aun si Docker asigna menos
nucleos.
La búsqueda nunca lee el archivo de clave. El intervalo de prueba es `[0,2^20)`,
no el dominio completo DES `[0,2^56)`. El programa acepta ambos límites.

## Validación y mediciones

```bash
make test
make benchmark
make benchmark-check
```

`make test` usa casos artificiales y un vector conocido para comprobar DES
independientemente del propio descifrado. El log es `evidence/verificacion.log`.

Tiempo secuencial: reloj monotónico del propio proceso. Tiempo MPI: máximo de
los intervalos medidos con `MPI_Wtime` por cada proceso. Ambos excluyen lectura
y preparación criptográfica; MPI incluye la comunicación de las tandas.

`make benchmark` genera tres casos controlados en un espacio de 2^20 y ejecuta
210 mediciones: secuencial y MPI con reparto por bloques o cíclico, 2, 4 y 6
procesos, y 10 repeticiones por configuración. Los datos están en
`evidence/distribution_benchmark.csv`; los promedios, speedups y eficiencias en
`evidence/distribution_summary.csv`. `make benchmark-check` recalcula y valida
la bitácora.

## Entregas

`dist/entrega_final.zip` reúne el código fuente, el informe DOCX/PDF, el texto
cifrado y la evidencia pública. La clave y el plaintext permanecen fuera de Git;
pueden entregarse al docente por un canal separado si solicita una verificación.
El paquete se genera con `python3 scripts/package.py` y se comprueba con
`python3 scripts/final_check.py`.

No se atribuyen resultados a otros equipos: los insumos disponibles solo incluyen
el ciphertext del Grupo 6. El programa y la metodología quedan listos para agregar
dos casos externos compatibles cuando se reciban sus archivos y palabras literales.
