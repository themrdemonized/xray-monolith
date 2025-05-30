(var {: iter-values} (import :/prelude/fennel/iterator))

(var ui_options (import :/ui_options))

;; Table of packages to ignore when glob importing
(var ignore {:options/builder true})

;; Monkey-patch options init function
(var ui_options_base ui_options.init_opt_base)
(fn ui_options.init_opt_base []
  ;; Call base init
  (ui_options_base)

  ;; Iterate submodules and add their contents to the options table
  (each [option (iter-values (import :* ignore))]
    (table.insert ui_options.options option)))

;; Run the init function
(ui_options.init_opt_base)

{}
