(var {: page
      : track}
     (import :.../builder))

(page :id :doppler 
      (track :id :snd_doppler_power
             :def 1.8
             :step 0.1)
      (track :id :snd_doppler_smoothing
             :def 15
             :step 1))
