(var {: iter-zip}
     (require :prelude/fennel/iterator))

(λ path-includes? [path]
  (λ impl [file]
    "Return true if unix-style path `path` includes `file`."
    ;; Split the path and file into segments and iterate them as pairs
    (accumulate [equal true
                 [va vb] (iter-zip [(path:gmatch "[^/]+")]
                                   [(file:gmatch "[^/]+")])
                 &until (not equal)]
      (and equal    ;; All previous segments must match
           va vb    ;; If either result is nil, length mismatch
           (or (= va :*) ;; If va is a glob, pass unconditionally
               (= (va:lower) (vb:lower)))))))

{: path-includes?}
