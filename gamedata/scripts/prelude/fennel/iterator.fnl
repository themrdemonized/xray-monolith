;; Iterators

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

{: iter-map
 : iter-filter}
