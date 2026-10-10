# Proyecto 2 - MPI y DES

CC3069 Computación Paralela y Distribuida, Universidad del Valle de Guatemala.

| Integrante | Carné |
| --- | --- |
| Ihan Marroquin | 23108 |
| Luis Gonzalez | 23353 |
| Diego Patzan | 23525 |

Repositorio existente: https://github.com/UVG-Tareas-y-proyectos/Proyecto2_MPI

## Estado del avance

Se implementaron DES secuencial y OpenMPI. Por indicación del usuario,
`src/bruteforce.c` es código propio. El formato es DES-ECB con PKCS#7 y está
documentado en `data/formato.txt`; queda por confirmar si es el formato acordado
por la clase. No se afirma compatibilidad con un ejemplo que no fue facilitado.

El texto elegido por el equipo ya está cifrado en `data/texto.bin`. La pista humana
es «Le tiene miedo a las mujeres» y la frase literal de búsqueda es `Estudiante`.
El original y la clave no se incluyen en el repositorio público.

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
La carpeta `docs/` contiene documentación privada del equipo y se conserva
sólo en la copia local; no se incluye al clonar el repositorio público.

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
nucleos; la asignación real de CPU se registra en `evidence/entorno.txt`.
La búsqueda nunca lee el archivo de clave. El intervalo de prueba es `[0,2^20)`,
no el dominio completo DES `[0,2^56)`. El programa acepta ambos límites.

## Validación y mediciones

```bash
make test
make measure
python3 scripts/package.py avance
python3 scripts/package.py foro --formato-propio
```

`make test` usa casos artificiales y un vector conocido para comprobar DES
independientemente del propio descifrado. El log es `evidence/verificacion.log`.
`make measure` ejecuta un calentamiento excluido y diez repeticiones secuenciales,
diez con 2 procesos y diez con 4. Cada recuperación se compara byte a byte.
El CSV y el log completos están en `private/evidence/`, porque los resultados
de búsqueda y los intentos revelan la clave. Los promedios están en
`evidence/promedios.txt`. Se fija la misma clave, cifrado, rango y compilación
en todas las repeticiones; el CSV identifica el commit del código.

Tiempo secuencial: reloj monotónico del propio proceso. Tiempo MPI: máximo de
los intervalos medidos con `MPI_Wtime` por cada proceso. Ambos excluyen lectura
y preparación criptográfica; MPI incluye la comunicación de las tandas.

## Entregas

`dist/foro_formato_propio.zip` contiene solamente el cifrado, los metadatos y
la publicación preparada. La aceptación del formato de clase sigue pendiente.
`dist/avance_preliminar.zip` incluye fuentes, documentación privada del equipo,
`docs/Avances.pdf`, `docs/Avances.docx`, planificación
y evidencia; sus logs privados de recuperación se destinan al docente y revelan
la solución. La clave de verificación se mantiene aparte en
`private/clave_equipo.txt`, para compartirla con el docente cuando la solicite.

Para preparar el paquete del avance se necesitan los documentos de `docs/`
y los datos de `private/` en la copia local. `scripts/render_report.py` llama
al generador privado para producir el DOCX; el PDF se exporta con Word o
LibreOffice y se revisa antes de empaquetar.

No se hicieron comparaciones con otros equipos, optimizaciones de speedup ni
ejecuciones en varias máquinas. Las capturas de terminal real y las fechas Canvas
siguen pendientes.
