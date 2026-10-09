;;; =====================================================================
;;; FillGaFl.lsp - GA-Funktionsliste befüllen
;;; =====================================================================
;;; Version: 1.5
;;;   - Integrationsart: Wert aus dem Symbol (OC_INTEGRATIONSART_DP_n) hat
;;;     Vorrang vor der Referenz-ODS (Spalte C). Bisher ueberschrieb die
;;;     Referenz einen im Symbol gesetzten Wert (z.B. "virtuell" bei FG_HW).
;;;     Regel jetzt: Symbol > Referenz > leer (analog zum Kommentar).
;;; Version: 1.4
;;;   - Performance: ATTRIB-Kette pro Block einmal gecacht (TAG . ename),
;;;     Lese-/Schreibzugriffe gehen ueber den Cache statt die Kette jedes
;;;     Mal komplett abzulaufen; entupd einmal pro Block am Ende statt
;;;     pro Attribut. Funktional identisch zu v1.3.
;;; Version: 1.3
;;;   - Logging: oc-log schreibt auf Konsole UND in Logdatei (fillgafl_log.txt)
;;;   - Uebertrag auf Folgeblaettern (Zeile 1): kumulierte Spaltensummen
;;;   - Kommentarspalte: CAD-Block > ODS-Referenz (Spalte BJ) > leer
;;;   - Fehlende DP-Referenzen: Fallback auf 0, Warnung + Alert
;;;
;;; Globale Variablen (vom C++ Plugin via SCR gesetzt):
;;;   *oc-fill-csv-path*      → Pfad zur extrahierten Quell-CSV
;;;   *oc-fill-ref-csv-path*  → Pfad zur GA_FL_VORLAGE.csv (Funktionsreferenz)
;;;   *oc-fill-sheet-num*     → Blattnummer (1-basiert)
;;;   *oc-fill-start-row*     → Erste Datenzeile in CSV (0-basiert, nach Header)
;;;   *oc-fill-dp-count*      → Anzahl DPs für dieses Blatt
;;;   *oc-fill-plankopf*      → Assoziationsliste mit Plankopf-Attributen
;;;
;;; Block: VDI3814_GA_FL_V_1_0
;;; 
;;; Blattaufteilung (Spec v1.1):
;;;   Erstes Blatt:  Zeile 1-25 = Datenpunkte, Summenzeile = OC_SUM_1..58
;;;   Folgeblätter:  Zeile 1 = Übertrag, Zeile 2-25 = Datenpunkte, Summenzeile = OC_SUM_1..58
;;; =====================================================================

;;; =====================================================================
;;; KONSTANTEN
;;; =====================================================================

(setq *OC-GAFL-BLOCK* "VDI3814_GA_FL_V_1_0")

;; Index der Kommentarspalte in der ODS-Referenz-CSV (Spalte BJ = Index 61)
(setq *OC-REF-COMMENT-INDEX* 61)

