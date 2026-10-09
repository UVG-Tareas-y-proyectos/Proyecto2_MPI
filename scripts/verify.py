"""Pruebas reales de DES y de terminación MPI; los casos NO son del foro."""
from pathlib import Path
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
CASE = ROOT / 'private' / 'tests'
CASE.mkdir(parents=True, exist_ok=True)
LOG = ROOT / 'evidence' / 'verificacion.log'
LOG.parent.mkdir(exist_ok=True)
TOOL = './build/des_tool'
MPI = ['mpirun', '--oversubscribe', '-np', '4', './build/bruteforce_mpi']
ENV = dict(os.environ, OMPI_ALLOW_RUN_AS_ROOT='1', OMPI_ALLOW_RUN_AS_ROOT_CONFIRM='1')

def key_index(key_bytes):
    result = 0
    for byte in key_bytes:
        result = (result << 7) | (byte >> 1)
    return result

with LOG.open('w', encoding='utf-8') as log:
    def run(args, expected=0):
        import shlex
        log.write('$ ' + shlex.join(args) + '\n')
        log.flush()
        result = subprocess.run(args, capture_output=True, text=True, env=ENV, timeout=60)
        log.write(result.stdout + result.stderr + f'exit_code={result.returncode}\n\n')
        log.flush()
        if result.returncode != expected:
            raise RuntimeError(f'{args}: expected {expected}, got {result.returncode}')
        return result.stdout

    def roundtrip(name, content, key=37):
        original = CASE / (name + '.original')
        cipher = CASE / (name + '.bin')
        recovered = CASE / (name + '.recovered')
        keyfile = CASE / (name + '.key')
        original.write_bytes(content)
        keyfile.write_text(str(key) + '\n', encoding='ascii')
        run([TOOL, 'encrypt', '--input', str(original), '--output', str(cipher), '--key-file', str(keyfile)])
        run([TOOL, 'decrypt', '--input', str(cipher), '--output', str(recovered), '--key-file', str(keyfile)])
        run(['cmp', str(original), str(recovered)])
        assert recovered.read_bytes() == content
        return original, cipher, keyfile

    # Vector DES publicado: primer bloque conocido, independientemente de nuestro descifrado.
    vector_key = bytes.fromhex('133457799bbcdff1')
    vector = roundtrip('vector_des', bytes.fromhex('0123456789abcdef'), key_index(vector_key))
    assert vector[1].read_bytes()[:8].hex() == '85e813540f0ab405'
    log.write('PASS vector DES 0123456789abcdef -> 85e813540f0ab405\n')
    # Referencia externa OpenSSL CLI: exige igualdad del binario completo con PKCS#7.
    reference = CASE / 'vector_reference.bin'
    run(['openssl', 'enc', '-des-ecb', '-provider', 'default', '-provider', 'legacy', '-K', vector_key.hex(), '-in', str(vector[0]), '-out', str(reference)])
    run(['cmp', str(vector[1]), str(reference)])
    roundtrip('empty', b'')
    roundtrip('unaligned', 'Prueba de longitud irregular: ñ.\n'.encode('utf-8'))
    original, cipher, keyfile = roundtrip('binary', b'\x00prefijo\x00PISTA_PRUEBA_MPI_DISTINTIVA\x00fin\xff', 37)
    for prefix, name in [([TOOL, 'search'], 'seq'), (MPI, 'mpi')]:
        common = ['--input', str(cipher), '--keyword', 'PISTA_PRUEBA_MPI_DISTINTIVA']
        recovered = CASE / (name + '.search')
        output = run(prefix + common + ['--start', '0', '--end', '100', '--output', str(recovered)])
        assert dict(re.findall(r'^([a-z_]+)=(.*)$', output, re.M))['key'] == '37'
        run(['cmp', str(original), str(recovered)])
        missing = run(prefix + common + ['--start', '0', '--end', '37'], expected=2)
        assert dict(re.findall(r'^([a-z_]+)=(.*)$', missing, re.M))['attempts'] == '37'
        run(prefix + common + ['--start', '37', '--end', '38'])
        run(prefix + common + ['--start', '5', '--end', '6'], expected=2)
        empty = run(prefix + common + ['--start', '0', '--end', '0'], expected=2)
        assert dict(re.findall(r'^([a-z_]+)=(.*)$', empty, re.M))['attempts'] == '0'
        unchanged = cipher.read_bytes()
        run(prefix + common + ['--start', '0', '--end', '100', '--output', str(cipher)], expected=1)
        assert cipher.read_bytes() == unchanged
        run(prefix + common + ['--start', '38', '--end', '37'], expected=1)
        run(prefix + common + ['--start', '-1', '--end', '100'], expected=1)
        run(prefix + common + ['--start', '0', '--end', '72057594037927937'], expected=1)
        run(prefix + ['--input', str(CASE / 'missing.bin'), '--keyword', 'pista'], expected=1)
    multi = run(MPI + ['--input', str(cipher), '--keyword', 'PISTA_INEXISTENTE_987654321', '--start', '0', '--end', '40001'], expected=2)
    assert dict(re.findall(r'^([a-z_]+)=(.*)$', multi, re.M))['attempts'] == '40001'
    original, cipher, keyfile = roundtrip('upper_bound', b'PISTA_LIMITE_DOMINIO_56_BITS', (1 << 56) - 1)
    for prefix in [[TOOL, 'search'], MPI]:
        recovered = CASE / ('upper.' + ('mpi' if prefix == MPI else 'seq'))
        run(prefix + ['--input', str(cipher), '--keyword', 'PISTA_LIMITE_DOMINIO_56_BITS', '--start', str((1 << 56) - 2), '--end', str(1 << 56), '--output', str(recovered)])
        run(['cmp', str(original), str(recovered)])
    malformed = CASE / 'malformed.bin'
    malformed.write_bytes(b'123')
    run([TOOL, 'decrypt', '--input', str(malformed), '--output', str(CASE / 'invalid.out'), '--key-file', str(keyfile)], expected=1)
    run(MPI + ['--input', str(malformed), '--keyword', 'pista', '--end', '100'], expected=1)
    invalid_key = CASE / 'invalid.key'
    invalid_key.write_text('72057594037927936\n')
    run([TOOL, 'encrypt', '--input', str(original), '--output', str(CASE / 'invalid.bin'), '--key-file', str(invalid_key)], expected=1)
    log.write('PASS: vector externo, roundtrip vacío/irregular/binario, rangos y errores, MPI termina.\n')
print('Verificación completa. Log: evidence/verificacion.log')
