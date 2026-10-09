;;; =====================================================================
;;; ExtractDP.lsp - Datenpunkt-Extraktion für GA-FL Generierung
;;; =====================================================================
;;; Version: 1.7
;;;   - Linux: Log-Pfad ohne die Umgebungsvariable TEMP (TMPDIR, sonst /tmp)
;;; Version: 1.5
;;;   - Logging: oc-log schreibt auf Konsole UND in Logdatei (extractdp_log.txt)
;;;   - Sortierung: Hauptlinie per Y-Cluster-Analyse, Ausreisser davor
;;;   - Debug-Ausgabe aller Bloecke mit Entscheidung GESAMMELT/UEBERSPRUNGEN
;;;
;;; Extrahiert alle aktiven Datenpunkte aus der aktuellen DWG in eine
;;; temporäre CSV-Datei. Diese wird vom C++ Plugin gelesen, um die
;;; GA-FL-Blätter zu planen und zu befüllen (Phase 2).
;;;
;;; CSV-Ausgabe (UTF-8, Semikolon-getrennt):
;;;   Zeile 1: Header
;;;   Zeile 2+: AKS;BEZEICHNUNG;AKS2;REF_DP;FCODE_DP;BAS_DP;INTEG_DP;
;;;             OC_1_1_1;...;OC_4_1_1;KOMMENTAR
;;;
;;; Globale Variable (vom C++ Plugin gesetzt):
;;;   *oc-extract-dir*   → Verzeichnis für temp CSV-Dateien
;;;
;;; Ausgabedatei: <extract-dir>/<DWG-Name>.csv
;;; =====================================================================

;;; =====================================================================
;;; LOGGING
;;; =====================================================================

