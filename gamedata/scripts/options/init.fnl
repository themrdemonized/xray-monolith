(var ui_options (import :/ui_options))

;; Assemble options data
(var options {:modded_exes (import :modded_exes)})

;; Override options init function and inject our data
(var ui_options_base ui_options.init_opt_base)
(fn ui_options.init_opt_base []
  (ui_options_base)
  (each [_ option (pairs options)]
    (table.insert ui_options.options option)))

;; Run the init function
(ui_options.init_opt_base)

{}
