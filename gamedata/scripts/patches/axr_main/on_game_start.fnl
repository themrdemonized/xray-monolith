(var axr-main (require :axr_main))

(var compiler (require :scam/compiler))
(var PATTERN-FILE-PATH (. compiler :PATTERN_FILE_PATH))

;; List of files that should not be loaded when searching for on_game_start
(var ignore
     {:init.lua true
      :_G.script true
      :class_registrator.script true
      :game_registrator.script true
      :ui_registrator.script true
      :ce_new_attachable_item.script true
      :ce_new_game_dm.script true
      :sim_faction_brain_human.script true
      :sim_faction_brain_mutant.script true
      :ce_switcher.script true
      :axr_main.script true
      :lua_help.script true
      :rx_gl.script true})

;; Gather file extensions from the compiler and format them as an FS mask
(var extensions
     (accumulate [exts nil
                  _ v (ipairs (compiler.get_extensions))]
       (let [v (.. "*." v)]
         (if exts
             (.. exts "," v)
             v))))

(fn axr-main.on_game_start []
  ;; Fetch a filesystem handle
  (var fs (getFS))

  ;; List scripts recursively
  (var flist (fs:file_list_open_ex "$game_scripts$" FS.FS_ListFiles extensions))

  ;; Accumulate on_game_start functions,
  ;; loading modules in the process
  (var starts
       (fcollect [index 0 (- (flist:Size) 1)]
         (case-try (flist:GetAt index)
           (where file (> (file:Size) 0))
           (file:NameShort)
           
           (where file-name (not (. ignore file-name)))
           (file-name:match PATTERN-FILE-PATH)
           
           (path name ext)
           (let [file-name (: (: (.. path name) :gsub "\\" "/")
                              :gsub "/init$" "")]
             (pcall require file-name))
           
           (true { :on_game_start on-game-start })
           on-game-start

           (catch _ start))))

  ;; Call the result
  (each [_ start (ipairs starts)]
    (start)))

{}
