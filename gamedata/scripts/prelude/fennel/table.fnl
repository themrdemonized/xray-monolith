;; Tables

(λ join [& tbls]
  "Flatten the list of tables TBLS into a single table."
  (accumulate [dest {}
               _ tbl (ipairs tbls)]
    (accumulate [d dest
                 k v (pairs tbl)]
      (do (set (. d k) v)
          d))))

 {: join}
