(var {: page
      : list-bool}
     (require :options/builder))

(page :id :ui_hud 
      (list-bool :id :g_draw_pickup_item_names)
      (list-bool :id :use_english_text_for_missing_translations
                 :restart true))
