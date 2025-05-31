(var {: iter-values} (import :/prelude/fennel/iterator))

(var axr_main (import :/axr_main))

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
      :rx_gl true
      :patches true})

(fn axr_main.on_game_start []
  ;; Call the result
  (each [package (iter-values (import :/** ignore))]
    (case package
      {: on_game_start} (on_game_start))))

{}
