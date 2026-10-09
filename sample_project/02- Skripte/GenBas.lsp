;;; =====================================================================
;;; GenBas.lsp - BAS (Benutzeradressierungssystem) Generator
;;; =====================================================================
;;; Version: 1.3 - Kommentare in BAS.csv: #-Zeilen und Spalten ab ; ignorieren
;;;
;;; Baut pro aktivem Datenpunkt einen BAS-String zusammen aus den
;;; Segmenten in BAS.csv und schreibt das Ergebnis in OC_BAS_DP_n.
;;;
;;; BAS.csv Format (UTF-8, eine Zeile pro Segment):
;;;   FREITEXT_01          → Attributwert aus Plankopf/Block lesen
;;;   "-"                  → Statischer Text (in Anführungszeichen)
;;;   OC_AKS               → Attributwert lesen
;;;   OC_FCODE_DP          → Endet mit _DP → wird zu OC_FCODE_DP_n
;;;
;;; Kommentare (ab v1.3):
;;;   # Text               → Zeile beginnt mit # → komplett ignoriert
;;;   OC_AKS;Kommentar     → nur erste Spalte (bis zum ersten ;) wird
;;;                          ausgewertet, Rest ist Kommentar
;;;   Einschränkung: ein statischer Text darf selbst kein ; enthalten
;;;
;;; Globale Variable (vom C++ Plugin gesetzt):
;;;   *oc-bas-csv-path*    → Pfad zur BAS.csv
;;;
;;; Aktiv-Prüfung: OC_FL_AKTIV_n = ja/true/1/x/high/aktiv/wahr
;;; =====================================================================

;;; =====================================================================
;;; HILFSFUNKTIONEN
;;; =====================================================================

