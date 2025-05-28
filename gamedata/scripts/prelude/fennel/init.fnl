;; Type Checking

(λ nil? [?arg]
  "Return true if ARG is nil."
  (= (type ?arg) :nil))

(λ number? [?arg]
  "Return true if ARG is a number."
  (= (type ?arg) :number))

(λ string? [?arg]
  "Return true if ARG is a string."
  (= (type ?arg) :string))

;; Alias to string, since symbols are strings in this lisp
(var symbol? string?)

(λ table? [?arg]
  "Return true if ARG is a table."
  (= (type ?arg) :table))

(λ list? [?arg]
  "Return true if ARG is a list."
  (and (table? ?arg)
       (or (= 0 (length ?arg))
           (number? (next ?arg)))))

(λ plist? [?arg]
  "Return true if ARG is a plist."
  (and (list? ?arg)
       (= 0 (% (length ?arg) 2))))

;; Numbers
(λ even? [number]
  ;; Returns true if NUMBER is even.
  (assert (number? number))
  (= 0 (% number 2)))

(λ odd? [number]
  ;; Returns true if NUMBER is odd.
  (assert (number? number))
  (= 1 (% number 2)))

;; Tables

(λ join [& tbls]
  "Flatten the list of tables TBLS into a single table."
  (accumulate [dest {}
               _ tbl (ipairs tbls)]
    (accumulate [d dest
                 k v (pairs tbl)]
      (do (set (. d k) v)
          d))))

;; Property Lists
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

;; Iterators

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

;; Files
(λ file-empty? [file]
  "Return true if `file` is empty."
  (= (file:Size) 0))

(λ file-not-empty? [file]
  "Return true if `file` is non-empty."
  (not (file-empty? file)))

(λ flist->iter [flist]
  "Produce an iterator over the paths in `flist`."
  (list-values
   flist
   #(do (var idx (- $2 1))
        (if (< idx ($1:Size))
            ($1:GetAt idx)))))

{: nil?
 : number?
 : string?
 : symbol?
 : table?
 : list?
 : plist?
 : even?
 : odd?
 : join
 : plist
 : plist->table
 : plist->header+list
 : table->plist
 : list-values
 : iter-map
 : iter-filter
 : flist->iter
 : file-empty?
 : file-not-empty?}
