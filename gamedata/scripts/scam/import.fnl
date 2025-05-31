;; Imports
(var {: iter-package-path}
     (require :prelude/fennel/package))

(λ fixup-_G [path]
  "Return _G if `path` is _g, otherwise return `path`.
   Avoids namespace mismatch when loading _g.script."
  (or (and (= path :_g) :_G)
      path))

(λ import-list-impl [path ?ignore ?handle-error]
  "Use the parent env `env` to resolve the unix-style path `path`
   and import the resulting modules, returning the results
   as a list of packages."
  (var handle-error (or ?handle-error error))
  (icollect [package (iter-package-path path)]
    (if (not (and ?ignore (. ?ignore package)))
        (case (pcall require (fixup-_G package))
          (true out) out
          (false err) (handle-error err)))))

(λ import-table-impl [path ?ignore ?handle-error]
  "Use the parent env `env` to resolve the unix-style path `path`
   and import the resulting modules, returning the results
   as a table of name-package pairs."
  (var handle-error (or ?handle-error error))
  (collect [package (iter-package-path path)]
    (if (not (and ?ignore (. ?ignore package)))
        (case (pcall require (fixup-_G package))
          (true out) (values package out)
          (false err) (handle-error err)))))

(λ absolute-path [env path]
  (var path path)

  (var is-init? (env._FILE:match "\\init%.[^.]+$"))
  (var is-parent? (path:match "^%.+/"))
  (var is-absolute? (path:match "^/"))

  (var base (or (and is-absolute? "")
                env._PACKAGE))

  ;; If the environment points at an init.* file,
  ;; strip a dot from parent paths
  (when (and is-init? is-parent?)
    (set path (path:sub 2)))
  
  ;; If path is non-parent, relative,
  ;; and the environment doesn't point at an init.* file,
  ;; strip the last segment from the base
  (when (and (not is-init?)
             (not is-parent?)
             (not is-absolute?))
    (set base (base:gsub "/[^/]+$" "")))
  
  ;; If the path is relative, prepend the base path
  (when (not is-absolute?)
    (set path (.. base "/" path)))

  path)

(λ _G.import_table [path ?ignore ?handle-error]
  "Resolve the unix-style path `path` and import the resulting modules,
   returning the results as a table of name-package pairs."
  (import-table-impl (absolute-path (getfenv 2) path)
                     ?ignore
                     ?handle-error))

(λ _G.import_list [path ?ignore ?handle-error]
  "Resolve the unix-style path `path` and import the resulting modules,
   returning the results as a list of packages."
  (import-list-impl (absolute-path (getfenv 2) path)
                    ?ignore
                    ?handle-error))

(λ _G.import [path ?ignore ?handle-error]
  "Resolve the unix-style path `path` and import the resulting modules,
   returning the results variadically."
  (unpack (import-list-impl (absolute-path (getfenv 2) path)
                            ?ignore
                            ?handle-error)))

