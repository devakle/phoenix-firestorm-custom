# Workflow de desarrollo (Firestorm custom)

## Ramas

- `master` — espejo local de `upstream/master` (FirestormViewer/phoenix-firestorm), siempre fast-forward puro.
- `emi-custom` — rama de trabajo principal con los cambios propios.
- Feature/bugfix → SIEMPRE en su propia rama `type/descripcion` nacida de `emi-custom`, no commits directos.

## Flujo por feature (obligatorio)

1. Actualizar base: `.\scripts\update-viewer.ps1` (fetch upstream, ff master, push, rebase emi-custom).
2. Nacer la rama desde `emi-custom`: `git checkout emi-custom && git checkout -b feat/<desc>`.
3. Implementar y commiteAR por unidad de trabajo:
   - Un commit = un comportamiento entregable (feature, fix, docs, refactor) — NO por tipo de archivo.
   - Tests y docs van en el MISMO commit que el comportamiento que verifican/describen.
   - Mensaje: Conventional Commits `type(scope): descripcion`.
   - Sin trailers de atribución IA (no `Co-Authored-By`).
4. Push y PR: `git push -u origin feat/<desc>` → `gh pr create` contra `emi-custom`.
   - PR: título conventional, descripción >20 chars (lo exige `.github/workflows/check-pr.yaml`), resumen de qué cambia y por qué.

## Detección de features

Al tocar código, identificar unidades de trabajo independientes (cada una commitea sola):

- Una feature funcional nueva.
- Un bugfix.
- Un refactor que no cambia comportamiento.
- Docs/UI de un cambio visible al usuario.
- Ajustes de build/tooling.

## Normas

- Rebase (nunca merge de upstream) para historia lineal.
- No commitear si el arbol tiene cambios sin commitear en archivos trackeados.
- Verificar antes de commitear: `git diff --stat`, `git log --oneline -5`.
- Buscar código con `rg` (ripgrep) para este repo — no usar otros buscadores.

## Comando de build (incremental, sin CMake manual)

- MSBuild: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` (localizable vía VS Installer `vswhere.exe`).
- Proyecto del viewer: `.\build-vc180-64\newview\firestorm-bin.vcxproj` (configs `Release`/`Debug`, platform `x64`). Solución completa: `.\build-vc180-64\Firestorm.slnx`.
- Build del viewer:
  ```
  & "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" .\build-vc180-64\newview\firestorm-bin.vcxproj -p:Configuration=Release -p:Platform=x64 -m -clp:ErrorsOnly
  ```
- Flags MSBuild en PowerShell con `-p:` (NO `//p:`: lo rechaza). `-m` = multi-proc, `-clp:ErrorsOnly` = solo errores.
- Cada shell es nueva sesión: `cmake` y `MSBuild.exe` NO están en PATH — usar la ruta absoluta o `vswhere`.
