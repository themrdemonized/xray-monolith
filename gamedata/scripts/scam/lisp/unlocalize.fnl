; =*= lang: scam/lisp/macro.compile =*=

(fn contains? [vals val]
  "Returns true if VAL is present in VALS."
  (accumulate [contains false
               _ v (ipairs vals)]
    (or contains (= val v))))

(var syms [:var :set :local :fn :lambda :λ])
(fn unlocalize-form [with names form]
  "If the head of FORM is one of SYMS and its NAME is in NAMES,
   append a call to (WITH :NAME NAME)."
  (match form
    ;; local
    (where [[sym] name & _]
           (contains? syms sym)
           (contains? names name))
    [form
     `(,with ,(tostring name) #,name)]

    ;; fallthrough
    _ [form]))

(fn unlocalize [with names forms]
  "Inserts a (WITH :NAME NAME) call for each top-level local binding in FORMS."
  (let [out []]
    (each [_ form (ipairs forms)]
      (each [_ v (ipairs (unlocalize-form with names form))]
        (table.insert out v)))
    out))

(fn wrap_do [forms]
  "Wrap FORMS in (do ...)"
  `(do ,(unpack forms)))

{: unlocalize
 : wrap_do}
