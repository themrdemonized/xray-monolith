(var {: group} (import :/options/builder))

(each [_ v (ipairs [(import :/*/boot/*)])]
  (print "glob-import:" v))

(group :id :modded_exes
       (import :*))
