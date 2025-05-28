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

{: flist->iter
 : file-empty?
 : file-not-empty?}
