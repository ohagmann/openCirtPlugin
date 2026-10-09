<#
.SYNOPSIS
    Beendet laufende BricsCAD-Instanzen vor einem Plugin-Build.

.DESCRIPTION
    Solange BricsCAD laeuft, haelt es batchtool.brx geoeffnet. MSBuild kann die
    Datei dann nicht ersetzen und der Build bricht mit einem Linker-Fehler ab.

    Das Skript geht in zwei Stufen vor:
      1. CloseMainWindow - BricsCAD beendet sich selbst und fragt dabei wie
         gewohnt nach ungespeicherten Zeichnungen.
      2. Erst wenn das nach -TimeoutSeconds nicht gefruchtet hat, wird nach
         Ruecksprache hart beendet. Mit -Force entfaellt die Rueckfrage.

    Rueckgabewert 0 = BricsCAD laeuft nicht mehr, der Build kann starten.
    Rueckgabewert 1 = BricsCAD laeuft noch, der Build sollte abbrechen.

.PARAMETER TimeoutSeconds
    Wartezeit fuer das freundliche Beenden. Standard 30 Sekunden.

.PARAMETER Force
    Ohne Rueckfrage hart beenden. Nicht gespeicherte Zeichnungen gehen dabei
    verloren - nur fuer unbeaufsichtigte Builds gedacht.
#>
param(
    [int]$TimeoutSeconds = 30,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

function Get-BricsCadProcesses {
    # erfasst bricscad.exe UND bricscad_crash_reporter.exe
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -like 'bricscad*' }
}

$procs = @(Get-BricsCadProcesses)

if ($procs.Count -eq 0) {
    Write-Host '  BricsCAD laeuft nicht - nichts zu tun.'
    exit 0
}

Write-Host '  Laufende BricsCAD-Prozesse:'
foreach ($p in $procs) {
    $title = if ($p.MainWindowTitle) { $p.MainWindowTitle } else { '(kein Fenster)' }
    Write-Host ('    {0}  PID {1}  {2}' -f $p.ProcessName, $p.Id, $title)
}

# --- Stufe 1: freundlich beenden -------------------------------------------
Write-Host '  Fordere BricsCAD zum Beenden auf.'
Write-Host '  Etwaige Speichern-Rueckfragen bitte in BricsCAD beantworten...'

foreach ($p in $procs) {
    if (-not $p.HasExited) { [void]$p.CloseMainWindow() }
}

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
    if (@(Get-BricsCadProcesses).Count -eq 0) {
        Write-Host '  BricsCAD wurde sauber beendet.'
        exit 0
    }
    Start-Sleep -Milliseconds 500
}

# --- Stufe 2: hart beenden --------------------------------------------------
$rest = @(Get-BricsCadProcesses)
Write-Host ('  Nach {0}s laufen noch: {1}' -f $TimeoutSeconds, (($rest | ForEach-Object { $_.Id }) -join ', '))

if (-not $Force) {
    Write-Host ''
    Write-Host '  Hinweis: Ein haengender BricsCAD-Prozess reagiert nicht mehr auf'
    Write-Host '  die Schliessen-Anforderung. Hartes Beenden verwirft alles, was in'
    Write-Host '  diesem Prozess noch nicht gespeichert ist.'
    $answer = Read-Host '  Erzwungen beenden? [j/N]'
    if ($answer -notmatch '^[jJyY]') {
        Write-Host '  Abbruch - BricsCAD laeuft weiter, batchtool.brx bleibt gesperrt.'
        exit 1
    }
}

foreach ($p in @(Get-BricsCadProcesses)) {
    Write-Host ('  Beende erzwungen: {0} (PID {1})' -f $p.ProcessName, $p.Id)
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}

Start-Sleep -Seconds 2

if (@(Get-BricsCadProcesses).Count -gt 0) {
    Write-Host '  FEHLER: BricsCAD laeuft weiterhin.'
    exit 1
}

Write-Host '  BricsCAD beendet.'
exit 0
