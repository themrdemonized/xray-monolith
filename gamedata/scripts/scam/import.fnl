;; Imports
(var {: iter-package-path}
     (require :prelude/fennel/package))

(λ fixup-_G [path]
  "Return _G if `path` is _g, otherwise return `path`.
   Avoids namespace mismatch when loading _g.script."
  (or (and (= path :_g) :_G)
      path))

(λ import-list-impl [path]
  "Use the parent env `env` to resolve the unix-style path `path`
   and import the resulting modules, returning the results
   as a list of packages."
  (icollect [package (iter-package-path path)]
    (require (fixup-_G package))))

(λ import-table-impl [path]
  "Use the parent env `env` to resolve the unix-style path `path`
   and import the resulting modules, returning the results
   as a table of name-package pairs."
  (collect [package (iter-package-path path)]
    (values package (require (-> package
                                 (fixup-_G))))))

(λ absolute-path [env path]
  (var path path)

  ;; If the environment points at an init.* file,
  ;; strip a dot from relative paths
  (when (and (path:match "^%.+/")
             (env._FILE:match "\\init%.[^.]+$"))
    (set path (path:sub 2)))
  
  ;; If the path doesn't begin with /,
  ;; it's relative and should inherit the parent package's path
  (or (and (path:match "^/") path)
      (.. env._PACKAGE "/" path)))

(λ _G.import_table [path]
  "Resolve the unix-style path `path` and import the resulting modules,
   returning the results as a table of name-package pairs."
  (import-table-impl (absolute-path (getfenv 2) path)))

(λ _G.import_list [path]
  "Resolve the unix-style path `path` and import the resulting modules,
   returning the results as a list of packages."
  (import-list-impl (absolute-path (getfenv 2) path)))

(λ _G.import [path]
  "Resolve the unix-style path `path` and import the resulting modules,
   returning the results variadically."
  (unpack (import-list-impl (absolute-path (getfenv 2) path))))
