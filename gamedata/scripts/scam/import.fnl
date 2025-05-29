;; Imports
(var {: iter-package-path}
     (require :prelude/fennel/package))

(λ relative-path [env path]
  "If `path` doesn't have a leading /, prepend the path from `env`."
  (or (and (path:match "^/") path)
      (.. (. env :_PACKAGE) :/ path)))

(λ fixup-_G [path]
  "Return _G if `path` is _g, otherwise return `path`.
   Avoids namespace mismatch when loading _g.script."
  (or (and (= path :_g) :_G)
      path))

(λ import-list-impl [env path]
  "Use the parent env `env` to resolve the unix-style path `path`
   and import the resulting modules, returning the results
   as a list of packages."
  (icollect [package (iter-package-path (relative-path env path))]
    (require (fixup-_G package))))

(λ import-table-impl [env path]
  "Use the parent env `env` to resolve the unix-style path `path`
   and import the resulting modules, returning the results
   as a table of name-package pairs."
  (collect [package (iter-package-path (relative-path env path))]
    (values package (require (fixup-_G package)))))

(λ _G.import_table [path]
  "Resolve the unix-style path `path` and import the resulting modules,
   returning the results as a table of name-package pairs."
  (import-table-impl (getfenv 2) path))

(λ _G.import_list [path]
  "Resolve the unix-style path `path` and import the resulting modules,
   returning the results as a list of packages."
  (import-list-impl (getfenv 2) path))

(λ _G.import [path]
  "Resolve the unix-style path `path` and import the resulting modules,
   returning the results variadically."
  (unpack (import-list-impl (getfenv 2) path)))
