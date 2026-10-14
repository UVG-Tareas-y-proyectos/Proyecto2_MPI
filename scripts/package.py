"""Prepara el paquete reproducible de la entrega final."""
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1]
dist = root / 'dist'
dist.mkdir(exist_ok=True)
files = []
for folder in ['src', 'include', 'scripts', 'evidence', 'data']:
    files.extend(path for path in (root / folder).rglob('*')
                 if path.is_file() and '__pycache__' not in path.parts)
files.extend(root / name for name in [
    'README.md', 'Informe_Final.md', 'Informe_Final_Proyecto_2.docx',
    'Informe_Final_Proyecto_2.pdf', 'Makefile', 'Dockerfile', '.gitignore',
    '.gitattributes', '.dockerignore'
])
output = dist / 'entrega_final.zip'
missing = [str(path.relative_to(root)) for path in files if not path.is_file()]
if missing:
    raise SystemExit('Faltan archivos locales para preparar la entrega: ' + ', '.join(missing))
with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for path in files:
        relative = path.relative_to(root)
        archive.write(path, relative)
print(f'Paquete creado: {output.relative_to(root)} ({len(files)} archivos).')
