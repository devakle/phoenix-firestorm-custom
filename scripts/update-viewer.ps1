#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Actualiza el viewer Firestorm custom desde upstream y rebasa la rama custom.
.DESCRIPTION
    Flujo recomendado:
      1. fetch upstream/master
      2. fast-forward master (base limpia => siempre ff-only, nunca merge)
      3. push de master a origin (tu fork)
      4. rebase de la rama custom sobre master
     Opcional (--rebuild): re-configura y recompila con autobuild.
.PARAMETER Branch
    Rama custom sobre la que trabajas (default: emi-custom).
.PARAMETER Rebuild
    Si esta presente, despues del update re-configura y compila.
.PARAMETER Config
    Target de autobuild (default: ReleaseFS_open). Solo se usa con --rebuild.
.EXAMPLE
    ./scripts/update-viewer.ps1
    ./scripts/update-viewer.ps1 -Branch emi-custom -Rebuild
#>
[CmdletBinding()]
param(
    [string]$Branch = "emi-custom",
    [switch]$Rebuild,
    [string]$Config = "ReleaseFS_open"
)

$ErrorActionPreference = "Stop"

function Invoke-Step($label, [ScriptBlock]$body) {
    Write-Host ""
    Write-Host "==> $label" -ForegroundColor Cyan
    & $body
    if ($LASTEXITCODE -ne 0) { throw "$label falló (exit $LASTEXITCODE)" }
}

# Do not touch the tree if there are uncommitted changes on tracked files.
    $status = git status --porcelain --untracked-files=no
    if ($status) {
        Write-Host "Hay cambios sin commitear en '$Branch'. Abortando para no pisarlos." -ForegroundColor Yellow
        exit 1
    }

    Invoke-Step "fetch upstream/master" { git fetch upstream master }

    $base = git rev-parse --short "origin/master"
    $up   = git rev-parse --short "upstream/master"
    Write-Host "origin/master: $base     upstream/master: $up"

    Invoke-Step "checkout master" { git checkout master }

    $counts = (git rev-list --left-right --count master...upstream/master)
    $behind = ($counts -split "\s+")[0] -ne "0"
    Invoke-Step "fast-forward master -> upstream/master" { git merge --ff-only upstream/master }

    if ($behind) {
        Invoke-Step "push origin master" { git push origin master }
    } else {
        Write-Host "master ya estaba al dia, sin push."
    }

    Invoke-Step "checkout $Branch" { git checkout $Branch }
    Invoke-Step "rebase $Branch sobre master" { git rebase master }

    Write-Host ""
    Write-Host "Update completo. Estado:" -ForegroundColor Green
    git status -sb

    if ($Rebuild) {
        Invoke-Step "autobuild configure -A 64 -c $Config" { autobuild configure -A 64 -c $Config }
        Invoke-Step "autobuild build -A 64 -c $Config --no-configure" { autobuild build -A 64 -c $Config --no-configure }
        Write-Host ""
        Write-Host "Build completo." -ForegroundColor Green
    }
