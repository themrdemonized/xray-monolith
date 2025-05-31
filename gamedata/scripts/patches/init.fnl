(var {: register_on_load_callback} (import :/boot/loader))

(register_on_load_callback
 #(when (not ($1:match "^patches/?"))
    (pcall require (.. :patches/ (: $1 :lower)))))

{}
