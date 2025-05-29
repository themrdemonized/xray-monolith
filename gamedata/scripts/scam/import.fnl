;; Imports
(var {: format-compiler-extensions}
     (require :prelude/fennel/file))

(var {: iter-packages}
     (require :prelude/fennel/package))

(var {: iter-filter
      : iter-zip}
     (require :prelude/fennel/iterator))

(var {: list-values}
     (require :prelude/fennel/list))

(λ filter-path [path file]
  "Return true if unix-style path `path` includes `file`."
  ;; Split the path and file into segments and iterate them as pairs
  (accumulate [equal true
               [va vb] (iter-zip [(path:gmatch "[^/]+")]
                                 [(file:gmatch "[^/]+")])
               &until (not equal)]
    (and equal ;; All previous segments must match
         va vb ;; If either result is nil, length mismatch
         (or (= va :*) ;; If va is a glob, pass unconditionally
             (= (va:lower) (vb:lower)))))) ;; Test case-insensitive equality

(λ _G.import [path]
  "Resolve the unix-style path `path` and import the resulting modules."

  ;; If we don't have a leading /, prepend the calling package's path
  (var path (if (not (path:match "^/"))
                (.. (. (getfenv 2) :_PACKAGE) :/ path)
                path))

  (var files (icollect [package (->> (iter-packages
                                      (getFS)
                                      "$game_scripts$"
                                      (format-compiler-extensions))
                                     (iter-filter (partial filter-path path)))]
               package))
  
  (unpack (icollect [file (list-values files)]
            (do (print :file: file)
                (var file (if (= file :_g)
                              :_G
                              file))
                (require file)))))
