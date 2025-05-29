(var {:list
      {: list-values}
      
      :iterator
      {: iter-map
       : iter-filter}
      
      :package
      {: iter-packages}}
     
     (import :/prelude/fennel))

(var axr_main (import :/axr_main))

(var {:PATTERN_FILE_PATH PATTERN-FILE-PATH
      &as compiler}
     (import :/scam/compiler))

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

(fn axr_main.on_game_start []
  (var starts
       (icollect [res out (->> (iter-packages)
                               (iter-filter #(not (. ignore $1)))
                               (iter-map #(pcall require $1)))]
         (case (values res out)
           (true {:on_game_start start}) start)))
  
  ;; Call the result
  (each [start (list-values starts)]
    (start)))

{}