;; Globale Variable fuer Uebertrag zwischen Blaettern
;; Wird nach jedem Blatt mit der Summenzeile befuellt
;; und auf dem naechsten Folgeblatt in Zeile 1 geschrieben.
;; Liste von 57 Integer-Werten (ohne OC_INTEG, das ist Text).
(if (not (boundp '*oc-fill-last-sums*)) (setq *oc-fill-last-sums* nil))

;;; =====================================================================
;;; LOGGING
;;; =====================================================================

;; Logging-Funktion: schreibt auf Konsole UND in Logdatei (wenn Pfad gesetzt).
;; *oc-log-path* wird vom C++ SCR-Generator als String gesetzt.
;; Pro Aufruf: open/append/close (robust ueber Dokumentwechsel hinweg).
;; Wenn kein Pfad gesetzt -> nur Konsole (Standalone-Betrieb).
(defun oc-log (msg / f)
  (princ msg)
  (if (and (boundp '*oc-log-path*) *oc-log-path*)
    (progn
      (setq f (open *oc-log-path* "a"))
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
;;; ATTRIBUT-CACHE (v1.4)
;;; =====================================================================
;;; Pro Block wird die ATTRIB-Kette genau einmal abgelaufen und als Liste
;;; von (TAG . ename) gehalten (TAG in Grossbuchstaben, Reihenfolge wie in
;;; der Kette). Alle Zugriffe laufen ueber diesen Cache. Blocks, in die
;;; geschrieben wurde, werden gemerkt und am Ende einmal per entupd
;;; aktualisiert.

(if (not (boundp '*oc-fl-attr-cache*)) (setq *oc-fl-attr-cache* nil))
(if (not (boundp '*oc-fl-dirty-blocks*)) (setq *oc-fl-dirty-blocks* nil))

;; Cache fuer einen Block aufbauen (einmaliger Kettendurchlauf)
(defun oc-fl-build-attr-cache (block-ent / next-ent attr-data cache)
  (setq cache '())
  (setq next-ent (entnext block-ent))
  (while (and next-ent
              (setq attr-data (entget next-ent))
              (= (cdr (assoc 0 attr-data)) "ATTRIB"))
    (setq cache (cons (cons (strcase (cdr (assoc 2 attr-data))) next-ent) cache))
    (setq next-ent (entnext next-ent))
  )
  (reverse cache)
)

;; Cache fuer einen Block holen, bei Bedarf aufbauen
(defun oc-fl-get-attr-cache (block-ent / pair cache)
  (setq pair (assoc block-ent *oc-fl-attr-cache*))
  (if pair
    (cdr pair)
    (progn
      (setq cache (oc-fl-build-attr-cache block-ent))
      (setq *oc-fl-attr-cache* (cons (cons block-ent cache) *oc-fl-attr-cache*))
      cache
    )
  )
)

;; Alle ATTRIB-enames eines Blocks mit gegebenem Tag (case-insensitive,
;; Reihenfolge wie in der Kette; auch doppelte Tags werden alle geliefert)
(defun oc-fl-attr-enames (block-ent attr-name / cache tag rest pair result)
  (setq cache (oc-fl-get-attr-cache block-ent))
  (setq tag (strcase attr-name))
  (setq result '())
  (setq rest cache)
  (while (setq pair (assoc tag rest))
    (setq result (cons (cdr pair) result))
    (setq rest (cdr (member pair rest)))
  )
  (reverse result)
)

;; Block als geaendert merken (fuer entupd am Ende)
(defun oc-fl-mark-dirty (block-ent)
  (if (not (member block-ent *oc-fl-dirty-blocks*))
    (setq *oc-fl-dirty-blocks* (cons block-ent *oc-fl-dirty-blocks*))
  )
)

;; Alle geaenderten Blocks einmal aktualisieren, Cache und Merkliste leeren
(defun oc-fl-flush-updates ()
  (foreach b *oc-fl-dirty-blocks*
    (entupd b)
  )
  (setq *oc-fl-dirty-blocks* nil)
  (setq *oc-fl-attr-cache* nil)
)

;;; =====================================================================
;;; HILFSFUNKTIONEN
;;; =====================================================================

;; Liest Attributwert aus einem Block (case-insensitive)
;; Bei mehrfach vorhandenem Tag: letzter Treffer (wie v1.3)
(defun oc-fl-read-attr (block-ent attr-name / found-val)
  (setq found-val nil)
  (foreach en (oc-fl-attr-enames block-ent attr-name)
    (setq found-val (cdr (assoc 1 (entget en))))
  )
  found-val
)

;; Konvertiert Non-ASCII-Zeichen zu BricsCAD \U+XXXX Escape-Sequenzen.
;; Erkennt automatisch ob Eingabe UTF-8 oder CP1252 kodiert ist:
;;   - UTF-8 2-Byte: 0xC0-0xDF gefolgt von 0x80-0xBF -> ein Codepoint
;;   - UTF-8 3-Byte: 0xE0-0xEF gefolgt von 2x 0x80-0xBF -> ein Codepoint
;;   - CP1252 Einzelbyte: 0x80-0xFF -> direkt als Codepoint
;; BricsCAD rendert \U+XXXX nativ in Attributen.
(defun oc-fl-fix-encoding (str / result i code code2 code3 codepoint hex-str slen)
  (if (not str) (setq str ""))
  (setq result "")
  (setq slen (strlen str))
  (setq i 1)
  (while (<= i slen)
    (setq code (ascii (substr str i 1)))
    (cond
      ;; ASCII: direkt uebernehmen
      ((< code 128)
       (setq result (strcat result (chr code)))
       (setq i (1+ i)))

      ;; UTF-8 2-Byte-Sequenz: 110xxxxx 10xxxxxx
      ((and (>= code 192) (<= code 223) (< i slen)
            (progn (setq code2 (ascii (substr str (1+ i) 1)))
                   (and (>= code2 128) (<= code2 191))))
       (setq codepoint (+ (* (- code 192) 64) (- code2 128)))
       (setq hex-str (oc-fl-int-to-hex4 codepoint))
       (setq result (strcat result "\\U+" hex-str))
       (setq i (+ i 2)))

      ;; UTF-8 3-Byte-Sequenz: 1110xxxx 10xxxxxx 10xxxxxx
      ((and (>= code 224) (<= code 239) (<= (+ i 1) slen)
            (progn (setq code2 (ascii (substr str (1+ i) 1)))
                   (setq code3 (ascii (substr str (+ i 2) 1)))
                   (and (>= code2 128) (<= code2 191)
                        (>= code3 128) (<= code3 191))))
       (setq codepoint (+ (* (- code 224) 4096)
                           (* (- code2 128) 64)
                           (- code3 128)))
       (setq hex-str (oc-fl-int-to-hex4 codepoint))
       (setq result (strcat result "\\U+" hex-str))
       (setq i (+ i 3)))

      ;; CP1252 Einzelbyte (kein UTF-8 Continuation-Byte danach)
      (T
       (setq hex-str (oc-fl-int-to-hex4 code))
       (setq result (strcat result "\\U+" hex-str))
       (setq i (1+ i)))
    )
  )
  result
)

;; Integer zu 4-stelligem Hex-String (z.B. 252 -> "00FC")
(defun oc-fl-int-to-hex4 (num / hex-chars h3 h2 h1 h0)
  (setq hex-chars "0123456789ABCDEF")
  (setq h0 (substr hex-chars (1+ (rem num 16)) 1))
  (setq num (/ num 16))
  (setq h1 (substr hex-chars (1+ (rem num 16)) 1))
  (setq num (/ num 16))
  (setq h2 (substr hex-chars (1+ (rem num 16)) 1))
  (setq num (/ num 16))
  (setq h3 (substr hex-chars (1+ (rem num 16)) 1))
  (strcat h3 h2 h1 h0)
)

;; Schreibt Attributwert in einen Block (mit CP1252->UTF-8 Konvertierung)
;; Bei mehrfach vorhandenem Tag werden alle beschrieben (wie v1.3).
;; entupd erfolgt gesammelt am Ende (oc-fl-flush-updates).
(defun oc-fl-write-attr (block-ent attr-name new-value / attr-data updated utf8-value)
  (setq updated 0)
  (setq utf8-value (oc-fl-fix-encoding new-value))
  (foreach en (oc-fl-attr-enames block-ent attr-name)
    (setq attr-data (entget en))
    (setq attr-data (subst (cons 1 utf8-value) (assoc 1 attr-data) attr-data))
    (entmod attr-data)
    (setq updated (1+ updated))
  )
  (if (> updated 0) (oc-fl-mark-dirty block-ent))
  updated
)

;; Schreibt Kommentar in 2 Zeilen: OC_KOMMENTAR_1_DP_n (Zeichen 1-40), OC_KOMMENTAR_2_DP_n (ab 41)
(defun oc-fl-write-kommentar (block-ent row-num kommentar-text / line1 line2)
  (if (> (strlen kommentar-text) 40)
    (progn
      (setq line1 (substr kommentar-text 1 40))
      (setq line2 (substr kommentar-text 41))
    )
    (progn
      (setq line1 kommentar-text)
      (setq line2 "")
    )
  )
  (oc-fl-write-attr block-ent
    (strcat "OC_KOMMENTAR_1_DP_" (itoa row-num)) line1)
  (if (> (strlen line2) 0)
    (oc-fl-write-attr block-ent
      (strcat "OC_KOMMENTAR_2_DP_" (itoa row-num)) line2)
  )
)

;; Setzt Element an Position idx in einer Liste (gibt neue Liste zurueck)
(defun oc-fl-list-set (lst idx val / result i)
  (setq result '())
  (setq i 0)
  (foreach item lst
    (if (= i idx)
      (setq result (append result (list val)))
      (setq result (append result (list item)))
    )
    (setq i (1+ i))
  )
  result
)

;; Findet alle INSERT-Entities des GA-FL Blocks
(defun oc-fl-find-gafl-blocks (/ ss i ent insert-data block-name result)
  (setq result '())
  (setq ss (ssget "X" (list '(0 . "INSERT") (cons 2 *OC-GAFL-BLOCK*))))
  (if ss
    (progn
      (setq i 0)
      (repeat (sslength ss)
        (setq ent (ssname ss i))
        (setq result (cons ent result))
        (setq i (1+ i))
      )
    )
  )
  ;; Auch Wildcard-Suche für Varianten
  (if (not result)
    (progn
      (setq ss (ssget "X" '((0 . "INSERT"))))
      (if ss
        (progn
          (setq i 0)
          (repeat (sslength ss)
            (setq ent (ssname ss i))
            (setq insert-data (entget ent))
            (setq block-name (cdr (assoc 2 insert-data)))
            (if (and block-name (wcmatch (strcase block-name) "*VDI3814*GA*FL*"))
              (setq result (cons ent result))
            )
            (setq i (1+ i))
          )
        )
      )
    )
  )
  result
)

;;; =====================================================================
;;; UEBERTRAG TEMP-DATEI (robust ueber CLOSE/OPEN hinweg)
;;; =====================================================================

;; Ermittelt Pfad der Carry-Datei neben der Quell-CSV
(defun oc-fl-carry-file-path (csv-path)
  (strcat (vl-filename-directory csv-path) "/oc_carry_sums.txt")
)

;; Schreibt Summenliste in Temp-Datei (ein Wert pro Zeile)
(defun oc-fl-write-carry-file (csv-path sum-list / carry-path f)
  (setq carry-path (oc-fl-carry-file-path csv-path))
  (setq f (open carry-path "w"))
  (if f
    (progn
      (foreach val sum-list
        (write-line (itoa val) f)
      )
      (close f)
      (oc-log (strcat "\n  Carry-Datei geschrieben: " carry-path))
    )
    (oc-log (strcat "\n  WARNUNG: Carry-Datei nicht schreibbar: " carry-path))
  )
)

;; Liest Summenliste aus Temp-Datei (gibt Liste oder nil zurueck)
(defun oc-fl-read-carry-file (csv-path / carry-path f line result)
  (setq carry-path (oc-fl-carry-file-path csv-path))
  (setq result nil)
  (if (findfile carry-path)
    (progn
      (setq f (open carry-path "r"))
      (if f
        (progn
          (while (setq line (read-line f))
            (setq line (vl-string-trim " \t\r" line))
            (if (> (strlen line) 0)
              (setq result (append result (list (atoi line))))
            )
          )
          (close f)
          (oc-log (strcat "\n  Carry-Datei gelesen: " (itoa (length result)) " Werte"))
        )
      )
    )
    (oc-log (strcat "\n  Carry-Datei nicht gefunden: " carry-path))
  )
  result
)

;;; =====================================================================
;;; CSV PARSER
;;; =====================================================================

;; Parst CSV-Zeile (Semikolon-getrennt)
(defun oc-fl-parse-csv-line (line / result current i ch)
  (setq result '())
  (setq current "")
  (setq i 1)
  (while (<= i (strlen line))
    (setq ch (substr line i 1))
    (if (= ch ";")
      (progn
        (setq result (append result (list current)))
        (setq current "")
      )
      (setq current (strcat current ch))
    )
    (setq i (1+ i))
  )
  (setq result (append result (list current)))
  result
)

;; Liest CSV-Datei komplett (überspringt Header und Kommentarzeilen)
(defun oc-fl-read-csv (csv-path / file line rows row-data)
  (setq rows '())
  (if (not (findfile csv-path))
    (progn
      (oc-log (strcat "\n  FEHLER: CSV nicht gefunden: " csv-path))
      nil
    )
    (progn
      (setq file (open csv-path "r"))
      (if file
        (progn
          (while (setq line (read-line file))
            (setq line (vl-string-trim " \t\r" line))
            (if (and (> (strlen line) 0)
                     (/= (substr line 1 1) "#"))
              (progn
                (setq row-data (oc-fl-parse-csv-line line))
                (setq rows (append rows (list row-data)))
              )
            )
          )
          (close file)
          rows
        )
        (progn
          (oc-log "\n  FEHLER: CSV konnte nicht geoeffnet werden")
          nil
        )
      )
    )
  )
)

;; Liest Plankopf-Daten aus der Kommentarzeile der extrahierten CSV
(defun oc-fl-read-plankopf-from-csv (csv-path / file line result parts kv key val)
  (setq result '())
  (if (findfile csv-path)
    (progn
      (setq file (open csv-path "r"))
      (if file
        (progn
          (while (setq line (read-line file))
            (if (and (> (strlen line) 10)
                     (= (substr line 1 10) "#PLANKOPF;"))
              (progn
                (setq parts (oc-fl-parse-csv-line (substr line 11)))
                (foreach part parts
                  (if (vl-string-search "=" part)
                    (progn
                      (setq kv (vl-string-search "=" part))
                      (setq key (substr part 1 kv))
                      (setq val (substr part (+ kv 2)))
                      (setq result (cons (cons key val) result))
                    )
                  )
                )
              )
            )
          )
          (close file)
        )
      )
    )
  )
  result
)

;;; =====================================================================
;;; GA_FL_VORLAGE.csv REFERENZDATEN (Komma-getrennt!)
;;; =====================================================================

(defun oc-fl-read-reference-csv (ref-csv-path / file line rows row-data)
  (setq rows '())
  (if (not (findfile ref-csv-path))
    (progn
      (oc-log (strcat "\n  Warnung: Referenz-CSV nicht gefunden: " ref-csv-path))
      nil
    )
    (progn
      (setq file (open ref-csv-path "r"))
      (if file
        (progn
          ;; Header überspringen
          (read-line file)
          (while (setq line (read-line file))
            (setq line (vl-string-trim " \t\r" line))
            (if (> (strlen line) 0)
              (progn
                (setq row-data (oc-fl-parse-comma-csv-line line))
                (setq rows (append rows (list row-data)))
              )
            )
          )
          (close file)
          (oc-log (strcat "\n  Referenz-CSV geladen: " (itoa (length rows)) " Eintraege"))
          rows
        )
        nil
      )
    )
  )
)

;; Komma-getrennter CSV Parser (für LibreOffice-Output)
(defun oc-fl-parse-comma-csv-line (line / result current i ch in-quotes)
  (setq result '())
  (setq current "")
  (setq in-quotes nil)
  (setq i 1)
  (while (<= i (strlen line))
    (setq ch (substr line i 1))
    (cond
      ((= ch "\"")
       (setq in-quotes (not in-quotes)))
      ((and (= ch ",") (not in-quotes))
       (setq result (append result (list (vl-string-trim " " current))))
       (setq current ""))
      (T
       (setq current (strcat current ch)))
    )
    (setq i (1+ i))
  )
  (setq result (append result (list (vl-string-trim " " current))))
  result
)

;; Sucht einen DP-Namen in der Referenz-CSV, gibt Funktionswerte zurück
(defun oc-fl-lookup-reference (ref-name ref-data / found row dp-name)
  (setq found nil)
  (foreach row ref-data
    (if (and (not found) (> (length row) 1))
      (progn
        (setq dp-name (vl-string-trim " " (nth 1 row)))
        (if (= (strcase dp-name) (strcase ref-name))
          (setq found row)
        )
      )
    )
  )
  found
)

;;; =====================================================================
;;; FUNKTIONSSPALTEN-NAMEN (für Attribut-Mapping)
;;; =====================================================================

(defun oc-fl-get-function-attr-bases ()
  '("OC_INTEG"
    "OC_1_1_1" "OC_1_1_2" "OC_1_1_3" "OC_1_1_4"
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
    ;; OC_4_1_1 entfernt: ist Kommentarspalte in ODS, keine GA-Funktion
  )
)

;;; =====================================================================
;;; HAUPTFUNKTION
;;; =====================================================================

(defun FillGaFl (/ csv-path ref-csv-path sheet-num start-row dp-count
                   csv-data ref-data gafl-blocks plankopf-data
                   row-offset target-row dp-row csv-entry
                   bmk bez aks ref-dp bas-dp integ-dp
                   bezeichnung-text ref-row
                   func-bases col-index csv-col-offset
                   attr-name attr-val
                   filled-count is-first-sheet
                   all-inserts ins-idx ins-ent
                   pk-gewerk pk-anlage pk-znr
                   sum-func-bases sum-counts col-idx count-row count-target
                   count-attr count-val num-val sum-idx sum-written sum-attr
                   carry-counts carry-idx carry-attr
                   kommentar-csv kommentar-ref kommentar-val)

  (oc-log "\n=== GA-FL Befuellung gestartet ===")

  ;; Attribut-Cache und Merkliste fuer diesen Lauf zuruecksetzen
  (setq *oc-fl-attr-cache* nil)
  (setq *oc-fl-dirty-blocks* nil)

  ;; Fehlende-Referenzen-Liste initialisieren
  (if (not (boundp '*oc-missing-refs*))
    (setq *oc-missing-refs* nil)
  )

  ;; Globale Variablen lesen
  (setq csv-path *oc-fill-csv-path*)
  (setq ref-csv-path *oc-fill-ref-csv-path*)
  (setq sheet-num (if *oc-fill-sheet-num* (fix *oc-fill-sheet-num*) 1))
  (setq start-row (if *oc-fill-start-row* (fix *oc-fill-start-row*) 0))
  (setq dp-count (if *oc-fill-dp-count* (fix *oc-fill-dp-count*) 24))

  (if (not csv-path)
    (progn
      (oc-log "\n  FEHLER: *oc-fill-csv-path* nicht gesetzt!")
      (princ) (exit)
    )
  )

  (setq is-first-sheet (= sheet-num 1))

  ;; Bei Erstblatt: alte Carry-Daten zuruecksetzen
  ;; (verhindert Bleed-Over zwischen GA-FL / ASP-Summe / Los-Summe)
  (if is-first-sheet
    (progn
      (setq *oc-fill-last-sums* nil)
      ;; Carry-Datei loeschen falls vorhanden
      (if csv-path
        (progn
          (setq carry-cleanup-path (oc-fl-carry-file-path csv-path))
          (if (findfile carry-cleanup-path)
            (vl-file-delete carry-cleanup-path)
          )
        )
      )
    )
  )
  (oc-log (strcat "\n  Blatt " (itoa sheet-num)
                 ", Start-DP: " (itoa start-row)
                 ", Anzahl: " (itoa dp-count)
                 (if is-first-sheet " (Erstblatt)" " (Folgeblatt)")))

  ;; GA-FL Block finden
  (setq gafl-blocks (oc-fl-find-gafl-blocks))
  (if (not gafl-blocks)
    (progn
      (oc-log (strcat "\n  FEHLER: Block " *OC-GAFL-BLOCK* " nicht gefunden!"))
      (princ) (exit)
    )
  )
  (oc-log (strcat "\n  " (itoa (length gafl-blocks)) " GA-FL Block(e) gefunden"))

  ;; Extrahierte CSV lesen (Header = Zeile 0, Daten ab Zeile 1)
  (setq csv-data (oc-fl-read-csv csv-path))
  (if (not csv-data)
    (progn
      (oc-log "\n  FEHLER: Keine CSV-Daten geladen!")
      (princ) (exit)
    )
  )
  ;; Erste Zeile ist Header - pruefen ob KOMMENTAR-Spalte vorhanden
  (setq csv-header (car csv-data))
  (setq csv-has-kommentar nil)
  (setq csv-kommentar-idx 0)
  (if csv-header
    (progn
      (setq hdr-idx 0)
      (foreach hdr-col csv-header
        (if (= (strcase (vl-string-trim " " hdr-col)) "KOMMENTAR")
          (progn
            (setq csv-has-kommentar T)
            (setq csv-kommentar-idx hdr-idx)
          )
        )
        (setq hdr-idx (1+ hdr-idx))
      )
    )
  )
  (setq csv-data (cdr csv-data))
  (oc-log (strcat "\n  CSV: " (itoa (length csv-data)) " Datenpunkte"
                 (if csv-has-kommentar
                   (strcat " (KOMMENTAR-Spalte: " (itoa csv-kommentar-idx) ")")
                   " (keine KOMMENTAR-Spalte)"
                 )))

  ;; Referenz-CSV lesen (GA_FL_VORLAGE.csv)
  (setq ref-data nil)
  (if ref-csv-path
    (setq ref-data (oc-fl-read-reference-csv ref-csv-path))
  )

  ;; Plankopf-Daten aus CSV lesen
  (setq plankopf-data (oc-fl-read-plankopf-from-csv csv-path))
  (if plankopf-data
    (progn
      ;; ZEICHNUNGSNUMMER erweitern: "<Original> GA-FL n"
      (setq pk-znr (cdr (assoc "ZEICHNUNGSNUMMER" plankopf-data)))
      (if pk-znr
        (setq plankopf-data
          (subst
            (cons "ZEICHNUNGSNUMMER"
                  (strcat pk-znr " GA-FL " (itoa sheet-num)))
            (assoc "ZEICHNUNGSNUMMER" plankopf-data)
            plankopf-data))
      )
      (oc-log "\n  Plankopf-Attribute uebertragen...")
      (setq all-inserts (ssget "X" '((0 . "INSERT"))))
      (if all-inserts
        (progn
          (setq ins-idx 0)
          (repeat (sslength all-inserts)
            (setq ins-ent (ssname all-inserts ins-idx))
            (foreach pk plankopf-data
              (oc-fl-write-attr ins-ent (car pk) (cdr pk))
            )
            (setq ins-idx (1+ ins-idx))
          )
          (oc-log (strcat "\n  Plankopf an " (itoa (sslength all-inserts)) " Bloecke verteilt"))
        )
      )
      ;; OC_GEWERK und OC_ANLAGE im GA-FL Datenblock setzen
      (setq pk-gewerk (cdr (assoc "GEWERK" plankopf-data)))
      (setq pk-anlage (cdr (assoc "ANLAGE" plankopf-data)))
      (if (not pk-gewerk) (setq pk-gewerk ""))
      (if (not pk-anlage) (setq pk-anlage ""))
      (foreach gafl-ent gafl-blocks
        (oc-fl-write-attr gafl-ent "OC_GEWERK" pk-gewerk)
        (oc-fl-write-attr gafl-ent "OC_ANLAGE" pk-anlage)
        (oc-log (strcat "\n  OC_GEWERK=" pk-gewerk " OC_ANLAGE=" pk-anlage))
      )
    )
  )

  ;; ================================================================
  ;; UEBERTRAG auf Folgeblaettern (Zeile 1)
  ;; ================================================================
  (setq func-bases (oc-fl-get-function-attr-bases))
  ;; sum-func-bases = ohne OC_INTEG (Text-Spalte, nicht summierbar)
  (setq sum-func-bases (cdr func-bases))
  (setq csv-col-offset 7)

  (if (not is-first-sheet)
    (progn
      (oc-log "\n  Uebertrag aus Vorgaengerblatt in Zeile 1...")
      ;; Summen aus Temp-Datei lesen (robust ueber CLOSE/OPEN hinweg)
      (setq *oc-fill-last-sums* (oc-fl-read-carry-file csv-path))
      (if *oc-fill-last-sums*
        (progn
          (foreach gafl-ent gafl-blocks
            ;; Bezeichnung fuer Uebertrag-Zeile
            (oc-fl-write-attr gafl-ent "OC_BEZEICHNUNG_DP_1" "Uebertrag")

            ;; Summenwerte vom Vorgaengerblatt in Zeile 1
            ;; 57 Werte (ohne OC_INTEG) -> OC_1_1_1_DP_1 .. OC_4_1_1_DP_1
            (setq carry-idx 0)
            (foreach fb sum-func-bases
              (setq carry-attr (strcat fb "_DP_1"))
              (if (< carry-idx (length *oc-fill-last-sums*))
                (progn
                  (setq carry-val (nth carry-idx *oc-fill-last-sums*))
                  (if (> carry-val 0)
                    (oc-fl-write-attr gafl-ent carry-attr (itoa carry-val))
                  )
                )
              )
              (setq carry-idx (1+ carry-idx))
            )
          )
          (oc-log (strcat "\n  Uebertrag in Zeile 1 geschrieben ("
                         (itoa (length *oc-fill-last-sums*)) " Spalten)."))
        )
        (oc-log "\n  WARNUNG: Keine Summendaten vom Vorgaengerblatt vorhanden.")
      )
    )
  )

  ;; ================================================================
  ;; Datenpunkte befuellen
  ;; ================================================================
  ;; Erstes Blatt: Zeilen 1-24 = DP, Zeile 25 = Summe
  ;; Folgeblätter: Zeile 1 = Übertrag, Zeilen 2-24 = DP, Zeile 25 = Summe
  (setq row-offset (if is-first-sheet 1 2))
  (setq filled-count 0)

  (setq dp-row 0)
  (while (and (< dp-row dp-count)
              (< (+ start-row dp-row) (length csv-data)))
    (progn
      (setq target-row (+ row-offset dp-row))
      (setq csv-entry (nth (+ start-row dp-row) csv-data))

      (if csv-entry
        (progn
          ;; Felder extrahieren
          (setq bmk (if (> (length csv-entry) 0) (nth 0 csv-entry) ""))
          (setq bez (if (> (length csv-entry) 1) (nth 1 csv-entry) ""))
          (setq aks (if (> (length csv-entry) 2) (nth 2 csv-entry) ""))
          (setq ref-dp (if (> (length csv-entry) 3) (nth 3 csv-entry) ""))
          (setq bas-dp (if (> (length csv-entry) 5) (nth 5 csv-entry) ""))
          (setq integ-dp (if (> (length csv-entry) 6) (nth 6 csv-entry) ""))

          ;; Kommentar aus extrahierter CSV: nur wenn Header KOMMENTAR-Spalte hat
          (setq kommentar-csv "")
          (if (and csv-has-kommentar (> (length csv-entry) csv-kommentar-idx))
            (setq kommentar-csv (vl-string-trim " " (nth csv-kommentar-idx csv-entry)))
          )

          ;; OC_BEZEICHNUNG_DP_n = BMK + " - " + Bezeichnung
          (setq bezeichnung-text
            (if (and (> (strlen bmk) 0) (> (strlen bez) 0))
              (strcat bmk " - " bez)
              (strcat bmk bez)
            )
          )

          (foreach gafl-ent gafl-blocks
            ;; Bezeichnung
            (setq attr-name (strcat "OC_BEZEICHNUNG_DP_" (itoa target-row)))
            (oc-fl-write-attr gafl-ent attr-name bezeichnung-text)

            ;; AKS / BAS
            (setq attr-name (strcat "OC_AKS_DP_" (itoa target-row)))
            (oc-fl-write-attr gafl-ent attr-name bas-dp)

            ;; Integration
            (setq attr-name (strcat "OC_INTEG_DP_" (itoa target-row)))
            (oc-fl-write-attr gafl-ent attr-name integ-dp)

            ;; Funktionsspalten aus Referenz-CSV befüllen
            (if (and ref-data (> (strlen ref-dp) 0))
              (progn
                (setq ref-row (oc-fl-lookup-reference ref-dp ref-data))
                (if ref-row
                  (progn
                    ;; Spalte C (Index 2) in Ref-CSV = OC_INTEG (Index 0 in func-bases)
                    (setq col-index 0)
                    (foreach func-base func-bases
                      (setq attr-name (strcat func-base "_DP_" (itoa target-row)))
                      (setq attr-val
                        (if (< (+ col-index 2) (length ref-row))
                          (nth (+ col-index 2) ref-row)
                          ""
                        )
                      )
                      ;; OC_INTEG: Symbolwert hat Vorrang - Referenz nur, wenn
                      ;; im Symbol nichts gesetzt ist (Symbol > Referenz > leer)
                      (if (and attr-val (> (strlen (vl-string-trim " " attr-val)) 0)
                               (not (and (= func-base "OC_INTEG")
                                         (> (strlen (vl-string-trim " " integ-dp)) 0))))
                        (oc-fl-write-attr gafl-ent attr-name attr-val)
                      )
                      (setq col-index (1+ col-index))
                    )

                    ;; ---- Kommentar: CAD > ODS > leer ----
                    (setq kommentar-val kommentar-csv)
                    (if (= (strlen kommentar-val) 0)
                      (progn
                        ;; Fallback: Kommentar aus ODS-Referenz (Spalte BJ = Index 61)
                        (setq kommentar-ref "")
                        (if (< *OC-REF-COMMENT-INDEX* (length ref-row))
                          (setq kommentar-ref
                            (vl-string-trim " " (nth *OC-REF-COMMENT-INDEX* ref-row)))
                        )
                        (if (> (strlen kommentar-ref) 0)
                          (setq kommentar-val kommentar-ref)
                        )
                      )
                    )
                    (if (> (strlen kommentar-val) 0)
                      (oc-fl-write-kommentar gafl-ent target-row kommentar-val)
                    )
                  )
                  ;; === Fehlende Referenz: Fallback auf 0, Warnung markieren ===
                  (progn
                    (oc-log (strcat "\n    !!! FEHLENDE REFERENZ: " ref-dp
                                   " nicht in GA_FL_VORLAGE.ods"))
                    (setq attr-name (strcat "OC_BEZEICHNUNG_DP_" (itoa target-row)))
                    (oc-fl-write-attr gafl-ent attr-name
                      (strcat "[!REF] " bezeichnung-text))
                    ;; Alle Funktionsspalten auf "0" setzen
                    (setq col-index 0)
                    (foreach func-base func-bases
                      (setq attr-name (strcat func-base "_DP_" (itoa target-row)))
                      (oc-fl-write-attr gafl-ent attr-name "0")
                      (setq col-index (1+ col-index))
                    )
                    ;; Kommentar trotzdem aus CAD uebernehmen
                    (if (> (strlen kommentar-csv) 0)
                      (oc-fl-write-kommentar gafl-ent target-row kommentar-csv)
                    )
                    ;; Fehlende Referenz in globale Liste
                    (if (not (boundp '*oc-missing-refs*))
                      (setq *oc-missing-refs* nil)
                    )
                    (if (not (member ref-dp *oc-missing-refs*))
                      (setq *oc-missing-refs*
                        (append *oc-missing-refs* (list ref-dp)))
                    )
                  )
                )
              )
              ;; Fallback: Funktionswerte direkt aus extrahierter CSV
              (progn
                (setq col-index 0)
                (foreach func-base func-bases
                  (setq attr-name (strcat func-base "_DP_" (itoa target-row)))
                  (setq attr-val
                    (if (< (+ csv-col-offset col-index) (length csv-entry))
                      (nth (+ csv-col-offset col-index) csv-entry)
                      ""
                    )
                  )
                  (if (and attr-val (> (strlen (vl-string-trim " " attr-val)) 0))
                    (oc-fl-write-attr gafl-ent attr-name attr-val)
                  )
                  (setq col-index (1+ col-index))
                )
                ;; Kommentar aus CAD (kein ODS-Fallback ohne Referenz)
                (if (> (strlen kommentar-csv) 0)
                  (oc-fl-write-kommentar gafl-ent target-row kommentar-csv)
                )
              )
            )
          )

          (setq filled-count (1+ filled-count))
          (oc-log (strcat "\n  Zeile " (itoa target-row) ": "
                         bezeichnung-text))
        )
      )
      (setq dp-row (1+ dp-row))
    )
  )

  ;; ================================================================
  ;; SUMMENZEILE: OC_SUM_1 bis OC_SUM_57 (ohne OC_INTEG)
  ;; OC_INTEG ist Textspalte und wird nicht summiert!
  ;; ================================================================
  (oc-log "\n  Summenzeile berechnen...")
  ;; sum-func-bases bereits oben gesetzt = (cdr func-bases) = 57 Eintraege
  (setq sum-counts nil)
  (foreach fb sum-func-bases
    (setq sum-counts (append sum-counts (list 0)))
  )
  
  ;; Zaehle: Uebertrag (Zeile 1, falls Folgeblatt) + aktuelle DPs
  ;; Uebertrag enthält bereits kumulierte Werte -> direkt addieren
  (setq count-row 0)
  (setq count-start (if is-first-sheet row-offset 1))  ; Ab Zeile 1 oder row-offset
  (setq count-total (+ dp-count (if is-first-sheet 0 1)))  ; +1 fuer Uebertrag
  (while (< count-row count-total)
    (setq count-target (+ count-start count-row))
    (setq col-idx 0)
    (foreach func-base sum-func-bases
      (setq count-attr (strcat func-base "_DP_" (itoa count-target)))
      (setq count-val (oc-fl-read-attr (car gafl-blocks) count-attr))
      (if (and count-val (> (strlen (vl-string-trim " " count-val)) 0))
        (progn
          (setq num-val (atoi count-val))
          (if (> num-val 0)
            (setq sum-counts (oc-fl-list-set sum-counts col-idx
                               (+ (nth col-idx sum-counts) num-val)))
            (setq sum-counts (oc-fl-list-set sum-counts col-idx
                               (1+ (nth col-idx sum-counts))))
          )
        )
      )
      (setq col-idx (1+ col-idx))
    )
    (setq count-row (1+ count-row))
  )
  
  ;; Schreibe Summen in OC_SUM_1 bis OC_SUM_57
  ;; (57 Funktionsspalten ohne OC_INTEG)
  (setq sum-idx 0)
  (setq sum-written 0)
  (foreach sum-val sum-counts
    (setq sum-idx (1+ sum-idx))
    (setq sum-attr (strcat "OC_SUM_" (itoa sum-idx)))
    (foreach gafl-ent gafl-blocks
      (if (> (oc-fl-write-attr gafl-ent sum-attr (itoa sum-val)) 0)
        (setq sum-written (1+ sum-written))
      )
    )
  )
  (oc-log (strcat "\n  Summenzeile: " (itoa sum-written) " Werte geschrieben"))

  ;; Summenzeile in globaler Variable UND Temp-Datei speichern
  (setq *oc-fill-last-sums* sum-counts)
  (oc-fl-write-carry-file csv-path sum-counts)
  (oc-log "\n  Summenwerte fuer Uebertrag gespeichert.")

  ;; Geaenderte Blocks einmal aktualisieren (statt entupd pro Attribut)
  (oc-fl-flush-updates)

  (oc-log (strcat "\n=== GA-FL Befuellung abgeschlossen: "
                 (itoa filled-count) " Datenpunkte in "
                 (itoa (length gafl-blocks)) " Block(e) ==="))

  (if (and (boundp '*oc-missing-refs*) *oc-missing-refs*)
    (oc-log (strcat "\n  ACHTUNG: " (itoa (length *oc-missing-refs*))
                   " fehlende DP-Referenz(en) in GA_FL_VORLAGE.ods!"))
  )

  (princ)
)

;;; Interaktiver Befehl
(defun c:FillGaFl ()
  (if (not *oc-fill-csv-path*)
    (setq *oc-fill-csv-path* (getfiled "Extrahierte CSV waehlen" "" "csv" 0))
  )
  (if (not *oc-fill-ref-csv-path*)
    (setq *oc-fill-ref-csv-path* (getfiled "GA_FL_VORLAGE.csv waehlen" "" "csv" 0))
  )
  (if (not *oc-fill-sheet-num*) (setq *oc-fill-sheet-num* 1))
  (if (not *oc-fill-start-row*) (setq *oc-fill-start-row* 0))
  (if (not *oc-fill-dp-count*) (setq *oc-fill-dp-count* 24))
  (FillGaFl)
)

;; Fehlende Referenzen am Ende anzeigen
(defun oc-fl-show-missing-refs (/ msg)
  (if (and (boundp '*oc-missing-refs*) *oc-missing-refs*)
    (progn
      (setq msg (strcat "ACHTUNG: Folgende Datenpunkt-Referenzen fehlen\n"
                        "in GA_FL_VORLAGE.ods und wurden mit 0 belegt:\n\n"))
      (foreach ref-name *oc-missing-refs*
        (setq msg (strcat msg "  - " ref-name "\n"))
      )
      (setq msg (strcat msg "\nBitte ergaenzen Sie diese in der ODS-Datei\n"
                        "und fuehren Sie 'Projekt erstellen' erneut aus."))
      (alert msg)
      (setq *oc-missing-refs* nil)
    )
  )
  (princ)
)

(oc-log "\nFillGaFl.lsp geladen (v1.4)")
(princ)
