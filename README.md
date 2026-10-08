# Proyecto 2 - MPI y DES

CC3069 Computación Paralela y Distribuida, Universidad del Valle de Guatemala.

| Integrante | Carné |
| --- | --- |
| Ihan Marroquin | 23108 |
| Luis Gonzalez | 23353 |
| Diego Patzan | 23525 |

Repositorio existente: https://github.com/UVG-Tareas-y-proyectos/Proyecto2_MPI

## Estado inicial

Se prepara un avance con DES secuencial y OpenMPI. El código es una implementación
propia provisional: falta el `bruteforce.c` del docente para comprobar la
representación de la clave, el modo y el relleno. No hay cifrado aprobado para el
foro hasta verificar esa compatibilidad y recibir el texto elegido por el equipo.

El número de grupo y las fechas vigentes de Canvas están pendientes.

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
Consultar `docs/ESTADO_AVANCE.md` para los resultados y pendientes reales.
