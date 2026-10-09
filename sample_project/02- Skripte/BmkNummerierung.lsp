;;; =====================================================================
;;; BmkNummerierung.lsp - BMK Nummerierung für openCirt BatchProcessing
;;; =====================================================================
;;; Version: 2.3 - Steuerattribut BMK_NUMMERIERUNG (bevorzugt), FREITEXT_05 als Fallback
;;;          2.2 - OC_AKS_LOCK Attribut: gesperrte Blöcke nicht nummerieren
;;; 
;;; Änderungen gegenüber v1.0:
;;;   - Hauptfunktion: BmkNummerierung (ohne c: Prefix, aufrufbar aus SCR)
;;;   - Steuerattribut: BMK_NUMMERIERUNG (ab v2.3 bevorzugt), sonst FREITEXT_05
;;;   - Modus-Werte: "NEUSTARTEN" / "FORTSETZEN"
;;;   - Sortierung: links→rechts, unten→oben (wie Spec v1.1)
;;;   - Zähler-Persistenz via bmk_counters.tmp im DWG-Verzeichnis
;;; 
;;; Konvention: Dateiname ohne .lsp = Funktionsname
;;; =====================================================================

;;; =====================================================================
;;; HILFSFUNKTIONEN
;;; =====================================================================

;; String an Delimiter aufteilen
(defun oc-str-split (str delimiter / pos result part)
  (setq result '())
  (setq str (strcat str delimiter))
  (while (setq pos (vl-string-search delimiter str))
    (setq part (substr str 1 pos))
    (if (> (strlen part) 0)
      (setq result (append result (list part)))
    )
    (setq str (substr str (+ pos 2)))
  )
  result
)

;; Zahl 2-stellig formatieren
(defun oc-format-number (num)
  (if (< num 10)
    (strcat "0" (itoa num))
    (itoa num)
  )
)

;; Zahlen und Trennzeichen am Ende des Strings entfernen
(defun oc-remove-trailing-numbers (text / i char)
  ;; Nur Ziffern am Ende entfernen, Trennzeichen (-_.) bleiben erhalten
  (setq i (strlen text))
  (while (and (> i 0)
              (setq char (substr text i 1))
              (and (>= (ascii char) 48) (<= (ascii char) 57)))
    (setq i (1- i))
  )
  (if (> i 0)
    (substr text 1 i)
    ""
  )
)

;;; =====================================================================
;;; BLOCK-ATTRIBUT FUNKTIONEN
;;; =====================================================================

;; Attribute eines Blocks als Liste von (entget)-Daten sammeln
(defun oc-get-block-attributes (block-ent / attr-list next-ent attr-data)
  (setq attr-list '())
  (setq next-ent (entnext block-ent))
  (while (and next-ent
              (setq attr-data (entget next-ent))
              (= (cdr (assoc 0 attr-data)) "ATTRIB"))
    (setq attr-list (cons attr-data attr-list))
    (setq next-ent (entnext next-ent))
  )
  attr-list
)

;; Attribut-Wert eines Blocks aktualisieren
(defun oc-update-attribute (block-ent attr-name new-value / next-ent attr-data)
  (setq next-ent (entnext block-ent))
  (while (and next-ent
              (setq attr-data (entget next-ent))
              (= (cdr (assoc 0 attr-data)) "ATTRIB"))
    (if (= (strcase (cdr (assoc 2 attr-data))) (strcase attr-name))
      (progn
        (setq attr-data (subst (cons 1 new-value) (assoc 1 attr-data) attr-data))
        (entmod attr-data)
        (entupd next-ent)
      )
    )
    (setq next-ent (entnext next-ent))
  )
)

;;; =====================================================================
;;; BLÖCKE MIT OC_AKS FINDEN
;;; =====================================================================

(defun oc-get-blocks-with-bmk (/ ss i ent ent-data ins-point attribs block-list)
  (setq block-list '())
  (setq ss (ssget "X" '((0 . "INSERT"))))
  (if ss
    (progn
      (setq i 0)
      (repeat (sslength ss)
        (setq ent (ssname ss i))
        (setq ent-data (entget ent))
        (setq ins-point (cdr (assoc 10 ent-data)))
        (setq attribs (oc-get-block-attributes ent))
        (foreach attr attribs
          (if (= (strcase (cdr (assoc 2 attr))) "OC_AKS")
            (setq block-list (cons (list ent attr ins-point) block-list))
          )
        )
        (setq i (1+ i))
      )
    )
  )
  block-list
)

;;; =====================================================================
;;; SORTIERUNG: links→rechts, unten→oben
;;; =====================================================================

(defun oc-sort-blocks-by-position (blocks / tolerance)
  (setq tolerance 10.0)
  (vl-sort blocks
    (function
      (lambda (a b / pt-a pt-b x-a y-a x-b y-b)
        (setq pt-a (caddr a) pt-b (caddr b))
        (setq x-a (car pt-a) y-a (cadr pt-a))
        (setq x-b (car pt-b) y-b (cadr pt-b))
        (if (< (abs (- x-a x-b)) tolerance)
          (< y-a y-b)      ; gleiche Spalte: unten nach oben
          (< x-a x-b)      ; links nach rechts
        )
      )
    )
  )
)

;;; =====================================================================
;;; BMK MODUS AUS PLANKOPF (BMK_NUMMERIERUNG, Fallback FREITEXT_05)
;;; =====================================================================

;; Sucht ein Attribut in allen Bloecken der Zeichnung.
;; Rueckgabe: getrimmter Wert (letzter nicht-leerer Treffer) oder nil.
(defun oc-find-attr-in-drawing (attr-name / ss i ent attribs val result)
  (setq result nil)
  (setq ss (ssget "X" '((0 . "INSERT"))))
  (if ss
    (progn
      (setq i 0)
      (repeat (sslength ss)
        (setq ent (ssname ss i))
        (setq attribs (oc-get-block-attributes ent))
        (foreach attr attribs
          (if (= (strcase (cdr (assoc 2 attr))) attr-name)
            (progn
              (setq val (vl-string-trim " \t" (cdr (assoc 1 attr))))
              (if (> (strlen val) 0) (setq result val))
            )
          )
        )
        (setq i (1+ i))
      )
    )
  )
  result
)

;; Rueckgabe: (MODUS . QUELLATTRIBUT)
;; BMK_NUMMERIERUNG hat Vorrang. Nur wenn es fehlt oder leer ist,
;; wird FREITEXT_05 ausgewertet. Beides leer -> NEUSTARTEN.
(defun oc-get-bmk-mode (/ val)
  (cond
    ((setq val (oc-find-attr-in-drawing "BMK_NUMMERIERUNG"))
     (cons (strcase val) "BMK_NUMMERIERUNG"))
    ((setq val (oc-find-attr-in-drawing "FREITEXT_05"))
     (cons (strcase val) "FREITEXT_05"))
    (T (cons "NEUSTARTEN" "Default"))
  )
)

;;; =====================================================================
;;; ZÄHLER-PERSISTENZ (bmk_counters.tmp)
;;; =====================================================================

(defun oc-load-counters (filename mode / counters file line parts)
  (setq counters '())
  (if (and (findfile filename) (= mode "FORTSETZEN"))
    (progn
      (setq file (open filename "r"))
      (if file
        (progn
          (while (setq line (read-line file))
            (if (vl-string-search ":" line)
              (progn
                (setq parts (oc-str-split line ":"))
                (if (= (length parts) 2)
                  (setq counters (cons (cons (car parts) (atoi (cadr parts))) counters))
                )
              )
            )
          )
          (close file)
          (princ "\n  Zaehler aus vorheriger Zeichnung geladen.")
        )
      )
    )
    (progn
      ;; NEUSTARTEN: tmp-Datei löschen falls vorhanden
      (if (findfile filename)
        (vl-file-delete filename)
      )
      (princ "\n  Neue Zaehlung gestartet.")
    )
  )
  counters
)

(defun oc-save-counters (filename counters / file)
  (setq file (open filename "w"))
  (if file
    (progn
      (foreach counter counters
        (write-line (strcat (car counter) ":" (itoa (cdr counter))) file)
      )
      (close file)
    )
  )
)

;;; =====================================================================
;;; ZÄHLER VERWALTEN
;;; =====================================================================

(defun oc-get-or-increment (bmk-type counters / pair)
  (setq pair (assoc bmk-type counters))
  (if pair (1+ (cdr pair)) 1)
)

(defun oc-update-counter (bmk-type new-value counters / updated found)
  (setq updated '() found nil)
  (foreach counter counters
    (if (= (car counter) bmk-type)
      (progn
        (setq updated (cons (cons bmk-type new-value) updated))
        (setq found T)
      )
      (setq updated (cons counter updated))
    )
  )
  (if (not found)
    (setq updated (cons (cons bmk-type new-value) updated))
  )
  updated
)

;;; =====================================================================
;;; BLOCK VERARBEITEN
;;; =====================================================================

;; Prueft ob OC_AKS_LOCK gesetzt ist (= nicht nummerieren)
(defun oc-is-locked (block-ent / lock-val v)
  (setq lock-val (oc-read-attr-value block-ent "OC_AKS_LOCK"))
  (if (and lock-val (> (strlen lock-val) 0))
    (progn
      (setq v (strcase (vl-string-trim " \t" lock-val)))
      (or (= v "JA") (= v "TRUE") (= v "1") (= v "X")
          (= v "HIGH") (= v "WAHR"))
    )
    nil
  )
)

;; Liest Attributwert aus Block (case-insensitive)
(defun oc-read-attr-value (block-ent attr-name / next-ent attr-data found-val)
  (setq found-val nil)
  (setq next-ent (entnext block-ent))
  (while (and next-ent
              (setq attr-data (entget next-ent))
              (= (cdr (assoc 0 attr-data)) "ATTRIB"))
    (if (= (strcase (cdr (assoc 2 attr-data))) (strcase attr-name))
      (setq found-val (cdr (assoc 1 attr-data)))
    )
    (setq next-ent (entnext next-ent))
  )
  found-val
)

(defun oc-process-bmk-block (block counters / attr-data bmk-text clean-text counter new-text)
  (setq attr-data (cadr block))
  (setq bmk-text (cdr (assoc 1 attr-data)))
  (if (and bmk-text (> (strlen bmk-text) 0))
    (progn
      ;; OC_AKS_LOCK Pruefung: gesperrte Bloecke ueberspringen
      (if (oc-is-locked (car block))
        (princ (strcat "\n  " bmk-text " -> [LOCK, uebersprungen]"))
        ;; Normaler Eintrag - nummerieren
        (progn
          (setq clean-text (oc-remove-trailing-numbers bmk-text))
          (if (> (strlen clean-text) 0)
            (progn
              (setq counter (oc-get-or-increment clean-text counters))
              (setq counters (oc-update-counter clean-text counter counters))
              (setq new-text (strcat clean-text (oc-format-number counter)))
              (oc-update-attribute (car block) (cdr (assoc 2 attr-data)) new-text)
              (princ (strcat "\n  " bmk-text " -> " new-text))
            )
          )
        )
      )
    )
  )
  counters
)

;;; =====================================================================
;;; HAUPTFUNKTION (aufgerufen vom C++ Plugin via SCR)
;;; =====================================================================

(defun BmkNummerierung (/ blocks sorted-blocks mode-info bmk-mode counter-file counters)
  (princ "\n=== BMK Nummerierung gestartet ===")

  (setq blocks (oc-get-blocks-with-bmk))
  (princ (strcat "\n  Gefundene Bloecke mit OC_AKS: " (itoa (length blocks))))

  (if blocks
    (progn
      (setq sorted-blocks (oc-sort-blocks-by-position blocks))

      ;; Modus lesen: BMK_NUMMERIERUNG bevorzugt, sonst FREITEXT_05
      (setq mode-info (oc-get-bmk-mode))
      (setq bmk-mode (car mode-info))
      (princ (strcat "\n  Modus (" (cdr mode-info) "): " bmk-mode))

      ;; Zähler-Datei im DWG-Verzeichnis
      (setq counter-file (strcat (getvar "DWGPREFIX") "bmk_counters.tmp"))
      (setq counters (oc-load-counters counter-file bmk-mode))

      ;; Alle Blöcke verarbeiten
      (foreach block sorted-blocks
        (setq counters (oc-process-bmk-block block counters))
      )

      ;; Zähler speichern
      (oc-save-counters counter-file counters)

      ;; OC_AKS_EINFUEG Attribute synchronisieren
      (oc-sync-aks-einfueg)

      (princ (strcat "\n=== BMK Nummerierung abgeschlossen: "
                     (itoa (length sorted-blocks)) " Bloecke ==="))
    )
    (princ "\n  Keine Bloecke mit OC_AKS Attribut gefunden.")
  )
  (princ)
)

;;; =====================================================================
;;; OC_AKS -> OC_AKS_EINFUEG_* SYNCHRONISATION
;;; Liest OC_AKS jeder Blockinstanz und schreibt den Wert in alle
;;; OC_AKS_EINFUEG_1 bis OC_AKS_EINFUEG_n Attribute derselben Instanz.
;;; =====================================================================

(defun oc-sync-aks-einfueg (/ ss i ent aks-val next-ent attr-data tag)
  (princ "\n=== OC_AKS -> OC_AKS_EINFUEG_* Synchronisation ===")
  (setq ss (ssget "X" '((0 . "INSERT"))))
  (if (not ss)
    (progn
      (princ "\n  Keine Bloecke gefunden.")
      (exit)
    )
  )
  (setq i 0)
  (repeat (sslength ss)
    (setq ent (ssname ss i))
    ;; OC_AKS Wert auslesen
    (setq aks-val (oc-read-attr-value ent "OC_AKS"))
    (if (and aks-val (> (strlen aks-val) 0))
      (progn
        ;; Alle ATTRIBs der Instanz durchgehen
        (setq next-ent (entnext ent))
        (while (and next-ent
                    (setq attr-data (entget next-ent))
                    (= (cdr (assoc 0 attr-data)) "ATTRIB"))
          (setq tag (strcase (cdr (assoc 2 attr-data))))
          ;; Pruefe ob Tag mit OC_AKS_EINFUEG_ beginnt
          (if (and (>= (strlen tag) 15)
                   (= (substr tag 1 15) "OC_AKS_EINFUEG_"))
            (progn
              (setq attr-data (subst (cons 1 aks-val) (assoc 1 attr-data) attr-data))
              (entmod attr-data)
              (entupd next-ent)
              (princ (strcat "\n  " (cdr (assoc 2 (entget next-ent))) " <- " aks-val))
            )
          )
          (setq next-ent (entnext next-ent))
        )
      )
    )
    (setq i (1+ i))
  )
  (princ "\n=== OC_AKS_EINFUEG Synchronisation abgeschlossen ===")
  (princ)
)

;;; Interaktiver Befehl (optional, für manuellen Aufruf)
(defun c:BmkNummerierung () (BmkNummerierung))

(princ "\nBmkNummerierung.lsp geladen (v2.3)")
(princ)
