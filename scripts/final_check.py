"""Comprueba datos medidos, coincidencia de claves recuperadas y paquetes."""
from pathlib import Path
import csv
import re
import statistics
import zipfile

root = Path(__file__).resolve().parents[1]
with (root / 'private/evidence/mediciones.csv').open(newline='', encoding='utf-8') as stream:
    rows = list(csv.DictReader(stream))
assert len(rows) == 30 and all(row['correcto'] == 'true' for row in rows)
for processes in ['1','2','4']:
    group = [row for row in rows if row['procesos'] == processes]
    assert len(group) == 10
    assert sorted(int(row['repeticion']) for row in group) == list(range(1,11))
    mean = statistics.mean(float(row['segundos']) for row in group)
    assert f'procesos={processes}, n=10, promedio={mean:.9f} s' in (root / 'evidence/promedios.txt').read_text()
known_key = (root / 'private/clave_equipo.txt').read_text().strip()
log = (root / 'private/evidence/mediciones.log').read_text()
keys = re.findall(r'^key=(\d+)$', log, re.M)
assert len(keys) == 33 and set(keys) == {known_key}
assert len(re.findall(r'^exit_code=0$', log, re.M)) == 33
with zipfile.ZipFile(root / 'dist/avance_preliminar.zip') as archive:
    assert archive.testzip() is None
    required = {'Makefile','README.md','src/des_common.c','src/des_tool.c','src/bruteforce.c','include/des_common.h','data/texto.bin','data/formato.txt','output/pdf/AVANCE_PRELIMINAR_MPI.pdf','evidence_docente/mediciones.csv','evidence_docente/mediciones.log','evidence_docente/cifrado.log'}
    assert required <= set(archive.namelist())
    assert not any(name.startswith(('private/','build/','.git/')) for name in archive.namelist())
    for name in required - {'evidence_docente/mediciones.csv','evidence_docente/mediciones.log','evidence_docente/cifrado.log'}:
        assert archive.read(name) == (root / name).read_bytes()
with zipfile.ZipFile(root / 'dist/foro_formato_propio.zip') as archive:
    assert archive.testzip() is None
    assert len(archive.namelist()) == 3
    assert archive.read('data/texto.bin') == (root / 'data/texto.bin').read_bytes()
print('Cierre verificado: 30 filas y 3 calentamientos reales; claves recuperadas coinciden; paquetes íntegros y fuentes/datos completos.')
