"""Comprueba la bitácora pública, el informe y el paquete final."""
from pathlib import Path
import csv
import hashlib
import statistics
import zipfile

root = Path(__file__).resolve().parents[1]
with (root / 'evidence/distribution_benchmark.csv').open(newline='', encoding='utf-8') as stream:
    rows = list(csv.DictReader(stream))
assert len(rows) == 210 and all(row['correcto'] == 'true' for row in rows)
groups = {}
for row in rows:
    key = (row['caso'], row['algoritmo'], row['distribucion'], row['procesos'])
    groups.setdefault(key, []).append(row)
assert len(groups) == 21
for group in groups.values():
    assert len(group) == 10
    assert sorted(int(row['repeticion']) for row in group) == list(range(1, 11))

with (root / 'evidence/distribution_summary.csv').open(newline='', encoding='utf-8') as stream:
    summaries = list(csv.DictReader(stream))
assert len(summaries) == 21
for summary in summaries:
    key = (summary['caso'], summary['algoritmo'], summary['distribucion'], summary['procesos'])
    mean = statistics.mean(float(row['segundos']) for row in groups[key])
    assert abs(mean - float(summary['promedio_segundos'])) < 1e-9

docx = root / 'Informe_Final_Proyecto_2.docx'
pdf = root / 'Informe_Final_Proyecto_2.pdf'
assert docx.stat().st_size > 100_000 and pdf.stat().st_size > 100_000
assert pdf.read_bytes().startswith(b'%PDF-')
with zipfile.ZipFile(docx) as document:
    assert document.testzip() is None
    xml = document.read('word/document.xml').decode('utf-8')
    for expected in ['Ihan Marroquin', '23108', 'Luis Gonzalez', '23353', 'Diego Patzan', '23525']:
        assert expected in xml

assert hashlib.sha256((root / 'data/texto.bin').read_bytes()).hexdigest() == (
    'ecf5afd1419d78339e37d970db5d136803161dfcb3444c80bdd297da7d9218db'
)

with zipfile.ZipFile(root / 'dist/entrega_final.zip') as archive:
    assert archive.testzip() is None
    required = {
        'Makefile', 'README.md', 'Informe_Final.md', 'Informe_Final_Proyecto_2.docx',
        'Informe_Final_Proyecto_2.pdf', 'src/des_common.c', 'src/des_tool.c',
        'src/bruteforce.c', 'include/des_common.h', 'data/texto.bin',
        'data/formato.txt', 'evidence/distribution_benchmark.csv',
        'evidence/distribution_summary.csv', 'scripts/validate_benchmark.py'
    }
    assert required <= set(archive.namelist())
    assert not any(name.startswith(('private/', 'build/', '.git/', 'docs/')) for name in archive.namelist())
    assert 'evidence/COMANDOS_EJECUTADOS.md' not in archive.namelist()
    for name in required:
        assert archive.read(name) == (root / name).read_bytes()
print('Cierre verificado: 210 mediciones, 21 resúmenes, informe íntegro y paquete final reproducible.')
