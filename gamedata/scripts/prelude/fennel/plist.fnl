;; Property Lists

(var {: table?
      : list?
      : symbol?}
     (require :prelude/fennel/type))

(var {: join}
     (require :prelude/fennel/table))

(λ plist? [?arg]
  "Return true if ARG is a plist."
  (and (list? ?arg)
       (= 0 (% (length ?arg) 2))))

(λ plist [& args]
  "Gather variadic ARGS into a property list.
   This is a lisp semantic; plists are structurally equivalent to Lua lists.
   We use a named form to distinguish them from associative lists ('alists'.)"
  args)

(λ plist->table [plist]
  "Treat PLIST as a flat list of key-value pairs to construct a table."
  (assert (plist? plist)
          (.. "PLIST is not a valid plist: " (tostring plist)))
  (case (length plist)
    0 {}
    1 (error (.. "odd-numbered plist input: " (tostring (. plist 1))))
    _ (do (var k (table.remove plist 1))
          (var v (table.remove plist 1))
          (var pair {k v})
          (if (> (length plist) 0)
              (join pair
                    (plist->table plist))
              pair))))

(λ plist->header+list [plist]
  "Remove symbol-value pairs from PLIST until a non-symbol is reached,
   then return the resulting header and list."
  (var header {})
  (while (symbol? (. plist 1))
    (var k (table.remove plist 1))
    (var v (table.remove plist 1))
    (set (. header k) v))
  (values header plist))

(λ table->plist [tbl]
  "Flatten TBL into a plist."
  (assert (table? tbl)
          (.. "TBL is not a valid table: " (tostring tbl)))
  (accumulate [plist [] k v (pairs tbl)]
    (do (table.insert plist k)
        (table.insert plist v)
        plist)))



{: plist?
 : plist
 : plist->table
 : plist->header+list
 : table->plist}
