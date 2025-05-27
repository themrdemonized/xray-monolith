(var {: page
      : list-bool}
     (require :options/builder))

(page :id :aim 
      (list-bool :id :aimmode_remember)
      (list-bool :id :allow_outfit_control_inertion_factor)
      (list-bool :id :allow_weapon_control_inertion_factor)
      (list-bool :id :fix_avelocity_spread)
      (list-bool :id :apply_pdm_to_ads)
      (list-bool :id :smooth_ads_transition))
