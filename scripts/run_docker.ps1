# Ejecutar desde PowerShell: .\scripts\run_docker.ps1 make
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($args.Count -eq 0) { throw 'Indica un comando: make, python3 scripts/verify.py, etc.' }
& docker run --rm --mount "type=bind,source=$projectRoot,target=/workspace" proyecto2-mpi-des @args
exit $LASTEXITCODE