;; Prüft ob ein Wert "aktiv" bedeutet
(defun oc-is-active-value (val / v)
  (if (and val (= (type val) 'STR) (> (strlen val) 0))
    (progn
      (setq v (strcase (vl-string-trim " \t" val)))
      (or (= v "JA") (= v "TRUE") (= v "1") (= v "X")
          (= v "HIGH") (= v "AKTIV") (= v "WAHR"))
    )
    nil
  )
)

;; Liest den Wert eines Attributs aus einem Block (case-insensitive)
(defun oc-read-attribute (block-ent attr-name / next-ent attr-data found-val)
  (setq found-val nil)
  (setq next-ent (entnext block-ent))
  (while (and next-ent
              (not found-val)
              (setq attr-data (entget next-ent))
              (= (cdr (assoc 0 attr-data)) "ATTRIB"))
    (if (= (strcase (cdr (assoc 2 attr-data))) (strcase attr-name))
      (setq found-val (cdr (assoc 1 attr-data)))
    )
    (setq next-ent (entnext next-ent))
  )
  (if found-val found-val "")
)

;; Aktualisiert ein Attribut eines Blocks
(defun oc-write-attribute (block-ent attr-name new-value / next-ent attr-data updated)
  (setq updated nil)
  (setq next-ent (entnext block-ent))
  (while (and next-ent
              (setq attr-data (entget next-ent))
              (= (cdr (assoc 0 attr-data)) "ATTRIB"))
    (if (= (strcase (cdr (assoc 2 attr-data))) (strcase attr-name))
      (progn
        (setq attr-data (subst (cons 1 new-value) (assoc 1 attr-data) attr-data))
        (entmod attr-data)
        (entupd next-ent)
        (setq updated T)
      )
    )
    (setq next-ent (entnext next-ent))
  )
  updated
)

;; Prüft ob String mit _DP endet (case-insensitive)
(defun oc-ends-with-dp (str / len)
  (setq len (strlen str))
  (if (>= len 3)
    (= (strcase (substr str (- len 2))) "_DP")
    nil
  )
)

;; Entfernt Anführungszeichen am Anfang und Ende (auch CSV-escaped """text""")
(defun oc-strip-quotes (str / len)
  (setq len (strlen str))
  (while (and (>= len 2)
              (or (and (= (substr str 1 1) "\"") (= (substr str len 1) "\""))
                  (and (= (substr str 1 1) "'") (= (substr str len 1) "'"))))
    (setq str (substr str 2 (- len 2)))
    (setq len (strlen str))
  )
  str
)

;;; =====================================================================
;;; BAS.CSV PARSER
;;; =====================================================================

(defun oc-parse-bas-csv (csv-path / file line segments seg trimmed pos)
  (setq segments '())
  (if (not (findfile csv-path))
    (progn
      (princ (strcat "\n  FEHLER: BAS.csv nicht gefunden: " csv-path))
      (setq segments nil)
    )
    (progn
      (setq file (open csv-path "r"))
      (if file
        (progn
          (while (setq line (read-line file))
            (setq trimmed (vl-string-trim " \t\r" line))
            ;; Kommentarzeile: beginnt mit # → komplett ignorieren
            (if (and (> (strlen trimmed) 0) (= (substr trimmed 1 1) "#"))
              (setq trimmed "")
            )
            ;; Nur erste Spalte auswerten: alles ab dem ersten ; ist Kommentar
            (if (setq pos (vl-string-search ";" trimmed))
              (setq trimmed (vl-string-trim " \t\r" (substr trimmed 1 pos)))
            )
            (if (> (strlen trimmed) 0)
              (progn
                ;; Prüfe ob statischer Text (in Anführungszeichen oder bare "-")
                (if (or (and (= (substr trimmed 1 1) "\"")
                             (= (substr trimmed (strlen trimmed) 1) "\""))
                        (and (= (substr trimmed 1 1) "'")
                             (= (substr trimmed (strlen trimmed) 1) "'")))
                  ;; Statisches Segment (strip alle Quote-Ebenen inkl. CSV-Escaping)
                  (setq seg (list "STATIC" (oc-strip-quotes trimmed)))
                  ;; Bare "-" als statischer Separator behandeln
                  (if (= trimmed "-")
                    (setq seg (list "STATIC" "-"))
                    ;; Attribut-Segment
                    (if (oc-ends-with-dp trimmed)
                      (setq seg (list "ATTR_DP" trimmed))  ; Braucht _n Suffix
                      (setq seg (list "ATTR" trimmed))      ; Direkter Attributname
                    )
                  )
                )
                (setq segments (append segments (list seg)))
              )
            )
          )
          (close file)
          (princ (strcat "\n  BAS.csv geladen: " (itoa (length segments)) " Segmente"))
        )
        (princ "\n  FEHLER: BAS.csv konnte nicht geoeffnet werden")
      )
    )
  )
  segments
)

;;; =====================================================================
;;; BAS-STRING FÜR EINEN DATENPUNKT BAUEN
;;; =====================================================================

;; block-ent: INSERT entity des Blocks
;; dp-index: Datenpunkt-Index (1, 2, 3, ...)
;; segments: Geparste BAS-Segmente
;; plankopf-ent: INSERT entity des Plankopf-Blocks (oder nil)
(defun oc-build-bas-string (block-ent dp-index segments plankopf-ent / result seg seg-type seg-value attr-name attr-val)
  (setq result "")
  (foreach seg segments
    (setq seg-type (car seg))
    (setq seg-value (cadr seg))
    (cond
      ;; Statischer Text
      ((= seg-type "STATIC")
       (setq result (strcat result seg-value))
      )
      ;; Attribut mit _DP Suffix → _DP_n anhängen
      ((= seg-type "ATTR_DP")
       (setq attr-name (strcat seg-value "_" (itoa dp-index)))
       ;; Erst im Block suchen, dann im Plankopf
       (setq attr-val (oc-read-attribute block-ent attr-name))
       (if (and (= attr-val "") plankopf-ent)
         (setq attr-val (oc-read-attribute plankopf-ent attr-name))
       )
       (setq result (strcat result attr-val))
      )
      ;; Normales Attribut (ohne _DP Suffix)
      ((= seg-type "ATTR")
       ;; Erst im Block suchen, dann im Plankopf
       (setq attr-val (oc-read-attribute block-ent seg-value))
       (if (and (= attr-val "") plankopf-ent)
         (setq attr-val (oc-read-attribute plankopf-ent seg-value))
       )
       (setq result (strcat result attr-val))
      )
    )
  )
  result
)

;;; =====================================================================
;;; PLANKOPF-BLOCK FINDEN
;;; =====================================================================

;; Sucht den Plankopf-Block anhand des Blocknamens (OC_RSH_Plankopf_quer*)
(defun oc-find-plankopf (/ ss i ent blk-name found)
  (setq found nil)
  (setq ss (ssget "X" '((0 . "INSERT"))))
  (if ss
    (progn
      (setq i 0)
      (while (and (< i (sslength ss)) (not found))
        (setq ent (ssname ss i))
        (setq blk-name (strcase (cdr (assoc 2 (entget ent)))))
        ;; Plankopf erkennen: Blockname beginnt mit OC_RSH_PLANKOPF_QUER
        (if (and (>= (strlen blk-name) 20)
                 (= (substr blk-name 1 20) "OC_RSH_PLANKOPF_QUER"))
          (setq found ent)
        )
        (setq i (1+ i))
      )
    )
  )
  found
)

;;; =====================================================================
;;; HAUPTFUNKTION
;;; =====================================================================

(defun GenBas (/ csv-path segments ss i ent insert-data
                 plankopf-ent dp-index active-attr active-val
                 bas-string bas-attr dp-count total-count)
  (princ "\n=== BAS-Generierung gestartet ===")

  ;; BAS.csv Pfad aus globaler Variable
  (setq csv-path *oc-bas-csv-path*)
  (if (not csv-path)
    (progn
      (princ "\n  FEHLER: *oc-bas-csv-path* nicht gesetzt!")
      (princ "\n  BAS.csv Pfad muss vom C++ Plugin uebergeben werden.")
      (princ)
      (exit)
    )
  )

  ;; BAS.csv parsen
  (setq segments (oc-parse-bas-csv csv-path))
  (if (not segments)
    (progn
      (princ "\n  Abbruch: Keine BAS-Segmente geladen.")
      (princ)
      (exit)
    )
  )

  ;; Plankopf-Block finden (für Attribute wie FREITEXT_01, OC_AKS etc.)
  (setq plankopf-ent (oc-find-plankopf))
  (if plankopf-ent
    (princ "\n  Plankopf-Block gefunden.")
    (princ "\n  Warnung: Kein Plankopf-Block gefunden (FREITEXT_01).")
  )

  ;; Alle INSERT-Blöcke durchsuchen
  (setq ss (ssget "X" '((0 . "INSERT"))))
  (setq total-count 0)

  (if ss
    (progn
      (princ (strcat "\n  Durchsuche " (itoa (sslength ss)) " Bloecke..."))
      (setq i 0)
      (repeat (sslength ss)
        (setq ent (ssname ss i))
        (setq insert-data (entget ent))

        ;; Prüfe ob Block Attribute hat
        (if (equal (cdr (assoc 66 insert-data)) 1)
          (progn
            ;; Für jeden möglichen Datenpunkt (1..25)
            (setq dp-index 1)
            (while (<= dp-index 25)
              ;; Prüfe OC_FL_AKTIV_n
              (setq active-attr (strcat "OC_FL_AKTIV_" (itoa dp-index)))
              (setq active-val (oc-read-attribute ent active-attr))

              (if (oc-is-active-value active-val)
                (progn
                  ;; BAS-String zusammenbauen
                  (setq bas-string (oc-build-bas-string ent dp-index segments plankopf-ent))

                  ;; In OC_BAS_DP_n schreiben
                  (setq bas-attr (strcat "OC_BAS_DP_" (itoa dp-index)))
                  (if (oc-write-attribute ent bas-attr bas-string)
                    (progn
                      (princ (strcat "\n  " bas-attr " = " bas-string))
                      (setq total-count (1+ total-count))
                    )
                  )
                )
              )
              (setq dp-index (1+ dp-index))
            )
          )
        )
        (setq i (1+ i))
      )
    )
    (princ "\n  Keine Bloecke in Zeichnung gefunden.")
  )

  (princ (strcat "\n=== BAS-Generierung abgeschlossen: "
                 (itoa total-count) " BAS-Strings geschrieben ==="))
  (princ)
)

;;; Interaktiver Befehl
(defun c:GenBas ()
  ;; Fallback: Pfad manuell fragen wenn nicht gesetzt
  (if (not *oc-bas-csv-path*)
    (progn
      (setq *oc-bas-csv-path*
        (getfiled "BAS.csv waehlen" "" "csv" 0))
    )
  )
  (GenBas)
)

(princ "\nGenBas.lsp geladen (v1.3)")
(princ)
