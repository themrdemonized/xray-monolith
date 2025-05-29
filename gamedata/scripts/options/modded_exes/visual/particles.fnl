(var {: page
      : track
      : list-bool}
     (import :.../builder))

(page :id :particles 
      (track :id :particle_update_mod
             :def 1
             :step 0.01)
      (list-bool :id :render_short_tracers)
      (list-bool :id :allow_silencer_hide_tracer))
