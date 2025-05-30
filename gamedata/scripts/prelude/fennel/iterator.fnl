;; Iterators

(var {: list-values} (require :prelude/fennel/list))

(λ iter-values [& vals]
  "Produce an iterator over the given variadic values."
  (list-values vals))

(λ iter-map [t f ...]
  "Map transformer `t` over the iterator defined by function `f`
   and stateful params `...`."
  (values
   (fn [...]
     (-?> ...
          (f)
          (t)))
   ...))

(λ iter-filter [take? f ...]
  "Modify the iterator defined by function `f` and stateful params `...`
   to include only values for which `take?` returns true."
  (values
   (fn [...]
     (var done? false)
     (var ?out nil)
     (while (and (not done?)
                 (= nil out?))
       (var val (f ...))
       (if val
           (do (when (take? val)
                 (do (set ?out val)
                     (set done? true))))
           (set done? true)))
     ?out)
   ...))

(λ iter-zip [[fa ?sa ?ca] [fb ?sb ?cb] ?short?]
  "Given two iterators captured in lists, produce an iterator of pairs.
   If `?short` is truthy, short-circuit once one of the iterators returns nil."
  (values (λ [[?sa ?sb] [?ca ?cb]]
            (var va (fa ?sa ?ca))
            (var vb (fb ?sb ?cb))
            (if (if ?short?
                    (and (not= nil va) (not= nil vb))
                    (or (not= nil va) (not= nil vb)))
                [va vb]))
          [?sa ?sb]
          [?ca ?cb]))

{: iter-values
 : iter-map
 : iter-filter
 : iter-zip}
