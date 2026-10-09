;;; Text-Breitenfaktor NUR bei Ueberschreitung der Zellgrenzen anpassen
;;;
;;; Version 3.0 - greift in Blockdefinitionen hinein
;;;
;;; Hintergrund:
;;; In der GA-FL liegt die gesamte Tabelle als EIN Block im Modellbereich
;;; (VDI3814_GA_FL_V_1_0: ~1740 geschlossene Zellrechtecke, ~1635 ATTDEFs).
;;; (ssget "_X" '((0 . "LWPOLYLINE"))) findet nur Objekte des Modellbereichs -
;;; die Zellrechtecke innerhalb der Blockdefinition waren damit unsichtbar,
;;; weshalb z.B. vierstellige Summenwerte in einer 5 mm breiten Zelle nie
;;; gestaucht wurden.
;;;
;;; Loesung:
;;; Die Zuordnung Text -> Zelle wird IM Blockdefinitionsraum gebildet
;;; (ATTDEF-Mittelpunkt vs. Rechteckmittelpunkt). Das ergibt eine Tabelle
;;; Tag -> Zellbreite, die anschliessend auf die ATTRIBs der Blockreferenz
;;; angewandt wird - inklusive Blockmassstab. Damit sind keinerlei
;;; Koordinatentransformationen noetig.
;;;
;;; Texte und Rechtecke, die direkt im Modellbereich liegen, werden weiterhin
;;; wie bisher behandelt.
;;;
;;; Es wird ausschliesslich gestaucht, nie gestreckt: passt der Text bereits,
;;; bleibt der Breitenfaktor unveraendert. Mehrfaches Ausfuehren ist daher
;;; unschaedlich.
;;;
;;; Konvention: Dateiname ohne .lsp = Funktionsname

;;; ---------------------------------------------------------------------------
;;; Konstanten
;;; ---------------------------------------------------------------------------

(setq *oc-tbf-tol*  0.01)   ; Toleranz "mittig im Rechteck" (Zeichnungseinheiten)
(setq *oc-tbf-fill* 0.95)   ; Zielbreite = 95 % der Zellbreite

;;; ---------------------------------------------------------------------------
;;; Geometrie-Hilfsfunktionen
;;; ---------------------------------------------------------------------------

;; Ist der Punkt mittig im Rechteck (innerhalb der Toleranz)?
(defun oc-tbf-centered-p (pt cx cy)
  (and (< (abs (- (car pt) cx)) *oc-tbf-tol*)
       (< (abs (- (cadr pt) cy)) *oc-tbf-tol*)))

;; Mittelpunkt/Breite/Hoehe einer geschlossenen 4-Punkt-LWPOLYLINE.
;; Rueckgabe (cx cy breite hoehe) oder nil.
(defun oc-tbf-rect-data (ed / pts xs ys x0 x1 y0 y1)
  (setq pts '())
  (foreach item ed
    (if (= (car item) 10) (setq pts (cons (cdr item) pts))))
  (if (= (length pts) 4)
    (progn
      (setq xs (mapcar 'car pts)
            ys (mapcar 'cadr pts))
      (setq x0 (apply 'min xs) x1 (apply 'max xs)
            y0 (apply 'min ys) y1 (apply 'max ys))
      ;; Nur achsenparallele Rechtecke: alle Punkte muessen auf dem Rand liegen
      (if (and (> (- x1 x0) 1e-6) (> (- y1 y0) 1e-6))
        (list (/ (+ x0 x1) 2.0) (/ (+ y0 y1) 2.0) (- x1 x0) (- y1 y0))
        nil))
    nil))

;; Einfuege-/Ausrichtungspunkt eines TEXT/ATTDEF/ATTRIB ermitteln
(defun oc-tbf-text-point (ed / al)
  (setq al (cdr (assoc 72 ed)))
  (if (and al (> al 0))
    (cdr (assoc 11 ed))     ; Ausrichtungspunkt (zentriert/mittig)
    (cdr (assoc 10 ed))))   ; Einfuegepunkt

;; Tatsaechliche Textbreite bzw. -hoehe aus der Bounding Box (WCS)
;; Rueckgabe (breite . hoehe) oder nil
(defun oc-tbf-text-extent (ent / res minpt maxpt)
  (setq res (vl-catch-all-apply
              '(lambda ()
                 (vla-getboundingbox (vlax-ename->vla-object ent) 'minpt 'maxpt)
                 (setq minpt (vlax-safearray->list minpt)
                       maxpt (vlax-safearray->list maxpt))
                 (cons (- (car maxpt) (car minpt))
                       (- (cadr maxpt) (cadr minpt))))))
  (if (vl-catch-all-error-p res) nil res))

;;; ---------------------------------------------------------------------------
;;; Zellzuordnung aus der Blockdefinition
;;; ---------------------------------------------------------------------------

;; Y-Buckets: Schluessel = auf 0.1 gerundeter Y-Wert, Wert = Liste von Rechtecken
(defun oc-tbf-ykey (y) (rtos y 2 1))

(defun oc-tbf-bucket-add (buckets r / key hit)
  (setq key (oc-tbf-ykey (cadr r)))
  (setq hit (assoc key buckets))
  (if hit
    (subst (cons key (cons r (cdr hit))) hit buckets)
    (cons (list key r) buckets)))

;; Rechteck zum Punkt suchen (Y-Bucket + Nachbarbuckets)
(defun oc-tbf-find-rect (buckets pt / keys y found)
  (setq y (cadr pt))
  (setq keys (list (oc-tbf-ykey y)
                   (oc-tbf-ykey (- y *oc-tbf-tol*))
                   (oc-tbf-ykey (+ y *oc-tbf-tol*))))
  (setq found nil)
  (foreach k keys
    (if (not found)
      (foreach r (cdr (assoc k buckets))
        (if (and (not found) (oc-tbf-centered-p pt (car r) (cadr r)))
          (setq found r)))))
  found)

;; Fallback fuer nicht exakt mittige Texte (z.B. linksbuendige Bezeichnungen,
;; aber auch ein Teil der Summenzellen OC_SUM_n): kleinstes Rechteck, das den
;; Punkt umschliesst. Zu gross geratene Treffer sind unkritisch - dann wird
;; schlicht nicht gestaucht, wie bisher.
(defun oc-tbf-find-enclosing (rects pt / x y best)
  (setq x (car pt) y (cadr pt))
  (setq best nil)
  (foreach r rects
    (if (and (>= x (- (car r)  (* 0.5 (caddr r))))
             (<= x (+ (car r)  (* 0.5 (caddr r))))
             (>= y (- (cadr r) (* 0.5 (cadddr r))))
             (<= y (+ (cadr r) (* 0.5 (cadddr r)))))
      (if (or (null best)
              (< (* (caddr r) (cadddr r)) (* (caddr best) (cadddr best))))
        (setq best r))))
  best)

;; Baut fuer eine Blockdefinition die Liste (TAG breite hoehe).
;; Ergebnis wird in *oc-tbf-cache* zwischengespeichert.
(defun oc-tbf-cell-map (bname / blk e ed buckets rects rd map tag pt hit)
  (setq hit (assoc bname *oc-tbf-cache*))
  (if hit
    (cdr hit)
    (progn
      (setq blk (tblobjname "BLOCK" bname))
      (setq buckets '() rects '() map '())
      (if blk
        (progn
          ;; 1. Durchlauf: alle geschlossenen Rechtecke einsammeln
          (setq e (entnext blk))
          (while e
            (setq ed (entget e))
            (if (and (= (cdr (assoc 0 ed)) "LWPOLYLINE")
                     (= (logand (cdr (assoc 70 ed)) 1) 1))
              (progn
                (setq rd (oc-tbf-rect-data ed))
                (if rd
                  (progn
                    (setq rects (cons rd rects))
                    (setq buckets (oc-tbf-bucket-add buckets rd))))))
            (setq e (entnext e)))

          ;; 2. Durchlauf: jedem ATTDEF seine Zelle zuordnen.
          ;; Zuerst der schnelle Mittelpunktstreffer, sonst das kleinste
          ;; umschliessende Rechteck.
          (setq e (entnext blk))
          (while e
            (setq ed (entget e))
            (if (= (cdr (assoc 0 ed)) "ATTDEF")
              (progn
                (setq tag (strcase (cdr (assoc 2 ed))))
                (setq pt (oc-tbf-text-point ed))
                (setq rd (if pt (oc-tbf-find-rect buckets pt) nil))
                (if (and pt (null rd))
                  (setq rd (oc-tbf-find-enclosing rects pt)))
                (if (and rd (not (assoc tag map)))
                  (setq map (cons (list tag (caddr rd) (cadddr rd)) map)))))
            (setq e (entnext e)))))

      (setq *oc-tbf-cache* (cons (cons bname map) *oc-tbf-cache*))
      map)))

;;; ---------------------------------------------------------------------------
;;; Breitenfaktor setzen
;;; ---------------------------------------------------------------------------

;; Stuft den Breitenfaktor herunter, wenn der Text ueber die verfuegbare
;; Breite hinausragt. Rueckgabe T bei Aenderung.
(defun oc-tbf-apply (ent avail / ed ext actual cur newf)
  (setq ed (entget ent))
  (setq ext (oc-tbf-text-extent ent))
  (if (and ext (> avail 1e-6))
    (progn
      ;; Bei um 90 Grad gedrehtem Text ist die Textbreite die Y-Ausdehnung
      (setq actual (if (> (abs (sin (if (assoc 50 ed) (cdr (assoc 50 ed)) 0.0))) 0.5)
                     (cdr ext)
                     (car ext)))
      (if (> actual (* avail *oc-tbf-fill*))
        (progn
          (setq cur (cdr (assoc 41 ed)))
          (if (not cur) (setq cur 1.0))
          (setq newf (* cur (/ (* avail *oc-tbf-fill*) actual)))
          (if (assoc 41 ed)
            (setq ed (subst (cons 41 newf) (assoc 41 ed) ed))
            (setq ed (append ed (list (cons 41 newf)))))
          (entmod ed)
          (entupd ent)
          T)
        nil))
    nil))

;;; ---------------------------------------------------------------------------
;;; Hauptfunktion
;;; ---------------------------------------------------------------------------

(defun TextBreitenAnpassenBloecke (/ ss i ins insed bname sx rot
                                     att atted tag cell avail
                                     msRects e ed rd pt
                                     count-block count-model count-skip)

  (setq *oc-tbf-cache* nil)
  (setq count-block 0 count-model 0 count-skip 0)

  (princ "\nTextbreitenanpassung: Bloecke und Modellbereich pruefen...\n")

  ;; =========================================================================
  ;; Teil 1: Attribute in Blockreferenzen gegen die Zellen der Blockdefinition
  ;; =========================================================================
  (setq ss (ssget "_X" '((0 . "INSERT"))))
  (if ss
    (progn
      (setq i 0)
      (while (< i (sslength ss))
        (setq ins (ssname ss i))
        (setq insed (entget ins))
        (setq bname (cdr (assoc 2 insed)))
        (setq sx (if (assoc 41 insed) (abs (cdr (assoc 41 insed))) 1.0))
        (if (<= sx 1e-6) (setq sx 1.0))

        (setq cell (oc-tbf-cell-map bname))

        (if cell
          (progn
            ;; ATTRIB-Kette der Blockreferenz durchlaufen
            (setq att (entnext ins))
            (while (and att
                        (setq atted (entget att))
                        (= (cdr (assoc 0 atted)) "ATTRIB"))
              (setq tag (strcase (cdr (assoc 2 atted))))
              ;; Leere Attribute kosten nur Zeit
              (if (and (cdr (assoc 1 atted))
                       (/= (cdr (assoc 1 atted)) ""))
                (progn
                  (setq rd (assoc tag cell))
                  (if rd
                    (progn
                      ;; Zellbreite im WCS = Breite der Blockdefinition * Massstab
                      (setq avail (* (cadr rd) sx))
                      (if (oc-tbf-apply att avail)
                        (setq count-block (1+ count-block))))
                    (setq count-skip (1+ count-skip)))))
              (setq att (entnext att)))))
        (setq i (1+ i)))))

  ;; =========================================================================
  ;; Teil 2: Texte und Rechtecke direkt im Modellbereich (wie bisher)
  ;; =========================================================================
  (setq msRects '())
  (setq ss (ssget "_X" '((0 . "LWPOLYLINE") (70 . 1))))
  (if ss
    (progn
      (setq i 0)
      (while (< i (sslength ss))
        (setq rd (oc-tbf-rect-data (entget (ssname ss i))))
        (if rd (setq msRects (oc-tbf-bucket-add msRects rd)))
        (setq i (1+ i)))))

  (if msRects
    (progn
      (setq ss (ssget "_X" '((0 . "TEXT"))))
      (if ss
        (progn
          (setq i 0)
          (while (< i (sslength ss))
            (setq e (ssname ss i))
            (setq ed (entget e))
            (if (and (cdr (assoc 1 ed)) (/= (cdr (assoc 1 ed)) ""))
              (progn
                (setq pt (oc-tbf-text-point ed))
                (setq rd (if pt (oc-tbf-find-rect msRects pt) nil))
                (if rd
                  (if (oc-tbf-apply e (caddr rd))
                    (setq count-model (1+ count-model))))))
            (setq i (1+ i))))))
  )

  (princ "\n========================================")
  (princ (strcat "\nBlockattribute angepasst : " (itoa count-block)))
  (princ (strcat "\nModellbereich angepasst  : " (itoa count-model)))
  (princ (strcat "\nOhne Zellzuordnung       : " (itoa count-skip)))
  (princ "\n========================================\n")

  (princ)
)

;;; ---------------------------------------------------------------------------
;;; Diagnose: zeigt fuer den gewaehlten Block die erkannten Zellen
;;; ---------------------------------------------------------------------------

(defun c:TEXTZELLEN (/ ent ed bname cell)
  (setq *oc-tbf-cache* nil)
  (princ "\nBlockreferenz waehlen: ")
  (setq ent (car (entsel)))
  (if ent
    (progn
      (setq ed (entget ent))
      (if (= (cdr (assoc 0 ed)) "INSERT")
        (progn
          (setq bname (cdr (assoc 2 ed)))
          (setq cell (oc-tbf-cell-map bname))
          (princ (strcat "\nBlock: " bname))
          (princ (strcat "\nZellen erkannt: " (itoa (length cell))))
          (foreach c (reverse cell)
            (princ (strcat "\n  " (car c)
                           "  b=" (rtos (cadr c) 2 2)
                           "  h=" (rtos (caddr c) 2 2)))))
        (princ "\nKein Block gewaehlt.")))
    (princ "\nNichts gewaehlt."))
  (princ)
)

;;; Interaktive Aliase
(defun c:TextBreitenAnpassenBloecke () (TextBreitenAnpassenBloecke))
(defun c:TEXTANPASSEN () (TextBreitenAnpassenBloecke))

(princ "\nTextBreitenAnpassenBloecke.lsp geladen (v3.0)")
(princ "\nBefehle: TextBreitenAnpassenBloecke, TEXTANPASSEN, TEXTZELLEN")
(princ "\n=========================================\n")
