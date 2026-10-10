"""Prepara archivos del avance; bloquea foro si falta validación de clase."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('kind', choices=['avance','foro'])
parser.add_argument('--formato-propio', action='store_true', help='Paquete provisional con formato documentado, sin afirmar acuerdo de clase')
args = parser.parse_args()
dist = root / 'dist'
dist.mkdir(exist_ok=True)
if args.kind == 'foro':
    validation = root / 'data/validacion_foro.json'
    if not validation.is_file():
        raise SystemExit('Foro pendiente: falta verificar bruteforce.c, texto elegido y compatibilidad de clase.')
    record = json.loads(validation.read_text(encoding='utf-8'))
    ciphertext = root / 'data/texto.bin'
    class_checked = record.get('compatible_clase') is True
    own_format = args.formato_propio and record.get('formato_propio_autorizado') is True
    if not (class_checked or own_format) or record.get('recuperacion_exacta') is not True or not 1 <= record.get('palabras',0) <= 350 or not record.get('pista'):
        raise SystemExit('Validación del foro incompleta.')
    if hashlib.sha256(ciphertext.read_bytes()).hexdigest() != record.get('sha256_cifrado'):
        raise SystemExit('El cifrado cambió después de validarlo.')
    files = [ciphertext, root / 'data/formato.txt', root / 'docs/PUBLICACION_FORO.md']
    output = dist / ('foro.zip' if class_checked else 'foro_formato_propio.zip')
else:
    files = []
    for folder in ['src','include','scripts','docs','evidence','base','data','output/pdf']:
        files.extend(path for path in (root / folder).rglob('*') if path.is_file() and '__pycache__' not in path.parts)
    files.extend(root / name for name in ['README.md','Makefile','Dockerfile','.gitignore','.gitattributes','.dockerignore'])
    # Mediciones del equipo contienen la solución; sólo entran en la entrega docente.
    files.extend(path for path in (root / 'private/evidence').glob('*') if path.is_file())
    output = dist / 'avance_preliminar.zip'
with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for path in files:
        relative = path.relative_to(root)
        if relative.parts[:2] == ('private','evidence'):
            relative = Path('evidence_docente') / path.name
        archive.write(path, relative)
print(f'Paquete creado: {output.relative_to(root)} ({len(files)} archivos).')
