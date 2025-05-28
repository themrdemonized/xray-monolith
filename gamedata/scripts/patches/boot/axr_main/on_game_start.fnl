(var axr_main (require :axr_main))

(var {:compiler {:PATTERN_FILE_PATH PATTERN-FILE-PATH
                 &as compiler}}
     (require :scam))

;; List of files that should not be loaded when searching for on_game_start
(var ignore
     {:init true
      :_G true
      :class_registrator true
      :game_registrator true
      :ui_registrator true
      :ce_new_attachable_item true
      :ce_new_game_dm true
      :sim_faction_brain_human true
      :sim_faction_brain_mutant true
      :ce_switcher true
      :axr_main true
      :lua_help true
      :rx_gl true})

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
  (format-extensions (compiler.get_extensions)))

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

(λ flist->iter [flist]
  "Produce an iterator over the paths in `flist`."
  (list-values
   flist
   #(do (var idx (- $2 1))
        (if (< idx ($1:Size))
            ($1:GetAt idx)))))

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

(λ strip-extension [s]
  "Remove any file extension from path `s`."
  (var (path name _) (: s :match PATTERN-FILE-PATH))
  (.. path name))

(λ backslashes->slashes [s]
  "Replace backslashes with slashes in string `s`."
  (s:gsub "\\" "/"))

(λ strip-/init [s]
  "Remove any `/init` suffix that may be present on `s`"
  (s:gsub "/init$" ""))

(λ file-empty? [file]
  "Return true if `file` is empty."
  (= (file:Size) 0))

(λ file-not-empty? [file]
  "Return true if `file` is non-empty."
  (not (file-empty? file)))

(λ iter-files [fs path ?extensions ?flags]
  (var flags (or ?flags 0))
  (set flags (bor FS.FS_ListFiles flags))
  
  (var flist (: fs :file_list_open_ex path flags ?extensions))

  (->> flist
       (flist->iter)
       (iter-filter file-not-empty?)
       (iter-map
        #(-> $1
             (: :NameShort)
             (strip-extension)
             (backslashes->slashes)
             (strip-/init)))))

(λ iter-scripts [fs extensions]
  (iter-files fs "$game_scripts$" extensions))

(fn axr_main.on_game_start []
  (var starts
       (icollect [res out (->> (iter-scripts
                                (getFS)
                                (format-compiler-extensions))
                               (iter-filter #(not (. ignore $1)))
                               (iter-map #(pcall require $1)))]
         (case (values res out)
           (true {:on_game_start start}) start)))
  
  ;; Call the result
  (each [start (list-values starts)]
    (start)))

{}
