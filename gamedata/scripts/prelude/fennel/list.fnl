;; Lists

(λ list-values [list ?index]
  "Produce an iterator over the values in `list`,
   indexing via `.` unless `?index` is specified."
  (var index (or ?index
                 #(. $1 $2)))
  
  (var i 1)
  (values
   (fn [list]
     (var v (index list i))
     (set i (+ 1 i))
     v)
   list))

{: list-values}
