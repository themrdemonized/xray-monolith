;; Basic Types

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

{: nil?
 : number?
 : string?
 : symbol?
 : table?
 : list?}