;; Logging-Funktion: schreibt auf Konsole UND in Logdatei (wenn Pfad gesetzt).
;; *oc-extract-log-path* wird vor dem Aufruf gesetzt (vom C++ SCR oder manuell).
;; Pro Aufruf: open/append/close (robust ueber Dokumentwechsel hinweg).
(defun oc-log (msg / f)
  (princ msg)
  (if (and (boundp '*oc-extract-log-path*) *oc-extract-log-path*)
    (progn
      (setq f (open *oc-extract-log-path* "a"))
      (if f
        (progn
          (princ msg f)
          (close f)
        )
      )
    )
  )
  msg
)

;;; =====================================================================
;;; HILFSFUNKTIONEN
;;; =====================================================================

;; Prüft ob ein Wert "aktiv" bedeutet
(defun oc-dp-is-active (val / v)
  (if (and val (= (type val) 'STR) (> (strlen val) 0))
    (progn
      (setq v (strcase (vl-string-trim " \t" val)))
      (or (= v "JA") (= v "TRUE") (= v "1") (= v "X")
          (= v "HIGH") (= v "AKTIV") (= v "WAHR"))
    )
    nil
  )
)

;; Liest Attributwert aus Block (case-insensitive)
(defun oc-dp-read-attr (block-ent attr-name / next-ent attr-data found-val)
  (setq found-val "")
  (setq next-ent (entnext block-ent))
  (while (and next-ent
              (setq attr-data (entget next-ent))
              (= (cdr (assoc 0 attr-data)) "ATTRIB"))
    (if (= (strcase (cdr (assoc 2 attr-data))) (strcase attr-name))
      (setq found-val (cdr (assoc 1 attr-data)))
    )
    (setq next-ent (entnext next-ent))
  )
  (if (not found-val) "" found-val)
)

;; Semikolons und Zeilenumbrüche in String escapen
(defun oc-dp-escape-csv (str / result i ch)
  (if (not str) (setq str ""))
  (setq result "")
  (setq i 1)
  (while (<= i (strlen str))
    (setq ch (substr str i 1))
    (cond
      ((= ch ";") (setq result (strcat result ",")))  ; Semikolon ersetzen
      ((= ch "\n") (setq result (strcat result " ")))  ; Newline ersetzen
      ((= ch "\r") nil)                                 ; CR ignorieren
      (T (setq result (strcat result ch)))
    )
    (setq i (1+ i))
  )
  result
)

;;; =====================================================================
;;; SPALTEN-NAMEN (59 VDI 3814 Funktionen, ohne OC_INTEG)
;;; =====================================================================

(defun oc-dp-get-function-columns ()
  '("OC_1_1_1" "OC_1_1_2" "OC_1_1_3" "OC_1_1_4"
    "OC_1_2_1" "OC_1_2_2"
    "OC_1_3_1" "OC_1_3_2" "OC_1_3_3" "OC_1_3_4" "OC_1_3_5"
    "OC_2_1_1" "OC_2_1_2"
    "OC_2_2_1" "OC_2_2_2" "OC_2_2_3" "OC_2_2_4" "OC_2_2_5"
    "OC_2_2_6" "OC_2_2_7" "OC_2_2_8" "OC_2_2_9" "OC_2_2_10"
    "OC_2_2_11" "OC_2_2_12"
    "OC_2_3_1" "OC_2_3_2" "OC_2_3_3" "OC_2_3_4" "OC_2_3_5"
    "OC_2_3_6" "OC_2_3_7" "OC_2_3_8" "OC_2_3_9"
    "OC_2_4_1" "OC_2_4_2" "OC_2_4_3" "OC_2_4_4" "OC_2_4_5"
    "OC_2_5_1" "OC_2_5_2" "OC_2_5_3" "OC_2_5_4"
    "OC_2_6_1" "OC_2_6_2" "OC_2_6_3" "OC_2_6_4" "OC_2_6_5"
    "OC_2_7_1" "OC_2_7_2" "OC_2_7_3" "OC_2_7_4"
    "OC_3_1_1" "OC_3_1_2" "OC_3_1_3" "OC_3_1_4" "OC_3_1_5" "OC_3_1_6"
    "OC_4_1_1"
  )
)

;;; =====================================================================
;;; PLANKOPF-ATTRIBUTE EXTRAHIEREN
;;; =====================================================================

(defun oc-dp-extract-plankopf (/ ss i ent next-ent attr-data result
                                  plankopf-attrs attr-name attr-val)
  (setq result '())
  (setq plankopf-attrs '(
    "ASP" "GEWERK" "ANLAGE" "ZEICHNUNGSNUMMER" "SSK"
    "LPH"
    "KOSTENGRUPPE" "ORTSKENNZEICHEN" "BEMERKUNG"
    "ERSTELLER" "ERSTELLDATUM" "GEPRUEFT" "NORM"
    "NAME1" "NAME2" "NAME3"
    "AN1" "AN2" "AN3" "AN4" "AN5"
    "AG1" "AG2" "AG3" "AG4" "AG5"
    "PR1" "PR2" "PR3" "PR4" "PR5"
    "FREITEXT_01" "FREITEXT_02" "FREITEXT_03"
    "FREITEXT_04" "FREITEXT_05"
    "AENDERUNG1" "AENDERUNG2" "AENDERUNG3"
    "DATUM1" "DATUM2" "DATUM3"
    "INDEX1" "INDEX2" "INDEX3"
    "ERSATZFUER"
  ))

  (setq ss (ssget "X" '((0 . "INSERT"))))
  (if ss
    (progn
      (setq i 0)
      (repeat (sslength ss)
        (setq ent (ssname ss i))
        (setq next-ent (entnext ent))
        (while (and next-ent
                    (setq attr-data (entget next-ent))
                    (= (cdr (assoc 0 attr-data)) "ATTRIB"))
          (setq attr-name (strcase (cdr (assoc 2 attr-data))))
          (setq attr-val (cdr (assoc 1 attr-data)))
          (foreach pa plankopf-attrs
            (if (= attr-name (strcase pa))
              (if (not (assoc pa result))
                (setq result (cons (cons pa attr-val) result))
              )
            )
          )
          (setq next-ent (entnext next-ent))
        )
        (setq i (1+ i))
      )
    )
  )
  result
)

;;; =====================================================================
;;; HAUPTFUNKTION
;;; =====================================================================

(defun ExtractDP (/ extract-dir dwg-name csv-path file
                    ss i ent insert-data
                    bez aks produkt dp-index
                    active-attr active-val ref-attr ref-val
                    fcode-attr fcode-val bas-attr bas-val
                    integ-attr integ-val komm-attr komm-val
                    func-cols func-attr func-val
                    csv-line dp-count
                    plankopf-data plankopf-line)

  ;; Log-Pfad setzen: im TEMP/OpenCirt_extract Verzeichnis
  ;; Windows: %TEMP%. Linux kennt TEMP nicht, dort TMPDIR, sonst /tmp.
  (if (not (and (boundp '*oc-extract-log-path*) *oc-extract-log-path*))
    (setq *oc-extract-log-path*
      (strcat (vl-string-right-trim "/\\"
                (cond ((getenv "TEMP")) ((getenv "TMPDIR")) (T "/tmp")))
              "/OpenCirt_extract/extractdp_log.txt"))
  )

  (oc-log "\n=== Datenpunkt-Extraktion gestartet ===")

  (setq extract-dir *oc-extract-dir*)
  (if (not extract-dir)
    (progn
      (oc-log "\n  FEHLER: *oc-extract-dir* nicht gesetzt!")
      (princ)
      (exit)
    )
  )

  (setq dwg-name (vl-filename-base (getvar "DWGNAME")))
  (setq csv-path (strcat extract-dir "/" dwg-name ".csv"))

  (oc-log (strcat "\n  Zeichnung: " dwg-name))
  (oc-log (strcat "\n  Ausgabe: " csv-path))

  (setq func-cols (oc-dp-get-function-columns))

  ;; OC_PRODUKT auf Block-Ebene lesen (wird fuer Sensorliste benoetigt)
  ;; Nur einmal pro Block, nicht pro DP

  (setq plankopf-data (oc-dp-extract-plankopf))

  (setq file (open csv-path "w"))
  (if (not file)
    (progn
      (oc-log (strcat "\n  FEHLER: Kann CSV nicht schreiben: " csv-path))
      (princ)
      (exit)
    )
  )

  ;; Header schreiben (PRODUKT nach INTEG_DP, KOMMENTAR als letzte Spalte)
  (setq csv-line "AKS;BEZEICHNUNG;AKS2;REF_DP;FCODE_DP;BAS_DP;INTEG_DP;PRODUKT")
  (foreach col func-cols
    (setq csv-line (strcat csv-line ";" col))
  )
  (setq csv-line (strcat csv-line ";KOMMENTAR"))
  (write-line csv-line file)

  ;; Plankopf-Daten als Kommentarzeile schreiben
  (setq plankopf-line "#PLANKOPF")
  (foreach pa plankopf-data
    (setq plankopf-line (strcat plankopf-line ";" (car pa) "=" (oc-dp-escape-csv (cdr pa))))
  )
  (write-line plankopf-line file)

  ;; ---------------------------------------------------------------
  ;; Pass 1: Alle relevanten Blöcke mit Koordinaten sammeln
  ;; ---------------------------------------------------------------
  (setq ss (ssget "X" '((0 . "INSERT"))))
  (setq dp-count 0)
  (setq block-collect '())

  (if ss
    (progn
      (oc-log (strcat "\n  DEBUG: ssget fand " (itoa (sslength ss)) " INSERT-Entities"))
      (setq i 0)
      (repeat (sslength ss)
        (setq ent (ssname ss i))
        (setq insert-data (entget ent))
        (setq dbg-blkname (cdr (assoc 2 insert-data)))
        (setq dbg-dxf66 (cdr (assoc 66 insert-data)))
        (setq dbg-layer (cdr (assoc 8 insert-data)))
        (oc-log (strcat "\n  DEBUG [" (itoa i) "]: " dbg-blkname
                       " | Layer=" dbg-layer
                       " | DXF66=" (if dbg-dxf66 (itoa dbg-dxf66) "nil")))
        (if (equal dbg-dxf66 1)
          (progn
            ;; Block sammeln wenn OC_AKS vorhanden ODER mindestens ein OC_FL_AKTIV_n aktiv
            (setq aks (oc-dp-read-attr ent "OC_AKS"))
            (setq has-active-dp nil)
            (oc-log (strcat " | OC_AKS=\"" aks "\""))
            (if (not (and aks (> (strlen aks) 0)))
              (progn
                ;; Kein OC_AKS: pruefe ob mindestens ein OC_FL_AKTIV_* aktiv ist
                (setq chk-ent (entnext ent))
                (while (and chk-ent (not has-active-dp)
                            (setq chk-data (entget chk-ent))
                            (= (cdr (assoc 0 chk-data)) "ATTRIB"))
                  (setq chk-tag (strcase (cdr (assoc 2 chk-data))))
                  (if (and (>= (strlen chk-tag) 12)
                           (= (substr chk-tag 1 12) "OC_FL_AKTIV_"))
                    (progn
                      (oc-log (strcat " | " chk-tag "=" (cdr (assoc 1 chk-data))))
                      (if (oc-dp-is-active (cdr (assoc 1 chk-data)))
                        (setq has-active-dp T)
                      )
                    )
                  )
                  (setq chk-ent (entnext chk-ent))
                )
              )
            )
            (if (or (and aks (> (strlen aks) 0)) has-active-dp)
              (progn
                (oc-log " -> GESAMMELT")
                (setq ins-pt (cdr (assoc 10 insert-data)))
                (setq block-collect
                  (cons (list (car ins-pt) (cadr ins-pt) ent) block-collect))
              )
              (oc-log " -> UEBERSPRUNGEN")
            )
          )
          (oc-log " -> KEIN ATTRIB-FLAG")
        )
        (setq i (1+ i))
      )
    )
    (oc-log "\n  DEBUG: ssget fand KEINE INSERT-Entities!")
  )

  ;; ---------------------------------------------------------------
  ;; Hauptlinien-Sortierung:
  ;; 1. Haeufigsten Y-Wert finden (LVB-Trennlinie, Toleranz 2mm)
  ;; 2. Bloecke auf Hauptlinie: X aufsteigend (links->rechts = Luftweg)
  ;; 3. Bloecke NICHT auf Hauptlinie: kommen davor (Anlagengrafik, ANL etc.)
  ;; ---------------------------------------------------------------
  (setq *oc-mainline-tol* 2.0)  ;; mm Toleranz fuer Hauptlinie

  ;; Schritt 1: Haeufigsten Y-Wert finden (Cluster-Analyse)
  (setq y-bins '())  ;; Liste von (gerundeter-Y . anzahl)
  (foreach blk block-collect
    (setq rounded-y (* (fix (+ (/ (cadr blk) *oc-mainline-tol*) 0.5)) *oc-mainline-tol*))
    (setq found-bin nil)
    (setq new-bins '())
    (foreach bin y-bins
      (if (and (not found-bin) (< (abs (- (car bin) rounded-y)) *oc-mainline-tol*))
        (progn
          (setq new-bins (append new-bins (list (cons (car bin) (1+ (cdr bin))))))
          (setq found-bin T)
        )
        (setq new-bins (append new-bins (list bin)))
      )
    )
    (if (not found-bin)
      (setq new-bins (append new-bins (list (cons rounded-y 1))))
    )
    (setq y-bins new-bins)
  )

  ;; Bin mit hoechster Anzahl finden
  (setq main-y 0.0)
  (setq max-count 0)
  (foreach bin y-bins
    (if (> (cdr bin) max-count)
      (progn
        (setq main-y (car bin))
        (setq max-count (cdr bin))
      )
    )
  )

  (oc-log (strcat "\n  Hauptlinie Y=" (rtos main-y 2 1)
                 " (" (itoa max-count) " von " (itoa (length block-collect)) " Bloecken)"))

  ;; Schritt 2: Aufteilen in Hauptlinie vs. Ausreisser
  (setq on-mainline '())
  (setq off-mainline '())
  (foreach blk block-collect
    (if (< (abs (- (cadr blk) main-y)) (* *oc-mainline-tol* 3))  ;; 6mm Toleranz fuer Zuordnung
      (setq on-mainline (cons blk on-mainline))
      (setq off-mainline (cons blk off-mainline))
    )
  )

  ;; Schritt 3: Beide Gruppen nach X aufsteigend sortieren
  (setq on-mainline
    (vl-sort on-mainline '(lambda (a b) (< (car a) (car b)))))
  (setq off-mainline
    (vl-sort off-mainline '(lambda (a b) (< (car a) (car b)))))

  ;; Ausreisser zuerst, dann Hauptlinie
  (setq block-collect (append off-mainline on-mainline))

  (oc-log (strcat "\n  " (itoa (length block-collect))
                 " Bl\U+00F6cke sortiert (" (itoa (length off-mainline))
                 " vor Hauptlinie, " (itoa (length on-mainline)) " auf Hauptlinie)"))

  ;; ---------------------------------------------------------------
  ;; Pass 2: Sortierte Blöcke extrahieren
  ;; ---------------------------------------------------------------
  (foreach blk block-collect
    (setq ent (caddr blk))
    (setq aks (oc-dp-read-attr ent "OC_AKS"))
    (setq bez (oc-dp-read-attr ent "OC_BEZEICHNUNG"))
    (setq produkt (oc-dp-read-attr ent "OC_PRODUKT"))

    (setq dp-index 1)
    (while (<= dp-index 25)
      (setq active-attr (strcat "OC_FL_AKTIV_" (itoa dp-index)))
      (setq active-val (oc-dp-read-attr ent active-attr))

      (if (oc-dp-is-active active-val)
        (progn
          (setq ref-attr (strcat "OC_REF_DP_" (itoa dp-index)))
          (setq ref-val (oc-dp-read-attr ent ref-attr))

          (setq fcode-attr (strcat "OC_FCODE_DP_" (itoa dp-index)))
          (setq fcode-val (oc-dp-read-attr ent fcode-attr))

          (setq bas-attr (strcat "OC_BAS_DP_" (itoa dp-index)))
          (setq bas-val (oc-dp-read-attr ent bas-attr))

          ;; Integrationsart: in den Datenpunktbloecken heisst das Attribut
          ;; OC_INTEGRATIONSART_DP_n (siehe Erstellliste, Spalte 32).
          ;; OC_INTEG_DP_n bleibt als Fallback fuer aeltere Blockstaende drin -
          ;; in der GA-FL-Vorlage traegt die Spalte diesen kurzen Namen.
          (setq integ-attr (strcat "OC_INTEGRATIONSART_DP_" (itoa dp-index)))
          (setq integ-val (oc-dp-read-attr ent integ-attr))
          (if (or (not integ-val) (= integ-val ""))
            (setq integ-val (oc-dp-read-attr ent (strcat "OC_INTEG_DP_" (itoa dp-index))))
          )

          ;; Kommentar aus CAD-Block
          (setq komm-attr (strcat "OC_KOMMENTAR_DP_" (itoa dp-index)))
          (setq komm-val (oc-dp-read-attr ent komm-attr))

          ;; CSV-Zeile aufbauen (PRODUKT nach INTEG_DP)
          (setq csv-line (strcat
            (oc-dp-escape-csv aks) ";"
            (oc-dp-escape-csv bez) ";"
            (oc-dp-escape-csv aks) ";"
            (oc-dp-escape-csv ref-val) ";"
            (oc-dp-escape-csv fcode-val) ";"
            (oc-dp-escape-csv bas-val) ";"
            (oc-dp-escape-csv integ-val) ";"
            (oc-dp-escape-csv produkt)
          ))

          ;; Funktionsspalten
          (foreach col func-cols
            (setq func-attr (strcat col "_DP_" (itoa dp-index)))
            (setq func-val (oc-dp-read-attr ent func-attr))
            (setq csv-line (strcat csv-line ";" (oc-dp-escape-csv func-val)))
          )

          ;; Kommentar als letzte Spalte
          (setq csv-line (strcat csv-line ";" (oc-dp-escape-csv komm-val)))

          (write-line csv-line file)
          (setq dp-count (1+ dp-count))

          (oc-log (strcat "\n  DP " (itoa dp-count) ": "
                         aks " / " ref-val " (" bas-val ")"))
        )
      )
      (setq dp-index (1+ dp-index))
    )
  )

  (close file)

  (oc-log (strcat "\n=== Extraktion abgeschlossen: "
                 (itoa dp-count) " Datenpunkte aus " dwg-name " ==="))
  (princ)
)

;;; Interaktiver Befehl
(defun c:ExtractDP ()
  (if (not *oc-extract-dir*)
    (setq *oc-extract-dir* (getvar "DWGPREFIX"))
  )
  (ExtractDP)
)

(oc-log "\nExtractDP.lsp geladen (v1.7 + OC_PRODUKT)")
(princ)
