;; Files

(var {: list-values} (require :prelude/fennel/list))

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

(λ format-extensions [extensions]
  "Format the list `extensions` into a filesystem mask."
  (accumulate [exts nil
               _ v (ipairs extensions)]
    (let [v (.. "*." v)]
      (if exts
          (.. exts "," v)
          v))))

(λ format-compiler-extensions []
  "Gather registered extensions from the compiler,
   and format them into a filesystem mask."
  (var {:get_extensions get-extensions} (require :scam/compiler))
  (format-extensions (get-extensions)))


{: file-empty?
 : file-not-empty?
 : flist->iter
 : format-extensions
 : format-compiler-extensions}
